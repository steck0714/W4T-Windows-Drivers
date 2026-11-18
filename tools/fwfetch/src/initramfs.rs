// SPDX-License-Identifier: GPL-2.0-only
//! Reads one member out of an initramfs file: (U-Boot uImage-wrapped) gzip or plain cpio "newc".
//! Decompression is capped, member names are never used as output paths, and nothing is written anywhere.
use crate::reader::read_fill;
use flate2::read::GzDecoder;
use std::fs::File;
use std::io::{BufReader, Read, Seek, SeekFrom};
use std::path::Path;

/// Upper bound on decompressed bytes examined (decompression-bomb guard).
const MAX_STREAM: u64 = 1 << 30;

pub fn try_extract(file: &Path, suffixes: &[String], max_bytes: u64) -> Result<Vec<u8>, String> {
    let mut f = File::open(file).map_err(|e| e.to_string())?;
    let mut head = [0u8; 64];
    let n = read_fill(&mut f, &mut head).map_err(|e| e.to_string())?;
    if n < 8 {
        return Err("too small".into());
    }
    let mut skip = 0u64;
    if head[0..4] == [0x27, 0x05, 0x19, 0x56] {
        // U-Boot legacy uImage: 64-byte header, byte 31 = compression (0 none, 1 gzip)
        skip = 64;
        if head[31] > 1 {
            return Err(format!("uImage compression {} not supported", head[31]));
        }
        f.seek(SeekFrom::Start(skip)).map_err(|e| e.to_string())?;
        let mut h8 = [0u8; 8];
        read_fill(&mut f, &mut h8).map_err(|e| e.to_string())?;
        head[..8].copy_from_slice(&h8);
    }
    f.seek(SeekFrom::Start(skip)).map_err(|e| e.to_string())?;
    let rd = BufReader::new(f);
    if head[0] == 0x1F && head[1] == 0x8B {
        find_member(GzDecoder::new(rd).take(MAX_STREAM), suffixes, max_bytes)
    } else if &head[0..4] == b"0707" {
        find_member(rd.take(MAX_STREAM), suffixes, max_bytes)
    } else if head[0..4] == [0x28, 0xB5, 0x2F, 0xFD] {
        Err("zstd initramfs not supported".into())
    } else if head[0..4] == [0x04, 0x22, 0x4D, 0x18] {
        Err("lz4 initramfs not supported".into())
    } else if head[0..4] == [0xFD, 0x37, 0x7A, 0x58] {
        Err("xz initramfs not supported".into())
    } else {
        Err("not an initramfs".into())
    }
}

fn hex_field(h: &[u8], o: usize) -> Result<u64, String> {
    let s = std::str::from_utf8(h.get(o..o + 8).ok_or("truncated cpio")?).map_err(|_| "not newc cpio")?;
    u64::from_str_radix(s, 16).map_err(|_| "not newc cpio".to_string())
}

fn skip<R: Read>(r: &mut R, mut n: u64) -> Result<(), String> {
    let mut buf = [0u8; 65536];
    while n > 0 {
        let want = n.min(buf.len() as u64) as usize;
        let got = r.read(&mut buf[..want]).map_err(|_| "corrupt gzip".to_string())?;
        if got == 0 {
            return Err("truncated cpio".into());
        }
        n -= got as u64;
    }
    Ok(())
}

fn find_member<R: Read>(mut s: R, suffixes: &[String], max_bytes: u64) -> Result<Vec<u8>, String> {
    let mut h = [0u8; 110];
    loop {
        let got = read_fill(&mut s, &mut h).map_err(|_| "corrupt gzip".to_string())?;
        if got == 0 {
            break;
        }
        if got != 110 {
            return Err("truncated cpio".into());
        }
        if &h[0..6] != b"070701" && &h[0..6] != b"070702" {
            return Err("not newc cpio".into());
        }
        let size = hex_field(&h, 54)?;
        let name_len = hex_field(&h, 94)?;
        let mode = hex_field(&h, 14)?;
        if name_len == 0 || name_len > 4096 {
            return Err("bad cpio name length".into());
        }
        let mut nb = vec![0u8; name_len as usize];
        if read_fill(&mut s, &mut nb).map_err(|_| "corrupt gzip".to_string())? != nb.len() {
            return Err("truncated cpio".into());
        }
        let name_raw = String::from_utf8_lossy(&nb[..nb.len() - 1]).into_owned();
        // used only for matching against the manifest; never as a filesystem path
        let name = name_raw.trim_start_matches("./").trim_start_matches('/');
        skip(&mut s, (4 - (110 + name_len) % 4) % 4)?;
        if name == "TRAILER!!!" {
            break;
        }
        let want = suffixes.iter().any(|suf| {
            let suf = suf.trim_start_matches('/');
            name == suf || name.ends_with(&format!("/{suf}"))
        });
        if want && mode & 0xF000 == 0x8000 {
            if size > max_bytes {
                return Err("member too large".into());
            }
            let mut d = vec![0u8; size as usize];
            if read_fill(&mut s, &mut d).map_err(|_| "corrupt gzip".to_string())? as u64 != size {
                return Err("truncated cpio".into());
            }
            return Ok(d);
        }
        skip(&mut s, size + (4 - size % 4) % 4)?;
    }
    Err("member not found".into())
}
