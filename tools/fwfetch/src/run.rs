// SPDX-License-Identifier: GPL-2.0-only
//! Orchestration: find candidates for each manifest entry, accept only those that match the published size/hash,
//! stage them OUTSIDE the repository, pin them in fw-lock.json, optionally deploy.
use crate::ext4::Ext4;
use crate::manifest::{self, Entry, Expect};
use crate::reader::Aligned;
use crate::{ihex, initramfs, part, util, winvol};
use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, BTreeSet};
use std::path::{Path, PathBuf};

const MAX_FILE: u64 = 64 * 1024 * 1024;

pub struct Opts {
    pub profile: String,
    pub out_dir: Option<PathBuf>,
    pub search_roots: Vec<PathBuf>,
    pub disks: Vec<PathBuf>,
    pub auto: bool,
    pub deploy: Option<PathBuf>,
    pub deploy_gpu: Option<PathBuf>,
    /// driver folder (normally %SystemRoot%\\System32\\drivers): Wi-Fi / Bluetooth firmware goes where the drivers read it
    pub deploy_drivers: Option<PathBuf>,
    pub require_known_hash: bool,
    pub quiet: bool,
    pub manifest: Option<PathBuf>,
}

struct Log {
    quiet: bool,
}
impl Log {
    fn say(&self, s: &str) {
        if !self.quiet {
            println!("{s}");
        }
    }
    fn warn(&self, s: &str) {
        eprintln!("warning: {s}");
    }
}

#[derive(Serialize, Deserialize)]
struct LockEntry {
    sha256: String,
    size: u64,
    origin: String,
    staged: String,
    verified: bool,
    utc: String,
    /// where `--deploy-drivers` puts the file, relative to the driver folder (lets the start-up task re-check it)
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    drivers: Vec<String>,
}

struct Volume {
    label: String,
    fs: Ext4,
}

struct Offer<'a> {
    expect: Option<&'a Expect>,
    got: Option<(Vec<u8>, String)>,
}
impl Offer<'_> {
    /// true once something has been accepted
    fn offer(&mut self, bytes: Vec<u8>, origin: String, log: &Log) -> bool {
        if self.got.is_some() {
            return true;
        }
        let ok = match self.expect {
            None => true,
            Some(e) => {
                e.size.is_none_or(|s| s == bytes.len() as u64)
                    && e.sha256.as_deref().is_none_or(|h| h == util::sha256_hex(&bytes))
            }
        };
        if ok {
            self.got = Some((bytes, origin));
            true
        } else {
            log.say(&format!("  reject {origin}: size/hash differs from the published value"));
            false
        }
    }
}

fn add_volumes(path: &Path, vols: &mut Vec<Volume>, log: &Log) {
    let mut d = match Aligned::open(path) {
        Ok(d) => d,
        Err(e) => {
            log.say(&format!("  (cannot open {} read-only: {e})", path.display()));
            return;
        }
    };
    let mut found = 0;
    if let Ok(fs) = Ext4::try_open(path, 0) {
        vols.push(Volume { label: format!("{} (whole)", path.display()), fs });
        found += 1;
    } else {
        for p in part::read(&mut d) {
            if let Ok(fs) = Ext4::try_open(path, p.offset) {
                vols.push(Volume { label: format!("{} partition {}", path.display(), p.index), fs });
                found += 1;
            }
        }
    }
    log.say(&format!("  {} : {found} ext4 volume(s)", path.display()));
}

/// Switchroot's installer ships the Linux root file system as `switchroot/install/l4t.00`, `l4t.01`, ... : one ext4 image cut into
/// pieces. Find such pieces under the search roots (also directly in a root) and read each set back to back, read-only,
/// so the firmware inside is found WITHOUT installing Switchroot on the Switch. Pieces are used in name order; a set must start at 00,
/// a gap ends it (what follows the gap is not read, so a partial download still yields whatever lies in the pieces that are there).
fn l4t_image_sets(roots: &[PathBuf]) -> Vec<Vec<PathBuf>> {
    let mut sets: Vec<Vec<PathBuf>> = Vec::new();
    for root in roots {
        for dir in [root.join("switchroot").join("install"), root.join("install"), root.clone()] {
            let Ok(rd) = std::fs::read_dir(&dir) else { continue };
            let mut n: BTreeMap<u32, PathBuf> = BTreeMap::new();
            for e in rd.flatten() {
                let name = e.file_name().to_string_lossy().to_ascii_lowercase();
                let Some(num) = name.strip_prefix("l4t.") else { continue };
                if num.len() == 2 && num.bytes().all(|b| b.is_ascii_digit()) && e.metadata().map(|m| m.is_file()).unwrap_or(false) {
                    n.insert(num.parse().unwrap_or(99), e.path());
                }
            }
            let mut set = Vec::new();
            for (i, (k, p)) in n.iter().enumerate() {
                if *k as usize != i {
                    break;
                }
                set.push(p.clone());
            }
            if !set.is_empty() && !sets.contains(&set) {
                sets.push(set);
            }
        }
    }
    sets
}

