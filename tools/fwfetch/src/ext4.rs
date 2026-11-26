// SPDX-License-Identifier: GPL-2.0-only
//! Minimal READ-ONLY ext2/3/4 reader: extents, symlinks, linear directory scan. No mounting, no driver.
//! Every offset taken from the (untrusted) image is bounds- and overflow-checked; failures are errors, not panics.
use crate::reader::Aligned;
use std::collections::{BTreeMap, HashMap};
use std::rc::Rc;

type Res<T> = Result<T, String>;

const S_IFMT: u16 = 0xF000;
const S_IFDIR: u16 = 0x4000;
const S_IFREG: u16 = 0x8000;
const S_IFLNK: u16 = 0xA000;
const EXTENTS_FL: u32 = 0x80000;

fn u16le(b: &[u8], o: usize) -> Res<u16> {
    b.get(o..o + 2).map(|s| u16::from_le_bytes([s[0], s[1]])).ok_or_else(|| "ext4: truncated structure".to_string())
}
fn u32le(b: &[u8], o: usize) -> Res<u32> {
    b.get(o..o + 4).map(|s| u32::from_le_bytes([s[0], s[1], s[2], s[3]])).ok_or_else(|| "ext4: truncated structure".to_string())
}

pub struct Ext4 {
    d: Aligned,
    base: u64,
    bs: u64,
    inode_size: u64,
    desc_size: u64,
    ipg: u64,
    first_data: u64,
    is64: bool,
    itable: HashMap<u64, u64>,
    dirs: HashMap<u64, Rc<BTreeMap<String, u64>>>,
}

impl Ext4 {
    /// Err(why) if there is no ext superblock at `base` or the volume uses features we cannot read.
    /// `d` is an already opened source (a raw disk, or several joined image parts).
    pub fn try_open_with(mut d: Aligned, base: u64) -> Res<Ext4> {
        let sb_off = base.checked_add(1024).ok_or("bad offset")?;
        let sb = d.read_vec(sb_off, 1024).map_err(|_| "short read".to_string())?;
        if u16le(&sb, 56)? != 0xEF53 {
            return Err("no ext superblock".into());
        }
        let log = u32le(&sb, 24)?;
        if log > 6 {
            return Err("bad superblock".into());
        }
        let bs = 1024u64 << log;
        let ipg = u32le(&sb, 40)? as u64;
        let first_data = u32le(&sb, 20)? as u64;
        let rev = u32le(&sb, 76)?;
        let inode_size = if rev == 0 { 128 } else { u16le(&sb, 88)? as u64 };
        let incompat = u32le(&sb, 96)?;
        let is64 = incompat & 0x80 != 0;
        let mut desc_size = if is64 { u16le(&sb, 254)? as u64 } else { 32 };
        if desc_size < 32 {
            desc_size = 32;
        }
        if incompat & 0x8000 != 0 || incompat & 0x10000 != 0 {
            return Err("inline_data/encrypted volume not supported".into());
        }
        if ipg == 0 || inode_size < 128 || inode_size > bs || desc_size > 1024 {
            return Err("bad superblock".into());
        }
        Ok(Ext4 { d, base, bs, inode_size, desc_size, ipg, first_data, is64, itable: HashMap::new(), dirs: HashMap::new() })
    }

    fn read_at(&mut self, off: u64, n: usize) -> Res<Vec<u8>> {
        let abs = self.base.checked_add(off).ok_or("ext4: offset overflow")?;
        self.d.read_vec(abs, n).map_err(|_| format!("ext4: short read at {off}"))
    }

    fn block_off(&self, blk: u64) -> Res<u64> {
        blk.checked_mul(self.bs).ok_or_else(|| "ext4: block number overflow".to_string())
    }

