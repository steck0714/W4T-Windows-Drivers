// SPDX-License-Identifier: GPL-2.0-only
use sha2::{Digest, Sha256};
use std::fs;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

pub fn sha256_hex(b: &[u8]) -> String {
    let d = Sha256::digest(b);
    d.iter().map(|x| format!("{x:02x}")).collect()
}

/// `*` matches any run of characters inside one path component (ASCII case-insensitive, as FAT32 is).
pub fn wild_match(pat: &str, name: &str) -> bool {
    let p: Vec<char> = pat.to_ascii_lowercase().chars().collect();
    let n: Vec<char> = name.to_ascii_lowercase().chars().collect();
    let (mut pi, mut ni, mut star, mut mark) = (0usize, 0usize, None::<usize>, 0usize);
    while ni < n.len() {
        if pi < p.len() && p[pi] == '*' {
            star = Some(pi);
            mark = ni;
            pi += 1;
        } else if pi < p.len() && p[pi] == n[ni] {
            pi += 1;
            ni += 1;
        } else if let Some(s) = star {
            pi = s + 1;
            mark += 1;
            ni = mark;
        } else {
            return false;
        }
    }
    while pi < p.len() && p[pi] == '*' {
        pi += 1;
    }
    pi == p.len()
}

/// Regular files (symlinks are NOT followed) matching `pattern` below `root`, in name order.
pub fn glob_files(root: &Path, pattern: &str) -> Vec<PathBuf> {
    let comps: Vec<&str> = pattern.split('/').collect();
    let mut cur = vec![root.to_path_buf()];
    for (i, c) in comps.iter().enumerate() {
        let last = i + 1 == comps.len();
        let mut next = Vec::new();
        for base in &cur {
            if c.contains('*') {
                let Ok(rd) = fs::read_dir(base) else { continue };
                let mut names: Vec<PathBuf> = rd
                    .flatten()
                    .filter(|e| wild_match(c, &e.file_name().to_string_lossy()))
                    .map(|e| e.path())
                    .collect();
                names.sort();
                next.extend(names);
            } else {
                next.push(base.join(c));
            }
        }
        cur = next;
        if cur.is_empty() {
            break;
        }
        if !last {
            cur.retain(|p| fs::symlink_metadata(p).map(|m| m.is_dir()).unwrap_or(false));
        }
    }
    cur.retain(|p| fs::symlink_metadata(p).map(|m| m.is_file()).unwrap_or(false));
    cur
}

/// Reads at most `limit` bytes; None if the file is larger (never allocates more than limit+1).
pub fn read_limited(p: &Path, limit: u64) -> Option<Vec<u8>> {
    use std::io::Read;
    let f = fs::File::open(p).ok()?;
    let mut v = Vec::new();
    f.take(limit + 1).read_to_end(&mut v).ok()?;
    if v.len() as u64 > limit { None } else { Some(v) }
}

/// Refuses a directory that is inside a git work tree (looks at the path and, through symlinks, at its real location).
pub fn refuse_git_tree(dir: &Path) -> Result<(), String> {
    let abs = std::path::absolute(dir).map_err(|e| format!("bad path {}: {e}", dir.display()))?;
    let mut candidates = vec![abs.clone()];
    // real location of the longest existing ancestor + the part that does not exist yet
    let mut tail = PathBuf::new();
    let mut a = abs.as_path();
    loop {
        if let Ok(real) = fs::canonicalize(a) {
            candidates.push(real.join(&tail));
            break;
        }
        match (a.parent(), a.file_name()) {
            (Some(par), Some(name)) => {
                tail = Path::new(name).join(&tail);
                a = par;
            }
            _ => break,
        }
    }
    for c in candidates {
        for anc in c.ancestors() {
            if anc.join(".git").exists() {
                return Err(format!(
                    "Refusing to stage firmware inside a git work tree ({}). Use a directory outside the repository.",
                    anc.display()
                ));
            }
        }
    }
    Ok(())
}

/// Write via a temp file + rename; refuses to write through a symlink.
pub fn write_atomic(dest: &Path, bytes: &[u8]) -> Result<(), String> {
    use std::io::Write;
    let parent = dest.parent().ok_or("no parent directory")?;
    fs::create_dir_all(parent).map_err(|e| format!("cannot create {}: {e}", parent.display()))?;
    if let Ok(m) = fs::symlink_metadata(dest) {
        if m.file_type().is_symlink() || m.is_dir() {
            return Err(format!("refusing to overwrite {} (symlink or directory)", dest.display()));
        }
    }
    let mut tmp_name = dest.file_name().ok_or("no file name")?.to_os_string();
    tmp_name.push(".fwfetch-tmp");
    let tmp = parent.join(tmp_name);
    let _ = fs::remove_file(&tmp);
    let res = (|| -> std::io::Result<()> {
        let mut f = fs::OpenOptions::new().write(true).create_new(true).open(&tmp)?;
        f.write_all(bytes)?;
        f.sync_all()?;
        fs::rename(&tmp, dest)
    })();
    if let Err(e) = res {
        let _ = fs::remove_file(&tmp);
        return Err(format!("cannot write {}: {e}", dest.display()));
    }
    Ok(())
}

pub fn utc_now() -> String {
    let secs = SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0) as i64;
    let (days, rem) = (secs.div_euclid(86400), secs.rem_euclid(86400));
    // Howard Hinnant's civil-from-days
    let z = days + 719468;
    let era = z.div_euclid(146097);
    let doe = z.rem_euclid(146097);
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let m = if mp < 10 { mp + 3 } else { mp - 9 };
    let y = yoe + era * 400 + if m <= 2 { 1 } else { 0 };
    format!("{y:04}-{m:02}-{d:02}T{:02}:{:02}:{:02}Z", rem / 3600, rem % 3600 / 60, rem % 60)
}

/// `macaddr=xx:xx:xx:xx:xx:xx` already present in a previously staged board file.
pub fn existing_mac(b: &[u8]) -> Option<String> {
    for line in b.split(|&c| c == b'\n') {
        if let Some(rest) = line.strip_prefix(b"macaddr=") {
            if rest.len() >= 17 && rest[..17].iter().all(|c| c.is_ascii_hexdigit() || *c == b':') {
                return Some(String::from_utf8_lossy(&rest[..17]).into_owned());
            }
        }
    }
    None
}

/// Replaces every `macaddr=` line with a locally administered unicast address (reusing `existing` so re-runs are stable).
/// Returns None if the board file has no `macaddr=` line.
pub fn set_random_mac(board: &[u8], existing: Option<&[u8]>) -> Result<Option<Vec<u8>>, String> {
    if !board.split(|&c| c == b'\n').any(|l| l.starts_with(b"macaddr=")) {
        return Ok(None);
    }
    let mac = match existing.and_then(existing_mac) {
        Some(m) => m,
        None => {
            let mut r = [0u8; 6];
            getrandom::fill(&mut r).map_err(|e| format!("no random source: {e}"))?;
            r[0] = (r[0] & 0xFC) | 0x02;
            r.iter().map(|x| format!("{x:02x}")).collect::<Vec<_>>().join(":")
        }
    };
    let mut out = Vec::with_capacity(board.len());
    for (i, line) in board.split(|&c| c == b'\n').enumerate() {
        if i > 0 {
            out.push(b'\n');
        }
        if line.starts_with(b"macaddr=") {
            out.extend_from_slice(b"macaddr=");
            out.extend_from_slice(mac.as_bytes());
            if line.ends_with(b"\r") {
                out.push(b'\r');
            }
        } else {
            out.extend_from_slice(line);
        }
    }
    Ok(Some(out))
}