fn skip_ext(p: &Path) -> bool {
    let e = p.extension().map(|e| e.to_string_lossy().to_ascii_lowercase()).unwrap_or_default();
    matches!(e.as_str(), "7z" | "zip" | "bmp" | "ini" | "dtb" | "dtimg" | "scr" | "txt" | "md")
}

fn collect(entry: &Entry, roots: &[PathBuf], vols: &mut [Volume], log: &Log, off: &mut Offer) {
    let has_sha = entry.expect.as_ref().is_some_and(|e| e.sha256.is_some());
    for src in &entry.sources {
        match src.kind.as_str() {
            "fs" => {
                for root in roots {
                    for p in &src.paths {
                        for h in util::glob_files(root, p) {
                            if let Some(b) = util::read_limited(&h, MAX_FILE) {
                                if off.offer(b, format!("file {}", h.display()), log) {
                                    return;
                                }
                            }
                        }
                    }
                }
            }
            "initramfs" => {
                let Some(member) = &src.member else { continue };
                for root in roots {
                    for p in &src.scan {
                        for h in util::glob_files(root, p) {
                            let len = std::fs::metadata(&h).map(|m| m.len()).unwrap_or(0);
                            if !(16 * 1024..=512 * 1024 * 1024).contains(&len) || skip_ext(&h) {
                                continue;
                            }
                            match initramfs::try_extract(&h, std::slice::from_ref(member), 16 * 1024 * 1024) {
                                Ok(b) => {
                                    if off.offer(b, format!("initramfs {} :: {member}", h.display()), log) {
                                        return;
                                    }
                                }
                                Err(why) => {
                                    if !matches!(why.as_str(), "not an initramfs" | "too small" | "member not found") {
                                        let n = h.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
                                        log.say(&format!("  skip {n}: {why}"));
                                    }
                                }
                            }
                        }
                    }
                }
            }
            "ext4" => {
                for v in vols.iter_mut() {
                    let mut tried = BTreeSet::new();
                    for p in &src.paths {
                        tried.insert(p.rsplit('/').next().unwrap_or("").to_string());
                        match v.fs.read_file(p, MAX_FILE) {
                            Ok(Some(b)) => {
                                if off.offer(b, format!("ext4 {} :: /{p}", v.label), log) {
                                    return;
                                }
                            }
                            Ok(None) => {}
                            Err(e) => log.say(&format!("  ext4 read failed ({p}): {e}")),
                        }
                    }
                    if let (Some(sd), true) = (&src.search_dir, has_sha) {
                        let names = v.fs.list_names(sd).unwrap_or_default();
                        for n in names {
                            if tried.contains(&n) {
                                continue;
                            }
                            if let Ok(Some(b)) = v.fs.read_file(&format!("{sd}/{n}"), MAX_FILE) {
                                if off.offer(b, format!("ext4 {} :: /{sd}/{n} (found by hash)", v.label), log) {
                                    return;
                                }
                            }
                        }
                    }
                }
            }
            "ihex" => {
                for root in roots {
                    for p in &src.paths {
                        for h in util::glob_files(root, p) {
                            let Some(raw) = util::read_limited(&h, MAX_FILE) else { continue };
                            let n = h.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
                            match String::from_utf8(raw).map_err(|_| "not text".to_string()).and_then(|t| ihex::to_bin(&t)) {
                                Ok(b) => {
                                    if off.offer(b, format!("ihex {}", h.display()), log) {
                                        return;
                                    }
                                }
                                Err(e) => log.say(&format!("  skip {n}: {e}")),
                            }
                        }
                    }
                }
            }
            _ => {}
        }
    }
}

fn default_out_dir() -> PathBuf {
    if let Some(b) = std::env::var_os("LOCALAPPDATA") {
        return PathBuf::from(b).join("W4T").join("fw-cache");
    }
    if let Some(h) = std::env::var_os("HOME").or_else(|| std::env::var_os("USERPROFILE")) {
        return PathBuf::from(h).join(".w4t").join("fw-cache");
    }
    PathBuf::from("fw-cache")
}

