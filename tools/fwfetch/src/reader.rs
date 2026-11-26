// SPDX-License-Identifier: GPL-2.0-only
//! Read-only, sector-aligned random access. Raw disks (\\.\PhysicalDriveN) only accept reads whose offset and
//! length are multiples of the sector size, so every read is served from a 64 KiB aligned chunk cache.
//! The file is opened with `File::open` (read access only): nothing in this program can write to a source.
use std::fs::File;
use std::io::{self, Read, Seek, SeekFrom};
use std::path::Path;

const CHUNK: u64 = 64 * 1024;

pub struct Aligned {
    /// (file, absolute start offset, length). One entry for a disk / image; several for a split image (Switchroot l4t.00, l4t.01, ...).
    parts: Vec<(File, u64, u64)>,
    cache_start: u64,
    cache: Vec<u8>,
    valid: bool,
}

impl Aligned {
    pub fn open(p: &Path) -> io::Result<Self> {
        Self::open_parts(&[p.to_path_buf()])
    }

    /// Several files read as ONE image, back to back in the given order (read-only). A raw disk can only be a single part.
    pub fn open_parts(paths: &[std::path::PathBuf]) -> io::Result<Self> {
        let mut parts = Vec::new();
        let mut start = 0u64;
        for p in paths {
            let f = File::open(p)?;
            let len = if paths.len() == 1 { u64::MAX / 2 } else { f.metadata()?.len() };
            parts.push((f, start, len));
            start = start.checked_add(len).ok_or_else(|| io::Error::other("image too large"))?;
        }
        if parts.is_empty() {
            return Err(io::Error::other("no image parts"));
        }
        Ok(Self { parts, cache_start: 0, cache: Vec::new(), valid: false })
    }

    fn fill(&mut self, start: u64) -> io::Result<()> {
        let mut buf = vec![0u8; CHUNK as usize];
        let mut got = 0usize;
        'outer: while got < buf.len() {
            let pos = start + got as u64;
            let Some(i) = self.parts.iter().position(|(_, s0, l)| pos >= *s0 && pos - *s0 < *l) else { break };
            let (file, s0, len) = &mut self.parts[i];
            file.seek(SeekFrom::Start(pos - *s0))?;
            let room = (*len - (pos - *s0)).min((buf.len() - got) as u64) as usize;
            let end = got + room;
            while got < end {
                match file.read(&mut buf[got..end]) {
                    Ok(0) => break 'outer,
                    Ok(n) => got += n,
                    Err(e) if e.kind() == io::ErrorKind::Interrupted => continue,
                    Err(e) => return Err(e),
                }
            }
        }
        buf.truncate(got);
        self.cache = buf;
        self.cache_start = start;
        self.valid = true;
        Ok(())
    }

    /// Fills `buf` completely from absolute offset `off`, or fails (short read / past the end).
    pub fn read_exact_at(&mut self, off: u64, buf: &mut [u8]) -> io::Result<()> {
        let eof = || io::Error::new(io::ErrorKind::UnexpectedEof, "short read");
        let mut done = 0usize;
        while done < buf.len() {
            let pos = off.checked_add(done as u64).ok_or_else(eof)?;
            let cs = pos - pos % CHUNK;
            if !self.valid || self.cache_start != cs {
                self.fill(cs)?;
            }
            let o = (pos - cs) as usize;
            if o >= self.cache.len() {
                return Err(eof());
            }
            let take = (buf.len() - done).min(self.cache.len() - o);
            buf[done..done + take].copy_from_slice(&self.cache[o..o + take]);
            done += take;
        }
        Ok(())
    }

    pub fn read_vec(&mut self, off: u64, n: usize) -> io::Result<Vec<u8>> {
        let mut v = vec![0u8; n];
        self.read_exact_at(off, &mut v)?;
        Ok(v)
    }
}

/// Reads until `buf` is full or EOF; returns the number of bytes read.
pub fn read_fill<R: Read>(r: &mut R, buf: &mut [u8]) -> io::Result<usize> {
    let mut got = 0;
    while got < buf.len() {
        match r.read(&mut buf[got..]) {
            Ok(0) => break,
            Ok(n) => got += n,
            Err(e) if e.kind() == io::ErrorKind::Interrupted => continue,
            Err(e) => return Err(e),
        }
    }
    Ok(got)
}
