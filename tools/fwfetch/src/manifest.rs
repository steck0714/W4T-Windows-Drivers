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

pub type Loaded = (BTreeMap<String, Vec<String>>, BTreeMap<String, Entry>);

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
                sources: vec![
                    Source { kind: "ext4".into(), paths: paths.clone(), ..Default::default() },
                    Source { kind: "fs".into(), paths, ..Default::default() },
                ],
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
    Ok((m.profiles, out))
}
