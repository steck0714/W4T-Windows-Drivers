// SPDX-License-Identifier: GPL-2.0-only
#![forbid(unsafe_code)]
//! fwfetch - collect, from YOUR OWN Switch SD card, the files this project may not redistribute
//! (NVIDIA / Broadcom / Switchroot builds). Read-only on every source; stages outside any git work tree.
mod archive;
mod download;
mod ext4;
mod ihex;
mod image;
mod initramfs;
mod manifest;
mod part;
mod reader;
mod run;
mod util;
mod winvol;

use std::path::PathBuf;
use std::process::ExitCode;

const HELP: &str = "\
fwfetch 1.2 - read firmware you may not redistribute from YOUR OWN files (SD card, downloads; read-only), verify, stage.
(No SD card with an installed Switchroot is needed: see --image and --archive.)

USAGE: fwfetch [OPTIONS]
  --profile <aula|lite|erista|all>   what to collect (default: aula)
  --search-root <DIR>                folder to search (FAT32 'switchroot' parent, rootfs copy, kernel tree); repeatable
  --disk <FILE|\\\\.\\PhysicalDriveN>    raw disk / partition image to scan for an ext4 volume (read-only); repeatable
  --image <FILE>                     same as --disk, meant for files. A numbered piece (l4t.00) is joined with l4t.01, l4t.02, ... so the
                                     downloaded Switchroot root-file-system image is read directly - nothing has to be flashed to an SD card
  --archive <FILE>                   a .deb, a tar, or a tar.gz / .tbz2 / .tar.xz to read the files from (read-only); repeatable.
                                     e.g. NVIDIA's jetson-210_linux_r32.7.6_aarch64.tbz2 (GPU + XUSB firmware). zstd is not supported
  --download                         for files still missing after the local search: download them from the OFFICIAL sources listed in the
                                     manifest (NVIDIA Jetson Linux package for GPU / XUSB firmware, the hekate release for sc7entry/sc7exit)
                                     with the system's curl, streamed and never saved whole. Meant for the Switch itself at Windows start-up.
                                     Wi-Fi / Bluetooth firmware and bl31.bin have no such source (Wi-Fi also needs that firmware to get online)
  --auto                             Windows: also every ready drive letter, every volume WITHOUT a drive letter (the Switch's FAT32
                                     'switchroot' partition often has none), and (administrator) every physical disk
  --out-dir <DIR>                    staging directory (default %LOCALAPPDATA%\\W4T\\fw-cache). Refused inside a git work tree
  --deploy <DIR>                     copy the windows-switch files into this boot-partition root
  --deploy-gpu <DIR|auto>            copy the GPU firmware into this directory
                                     auto = <SystemRoot>\\System32\\drivers\\TegraGpu of the RUNNING Windows (any drive letter)
  --deploy-drivers <DIR|auto>        copy the Wi-Fi / Bluetooth firmware to the places the installed drivers read it from
                                     (DIR = <SystemRoot>\\System32\\drivers -> SwitchWOA\\..., switchwifi\\...; auto = that folder)
  Paths may contain %VAR% (e.g. %SystemRoot%). Windows on a Switch is rarely on C: - use auto, do not type a letter.
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
        archives: vec![],
        download: false,
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
            "--out-dir" => o.out_dir = Some(winvol::resolve_dir(&val("--out-dir")?, None)?),
            "--search-root" => o.search_roots.push(PathBuf::from(val("--search-root")?)),
            "--disk" => o.disks.push(PathBuf::from(val("--disk")?)),
            "--image" => o.disks.push(PathBuf::from(val("--image")?)),
            "--download" => o.download = true,
            "--archive" => o.archives.push(PathBuf::from(val("--archive")?)),
            "--deploy" => o.deploy = Some(winvol::resolve_dir(&val("--deploy")?, None)?),
            "--deploy-gpu" => o.deploy_gpu = Some(winvol::resolve_dir(&val("--deploy-gpu")?, Some("TegraGpu"))?),
            "--deploy-drivers" => o.deploy_drivers = Some(winvol::resolve_dir(&val("--deploy-drivers")?, Some(""))?),
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
