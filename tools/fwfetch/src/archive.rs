// SPDX-License-Identifier: GPL-2.0-only
//! Reads the files fwfetch wants out of archives you can download WITHOUT installing anything on an SD card:
//! a Debian package (.deb = ar + data.tar.*), a tar, or a tar compressed with gzip / bzip2 / xz (.tar.gz .tgz .tbz2 .tar.xz), or a .zip (e.g. a hekate release).
//!
//! NVIDIA's Jetson Linux driver package (`jetson-210_linux_r32.x_aarch64.tbz2`) keeps the Tegra firmware one level down, in
//! `Linux_for_Tegra/nv_tegra/nvidia_drivers.tbz2`, so that member (and `.deb` members with "firmware" in their name) is entered too.
//! Nothing is extracted to disk: only the members the manifest asks for are kept in memory, and a member's name is used for matching
//! only, never as an output path. Every size is bounded (decompression-bomb guard). zstd is NOT supported (it is reported, not guessed).
use crate::reader::read_fill;
use flate2::read::{DeflateDecoder, MultiGzDecoder};
use std::collections::BTreeMap;
use std::io::{self, Cursor, Read, Write};
use std::path::Path;

/// Decompressed bytes examined per stream (a whole Jetson BSP is about 1.5 GB as a tar).
const MAX_STREAM: u64 = 6 << 30;
/// xz is decoded into memory (the decoder has no streaming interface).
const MAX_XZ: usize = 1 << 30;
/// A nested archive is held in memory while it is read.
const MAX_NESTED: u64 = 512 << 20;
/// Total size of the members that are kept.
const MAX_KEPT: u64 = 512 << 20;
/// Wrapper / nesting depth (gzip inside tar inside bzip2 ...).
const MAX_LAYERS: u8 = 6;

type Rd<'a> = Box<dyn Read + 'a>;

/// The kept members, by normalised path (no leading `./` or `/`; `usr/lib/x` is also kept as `lib/x`).
pub struct Archive {
    files: BTreeMap<String, Vec<u8>>,
    /// problems with nested archives that did not stop the scan
    pub notes: Vec<String>,
}

impl Archive {
    pub fn len(&self) -> usize {
        self.files.len()
    }

    pub fn read_file(&self, path: &str, max: u64) -> Option<Vec<u8>> {
        self.files.get(path.trim_start_matches('/')).filter(|d| d.len() as u64 <= max).cloned()
    }

    /// Names of the kept files directly inside `dir`.
    pub fn list_names(&self, dir: &str) -> Vec<String> {
        let prefix = format!("{}/", dir.trim_matches('/'));
        self.files.keys().filter_map(|k| k.strip_prefix(&prefix)).filter(|rest| !rest.contains('/')).map(str::to_string).collect()
    }
}

struct Cx<'a> {
    wanted: &'a dyn Fn(&str) -> bool,
    max_member: u64,
    files: BTreeMap<String, Vec<u8>>,
    kept: u64,
    notes: Vec<String>,
}

impl Cx<'_> {
    fn keep(&mut self, key: String, data: Vec<u8>) -> Result<(), String> {
        self.kept = self.kept.saturating_add(data.len() as u64);
        if self.kept > MAX_KEPT {
            return Err("archive: too much wanted data (limit reached)".into());
        }
        if let Some(rest) = key.strip_prefix("usr/lib/") {
            self.files.entry(format!("lib/{rest}")).or_insert_with(|| data.clone());
        }
        self.files.entry(key).or_insert(data);
        Ok(())
    }
}

/// Opens `path` read-only and keeps every member for which `wanted(normalised path)` is true (and that is at most `max_member` bytes).
pub fn open(path: &Path, wanted: &dyn Fn(&str) -> bool, max_member: u64) -> Result<Archive, String> {
    let f = std::fs::File::open(path).map_err(|e| e.to_string())?;
    open_reader(Box::new(io::BufReader::with_capacity(1 << 20, f)), wanted, max_member)
}

/// Same, on any byte stream (a download that is read while it arrives).
pub fn open_reader(r: Box<dyn Read + '_>, wanted: &dyn Fn(&str) -> bool, max_member: u64) -> Result<Archive, String> {
    let mut cx = Cx { wanted, max_member, files: BTreeMap::new(), kept: 0, notes: Vec::new() };
    scan(r, 0, &mut cx)?;
    Ok(Archive { files: cx.files, notes: cx.notes })
}

fn ioe(e: io::Error) -> String {
    format!("corrupt or unreadable archive: {e}")
}

enum Kind {
    Deb,
    Zip,
    Gz,
    Bz2,
    Xz,
    Zst,
    Tar,
    Unknown,
}

