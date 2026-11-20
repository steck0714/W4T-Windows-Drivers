// SPDX-License-Identifier: GPL-2.0-only
//! Things that must NOT assume "Windows is on C:".
//!
//! Windows on a Switch lives on an SD card, so the system volume is often not C:, the FAT32 "switchroot" partition
//! frequently has no drive letter at all, and letters can change between boots (or when the card is read in a PC).
//! This module therefore
//!   * resolves the place the running Windows keeps its drivers from %SystemRoot% (never from a typed-in letter),
//!   * expands %VAR% tokens in paths (a scheduled task or a script may hand them over unexpanded), and
//!   * lists volumes that have NO drive letter (as \\?\Volume{GUID}\ roots) so their `switchroot\` folder is searched too.
//!
//! Everything here is read-only; the only external program started is mountvol.exe (list mode, no arguments).
use std::path::{Path, PathBuf};

/// `%NAME%` -> value of the environment variable (ASCII case-insensitive name, as on Windows). Unknown or empty variables
/// are an error: a path that silently lost its drive part could end up somewhere else.
pub fn expand_env(s: &str) -> Result<String, String> {
    expand_env_with(s, |name| {
        std::env::vars_os()
            .find(|(k, _)| k.to_string_lossy().eq_ignore_ascii_case(name))
            .map(|(_, v)| v.to_string_lossy().into_owned())
    })
}

pub fn expand_env_with(s: &str, get: impl Fn(&str) -> Option<String>) -> Result<String, String> {
    let mut out = String::with_capacity(s.len());
    let mut rest = s;
    while let Some(i) = rest.find('%') {
        out.push_str(&rest[..i]);
        let after = &rest[i + 1..];
        match after.find('%') {
            // "%%" or an unmatched "%": keep a literal percent sign
            Some(0) => {
                out.push('%');
                rest = &after[1..];
            }
            Some(j) if after[..j].chars().all(|c| c.is_ascii_alphanumeric() || c == '_' || c == '(' || c == ')') => {
                let name = &after[..j];
                match get(name).filter(|v| !v.is_empty()) {
                    Some(v) => out.push_str(&v),
                    None => return Err(format!("environment variable %{name}% is not set (needed for the path '{s}')")),
                }
                rest = &after[j + 1..];
            }
            _ => {
                out.push('%');
                rest = after;
            }
        }
    }
    out.push_str(rest);
    Ok(out)
}

/// Where the RUNNING Windows keeps its drivers: `<SystemRoot>\System32\drivers` (SystemRoot is whatever drive Windows booted from).
pub fn system_drivers_dir() -> Result<PathBuf, String> {
    system_drivers_dir_from(std::env::var_os("SystemRoot").or_else(|| std::env::var_os("windir")).map(PathBuf::from))
}

pub fn system_drivers_dir_from(system_root: Option<PathBuf>) -> Result<PathBuf, String> {
    let root = system_root
        .filter(|p| !p.as_os_str().is_empty())
        .ok_or("'auto' needs the running Windows (%SystemRoot% is not set). Give the folder explicitly, e.g. X:\\Windows\\System32\\drivers\\TegraGpu")?;
    Ok(root.join("System32").join("drivers"))
}

/// A `--deploy*` / `--out-dir` value: `auto` (only where the caller allows it) -> `<SystemRoot>\System32\drivers[\sub]`,
/// otherwise the text with %VAR% expanded.
pub fn resolve_dir(value: &str, auto_sub: Option<&str>) -> Result<PathBuf, String> {
    if value.eq_ignore_ascii_case("auto") {
        return match auto_sub {
            Some(sub) => {
                let d = system_drivers_dir()?;
                Ok(if sub.is_empty() { d } else { d.join(sub) })
            }
            None => Err("'auto' is not accepted here; give a folder".into()),
        };
    }
    Ok(PathBuf::from(expand_env(value)?))
}

/// A drive-letter root such as `E:\` (what `mountvol` prints for lettered volumes).
fn is_letter_root(s: &str) -> bool {
    let b = s.as_bytes();
    b.len() == 3 && b[0].is_ascii_alphabetic() && b[1] == b':' && b[2] == b'\\'
}

/// Parses the output of `mountvol` (no arguments) and returns the `\\?\Volume{GUID}\` roots that are NOT reachable through a
/// plain drive letter (no mount point, or only folder mount points). Lettered volumes are covered by the A:..Z: scan.
pub fn parse_mountvol(text: &str) -> Vec<String> {
    let mut out = Vec::new();
    let mut cur: Option<(String, bool)> = None; // (guid path, has a drive letter)
    let flush = |cur: &mut Option<(String, bool)>, out: &mut Vec<String>| {
        if let Some((g, lettered)) = cur.take() {
            if !lettered {
                out.push(g);
            }
        }
    };
    for line in text.lines() {
        let l = line.trim();
        if l.starts_with(r"\\?\Volume{") && l.ends_with('\\') && l.len() > 12 && !l.contains(' ') {
            flush(&mut cur, &mut out);
            cur = Some((l.to_string(), false));
        } else if let Some((_, lettered)) = cur.as_mut() {
            if is_letter_root(l) {
                *lettered = true;
            }
        }
    }
    flush(&mut cur, &mut out);
    out.dedup();
    out
}

