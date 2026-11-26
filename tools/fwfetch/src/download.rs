// SPDX-License-Identifier: GPL-2.0-only
//! Official downloads, done by the system's own `curl` (Windows 10 1803+ ships curl.exe in System32) so that no TLS stack is built in.
//! Meant to run ON the Switch, from the start-up task, when a file is still missing: the data is streamed straight into the
//! archive reader and nothing but the wanted members is kept - no multi-hundred-MB file is written to the SD card.
//!
//! Safety: only https, only the hosts below (redirects are limited to https, too), no URL comes from a downloaded file except the
//! GitHub release asset, which must start with the manifest's `download_prefix`. `FWFETCH_TEST_LOOPBACK=1` additionally accepts
//! `http://127.0.0.1:PORT/` and exists only so the test-suite can run a local server.
use crate::manifest::Download;
use std::io::{self, Read};
use std::process::{Child, ChildStdout, Command, Stdio};

/// Hosts a manifest URL may name. (NVIDIA redirects its package link to developer.download.nvidia.com.)
pub const HOSTS: &[&str] = &["developer.nvidia.com", "developer.download.nvidia.com", "github.com", "api.github.com"];

fn loopback_ok() -> bool {
    std::env::var_os("FWFETCH_TEST_LOOPBACK").is_some_and(|v| v == "1")
}

pub fn check_url(u: &str) -> Result<(), String> {
    if loopback_ok() && u.starts_with("http://127.0.0.1:") && !u.contains('@') && u.bytes().all(|b| b.is_ascii_graphic()) {
        return Ok(());
    }
    let rest = u.strip_prefix("https://").ok_or_else(|| format!("only https URLs are allowed: '{u}'"))?;
    let host_end = rest.find(['/', '?', '#']).unwrap_or(rest.len());
    let host = &rest[..host_end];
    if !HOSTS.contains(&host) {
        return Err(format!("host '{host}' is not one of the allowed download hosts"));
    }
    if !u.bytes().all(|b| b.is_ascii_graphic()) || u.contains('\\') {
        return Err("URL contains characters that are not allowed".into());
    }
    Ok(())
}

fn curl_cmd(url: &str, retry: bool) -> Command {
    let mut c = Command::new(if cfg!(windows) { "curl.exe" } else { "curl" });
    let proto = if url.starts_with("https://") { "=https" } else { "=http" };
    c.args(["--fail", "--silent", "--show-error", "--location", "--max-redirs", "5", "--proto", proto, "--proto-redir", "=https"]);
    if proto == "=http" {
        c.args(["--proto-redir", "=http,https"]); // test server only
    }
    // a stalled link ends the attempt (1 KB/s for 2 minutes), the next start tries again
    c.args(["--connect-timeout", "30", "--speed-limit", "1000", "--speed-time", "120", "-A", "fwfetch/1.2"]);
    if retry {
        c.args(["--retry", "3", "--retry-delay", "5"]);
    }
    c.arg("--").arg(url);
    c
}

fn spawn_err(e: io::Error) -> String {
    if e.kind() == io::ErrorKind::NotFound {
        "curl was not found (Windows 10 1803 and later ship curl.exe in System32; otherwise install curl, or pass the files with --archive)".into()
    } else {
        format!("cannot start curl: {e}")
    }
}

/// A running download whose bytes are read as they arrive.
pub struct Stream {
    child: Child,
    out: Option<ChildStdout>,
}

impl Read for Stream {
    fn read(&mut self, b: &mut [u8]) -> io::Result<usize> {
        self.out.as_mut().map_or(Ok(0), |o| o.read(b))
    }
}

impl Stream {
    /// Stops curl and returns what it said on stderr (empty when it simply was not needed any more).
    pub fn finish(mut self) -> String {
        drop(self.out.take());
        let _ = self.child.kill();
        let mut msg = String::new();
        if let Some(mut e) = self.child.stderr.take() {
            let _ = e.by_ref().take(4096).read_to_string(&mut msg);
        }
        let _ = self.child.wait();
        msg.trim().to_string()
    }
}

pub fn open_stream(url: &str) -> Result<Stream, String> {
    check_url(url)?;
    let mut child = curl_cmd(url, false).stdin(Stdio::null()).stdout(Stdio::piped()).stderr(Stdio::piped()).spawn().map_err(spawn_err)?;
    let out = child.stdout.take();
    Ok(Stream { child, out })
}

/// A small response (release JSON), at most `max` bytes.
pub fn fetch_small(url: &str, max: u64) -> Result<Vec<u8>, String> {
    check_url(url)?;
    let o = curl_cmd(url, true).stdin(Stdio::null()).output().map_err(spawn_err)?;
    if !o.status.success() {
        return Err(format!("download failed: {}", String::from_utf8_lossy(&o.stderr).trim()));
    }
    if o.stdout.len() as u64 > max {
        return Err("response too large".into());
    }
    Ok(o.stdout)
}

/// The URL to stream for this download.
pub fn resolve(d: &Download) -> Result<String, String> {
    match d.kind.as_str() {
        "url" => d.url.clone().ok_or_else(|| "no url".to_string()),
        "github_latest" => {
            let (Some(api), Some(prefix), Some(a), Some(z)) = (&d.api, &d.download_prefix, &d.asset_prefix, &d.asset_suffix) else {
                return Err("incomplete download entry".into());
            };
            let body = fetch_small(api, 4 << 20)?;
            let v: serde_json::Value = serde_json::from_slice(&body).map_err(|e| format!("release information is not JSON: {e}"))?;
            let assets = v.get("assets").and_then(|a| a.as_array()).ok_or("release information has no assets")?;
            for it in assets {
                let name = it.get("name").and_then(|n| n.as_str()).unwrap_or("");
                let url = it.get("browser_download_url").and_then(|n| n.as_str()).unwrap_or("");
                if name.starts_with(a.as_str()) && name.ends_with(z.as_str()) && url.starts_with(prefix.as_str()) && check_url(url).is_ok() {
                    return Ok(url.to_string());
                }
            }
            Err("no matching release asset".into())
        }
        k => Err(format!("unknown download type {k}")),
    }
}
