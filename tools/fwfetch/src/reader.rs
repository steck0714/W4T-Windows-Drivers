// SPDX-License-Identifier: GPL-2.0-only
//! Read-only, sector-aligned random access. Raw disks (\\.\PhysicalDriveN) only accept reads whose offset and
//! length are multiples of the sector size, so every read is served from a 64 KiB aligned chunk cache.
//! The file is opened with `File::open` (read access only): nothing in this program can write to a source.
use std::fs::File;
use std::io::{self, Read, Seek, SeekFrom};
use std::path::Path;

const CHUNK: u64 = 64 * 1024;

pub struct Aligned {
    file: File,
    cache_start: u64,
    cache: Vec<u8>,
    valid: bool,
}

impl Aligned {
    pub fn open(p: &Path) -> io::Result<Self> {
        Ok(Self { file: File::open(p)?, cache_start: 0, cache: Vec::new(), valid: false })
    }

    fn fill(&mut self, start: u64) -> io::Result<()> {
        self.file.seek(SeekFrom::Start(start))?;
        let mut buf = vec![0u8; CHUNK as usize];
        let mut got = 0usize;
        while got < buf.len() {
            match self.file.read(&mut buf[got..]) {
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
