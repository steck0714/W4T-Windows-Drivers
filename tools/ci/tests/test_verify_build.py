#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Synthetic PE / INF files only: proves verify_build.py rejects what it should. Says nothing about real drivers."""
import os, struct, subprocess, sys, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "verify_build.py")


def make_pe(machine=0xAA64, subsystem=1, signed=True, magic=0x20B):
    dos = bytearray(0x80); dos[:2] = b"MZ"; struct.pack_into("<I", dos, 0x3C, 0x80)
    opt_size = 240 if magic == 0x20B else 224
    coff = struct.pack("<HHIIIHH", machine, 0, 0, 0, 0, opt_size, 0x22)
    opt = bytearray(opt_size)
    struct.pack_into("<H", opt, 0, magic)
    struct.pack_into("<H", opt, 68, subsystem)
    dd_count_off = 108 if magic == 0x20B else 92
    dd_off = 112 if magic == 0x20B else 96
    struct.pack_into("<I", opt, dd_count_off, 16)
    if signed:
        struct.pack_into("<II", opt, dd_off + 4 * 8, 0x1000, 0x200)
    return bytes(dos) + b"PE\0\0" + coff + bytes(opt)


INF = ("[Version]\nSignature=\"$WINDOWS NT$\"\nCatalogFile=x.cat\nDriverVer=11/19/2026,1.0.0.0\n"
       "[Manufacturer]\n%m%=Sec,NTarm64\n[SourceDisksFiles]\nx.sys=1\n")


def kit(root, pe, inf=INF, cat=True):
    d = os.path.join(root, "Switch-X"); os.makedirs(d)
    with open(os.path.join(d, "x.sys"), "wb") as f:
        f.write(pe)
    if inf is not None:
        with open(os.path.join(d, "x.inf"), "w") as f:
            f.write(inf)
    if cat:
        with open(os.path.join(d, "x.cat"), "wb") as f:
            f.write(b"c")
    return root


def run(root, *extra):
    r = subprocess.run([sys.executable, TOOL, root, *extra], capture_output=True, text=True)
    return r.returncode, r.stdout


class T(unittest.TestCase):
    def check(self, want_rc, needle=None, **kw):
        with tempfile.TemporaryDirectory() as t:
            rc, out = run(kit(t, **kw))
            self.assertEqual(rc, want_rc, out)
            if needle:
                self.assertIn(needle, out)

    def test_good(self): self.check(0, pe=make_pe())
    def test_x64_rejected(self): self.check(1, "not ARM64", pe=make_pe(machine=0x8664))
    def test_arm32_rejected(self): self.check(1, "not ARM64", pe=make_pe(machine=0x1C4))
    def test_gui_subsystem_rejected(self): self.check(1, "NATIVE", pe=make_pe(subsystem=2))
    def test_unsigned_rejected(self): self.check(1, "no embedded signature", pe=make_pe(signed=False))
    def test_not_pe(self): self.check(1, "not a PE", pe=b"hello world" * 20)
    def test_truncated(self): self.check(1, "PE", pe=make_pe()[:0x60])
    def test_missing_cat(self): self.check(1, "CatalogFile", pe=make_pe(), cat=False)
    def test_no_inf(self): self.check(1, "no INF", pe=make_pe(), inf=None, cat=False)
    def test_inf_not_arm64(self): self.check(1, "NTarm64", pe=make_pe(), inf=INF.replace("NTarm64", "NTamd64"))
    def test_inf_no_driverver(self): self.check(1, "DriverVer", pe=make_pe(), inf=INF.replace("DriverVer=11/19/2026,1.0.0.0\n", ""))
    def test_inf_lists_missing_file(self): self.check(1, "SourceDisksFiles", pe=make_pe(), inf=INF + "y.sys=1\n")

    def test_allow_unsigned(self):
        with tempfile.TemporaryDirectory() as t:
            rc, out = run(kit(t, pe=make_pe(signed=False)), "--allow-unsigned")
            self.assertEqual(rc, 0, out)

    def test_empty_folder(self):
        with tempfile.TemporaryDirectory() as t:
            self.assertEqual(run(t)[0], 2)


class Apps(unittest.TestCase):
    def app(self, root, name="SwitchCenter", machine=0xAA64, extra=None):
        d = os.path.join(root, name); os.makedirs(d)
        with open(os.path.join(d, name + ".exe"), "wb") as f:
            f.write(make_pe(machine=machine, subsystem=2, signed=False))
        if extra:
            with open(os.path.join(d, extra), "wb") as f:
                f.write(b"k")

    def test_good(self):
        with tempfile.TemporaryDirectory() as t:
            self.app(t)
            self.assertEqual(run(t, "--apps")[0], 0)

    def test_x64_exe(self):
        with tempfile.TemporaryDirectory() as t:
            self.app(t, machine=0x8664)
            rc, out = run(t, "--apps")
            self.assertEqual(rc, 1); self.assertIn("not ARM64", out)

    def test_key_file(self):
        with tempfile.TemporaryDirectory() as t:
            self.app(t, extra="x.pfx")
            rc, out = run(t, "--apps")
            self.assertEqual(rc, 1); self.assertIn("key material", out)

    def test_missing_exe(self):
        with tempfile.TemporaryDirectory() as t:
            os.makedirs(os.path.join(t, "SwitchCenter"))
            rc, out = run(t, "--apps")
            self.assertEqual(rc, 1); self.assertIn("missing", out)


if __name__ == "__main__":
    unittest.main(verbosity=2)
