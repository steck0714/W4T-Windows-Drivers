// SPDX-License-Identifier: GPL-2.0-only
//! The firmware manifest (firmware-manifest.json, compiled in; `--manifest` overrides it) and its validation.
//! Every path the manifest names is checked: relative, no `..`, no drive letters, no backslashes.
use serde::Deserialize;
use std::collections::BTreeMap;

pub const BUILTIN: &str = include_str!("../firmware-manifest.json");

#[derive(Deserialize, Clone, Default)]
pub struct Expect {
    pub size: Option<u64>,
    pub sha256: Option<String>,
}

#[derive(Deserialize, Clone, Default)]
pub struct Deploy {
    #[serde(rename = "windows-switch", default)]
    pub windows_switch: Vec<String>,
    /// fixed places, relative to the driver folder (%SystemRoot%\\System32\\drivers), where the installed drivers read the file
    /// at run time (`--deploy-drivers`)
    #[serde(default)]
    pub drivers: Vec<String>,
}

#[derive(Deserialize, Clone, Default)]
pub struct Source {
    #[serde(rename = "type")]
    pub kind: String,
    #[serde(default)]
    pub paths: Vec<String>,
    #[serde(default)]
    pub scan: Vec<String>,
    pub member: Option<String>,
    pub search_dir: Option<String>,
    /// SHA-256 that THIS source must match (and that counts as verification when it does). For a file that is stored under a
    /// different name elsewhere (e.g. L4T's gm20b/acr_ucode.bin): it is taken only if its bytes are exactly the documented ones.
    pub sha256: Option<String>,
}

#[derive(Deserialize, Clone, Default)]
pub struct FileEntry {
    pub id: String,
    #[serde(default)]
    pub what: String,
    pub gpu: Option<String>,
    pub expect: Option<Expect>,
    pub postprocess: Option<String>,
    #[serde(default)]
    pub deploy: Deploy,
    #[serde(default)]
    pub sources: Vec<Source>,
}

/// An official download that can supply some of the files when they are not found locally (`--download`).
/// `url` is fetched as it is; `github_latest` asks the GitHub release API for the newest release and takes the asset whose
/// name starts with `asset_prefix` and ends with `asset_suffix`. Only https URLs on the hosts in `download::HOSTS` are accepted.
#[derive(Deserialize, Clone, Default)]
pub struct Download {
    pub id: String,
    #[serde(default)]
    pub what: String,
    #[serde(rename = "type")]
    pub kind: String,
    pub url: Option<String>,
    pub api: Option<String>,
    pub asset_prefix: Option<String>,
    pub asset_suffix: Option<String>,
    pub download_prefix: Option<String>,
    /// ids of the files this download can supply
    #[serde(default)]
    pub provides: Vec<String>,
}

#[derive(Deserialize)]
pub struct GpuTemplate {
    pub what: String,
    pub dirs: Vec<String>,
}

#[derive(Deserialize)]
pub struct Manifest {
    pub profiles: BTreeMap<String, Vec<String>>,
    pub files: Vec<FileEntry>,
    pub gpu_template: GpuTemplate,
    #[serde(default)]
    pub downloads: Vec<Download>,
}

/// One concrete thing to collect (GPU shorthand already expanded).
#[derive(Clone)]
pub struct Entry {
    pub id: String,
    pub what: String,
    pub expect: Option<Expect>,
    pub random_mac: bool,
    pub deploy: Vec<String>,
    pub drivers: Vec<String>,
    pub gpu_name: Option<String>,
    /// path of the staged copy, relative to the staging directory
    pub stage_rel: String,
    pub sources: Vec<Source>,
}

/// A manifest path must stay inside its base directory. '*' is allowed (matches inside one path component).
pub fn safe_rel(p: &str) -> bool {
    !p.is_empty()
        && !p.contains('\\')
        && !p.contains(':')
        && !p.starts_with('/')
        && !p.contains('\0')
        && p.split('/').all(|c| !c.is_empty() && c != "." && c != "..")
}

fn check_hash(id: &str, e: &Expect) -> Result<(), String> {
    if let Some(h) = &e.sha256 {
        if h.len() != 64 || !h.bytes().all(|b| b.is_ascii_digit() || (b'a'..=b'f').contains(&b)) {
            return Err(format!("manifest: '{id}' has a malformed sha256 (need 64 lowercase hex digits)"));
        }
    }
    Ok(())
}

pub type Loaded = (BTreeMap<String, Vec<String>>, BTreeMap<String, Entry>, Vec<Download>);

