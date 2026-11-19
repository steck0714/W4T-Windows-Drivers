#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Bring a firmware bundle that lives OUTSIDE this repository onto a CI runner, so fwfetch can read it.

The files fwfetch handles (NVIDIA GPU / XUSB, Broadcom Wi-Fi + Bluetooth, Switchroot bl31) must not be redistributed.
They are never in this repository. In CI you supply them as a private zip (a copy of the tree fwfetch searches on your
SD card: switchroot/<distro>/..., lib/firmware/...) behind a URL kept in a repository secret. This script only moves
bytes; fwfetch does the matching and the SHA-256 checks. fwfetch itself stays offline.

  fw_bundle.py fetch   --url URL --out FILE [--sha256 HEX]      (https only; header from env FW_BUNDLE_AUTH_HEADER)
  fw_bundle.py extract --zip FILE --dest DIR [--manifest FILE]  (zip-slip / symlink / bomb safe)

`extract` also accepts the flat layout of the older bundles (the nine GPU files at the zip root): the GPU files are
copied into <dest>/lib/firmware/tegra21x/ so that fwfetch's own path rules find them. File names come from
tools/fwfetch/firmware-manifest.json, not from this script.
"""
import argparse, hashlib, json, os, shutil, stat, sys, urllib.error, urllib.parse, urllib.request, zipfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_MANIFEST = os.path.join(ROOT, "tools", "fwfetch", "firmware-manifest.json")
MAX_DOWNLOAD = 4 << 30        # 4 GiB
MAX_ENTRIES = 20000
MAX_FILE = 1536 << 20         # 1.5 GiB per file
MAX_TOTAL = 3 << 30           # 3 GiB unpacked


class _DropAuthOnHostChange(urllib.request.HTTPRedirectHandler):
    """A signed download link on another host (object storage) must not receive our Authorization header."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        new = super().redirect_request(req, fp, code, msg, headers, newurl)
        if new is not None and urllib.parse.urlsplit(newurl).netloc != urllib.parse.urlsplit(req.full_url).netloc:
            new.headers = {k: v for k, v in new.headers.items() if k.lower() != "authorization"}
            new.unredirected_hdrs = {k: v for k, v in new.unredirected_hdrs.items() if k.lower() != "authorization"}
        return new


def fetch(url, out, sha256=None, auth=None):
    parts = urllib.parse.urlsplit(url)
    if parts.scheme != "https" or not parts.netloc:
        sys.exit("fetch: the bundle URL must be https")
    req = urllib.request.Request(url, headers={"User-Agent": "w4t-fw-bundle"})
    if auth:
        req.add_header("Authorization", auth)
    opener = urllib.request.build_opener(_DropAuthOnHostChange)
    h = hashlib.sha256()
    n = 0
    try:
        with opener.open(req, timeout=120) as r, open(out, "wb") as f:
            while True:
                chunk = r.read(1 << 20)
                if not chunk:
                    break
                n += len(chunk)
                if n > MAX_DOWNLOAD:
                    sys.exit("fetch: bundle larger than %d bytes, giving up" % MAX_DOWNLOAD)
                h.update(chunk)
                f.write(chunk)
    except urllib.error.HTTPError as e:   # never echo the URL: it may carry a token
        sys.exit("fetch: HTTP %s from %s" % (e.code, parts.netloc))
    except (urllib.error.URLError, OSError) as e:
        sys.exit("fetch: %s (host %s)" % (getattr(e, "reason", e), parts.netloc))
    digest = h.hexdigest()
    if sha256 and digest != sha256.lower():
        os.remove(out)
        sys.exit("fetch: SHA-256 of the bundle is %s, expected %s - deleted" % (digest, sha256.lower()))
    print("fetched %d bytes from %s, sha256 %s%s" % (n, parts.netloc, digest, " (matches the pin)" if sha256 else " (no pin given)"))


def _safe_member(name):
    if not name or name.startswith(("/", "\\")) or "\\" in name or ":" in name or "\x00" in name:
        return None
    parts = name.split("/")
    if any(p in ("..",) for p in parts):
        return None
    return [p for p in parts if p not in ("", ".")]


def gpu_names(manifest_path):
    with open(manifest_path, encoding="utf-8") as f:
        man = json.load(f)
    return [f["gpu"] for f in man["files"] if "gpu" in f], man["gpu_template"]["dirs"]


def extract(zip_path, dest, manifest_path=DEFAULT_MANIFEST):
    dest = os.path.abspath(dest)
    os.makedirs(dest, exist_ok=True)
    total = 0
    count = 0
    with zipfile.ZipFile(zip_path) as z:
        infos = z.infolist()
        if len(infos) > MAX_ENTRIES:
            sys.exit("extract: %d entries (limit %d)" % (len(infos), MAX_ENTRIES))
        for info in infos:
            parts = _safe_member(info.filename)
            if parts is None:
                sys.exit("extract: unsafe path in the zip: %r" % info.filename)
            mode = info.external_attr >> 16
            if mode and stat.S_ISLNK(mode):
                sys.exit("extract: symbolic link in the zip: %r" % info.filename)
            if info.is_dir() or not parts:
                continue
            if info.file_size > MAX_FILE:
                sys.exit("extract: %r is larger than %d bytes" % (info.filename, MAX_FILE))
            total += info.file_size
            if total > MAX_TOTAL:
                sys.exit("extract: more than %d bytes unpacked" % MAX_TOTAL)
            target = os.path.join(dest, *parts)
            if os.path.commonpath([dest, os.path.abspath(target)]) != dest:
                sys.exit("extract: path escapes the destination: %r" % info.filename)
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with z.open(info) as src, open(target, "wb") as out:
                shutil.copyfileobj(src, out, 1 << 20)
            count += 1
    print("extracted %d file(s), %d bytes" % (count, total))
    # flat layout: GPU files named like the manifest's, anywhere in the tree, but not yet under a GPU dir
    names, dirs = gpu_names(manifest_path)
    have = {n for n in names for d in dirs if os.path.isfile(os.path.join(dest, *d.split("/"), n))}
    moved = []
    for d, _, fs in os.walk(dest):
        for fn in fs:
            if fn in names and fn not in have:
                to = os.path.join(dest, *dirs[0].split("/"))
                os.makedirs(to, exist_ok=True)
                shutil.copyfile(os.path.join(d, fn), os.path.join(to, fn))
                have.add(fn)
                moved.append(fn)
    if moved:
        print("flat layout: copied %d GPU file(s) into %s/: %s" % (len(moved), dirs[0], ", ".join(sorted(moved))))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    f = sub.add_parser("fetch")
    f.add_argument("--url", required=True)
    f.add_argument("--out", required=True)
    f.add_argument("--sha256")
    e = sub.add_parser("extract")
    e.add_argument("--zip", required=True)
    e.add_argument("--dest", required=True)
    e.add_argument("--manifest", default=DEFAULT_MANIFEST)
    a = ap.parse_args(argv)
    if a.cmd == "fetch":
        fetch(a.url, a.out, a.sha256, os.environ.get("FW_BUNDLE_AUTH_HEADER") or None)
    else:
        extract(a.zip, a.dest, a.manifest)


if __name__ == "__main__":
    main()
