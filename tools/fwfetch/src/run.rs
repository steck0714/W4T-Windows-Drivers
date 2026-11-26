// SPDX-License-Identifier: GPL-2.0-only
//! Orchestration: find candidates for each manifest entry, accept only those that match the published size/hash,
//! stage them OUTSIDE the repository, pin them in fw-lock.json, optionally deploy.
use crate::archive::{self, Archive};
use crate::download;
use crate::ext4::Ext4;
use crate::manifest::{self, Entry, Expect};
use crate::reader::Aligned;
use crate::{ihex, image, initramfs, part, util, winvol};
use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, BTreeSet};
use std::path::{Path, PathBuf};

const MAX_FILE: u64 = 64 * 1024 * 1024;

pub struct Opts {
    pub profile: String,
    pub out_dir: Option<PathBuf>,
    pub search_roots: Vec<PathBuf>,
    pub disks: Vec<PathBuf>,
    /// .deb / tar / tar.gz / .tbz2 / .tar.xz files that are read like a root file system
    pub archives: Vec<PathBuf>,
    /// fetch what is still missing from the official downloads of the manifest
    pub download: bool,
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

/// Something that looks like a root file system: an ext4 volume, or the wanted members of an archive.
enum Fs {
    Ext4(Box<Ext4>),
    Archive(Archive),
}

impl Fs {
    fn read_file(&mut self, p: &str, max: u64) -> Result<Option<Vec<u8>>, String> {
        match self {
            Fs::Ext4(e) => e.read_file(p, max),
            Fs::Archive(a) => Ok(a.read_file(p, max)),
        }
    }
    fn list_names(&mut self, dir: &str) -> Result<Vec<String>, String> {
        match self {
            Fs::Ext4(e) => e.list_names(dir),
            Fs::Archive(a) => Ok(a.list_names(dir)),
        }
    }
}

struct Volume {
    label: String,
    fs: Fs,
}

struct Offer<'a> {
    expect: Option<&'a Expect>,
    got: Option<(Vec<u8>, String)>,
    /// the accepted bytes matched a SHA-256 (the entry's, or the one the accepting source carries)
    verified: bool,
}
impl Offer<'_> {
    /// true once something has been accepted. `src_hash` is the SHA-256 the offering source itself requires (aliases).
    fn offer(&mut self, bytes: Vec<u8>, origin: String, log: &Log, src_hash: Option<&str>) -> bool {
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
        if !ok {
            log.say(&format!("  reject {origin}: size/hash differs from the published value"));
            return false;
        }
        if let Some(h) = src_hash {
            if util::sha256_hex(&bytes) != h {
                log.say(&format!("  reject {origin}: this name is accepted only with the documented SHA-256 ({}...)", &h[..12]));
                return false;
            }
            self.verified = true;
        }
        self.got = Some((bytes, origin));
        true
    }
}

fn add_image(path: &Path, vols: &mut Vec<Volume>, log: &Log) {
    // a numbered piece (l4t.00) is joined with the pieces after it, so a split root-fs image reads as one
    let parts = image::parts_of(path);
    let open = || Aligned::open_parts(&parts);
    let mut d = match open() {
        Ok(d) => d,
        Err(e) => {
            log.say(&format!("  (cannot open {} read-only: {e})", path.display()));
            return;
        }
    };
    let what = if parts.len() > 1 { format!("{} (+{} more piece(s))", path.display(), parts.len() - 1) } else { path.display().to_string() };
    let mut found = 0;
    if let Some(fs) = open().ok().and_then(|a| Ext4::try_open_with(a, 0).ok()) {
        vols.push(Volume { label: format!("{what} (whole)"), fs: Fs::Ext4(Box::new(fs)) });
        found += 1;
    } else {
        for p in part::read(&mut d) {
            if let Some(fs) = open().ok().and_then(|a| Ext4::try_open_with(a, p.offset).ok()) {
                vols.push(Volume { label: format!("{what} partition {}", p.index), fs: Fs::Ext4(Box::new(fs)) });
                found += 1;
            }
        }
    }
    log.say(&format!("  {what} : {found} ext4 volume(s)"));
}