/// Volumes of this PC without a drive letter, as `\\?\Volume{GUID}\` roots that exist. Empty when mountvol cannot be run
/// (not Windows, or blocked). Uses the copy in `%SystemRoot%\System32` (never a PATH lookup: this may run as SYSTEM).
pub fn unlettered_volumes() -> Vec<PathBuf> {
    let Some(sys) = std::env::var_os("SystemRoot").or_else(|| std::env::var_os("windir")) else { return vec![] };
    let exe = Path::new(&sys).join("System32").join("mountvol.exe");
    let Ok(o) = std::process::Command::new(exe).stdin(std::process::Stdio::null()).output() else { return vec![] };
    if !o.status.success() {
        return vec![];
    }
    parse_mountvol(&String::from_utf8_lossy(&o.stdout))
        .into_iter()
        .map(PathBuf::from)
        .filter(|p| std::fs::metadata(p).map(|m| m.is_dir()).unwrap_or(false))
        .collect()
}

/// Removes repeated roots (ASCII case-insensitive, trailing separators ignored), keeping the first of each.
pub fn dedup_roots(roots: Vec<PathBuf>) -> Vec<PathBuf> {
    let mut seen = std::collections::BTreeSet::new();
    let mut out = Vec::new();
    for r in roots {
        let key = r.to_string_lossy().to_ascii_lowercase().trim_end_matches(['\\', '/']).to_string();
        if seen.insert(key) {
            out.push(r);
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn expands_known_variables_case_insensitively() {
        let get = |n: &str| if n.eq_ignore_ascii_case("SystemRoot") { Some("D:\\Windows".to_string()) } else { None };
        assert_eq!(expand_env_with("%SystemRoot%\\System32", get).unwrap(), "D:\\Windows\\System32");
        assert_eq!(expand_env_with("%systemroot%/x", get).unwrap(), "D:\\Windows/x");
        assert_eq!(expand_env_with("100%% sure", get).unwrap(), "100% sure");
        assert_eq!(expand_env_with("E:\\plain", get).unwrap(), "E:\\plain");
    }

    #[test]
    fn unknown_variable_is_an_error_not_an_empty_string() {
        assert!(expand_env_with("%NOPE%\\x", |_| None).is_err());
        assert!(expand_env_with("%SystemRoot%\\x", |_| Some(String::new())).is_err());
    }

    #[test]
    fn auto_uses_the_running_windows_not_c() {
        let d = system_drivers_dir_from(Some(PathBuf::from("G:\\Windows"))).unwrap();
        assert_eq!(d, Path::new("G:\\Windows").join("System32").join("drivers"));
        assert!(system_drivers_dir_from(None).is_err());
        assert!(system_drivers_dir_from(Some(PathBuf::new())).is_err());
    }

    #[test]
    fn mountvol_keeps_only_volumes_without_a_drive_letter() {
        let t = "Possible values for VolumeName along with current mount points are:\r\n\r\n\
    \\\\?\\Volume{11111111-1111-1111-1111-111111111111}\\\r\n        D:\\\r\n\r\n\
    \\\\?\\Volume{22222222-2222-2222-2222-222222222222}\\\r\n        *** NO MOUNT POINTS ***\r\n\r\n\
    \\\\?\\Volume{33333333-3333-3333-3333-333333333333}\\\r\n        C:\\mnt\\sd\\\r\n\r\n\
    \\\\?\\Volume{44444444-4444-4444-4444-444444444444}\\\r\n        C:\\mnt\\other\\\r\n        F:\\\r\n";
        let v = parse_mountvol(t);
        assert_eq!(
            v,
            vec![
                "\\\\?\\Volume{22222222-2222-2222-2222-222222222222}\\".to_string(),
                "\\\\?\\Volume{33333333-3333-3333-3333-333333333333}\\".to_string()
            ]
        );
        assert!(parse_mountvol("").is_empty());
        assert!(parse_mountvol("garbage \\\\?\\Volume{x} not a root").is_empty());
    }

    #[test]
    fn dedup_ignores_case_and_trailing_separator() {
        let v = dedup_roots(vec![PathBuf::from("E:\\"), PathBuf::from("e:\\"), PathBuf::from("F:\\"), PathBuf::from("E:")]);
        assert_eq!(v, vec![PathBuf::from("E:\\"), PathBuf::from("F:\\")]);
    }
}
