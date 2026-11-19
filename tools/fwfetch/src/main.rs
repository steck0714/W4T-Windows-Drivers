// SPDX-License-Identifier: GPL-2.0-only
#![forbid(unsafe_code)]
//! fwfetch - collect, from YOUR OWN Switch SD card, the files this project may not redistribute
//! (NVIDIA / Broadcom / Switchroot builds). Read-only on every source; stages outside any git work tree.
mod ext4;
mod ihex;
mod initramfs;
mod manifest;
mod part;
mod reader;
mod run;
mod util;

use std::path::PathBuf;
use std::process::ExitCode;

const HELP: &str = "\
fwfetch 1.0 - read firmware you may not redistribute from YOUR OWN Switch SD card (read-only), verify, stage.

USAGE: fwfetch [OPTIONS]
  --profile <aula|lite|erista|all>   what to collect (default: aula)
  --search-root <DIR>                folder to search (FAT32 'switchroot' parent, rootfs copy, kernel tree); repeatable
  --disk <FILE|\\\\.\\PhysicalDriveN>    raw disk / partition image to scan for an ext4 volume (read-only); repeatable
  --auto                             Windows: also every ready drive letter, and (administrator) every physical disk
  --out-dir <DIR>                    staging directory (default %LOCALAPPDATA%\\W4T\\fw-cache). Refused inside a git work tree
  --deploy <DIR>                     copy the windows-switch files into this boot-partition root
  --deploy-gpu <DIR>                 copy the GPU firmware into this directory
  --deploy-drivers <DIR>             copy the Wi-Fi / Bluetooth firmware to the places the installed drivers read it from
                                     (DIR = %SystemRoot%\\System32\\drivers -> SwitchWOA\\..., switchwifi\\...)
  --require-known-hash               refuse files that have no published SHA-256
  --manifest <FILE>                  use this manifest instead of the built-in one
  --quiet                            print only warnings and errors
  --help, --version
Exit code: 0 everything found, 2 something is missing, 1 error.
";

fn parse() -> Result<Option<run::Opts>, String> {
    let mut o = run::Opts {
        profile: "aula".into(),
        out_dir: None,
        search_roots: vec![],
        disks: vec![],
        auto: false,
        deploy: None,
        deploy_gpu: None,
        deploy_drivers: None,
        require_known_hash: false,
        quiet: false,
        manifest: None,
    };
    let mut args = std::env::args_os().skip(1);
    while let Some(a) = args.next() {
        let a = a.to_string_lossy().into_owned();
        let (flag, inline) = match a.split_once('=') {
            Some((f, v)) if f.starts_with("--") => (f.to_string(), Some(v.to_string())),
            _ => (a.clone(), None),
        };
        let mut val = |name: &str| -> Result<String, String> {
            match &inline {
                Some(v) => Ok(v.clone()),
                None => args.next().map(|v| v.to_string_lossy().into_owned()).ok_or_else(|| format!("{name} needs a value")),
            }
        };
        match flag.as_str() {
            "--help" | "-h" => {
                print!("{HELP}");
                return Ok(None);
            }
            "--version" => {
                println!("fwfetch {}", env!("CARGO_PKG_VERSION"));
                return Ok(None);
            }
            "--profile" => o.profile = val("--profile")?,
            "--out-dir" => o.out_dir = Some(PathBuf::from(val("--out-dir")?)),
            "--search-root" => o.search_roots.push(PathBuf::from(val("--search-root")?)),
            "--disk" => o.disks.push(PathBuf::from(val("--disk")?)),
            "--deploy" => o.deploy = Some(PathBuf::from(val("--deploy")?)),
            "--deploy-gpu" => o.deploy_gpu = Some(PathBuf::from(val("--deploy-gpu")?)),
            "--deploy-drivers" => o.deploy_drivers = Some(PathBuf::from(val("--deploy-drivers")?)),
            "--manifest" => o.manifest = Some(PathBuf::from(val("--manifest")?)),
            "--auto" => o.auto = true,
            "--require-known-hash" => o.require_known_hash = true,
            "--quiet" => o.quiet = true,
            other => return Err(format!("unknown option '{other}' (see --help)")),
        }
    }
    Ok(Some(o))
}

fn main() -> ExitCode {
    let opts = match parse() {
        Ok(Some(o)) => o,
        Ok(None) => return ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("error: {e}");
            return ExitCode::from(1);
        }
    };
    match run::run(opts) {
        Ok(code) => ExitCode::from(code as u8),
        Err(e) => {
            eprintln!("error: {e}");
            ExitCode::from(1)
        }
    }
}