/// Exit code: 0 all found, 2 something missing.
pub fn run(o: Opts) -> Result<i32, String> {
    let log = Log { quiet: o.quiet };
    let out_dir = std::path::absolute(o.out_dir.clone().unwrap_or_else(default_out_dir)).map_err(|e| e.to_string())?;
    util::refuse_git_tree(&out_dir)?;
    for d in [&o.deploy, &o.deploy_gpu, &o.deploy_drivers].into_iter().flatten() {
        util::refuse_git_tree(d)?;
    }

    let text = match &o.manifest {
        Some(p) => String::from_utf8(util::read_limited(p, 4 * 1024 * 1024).ok_or("cannot read the manifest (missing or > 4 MiB)")?)
            .map_err(|_| "manifest is not UTF-8")?,
        None => manifest::BUILTIN.to_string(),
    };
    let (profiles, entries) = manifest::load(&text)?;
    let ids: Vec<String> = if o.profile == "all" {
        entries.keys().cloned().collect()
    } else {
        profiles.get(&o.profile).cloned().ok_or_else(|| {
            format!("unknown profile '{}' (have: {}, all)", o.profile, profiles.keys().cloned().collect::<Vec<_>>().join(", "))
        })?
    };

    // ---- where to look ----
    let mut roots: Vec<PathBuf> = Vec::new();
    for r in &o.search_roots {
        if r.is_dir() {
            roots.push(std::path::absolute(r).map_err(|e| e.to_string())?);
        } else {
            log.warn(&format!("search root not found: {}", r.display()));
        }
    }
    if o.auto {
        if cfg!(windows) {
            // every ready drive letter - the Switch's SD partitions get arbitrary letters (Windows itself is rarely on C:)
            for l in b'A'..=b'Z' {
                let p = PathBuf::from(format!("{}:\\", l as char));
                if std::fs::metadata(&p).map(|m| m.is_dir()).unwrap_or(false) {
                    roots.push(p);
                }
            }
            // ...and volumes that have NO letter at all (typically the FAT32 'switchroot' partition), reached by volume GUID
            let hidden = winvol::unlettered_volumes();
            log.say(&format!("  {} volume(s) without a drive letter", hidden.len()));
            roots.extend(hidden);
        } else {
            log.say("  (--auto lists drive letters on Windows only; give --search-root / --disk)");
        }
    }
    let roots = winvol::dedup_roots(roots);
    for r in &roots {
        log.say(&format!("  search root: {}", r.display()));
    }

    let mut vols: Vec<Volume> = Vec::new();
    for d in &o.disks {
        add_volumes(d, &mut vols, &log);
    }
    if o.auto && cfg!(windows) {
        let mut denied = false;
        for n in 0..32 {
            let p = PathBuf::from(format!(r"\\.\PhysicalDrive{n}"));
            match std::fs::File::open(&p) {
                Ok(_) => add_volumes(&p, &mut vols, &log),
                Err(e) if e.kind() == std::io::ErrorKind::PermissionDenied => {
                    denied = true;
                    break;
                }
                Err(_) => {}
            }
        }
        if denied {
            log.say("  (not administrator: physical disks are not scanned for ext4; FAT32/folder sources still work)");
        }
    }

    for set in l4t_image_sets(&roots) {
        let names: Vec<String> = set.iter().map(|p| p.display().to_string()).collect();
        match Ext4::try_open_parts(&set, 0) {
            Ok(fs) => {
                log.say(&format!("  Switchroot image pieces ({}): ext4 volume, read-only, nothing is installed", names.join(" + ")));
                vols.push(Volume { label: format!("l4t image {}", set[0].display()), fs });
            }
            Err(e) => log.say(&format!("  Switchroot image pieces ({}): not readable as ext4 ({e})", names.join(" + "))),
        }
    }

    // ---- lock file ----
    std::fs::create_dir_all(&out_dir).map_err(|e| format!("cannot create {}: {e}", out_dir.display()))?;
    let lock_path = out_dir.join("fw-lock.json");
    let mut lock: BTreeMap<String, LockEntry> = BTreeMap::new();
    if let Some(raw) = util::read_limited(&lock_path, 4 * 1024 * 1024) {
        match serde_json::from_slice(&raw) {
            Ok(l) => lock = l,
            Err(e) => log.warn(&format!("ignoring unreadable {}: {e}", lock_path.display())),
        }
    }

    log.say(&format!(
        "fwfetch profile={}  roots={}  ext4 volumes={}  staging={}",
        o.profile,
        roots.len(),
        vols.len(),
        out_dir.display()
    ));

    let mut missing = 0usize;
    let mut staged_now: BTreeSet<String> = BTreeSet::new();
    for id in &ids {
        let Some(e) = entries.get(id) else { continue };
        let has_hash = e.expect.as_ref().is_some_and(|x| x.sha256.is_some());
        if o.require_known_hash && !has_hash {
            log.say(&format!("[skip ] {id} : no published hash and --require-known-hash"));
            missing += 1;
            continue;
        }
        let mut off = Offer { expect: e.expect.as_ref(), got: None };
        collect(e, &roots, &mut vols, &log, &mut off);
        let Some((mut bytes, origin)) = off.got else {
            log.say(&format!("[MISS ] {id} : not found ({})", e.what));
            missing += 1;
            continue;
        };
        let dest = out_dir.join(&e.stage_rel);
        if e.random_mac {
            let old = util::read_limited(&dest, MAX_FILE);
            match util::set_random_mac(&bytes, old.as_deref())? {
                Some(b) => bytes = b,
                None => log.warn("board file has no macaddr= line; left unchanged"),
            }
        }
        util::write_atomic(&dest, &bytes)?;
        let sha = util::sha256_hex(&bytes);
        if !has_hash && !e.random_mac {
            if let Some(old) = lock.get(id) {
                if old.sha256 != sha {
                    log.warn(&format!(
                        "{id} differs from the copy pinned in fw-lock.json ({} -> {}). Different firmware version?",
                        &old.sha256[..old.sha256.len().min(12)],
                        &sha[..12]
                    ));
                }
            }
        }
        let status = if has_hash { "OK (hash matches published value)" } else { "UNVERIFIED (accepted on first use)" };
        log.say(&format!(
            "[{}] {id} : {} bytes  {status}\n        from {origin}",
            if has_hash { "OK   " } else { "WARN " },
            bytes.len()
        ));
        lock.insert(
            id.clone(),
            LockEntry { sha256: sha, size: bytes.len() as u64, origin, staged: e.stage_rel.clone(), verified: has_hash, utc: util::utc_now(), drivers: e.drivers.clone() },
        );
        staged_now.insert(id.clone());
    }
    let lock_json = serde_json::to_vec_pretty(&lock).map_err(|e| e.to_string())?;
    util::write_atomic(&lock_path, &lock_json)?;

    // ---- deploy (re-verifies the staged bytes against the lock first, so a tampered staging dir is refused) ----
    let staged_bytes = |id: &str, e: &Entry| -> Result<Vec<u8>, String> {
        let b = util::read_limited(&out_dir.join(&e.stage_rel), MAX_FILE).ok_or_else(|| format!("staged copy of {id} is missing"))?;
        let want = lock.get(id).map(|l| l.sha256.as_str()).unwrap_or("");
        if util::sha256_hex(&b) != want {
            return Err(format!("staged copy of {id} no longer matches fw-lock.json; refusing to deploy it"));
        }
        Ok(b)
    };
    if let Some(dep) = &o.deploy {
        for id in &ids {
            let Some(e) = entries.get(id) else { continue };
            if !staged_now.contains(id) || e.gpu_name.is_some() {
                continue;
            }
            let b = staged_bytes(id, e)?;
            for t in &e.deploy {
                util::write_atomic(&dep.join(t), &b)?;
                log.say(&format!("deployed {t}"));
            }
        }
    }
    if let Some(dep) = &o.deploy_gpu {
        for id in &ids {
            let Some(e) = entries.get(id) else { continue };
            let Some(name) = &e.gpu_name else { continue };
            if !staged_now.contains(id) {
                continue;
            }
            util::write_atomic(&dep.join(name), &staged_bytes(id, e)?)?;
            log.say(&format!("deployed GPU {name}"));
        }
    }

    if let Some(dep) = &o.deploy_drivers {
        for id in &ids {
            let Some(e) = entries.get(id) else { continue };
            if !staged_now.contains(id) || e.drivers.is_empty() {
                continue;
            }
            let b = staged_bytes(id, e)?;
            for t in &e.drivers {
                util::write_atomic(&dep.join(t), &b)?;
                log.say(&format!("deployed driver file {t}"));
            }
        }
    }

    log.say("");
    log.say(&format!("{} of {} found. Staged copies and fw-lock.json are in {}", ids.len() - missing, ids.len(), out_dir.display()));
    Ok(if missing > 0 { 2 } else { 0 })
}