    fn inode(&mut self, ino: u64) -> Res<Vec<u8>> {
        if ino == 0 {
            return Err("ext4: inode 0".into());
        }
        let grp = (ino - 1) / self.ipg;
        let idx = (ino - 1) % self.ipg;
        let tbl = match self.itable.get(&grp) {
            Some(t) => *t,
            None => {
                let gd_off = self.block_off(self.first_data + 1)? + grp.checked_mul(self.desc_size).ok_or("ext4: group overflow")?;
                let gd = self.read_at(gd_off, self.desc_size as usize)?;
                let mut t = u32le(&gd, 8)? as u64;
                if self.is64 && self.desc_size >= 64 {
                    t |= (u32le(&gd, 0x28)? as u64) << 32;
                }
                self.itable.insert(grp, t);
                t
            }
        };
        let off = self.block_off(tbl)? + idx.checked_mul(self.inode_size).ok_or("ext4: inode overflow")?;
        self.read_at(off, self.inode_size as usize)
    }

    fn size(ino: &[u8]) -> Res<u64> {
        let lo = u32le(ino, 4)? as u64;
        let hi = if ino.len() >= 112 { u32le(ino, 108)? as u64 } else { 0 };
        Ok(lo | (hi << 32))
    }
    fn itype(ino: &[u8]) -> u16 {
        u16le(ino, 0).unwrap_or(0) & S_IFMT
    }

    /// logical block -> physical block (0 = hole)
    fn map_block(&mut self, ino: &[u8], lblk: u64) -> Res<u64> {
        if u32le(ino, 32)? & EXTENTS_FL == 0 {
            return Err("ext4: file without extents (old block map) is not supported".into());
        }
        let root = ino.get(40..100).ok_or("ext4: truncated inode")?.to_vec();
        self.map_extent(&root, lblk, 0)
    }

    fn map_extent(&mut self, buf: &[u8], lblk: u64, guard: u32) -> Res<u64> {
        if guard > 8 {
            return Err("ext4: extent tree too deep".into());
        }
        if u16le(buf, 0)? != 0xF30A {
            return Err("ext4: bad extent header".into());
        }
        let n = u16le(buf, 2)? as usize;
        let depth = u16le(buf, 6)?;
        if 12 + n * 12 > buf.len() {
            return Err("ext4: extent count exceeds node".into());
        }
        if depth == 0 {
            for i in 0..n {
                let o = 12 + i * 12;
                let first = u32le(buf, o)? as u64;
                let mut len = u16le(buf, o + 4)? as u64;
                let unwritten = len > 32768;
                if unwritten {
                    len -= 32768;
                }
                let start = ((u16le(buf, o + 6)? as u64) << 32) | u32le(buf, o + 8)? as u64;
                if lblk >= first && lblk < first + len {
                    return Ok(if unwritten { 0 } else { start + (lblk - first) }); // unwritten extents read as zeros
                }
            }
            return Ok(0);
        }
        let mut pick: Option<usize> = None;
        for i in 0..n {
            if u32le(buf, 12 + i * 12)? as u64 <= lblk {
                pick = Some(i);
            } else {
                break;
            }
        }
        let Some(p) = pick else { return Ok(0) };
        let po = 12 + p * 12;
        let leaf = ((u16le(buf, po + 8)? as u64) << 32) | u32le(buf, po + 4)? as u64;
        let off = self.block_off(leaf)?;
        let node = self.read_at(off, self.bs as usize)?;
        self.map_extent(&node, lblk, guard + 1)
    }

    fn read_all(&mut self, ino: &[u8], max: u64) -> Res<Vec<u8>> {
        let size = Self::size(ino)?;
        if size > max {
            return Err(format!("ext4: file larger than {max} bytes"));
        }
        let mut data = vec![0u8; size as usize];
        let blocks = size.div_ceil(self.bs);
        for b in 0..blocks {
            let p = self.map_block(ino, b)?;
            if p == 0 {
                continue;
            }
            let start = b * self.bs;
            let take = self.bs.min(size - start) as usize;
            let off = self.block_off(p)?;
            let blk = self.read_at(off, take)?;
            data[start as usize..start as usize + take].copy_from_slice(&blk);
        }
        Ok(data)
    }

