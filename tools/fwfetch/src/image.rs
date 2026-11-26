// SPDX-License-Identifier: GPL-2.0-only
//! Split disk / file-system images: `l4t.00`, `l4t.01`, ... are one image cut into pieces (FAT32 cannot hold a file over 4 GiB).
use std::fs;
use std::path::{Path, PathBuf};

/// Most pieces joined (4 GiB each would already be 1 TiB).
const MAX_PARTS: usize = 256;

/// `first` plus the pieces that follow it (`name.NN` -> `name.NN+1` ... in the same folder, same digit count).
/// A file whose name does not end in `.` + at least two digits is a single image.
pub fn parts_of(first: &Path) -> Vec<PathBuf> {
    let single = || vec![first.to_path_buf()];
    let Some(name) = first.file_name().and_then(|n| n.to_str()) else { return single() };
    let Some((stem, num)) = name.rsplit_once('.') else { return single() };
    if stem.is_empty() || num.len() < 2 || !num.bytes().all(|b| b.is_ascii_digit()) {
        return single();
    }
    let Ok(mut n) = num.parse::<u64>() else { return single() };
    let width = num.len();
    let dir = first.parent().map(Path::to_path_buf).unwrap_or_default();
    let mut v = single();
    while v.len() < MAX_PARTS {
        n += 1;
        let cand = dir.join(format!("{stem}.{n:0width$}"));
        // regular files only (a symlink is not followed)
        if fs::symlink_metadata(&cand).map(|m| m.is_file()).unwrap_or(false) {
            v.push(cand);
        } else {
            break;
        }
    }
    v
}