fn sniff(h: &[u8]) -> Kind {
    if h.starts_with(b"!<arch>\n") {
        Kind::Deb
    } else if h.starts_with(b"PK\x03\x04") {
        Kind::Zip
    } else if h.starts_with(&[0x1F, 0x8B]) {
        Kind::Gz
    } else if h.len() >= 4 && h.starts_with(b"BZh") && (b'1'..=b'9').contains(&h[3]) {
        Kind::Bz2
    } else if h.starts_with(&[0xFD, b'7', b'z', b'X', b'Z', 0]) {
        Kind::Xz
    } else if h.starts_with(&[0x28, 0xB5, 0x2F, 0xFD]) {
        Kind::Zst
    } else if h.len() == 512 && h[0] != 0 && tar_sum_ok(h) {
        Kind::Tar
    } else {
        Kind::Unknown
    }
}

fn scan<'a>(mut r: Rd<'a>, layer: u8, cx: &mut Cx) -> Result<(), String> {
    if layer > MAX_LAYERS {
        return Err("archives nested too deeply".into());
    }
    let mut head = [0u8; 512];
    let n = read_fill(&mut r, &mut head).map_err(ioe)?;
    let head = &head[..n];
    let kind = sniff(head);
    // give the sniffed bytes back to whoever reads next
    let all: Rd<'a> = Box::new(Cursor::new(head.to_vec()).chain(r));
    match kind {
        Kind::Deb => scan_ar(all, layer, cx),
        Kind::Zip => scan_zip(all, cx),
        Kind::Gz => scan(Box::new(MultiGzDecoder::new(all).take(MAX_STREAM)), layer + 1, cx),
        Kind::Bz2 => scan(Box::new(bzip2::read::MultiBzDecoder::new(all).take(MAX_STREAM)), layer + 1, cx),
        Kind::Xz => {
            let mut out = Capped(Vec::new());
            lzma_rs::xz_decompress(&mut io::BufReader::new(all), &mut out).map_err(|e| format!("xz: {e}"))?;
            scan(Box::new(Cursor::new(out.0)), layer + 1, cx)
        }
        Kind::Zst => Err("zstd archive not supported".into()),
        Kind::Tar => scan_tar(all, layer, cx),
        Kind::Unknown => Err("not a recognised archive (expected .deb, .zip, tar, or tar.gz / .bz2 / .xz)".into()),
    }
}