    fn list_dir(&mut self, ino_no: u64, ino: &[u8]) -> Res<Rc<BTreeMap<String, u64>>> {
        if let Some(d) = self.dirs.get(&ino_no) {
            return Ok(Rc::clone(d));
        }
        let data = self.read_all(ino, 64 * 1024 * 1024)?;
        let bs = self.bs as usize;
        let mut d = BTreeMap::new();
        let mut p = 0usize;
        while p + 8 <= data.len() {
            let i = u32le(&data, p)? as u64;
            let rl = u16le(&data, p + 4)? as usize;
            let nl = data[p + 6] as usize;
            if rl < 8 || p + rl > data.len() {
                p = (p / bs + 1) * bs;
                continue;
            }
            if i != 0 && nl > 0 && p + 8 + nl <= data.len() {
                d.insert(String::from_utf8_lossy(&data[p + 8..p + 8 + nl]).into_owned(), i);
            }
            p += rl;
        }
        let rc = Rc::new(d);
        if self.dirs.len() < 64 {
            self.dirs.insert(ino_no, Rc::clone(&rc));
        }
        Ok(rc)
    }

    fn read_link(&mut self, ino: &[u8]) -> Res<String> {
        let size = Self::size(ino)?;
        if size < 60 && u32le(ino, 32)? & EXTENTS_FL == 0 {
            let s = ino.get(40..40 + size as usize).ok_or("ext4: bad symlink")?;
            return Ok(String::from_utf8_lossy(s).into_owned());
        }
        let b = self.read_all(ino, 4096)?;
        Ok(String::from_utf8_lossy(&b).into_owned())
    }

    /// Returns the inode number, or 0 when the path does not exist.
    fn resolve(&mut self, path: &str, follow_last: bool, depth: u32) -> Res<u64> {
        if depth > 16 {
            return Err("ext4: too many symlinks".into());
        }
        let parts: Vec<&str> = path.split('/').filter(|s| !s.is_empty()).collect();
        let mut cur = 2u64;
        let mut stack = vec![2u64];
        for (k, name) in parts.iter().enumerate() {
            if *name == "." {
                continue;
            }
            if *name == ".." {
                if stack.len() > 1 {
                    stack.pop();
                }
                cur = stack.last().copied().unwrap_or(2);
                continue;
            }
            let dino = self.inode(cur)?;
            if Self::itype(&dino) != S_IFDIR {
                return Ok(0);
            }
            let entries = self.list_dir(cur, &dino)?;
            let Some(&nxt) = entries.get(*name) else { return Ok(0) };
            let ni = self.inode(nxt)?;
            let last = k == parts.len() - 1;
            if Self::itype(&ni) == S_IFLNK && (!last || follow_last) {
                let target = self.read_link(&ni)?;
                let rest = parts[k + 1..].join("/");
                let base = if target.starts_with('/') { target } else { format!("{}/{}", parts[..k].join("/"), target) };
                let np = if rest.is_empty() { base } else { format!("{base}/{rest}") };
                return self.resolve(&np, follow_last, depth + 1);
            }
            cur = nxt;
            stack.push(cur);
        }
        Ok(cur)
    }

    /// File contents, or Ok(None) if the path does not exist / is not a regular file.
    pub fn read_file(&mut self, path: &str, max: u64) -> Res<Option<Vec<u8>>> {
        let i = self.resolve(path, true, 0)?;
        if i == 0 {
            return Ok(None);
        }
        let ino = self.inode(i)?;
        if Self::itype(&ino) != S_IFREG {
            return Ok(None);
        }
        self.read_all(&ino, max).map(Some)
    }

    pub fn list_names(&mut self, dir: &str) -> Res<Vec<String>> {
        let i = self.resolve(dir, true, 0)?;
        if i == 0 {
            return Ok(Vec::new());
        }
        let ino = self.inode(i)?;
        if Self::itype(&ino) != S_IFDIR {
            return Ok(Vec::new());
        }
        Ok(self.list_dir(i, &ino)?.keys().cloned().collect())
    }
}