pub fn load(text: &str) -> Result<Loaded, String> {
    let m: Manifest = serde_json::from_str(text).map_err(|e| format!("manifest: {e}"))?;
    let mut out = BTreeMap::new();
    for f in m.files {
        if let Some(e) = &f.expect {
            check_hash(&f.id, e)?;
        }
        let entry = if let Some(g) = &f.gpu {
            if !safe_rel(g) || g.contains('/') {
                return Err(format!("manifest: bad GPU file name '{g}'"));
            }
            let paths: Vec<String> = m.gpu_template.dirs.iter().map(|d| format!("{d}/{g}")).collect();
            Entry {
                id: f.id.clone(),
                what: format!("{} - {g}", m.gpu_template.what),
                expect: f.expect.clone(),
                random_mac: false,
                deploy: Vec::new(),
                drivers: Vec::new(),
                gpu_name: Some(g.clone()),
                stage_rel: format!("gpu/{g}"),
                // the template places first; a GPU entry may add its own sources (hash-gated aliases) after them
                sources: [
                    Source { kind: "ext4".into(), paths: paths.clone(), ..Default::default() },
                    Source { kind: "fs".into(), paths, ..Default::default() },
                ]
                .into_iter()
                .chain(f.sources.iter().cloned())
                .collect(),
            }
        } else {
            let first = f.deploy.windows_switch.first().ok_or_else(|| format!("manifest: '{}' has no deploy path", f.id))?;
            Entry {
                id: f.id.clone(),
                what: f.what.clone(),
                expect: f.expect.clone(),
                random_mac: f.postprocess.as_deref() == Some("random_macaddr"),
                deploy: f.deploy.windows_switch.clone(),
                drivers: f.deploy.drivers.clone(),
                gpu_name: None,
                stage_rel: first.clone(),
                sources: f.sources.clone(),
            }
        };
        for p in std::iter::once(&entry.stage_rel).chain(entry.deploy.iter()).chain(entry.drivers.iter()) {
            if !safe_rel(p) || p.contains('*') {
                return Err(format!("manifest: unsafe output path '{p}' in '{}'", entry.id));
            }
        }
        for s in &entry.sources {
            if !matches!(s.kind.as_str(), "fs" | "initramfs" | "ext4" | "ihex") {
                return Err(format!("manifest: unknown source type '{}' in '{}'", s.kind, entry.id));
            }
            for p in s.paths.iter().chain(s.scan.iter()).chain(s.member.iter()).chain(s.search_dir.iter()) {
                if !safe_rel(p) {
                    return Err(format!("manifest: unsafe source path '{p}' in '{}'", entry.id));
                }
            }
            if s.sha256.is_some() {
                check_hash(&entry.id, &Expect { size: None, sha256: s.sha256.clone() })?;
            }
        }
        if out.insert(entry.id.clone(), entry).is_some() {
            return Err(format!("manifest: duplicate id '{}'", f.id));
        }
    }
    for (p, ids) in &m.profiles {
        for id in ids {
            if !out.contains_key(id) {
                return Err(format!("manifest: profile '{p}' names unknown id '{id}'"));
            }
        }
    }
    let mut seen = std::collections::BTreeSet::new();
    for d in &m.downloads {
        if !seen.insert(d.id.clone()) {
            return Err(format!("manifest: duplicate download id '{}'", d.id));
        }
        match d.kind.as_str() {
            "url" => {
                let u = d.url.as_deref().ok_or_else(|| format!("manifest: download '{}' has no url", d.id))?;
                crate::download::check_url(u).map_err(|e| format!("manifest: download '{}': {e}", d.id))?;
            }
            "github_latest" => {
                for (name, v) in [("api", &d.api), ("download_prefix", &d.download_prefix), ("asset_prefix", &d.asset_prefix), ("asset_suffix", &d.asset_suffix)] {
                    if v.is_none() {
                        return Err(format!("manifest: download '{}' has no {name}", d.id));
                    }
                }
                for u in [d.api.as_deref().unwrap_or(""), d.download_prefix.as_deref().unwrap_or("")] {
                    crate::download::check_url(u).map_err(|e| format!("manifest: download '{}': {e}", d.id))?;
                }
            }
            other => return Err(format!("manifest: unknown download type '{other}' in '{}'", d.id)),
        }
        for id in &d.provides {
            if !out.contains_key(id) {
                return Err(format!("manifest: download '{}' provides unknown id '{id}'", d.id));
            }
        }
    }
    Ok((m.profiles, out, m.downloads))
}