/// A Vec that refuses to grow past MAX_XZ.
struct Capped(Vec<u8>);
impl Write for Capped {
    fn write(&mut self, b: &[u8]) -> io::Result<usize> {
        if self.0.len().saturating_add(b.len()) > MAX_XZ {
            return Err(io::Error::other("xz stream too large"));
        }
        self.0.extend_from_slice(b);
        Ok(b.len())
    }
    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

fn skip<R: Read + ?Sized>(r: &mut R, mut n: u64) -> Result<(), String> {
    let mut buf = [0u8; 65536];
    while n > 0 {
        let want = n.min(buf.len() as u64) as usize;
        let got = read_fill(r, &mut buf[..want]).map_err(ioe)?;
        if got < want {
            return Err("truncated archive".into());
        }
        n -= want as u64;
    }
    Ok(())
}

// ---------------------------------------------------------------- ar (.deb)

fn scan_ar(mut r: Rd<'_>, layer: u8, cx: &mut Cx) -> Result<(), String> {
    let mut magic = [0u8; 8];
    if read_fill(&mut r, &mut magic).map_err(ioe)? != 8 || &magic != b"!<arch>\n" {
        return Err("not an ar archive".into());
    }
    loop {
        let mut h = [0u8; 60];
        let n = read_fill(&mut r, &mut h).map_err(ioe)?;
        if n == 0 {
            return Err("no data.tar member in this .deb".into());
        }
        if n < 60 || &h[58..60] != b"`\n" {
            return Err("bad ar member header".into());
        }
        let name = String::from_utf8_lossy(&h[0..16]).trim().trim_end_matches('/').to_string();
        let size: u64 = String::from_utf8_lossy(&h[48..58]).trim().parse().map_err(|_| "bad ar member size".to_string())?;
        let pad = size & 1;
        if name == "data.tar" || name.starts_with("data.tar.") {
            let mut t = (&mut r).take(size);
            return scan(Box::new(&mut t), layer + 1, cx); // the rest of the .deb is not needed
        }
        skip(&mut r, size.checked_add(pad).ok_or("ar size overflow")?)?;
    }
}

// ---------------------------------------------------------------- tar

/// Octal number field (NUL / space padded). Base-256 (binary) sizes are not supported.
fn parse_octal(f: &[u8]) -> Option<u64> {
    if f.first().is_some_and(|b| b & 0x80 != 0) {
        return None;
    }
    let mut v = 0u64;
    let mut seen = false;
    for &b in f {
        match b {
            b' ' | 0 if !seen => continue,
            b'0'..=b'7' => {
                v = v.checked_mul(8)?.checked_add((b - b'0') as u64)?;
                seen = true;
            }
            b' ' | 0 => break,
            _ => return None,
        }
    }
    Some(v)
}

fn tar_sum_ok(h: &[u8]) -> bool {
    let Some(stored) = parse_octal(&h[148..156]) else { return false };
    let sum: u64 = h.iter().enumerate().map(|(i, &b)| if (148..156).contains(&i) { 32 } else { b as u64 }).sum();
    sum == stored
}

fn cstr(b: &[u8]) -> String {
    let end = b.iter().position(|&c| c == 0).unwrap_or(b.len());
    String::from_utf8_lossy(&b[..end]).into_owned()
}

fn ustar_name(h: &[u8]) -> String {
    let name = cstr(&h[0..100]);
    if &h[257..263] == b"ustar\0" {
        let prefix = cstr(&h[345..500]);
        if !prefix.is_empty() {
            return format!("{prefix}/{name}");
        }
    }
    name
}

/// The `path=` record of a pax extended header ("LEN key=value\n" records).
fn pax_path(d: &[u8]) -> Option<String> {
    let mut p = 0usize;
    let mut found = None;
    while p < d.len() {
        let Some(sp) = d[p..].iter().position(|&b| b == b' ') else { break };
        let Some(len) = std::str::from_utf8(&d[p..p + sp]).ok().and_then(|s| s.parse::<usize>().ok()) else { break };
        if len <= sp + 1 || p + len > d.len() {
            break;
        }
        let rec = &d[p + sp + 1..p + len];
        let rec = rec.strip_suffix(b"\n").unwrap_or(rec);
        if let Some(v) = rec.strip_prefix(b"path=") {
            found = Some(String::from_utf8_lossy(v).into_owned());
        }
        p += len;
    }
    found
}

fn normalize(n: &str) -> String {
    let mut s = n;
    loop {
        if let Some(r) = s.strip_prefix("./") {
            s = r;
        } else if let Some(r) = s.strip_prefix('/') {
            s = r;
        } else {
            break;
        }
    }
    s.trim_end_matches('/').to_string()
}

/// Archives that are worth opening when met inside another archive (everything else nested is skipped unread).
fn nested_ok(key: &str) -> bool {
    let base = key.rsplit('/').next().unwrap_or(key);
    base == "nvidia_drivers.tbz2" || (base.ends_with(".deb") && base.contains("firmware"))
}

fn scan_tar(mut r: Rd<'_>, layer: u8, cx: &mut Cx) -> Result<(), String> {
    let mut long_name: Option<String> = None;
    let mut pax: Option<String> = None;
    loop {
        let mut h = [0u8; 512];
        let n = read_fill(&mut r, &mut h).map_err(ioe)?;
        if n == 0 {
            break; // some tars have no end marker
        }
        if n < 512 {
            return Err("truncated tar".into());
        }
        if h.iter().all(|&b| b == 0) {
            break;
        }
        if !tar_sum_ok(&h) {
            return Err("bad tar header (not a tar, or corrupt)".into());
        }
        let size = parse_octal(&h[124..136]).ok_or("unsupported tar size field")?;
        let padded = size.checked_add(511).ok_or("tar size overflow")? & !511;
        let ty = h[156];
        match ty {
            b'L' | b'x' => {
                if size > 64 * 1024 {
                    return Err("tar extension header too large".into());
                }
                let mut d = vec![0u8; padded as usize];
                if read_fill(&mut r, &mut d).map_err(ioe)? != d.len() {
                    return Err("truncated tar".into());
                }
                d.truncate(size as usize);
                if ty == b'L' {
                    long_name = Some(cstr(&d));
                } else {
                    pax = pax_path(&d);
                }
                continue;
            }
            b'g' => {
                skip(&mut r, padded)?;
                continue;
            }
            _ => {}
        }
        let (a, b) = (pax.take(), long_name.take());
        let name = a.or(b).unwrap_or_else(|| ustar_name(&h));
        if !(ty == b'0' || ty == 0 || ty == b'7') {
            skip(&mut r, padded)?; // directories, links, devices
            continue;
        }
        let key = normalize(&name);
        let keep = (cx.wanted)(&key) && size <= cx.max_member;
        let nested = !keep && layer < MAX_LAYERS && size <= MAX_NESTED && nested_ok(&key);
        if keep || nested {
            let mut d = vec![0u8; size as usize];
            if read_fill(&mut r, &mut d).map_err(ioe)? != d.len() {
                return Err("truncated tar".into());
            }
            skip(&mut r, padded - size)?;
            if keep {
                cx.keep(key, d)?;
            } else if let Err(e) = scan(Box::new(Cursor::new(d)), layer + 1, cx) {
                cx.notes.push(format!("nested {key}: {e}"));
            }
        } else {
            skip(&mut r, padded)?;
        }
    }
    Ok(())
}

// ---------------------------------------------------------------- zip

/// A zip is read through its central directory, so the whole file is held in memory (release zips are small).
const MAX_ZIP: u64 = 64 << 20;

fn le16(b: &[u8], o: usize) -> Option<usize> {
    Some(u16::from_le_bytes(b.get(o..o.checked_add(2)?)?.try_into().ok()?) as usize)
}
fn le32(b: &[u8], o: usize) -> Option<u64> {
    Some(u32::from_le_bytes(b.get(o..o.checked_add(4)?)?.try_into().ok()?) as u64)
}

fn scan_zip(r: Rd<'_>, cx: &mut Cx) -> Result<(), String> {
    let mut z = Vec::new();
    r.take(MAX_ZIP + 1).read_to_end(&mut z).map_err(ioe)?;
    if z.len() as u64 > MAX_ZIP {
        return Err("zip too large".into());
    }
    let bad = || "corrupt zip".to_string();
    let from = z.len().saturating_sub(22 + 65535);
    let eocd = (from..z.len().saturating_sub(21)).rev().find(|&i| z[i..].starts_with(b"PK\x05\x06")).ok_or("zip has no end record (truncated?)")?;
    let n = le16(&z, eocd + 10).ok_or_else(bad)?;
    let cd_size = le32(&z, eocd + 12).ok_or_else(bad)?;
    let cd_off = le32(&z, eocd + 16).ok_or_else(bad)?;
    if cd_off == 0xFFFF_FFFF || cd_size == 0xFFFF_FFFF || n == 0xFFFF {
        return Err("zip64 is not supported".into());
    }
    let mut p = usize::try_from(cd_off).map_err(|_| bad())?;
    for _ in 0..n {
        if z.get(p..p + 4) != Some(b"PK\x01\x02") {
            return Err(bad());
        }
        let flags = le16(&z, p + 8).ok_or_else(bad)?;
        let method = le16(&z, p + 10).ok_or_else(bad)?;
        let crc = le32(&z, p + 16).ok_or_else(bad)?;
        let csize = le32(&z, p + 20).ok_or_else(bad)?;
        let usize_ = le32(&z, p + 24).ok_or_else(bad)?;
        let (nl, el, cl) = (le16(&z, p + 28).ok_or_else(bad)?, le16(&z, p + 30).ok_or_else(bad)?, le16(&z, p + 32).ok_or_else(bad)?);
        let lho = usize::try_from(le32(&z, p + 42).ok_or_else(bad)?).map_err(|_| bad())?;
        let name = String::from_utf8_lossy(z.get(p + 46..p + 46 + nl).ok_or_else(bad)?).into_owned();
        p = p.checked_add(46 + nl + el + cl).ok_or_else(bad)?;
        let key = normalize(&name);
        if name.ends_with('/') || !(cx.wanted)(&key) || usize_ > cx.max_member || flags & 1 != 0 {
            continue;
        }
        if z.get(lho..lho + 4) != Some(b"PK\x03\x04") {
            return Err(bad());
        }
        let start = lho.checked_add(30 + le16(&z, lho + 26).ok_or_else(bad)? + le16(&z, lho + 28).ok_or_else(bad)?).ok_or_else(bad)?;
        let end = start.checked_add(usize::try_from(csize).map_err(|_| bad())?).ok_or_else(bad)?;
        let raw = z.get(start..end).ok_or_else(bad)?;
        let data = match method {
            0 => raw.to_vec(),
            8 => {
                let mut d = Vec::new();
                DeflateDecoder::new(raw).take(usize_ + 1).read_to_end(&mut d).map_err(ioe)?;
                d
            }
            m => return Err(format!("zip member {key}: compression method {m} is not supported")),
        };
        if data.len() as u64 != usize_ || crc32fast::hash(&data) as u64 != crc {
            return Err(format!("zip member {key}: size or CRC does not match"));
        }
        cx.keep(key, data)?;
    }
    Ok(())
}