/// Every path an `ext4` source of the chosen entries could ask for (so an archive keeps only those members in memory).
fn wanted_paths(ids: &[String], entries: &BTreeMap<String, Entry>) -> (BTreeSet<String>, Vec<String>) {
    let (mut files, mut dirs) = (BTreeSet::new(), Vec::new());
    for e in ids.iter().filter_map(|i| entries.get(i)) {
        for s in e.sources.iter().filter(|s| s.kind == "ext4" || s.kind == "fs") {
            files.extend(s.paths.iter().filter(|p| !p.contains('*')).cloned());
            if let Some(d) = &s.search_dir {
                dirs.push(format!("{}/", d.trim_matches('/')));
            }
        }
    }
    (files, dirs)
}

fn add_archive(path: &Path, files: &BTreeSet<String>, dirs: &[String], vols: &mut Vec<Volume>, log: &Log) {
    log.say(&format!("  reading archive {} (a large one can take a minute)", path.display()));
    let wanted = |k: &str| files.contains(k) || dirs.iter().any(|d| k.strip_prefix(d.as_str()).is_some_and(|r| !r.contains('/')));
    match archive::open(path, &wanted, MAX_FILE) {
        Ok(a) => {
            for n in &a.notes {
                log.say(&format!("  note: {n}"));
            }
            log.say(&format!("  {} : {} wanted file(s) found", path.display(), a.len()));
            vols.push(Volume { label: format!("archive {}", path.display()), fs: Fs::Archive(a) });
        }
        Err(e) => log.warn(&format!("cannot use archive {}: {e}", path.display())),
    }
}

/// Downloads one official source and keeps the wanted members in memory, as a volume. Never fatal: a problem is reported and the
/// next start tries again.
fn add_download(d: &manifest::Download, files: &BTreeSet<String>, dirs: &[String], vols: &mut Vec<Volume>, log: &Log) {
    let url = match download::resolve(d) {
        Ok(u) => u,
        Err(e) => {
            log.warn(&format!("download '{}': {e}", d.id));
            return;
        }
    };
    log.say(&format!("  downloading {} ({}): {url}  (a large package can take many minutes)", d.id, d.what));
    let st = match download::open_stream(&url) {
        Ok(s) => s,
        Err(e) => {
            log.warn(&format!("download '{}': {e}", d.id));
            return;
        }
    };
    let cell = std::rc::Rc::new(std::cell::RefCell::new(Some(st)));
    struct Shared(std::rc::Rc<std::cell::RefCell<Option<download::Stream>>>);
    impl std::io::Read for Shared {
        fn read(&mut self, b: &mut [u8]) -> std::io::Result<usize> {
            self.0.borrow_mut().as_mut().map_or(Ok(0), |s| s.read(b))
        }
    }
    let wanted = |k: &str| files.contains(k) || dirs.iter().any(|d| k.strip_prefix(d.as_str()).is_some_and(|r| !r.contains('/')));
    let res = archive::open_reader(Box::new(std::io::BufReader::with_capacity(1 << 20, Shared(cell.clone()))), &wanted, MAX_FILE);
    let said = cell.borrow_mut().take().map(download::Stream::finish).unwrap_or_default();
    match res {
        Ok(a) => {
            for n in &a.notes {
                log.say(&format!("  note: {n}"));
            }
            log.say(&format!("  download '{}': {} wanted file(s) found", d.id, a.len()));
            vols.push(Volume { label: format!("download {} ({url})", d.id), fs: Fs::Archive(a) });
        }
        Err(e) => log.warn(&format!("download '{}' failed: {e}{}", d.id, if said.is_empty() { String::new() } else { format!(" [{said}]") })),
    }
}

fn skip_ext(p: &Path) -> bool {
    let e = p.extension().map(|e| e.to_string_lossy().to_ascii_lowercase()).unwrap_or_default();
    matches!(e.as_str(), "7z" | "zip" | "bmp" | "ini" | "dtb" | "dtimg" | "scr" | "txt" | "md")
}

