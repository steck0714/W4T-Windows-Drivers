#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Tests for tools/ci/fw_bundle.py (safe unpacking of the external firmware bundle). Synthetic data only: random bytes
with the NAMES of the GPU files, no real firmware.   python3 tools/ci/tests/test_fw_bundle.py"""
import hashlib, io, json, os, stat, subprocess, sys, tempfile, unittest, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "ci"))
import fw_bundle  # noqa: E402

MAN = os.path.join(ROOT, "tools", "fwfetch", "firmware-manifest.json")
NAMES, DIRS = fw_bundle.gpu_names(MAN)


def make_zip(path, entries):
    with zipfile.ZipFile(path, "w") as z:
        for name, data in entries:
            if isinstance(name, zipfile.ZipInfo):
                z.writestr(name, data)
            else:
                z.writestr(name, data)


class Extract(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.zip = os.path.join(self.tmp.name, "b.zip")
        self.dest = os.path.join(self.tmp.name, "out")

    def run_extract(self):
        fw_bundle.extract(self.zip, self.dest, MAN)

    def test_nine_gpu_names_from_manifest(self):
        self.assertEqual(len(NAMES), 9)
        self.assertIn("fecs.bin", NAMES)

    def test_flat_layout_is_moved_under_the_first_gpu_dir(self):
        make_zip(self.zip, [(n, os.urandom(32)) for n in NAMES])
        self.run_extract()
        for n in NAMES:
            self.assertTrue(os.path.isfile(os.path.join(self.dest, *DIRS[0].split("/"), n)), n)

    def test_rootfs_layout_is_left_alone(self):
        make_zip(self.zip, [(DIRS[1] + "/" + n, b"x" * 16) for n in NAMES])
        self.run_extract()
        self.assertFalse(os.path.exists(os.path.join(self.dest, *DIRS[0].split("/"))))
        self.assertTrue(os.path.isfile(os.path.join(self.dest, *DIRS[1].split("/"), "fecs.bin")))

    def test_zip_slip_is_refused(self):
        for bad in ("../evil.bin", "a/../../evil.bin", "/abs.bin", "C:/x.bin", "a\\b.bin"):
            make_zip(self.zip, [(bad, b"x")])
            with self.assertRaises(SystemExit, msg=bad):
                self.run_extract()
        self.assertFalse(os.path.exists(os.path.join(self.tmp.name, "evil.bin")))

    def test_symlink_is_refused(self):
        zi = zipfile.ZipInfo("link")
        zi.create_system = 3
        zi.external_attr = (stat.S_IFLNK | 0o777) << 16
        make_zip(self.zip, [(zi, b"/etc/passwd")])
        with self.assertRaises(SystemExit):
            self.run_extract()

    def test_too_many_entries_is_refused(self):
        old = fw_bundle.MAX_ENTRIES
        fw_bundle.MAX_ENTRIES = 3
        self.addCleanup(setattr, fw_bundle, "MAX_ENTRIES", old)
        make_zip(self.zip, [("f%d" % i, b"x") for i in range(5)])
        with self.assertRaises(SystemExit):
            self.run_extract()

    def test_size_limit_is_refused(self):
        old = fw_bundle.MAX_FILE
        fw_bundle.MAX_FILE = 10
        self.addCleanup(setattr, fw_bundle, "MAX_FILE", old)
        make_zip(self.zip, [("big", b"x" * 100)])
        with self.assertRaises(SystemExit):
            self.run_extract()


class Fetch(unittest.TestCase):
    def test_only_https(self):
        with self.assertRaises(SystemExit):
            fw_bundle.fetch("http://example.invalid/a.zip", os.devnull)
        with self.assertRaises(SystemExit):
            fw_bundle.fetch("file:///etc/passwd", os.devnull)


class EndToEnd(unittest.TestCase):
    """bundle zip -> extract -> fwfetch --search-root -> --deploy-gpu. Needs cargo; skipped without it."""

    def test_fwfetch_stages_the_gpu_files_from_a_flat_bundle(self):
        exe = os.path.join(ROOT, "tools", "fwfetch", "target", "release", "fwfetch")
        if not os.path.exists(exe):
            try:
                have_cargo = subprocess.run(["cargo", "--version"], capture_output=True).returncode == 0
            except FileNotFoundError:
                have_cargo = False
            if not have_cargo:
                self.skipTest("no cargo")
            r = subprocess.run(["cargo", "build", "--locked", "--release", "--manifest-path",
                                os.path.join(ROOT, "tools", "fwfetch", "Cargo.toml")], capture_output=True, text=True)
            if r.returncode != 0:
                self.skipTest("fwfetch did not build: " + r.stderr[-300:])
        with tempfile.TemporaryDirectory() as tmp:
            data = {n: os.urandom(64) for n in NAMES}
            z = os.path.join(tmp, "b.zip")
            make_zip(z, list(data.items()))
            inn, cache, gpu = (os.path.join(tmp, d) for d in ("in", "cache", "gpu"))
            fw_bundle.extract(z, inn, MAN)
            r = subprocess.run([exe, "--profile", "lite", "--search-root", inn, "--out-dir", cache, "--deploy-gpu", gpu],
                               capture_output=True, text=True)
            # lite = bl31, xusb, Wi-Fi... are not in the bundle -> exit 2 (some missing), never 1 (error)
            self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
            for n, b in data.items():
                with open(os.path.join(gpu, n), "rb") as f:
                    self.assertEqual(hashlib.sha256(f.read()).hexdigest(), hashlib.sha256(b).hexdigest(), n)


if __name__ == "__main__":
    unittest.main(verbosity=2)
