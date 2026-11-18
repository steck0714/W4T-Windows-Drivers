// SPDX-License-Identifier: GPL-2.0-only
//! MBR / GPT partition table reader (read-only).
use crate::reader::Aligned;

pub struct PartInfo {
    pub index: u32,
    pub offset: u64,
}

fn le32(b: &[u8], o: usize) -> u32 {
    b.get(o..o + 4).map(|s| u32::from_le_bytes([s[0], s[1], s[2], s[3]])).unwrap_or(0)
}
fn le64(b: &[u8], o: usize) -> u64 {
    b.get(o..o + 8).map(|s| u64::from_le_bytes([s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7]])).unwrap_or(0)
}

pub fn read(d: &mut Aligned) -> Vec<PartInfo> {
    let mut list = Vec::new();
    let Ok(b) = d.read_vec(0, 512) else { return list };
    if b[510] != 0x55 || b[511] != 0xAA {
        return list;
    }
    let mut gpt = false;
    for i in 0..4usize {
        let o = 446 + i * 16;
        let ty = b[o + 4];
        let lba = le32(&b, o + 8) as u64;
        let cnt = le32(&b, o + 12) as u64;
        if ty == 0xEE {
            gpt = true;
        } else if ty != 0 && ty != 0x05 && ty != 0x0F && ty != 0x85 && cnt > 0 {
            list.push(PartInfo { index: i as u32 + 1, offset: lba * 512 });
        }
    }
    if !gpt {
        return list;
    }
    list.clear();
    let Ok(h) = d.read_vec(512, 512) else { return list };
    if &h[0..8] != b"EFI PART" {
        return list;
    }
    let ent_lba = le64(&h, 72);
    let num = le32(&h, 80) as u64;
    let esz = le32(&h, 84) as u64;
    if !(128..=4096).contains(&esz) || num > 256 {
        return list;
    }
    for i in 0..num {
        let Some(off) = ent_lba.checked_mul(512).and_then(|x| x.checked_add(i * esz)) else { break };
        let Ok(e) = d.read_vec(off, esz as usize) else { break };
        if e[0..16].iter().all(|&x| x == 0) {
            continue;
        }
        let first = le64(&e, 32);
        let last = le64(&e, 40);
        if last < first || last >= (1u64 << 52) {
            continue;
        }
        list.push(PartInfo { index: i as u32 + 1, offset: first * 512 });
    }
    list
}