fn collect(entry: &Entry, roots: &[PathBuf], vols: &mut [Volume], log: &Log, off: &mut Offer, strict: bool) {
    let has_sha = entry.expect.as_ref().is_some_and(|e| e.sha256.is_some());
    for src in &entry.sources {
        // --require-known-hash: only sources that can be checked against a SHA-256
        if strict && !has_sha && src.sha256.is_none() {
            continue;
        }
        let sh = src.sha256.as_deref();
        match src.kind.as_str() {
            "fs" => {
                for v in vols.iter() {
                    if let Fs::Archive(a) = &v.fs {
                        for p in src.paths.iter().filter(|p| !p.contains('*')) {
                            if let Some(b) = a.read_file(p, MAX_FILE) {
                                if off.offer(b, format!("{} :: {p}", v.label), log, sh) {
                                    return;
                                }
                            }
                        }
                    }
                }
                for root in roots {
                    for p in &src.paths {
                        for h in util::glob_files(root, p) {
                            if let Some(b) = util::read_limited(&h, MAX_FILE) {
                                if off.offer(b, format!("file {}", h.display()), log, sh) {
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
                                    if off.offer(b, format!("initramfs {} :: {member}", h.display()), log, sh) {
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
                                if off.offer(b, format!("ext4 {} :: /{p}", v.label), log, sh) {
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
                                if off.offer(b, format!("ext4 {} :: /{sd}/{n} (found by hash)", v.label), log, sh) {
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
                                    if off.offer(b, format!("ihex {}", h.display()), log, sh) {
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
    let (profiles, entries, downloads) = manifest::load(&text)?;
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
    let mut seen_images: BTreeSet<PathBuf> = BTreeSet::new();
    for d in &o.disks {
        seen_images.insert(std::fs::canonicalize(d).unwrap_or_else(|_| d.clone()));
        add_image(d, &mut vols, &log);
    }
    // The Switchroot download keeps its root file system as switchroot/install/l4t.00, .01, ... (cut into pieces for FAT32):
    // read it where it lies, no flashing to a partition needed. (File names from memory of the Switchroot packages: not verified.)
    for root in &roots {
        for h in util::glob_files(root, "switchroot/install/l4t.00") {
            if seen_images.insert(std::fs::canonicalize(&h).unwrap_or_else(|_| h.clone())) {
                add_image(&h, &mut vols, &log);
            }
        }
    }
    if !o.archives.is_empty() {
        let (files, dirs) = wanted_paths(&ids, &entries);
        for a in &o.archives {
            if a.is_file() {
                add_archive(a, &files, &dirs, &mut vols, &log);
            } else {
                log.warn(&format!("archive not found: {}", a.display()));
            }
        }
    }
    if o.auto && cfg!(windows) {
        let mut denied = false;
        for n in 0..32 {
            let p = PathBuf::from(format!(r"\\.\PhysicalDrive{n}"));
            match std::fs::File::open(&p) {
                Ok(_) => add_image(&p, &mut vols, &log),
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
        "fwfetch profile={}  roots={}  volumes/archives={}  staging={}",
        o.profile,
        roots.len(),
        vols.len(),
        out_dir.display()
    ));

    let mut missing = 0usize;
    let mut staged_now: BTreeSet<String> = BTreeSet::new();
    let mut tried_downloads: BTreeSet<String> = BTreeSet::new();
    for id in &ids {
        let Some(e) = entries.get(id) else { continue };
        let has_hash = e.expect.as_ref().is_some_and(|x| x.sha256.is_some());
        if o.require_known_hash && !has_hash && !e.sources.iter().any(|s| s.sha256.is_some()) {
            log.say(&format!("[skip ] {id} : no published hash and --require-known-hash"));
            missing += 1;
            continue;
        }
        let mut off = Offer { expect: e.expect.as_ref(), got: None, verified: false };
        collect(e, &roots, &mut vols, &log, &mut off, o.require_known_hash);
        if off.got.is_none() && o.download {
            let mut fetched_now = false;
            for d in downloads.iter().filter(|d| d.provides.contains(id)) {
                if !tried_downloads.insert(d.id.clone()) {
                    continue;
                }
                let (files, dirs) = wanted_paths(&ids, &entries);
                add_download(d, &files, &dirs, &mut vols, &log);
                fetched_now = true;
            }
            if fetched_now {
                collect(e, &roots, &mut vols, &log, &mut off, o.require_known_hash);
            }
        }
        let verified = has_hash || off.verified;
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
        if !verified && !e.random_mac {
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
        let status = if verified { "OK (hash matches published value)" } else { "UNVERIFIED (accepted on first use)" };
        log.say(&format!(
            "[{}] {id} : {} bytes  {status}\n        from {origin}",
            if verified { "OK   " } else { "WARN " },
            bytes.len()
        ));
        lock.insert(
            id.clone(),
            LockEntry { sha256: sha, size: bytes.len() as u64, origin, staged: e.stage_rel.clone(), verified, utc: util::utc_now(), drivers: e.drivers.clone() },
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
