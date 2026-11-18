// SPDX-License-Identifier: GPL-2.0-only
//! Intel HEX -> binary (the form the CTCaer kernel tree keeps Broadcom firmware in).
use std::collections::BTreeMap;

const MAX_SPAN: u64 = 64 * 1024 * 1024;

fn hex_byte(s: &str, at: usize) -> Result<u8, String> {
    let t = s.get(at..at + 2).ok_or("Intel HEX: short line")?;
    u8::from_str_radix(t, 16).map_err(|_| "Intel HEX: bad hex digit".to_string())
}

pub fn to_bin(text: &str) -> Result<Vec<u8>, String> {
    let mut mem: BTreeMap<u64, u8> = BTreeMap::new();
    let mut ext: u64 = 0;
    let mut eof = false;
    for raw in text.split('\n') {
        let line = raw.trim();
        if line.is_empty() {
            continue;
        }
        if !line.starts_with(':') || !line.is_ascii() {
            return Err("not Intel HEX".into());
        }
        let len = hex_byte(line, 1)? as usize;
        let addr = ((hex_byte(line, 3)? as u64) << 8) | hex_byte(line, 5)? as u64;
        let ty = hex_byte(line, 7)?;
        if line.len() != 1 + 2 * (5 + len) {
            return Err("Intel HEX: length mismatch".into());
        }
        let mut sum = 0u32;
        let mut i = 1;
        while i + 1 < line.len() {
            sum += hex_byte(line, i)? as u32;
            i += 2;
        }
        if sum & 0xFF != 0 {
            return Err("Intel HEX checksum error".into());
        }
        match ty {
            0 => {
                for k in 0..len {
                    mem.insert(ext + addr + k as u64, hex_byte(line, 9 + k * 2)?);
                    if mem.len() as u64 > MAX_SPAN {
                        return Err("Intel HEX: too large".into());
                    }
                }
            }
            1 => {
                eof = true;
                break;
            }
            2 => ext = (((hex_byte(line, 9)? as u64) << 8) | hex_byte(line, 11)? as u64) << 4,
            4 => ext = (((hex_byte(line, 9)? as u64) << 8) | hex_byte(line, 11)? as u64) << 16,
            _ => {}
        }
    }
    if !eof {
        return Err("Intel HEX without end record".into());
    }
    let (Some((&min, _)), Some((&max, _))) = (mem.first_key_value(), mem.last_key_value()) else {
        return Ok(Vec::new());
    };
    if max - min + 1 > MAX_SPAN {
        return Err("Intel HEX: address span too large".into());
    }
    let mut bin = vec![0u8; (max - min + 1) as usize];
    for (k, v) in mem {
        bin[(k - min) as usize] = v;
    }
    Ok(bin)
}
