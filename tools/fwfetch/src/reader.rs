// SPDX-License-Identifier: GPL-2.0-only
//! Read-only, sector-aligned random access. Raw disks (\\.\PhysicalDriveN) only accept reads whose offset and
//! length are multiples of the sector size, so every read is served from a 64 KiB aligned chunk cache.
//! The files are opened with `File::open` (read access only): nothing in this program can write to a source.
//!
//! One `Aligned` can also present several files as ONE contiguous image (`open_parts`): that is how a root file system
//! that was split into `l4t.00`, `l4t.01`, ... (to fit FAT32's 4 GiB limit) is read without flashing it to a partition.
use std::fs::File;
use std::io::{self, Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};

const CHUNK: u64 = 64 * 1024;

struct Seg {
    file: File,
    /// offset of this file inside the joined image
    start: u64,
    /// length of this file (u64::MAX for a single raw device, whose metadata length is not reliable)
    len: u64,
}

pub struct Aligned {
    segs: Vec<Seg>,
    cache_start: u64,
    cache: Vec<u8>,
    valid: bool,
}

impl Aligned {
    fn with_segs(segs: Vec<Seg>) -> Self {
        Self { segs, cache_start: 0, cache: Vec::new(), valid: false }
    }

    pub fn open(p: &Path) -> io::Result<Self> {
        Ok(Self::with_segs(vec![Seg { file: File::open(p)?, start: 0, len: u64::MAX }]))
    }

    /// The files, in the given order, as one contiguous read-only image.
    pub fn open_parts(paths: &[PathBuf]) -> io::Result<Self> {
        if paths.is_empty() {
            return Err(io::Error::new(io::ErrorKind::InvalidInput, "no image parts"));
        }
        if paths.len() == 1 {
            return Self::open(&paths[0]);
        }
        let mut segs = Vec::with_capacity(paths.len());
        let mut start = 0u64;
        for p in paths {
            let file = File::open(p)?;
            let len = file.metadata()?.len();
            segs.push(Seg { file, start, len });
            start = start.checked_add(len).ok_or_else(|| io::Error::new(io::ErrorKind::InvalidData, "image parts too large"))?;
        }
        Ok(Self::with_segs(segs))
    }

    fn fill(&mut self, start: u64) -> io::Result<()> {
        let mut buf = vec![0u8; CHUNK as usize];
        let mut got = 0usize;
        while got < buf.len() {
            let pos = start + got as u64;
            // the part that holds `pos` (a chunk may straddle two parts)
            let Some(seg) = self.segs.iter_mut().find(|s| pos >= s.start && pos - s.start < s.len) else { break };
            let local = pos - seg.start;
            seg.file.seek(SeekFrom::Start(local))?;
            let room = usize::try_from(seg.len - local).unwrap_or(usize::MAX);
            let want = (buf.len() - got).min(room);
            match seg.file.read(&mut buf[got..got + want]) {
                Ok(0) => break,
                Ok(n) => got += n,
                Err(e) if e.kind() == io::ErrorKind::Interrupted => continue,
                Err(e) => return Err(e),
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
pub fn read_fill<R: Read + ?Sized>(r: &mut R, buf: &mut [u8]) -> io::Result<usize> {
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
