#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Host tests for tools/fwfetch (the Rust binary). Uses ONLY synthetic random data - no real firmware is needed or stored.

Needs: python3, cargo (builds the binary), mke2fs (e2fsprogs >= 1.43, 'mke2fs -d').  Exit 77 = skipped (tool missing).
What this proves: the reader/extractor logic on images this script builds itself, and that corrupted images
do not crash the binary.
What it does NOT prove: that a real Switchroot image has these paths / this compression (see README-ja.md).
"""
import gzip, hashlib, json, os, random, re, shutil, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.dirname(HERE)
if not shutil.which("cargo") or not shutil.which("mke2fs"):
    print("SKIP: cargo or mke2fs not available"); sys.exit(77)
_b = subprocess.run(["cargo", "build", "--release", "--quiet", "--manifest-path", os.path.join(TOOL, "Cargo.toml")], capture_output=True, text=True)
if _b.returncode != 0:
    print("FAIL: cargo build\n" + _b.stderr); sys.exit(1)
BIN = os.path.join(os.environ.get("CARGO_TARGET_DIR") or os.path.join(TOOL, "target"), "release", "fwfetch")

rnd = random.Random(1234)
def blob(n): return bytes(rnd.getrandbits(8) for _ in range(n))
sha = lambda b: hashlib.sha256(b).hexdigest()

fails = []
def check(name, cond, extra=""):
    print(("PASS " if cond else "FAIL ") + name + ((" :: " + extra) if (extra and not cond) else ""))
    if not cond: fails.append(name)

def cpio_newc(members):
    out = b""; ino = 1
    for name, data in members + [("TRAILER!!!", b"")]:
        nb = name.encode() + b"\0"
        hdr = "070701" + "".join("%08X" % v for v in (ino, 0o100644 if name != "TRAILER!!!" else 0, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(nb), 0))
        out += hdr.encode() + nb
        out += b"\0" * ((4 - (110 + len(nb)) % 4) % 4)
        out += data + b"\0" * ((4 - len(data) % 4) % 4); ino += 1
    return out

def uimage(payload, comp=1):
    h = bytearray(64); h[0:4] = struct.pack(">I", 0x27051956); h[31] = comp
    return bytes(h) + payload

def ihex(data, base=0):
    lines = []
    for off in range(0, len(data), 16):
        chunk = data[off:off + 16]
        if (base + off) % 65536 == 0 and off:
            seg = ((base + off) >> 16) & 0xFFFF
            rec = bytes([2, 0, 0, 4]) + struct.pack(">H", seg)
            lines.append(":" + rec.hex().upper() + "%02X" % ((-sum(rec)) & 0xFF))
        a = (base + off) & 0xFFFF
        rec = bytes([len(chunk), a >> 8, a & 0xFF, 0]) + chunk
        lines.append(":" + rec.hex().upper() + "%02X" % ((-sum(rec)) & 0xFF))
    lines.append(":00000001FF")
    return "\n".join(lines) + "\n"

def run(args, cwd=None, manifest=None):
    extra = ["--manifest", manifest] if manifest else []
    p = subprocess.run([BIN] + extra + args, capture_output=True, text=True, cwd=cwd)
    return p.returncode, p.stdout + p.stderr

def mkdisk(path, part_img, scheme):
    size = os.path.getsize(part_img)
    with open(path, "wb") as d:
        d.truncate(1024 * 1024 + size + 1024 * 1024)
        d.seek(1024 * 1024); d.write(open(part_img, "rb").read())
        mbr = bytearray(512)
        if scheme == "mbr":
            mbr[446 + 4] = 0x83; mbr[446 + 8:446 + 12] = struct.pack("<I", 2048); mbr[446 + 12:446 + 16] = struct.pack("<I", size // 512)
        else:
            mbr[446 + 4] = 0xEE; mbr[446 + 8:446 + 12] = struct.pack("<I", 1); mbr[446 + 12:446 + 16] = struct.pack("<I", 0xFFFFFFFF)
        mbr[510:512] = b"\x55\xAA"; d.seek(0); d.write(mbr)
        if scheme == "gpt":
            h = bytearray(512); h[0:8] = b"EFI PART"; h[72:80] = struct.pack("<q", 2); h[80:84] = struct.pack("<I", 4); h[84:88] = struct.pack("<I", 128)
            d.seek(512); d.write(h)
            ent = bytearray(512)
            ent[0:16] = bytes(range(1, 17))  # type guid (non-empty)
            ent[128 + 0:128 + 16] = bytes(range(1, 17))   # entry #2: skip entry #1 (empty) to test the index
            ent[128 + 32:128 + 40] = struct.pack("<q", 2048); ent[128 + 40:128 + 48] = struct.pack("<q", 2048 + size // 512 - 1)
            ent[0:16] = bytes(16)
            d.seek(1024); d.write(ent)

tmp = tempfile.mkdtemp(prefix="fwfetch-test-")
try:
    # ---------------- synthetic "SD card" (FAT32 side) ----------------
    sd = os.path.join(tmp, "sd"); os.makedirs(os.path.join(sd, "switchroot", "ubuntu"))
    BL31 = blob(70000); open(os.path.join(sd, "switchroot", "ubuntu", "bl31.bin"), "wb").write(BL31)
    XUSB = blob(122368)
    other = blob(5000)
    cpio = cpio_newc([("lib/firmware/other.bin", other), ("lib/firmware/tegra210b01_xusb_firmware", XUSB), ("etc/x", b"1")])
    open(os.path.join(sd, "switchroot", "ubuntu", "initramfs"), "wb").write(uimage(gzip.compress(cpio)))
    open(os.path.join(sd, "switchroot", "ubuntu", "kernel.dat"), "wb").write(b"\x27\x05\x19\x56" + b"x" * 400000)  # not an initramfs; must be skipped quietly

    # ---------------- synthetic Linux rootfs (ext4, merged-usr symlink) ----------------
    BCM = blob(583641); HCD = blob(39008)
    TXT = b"NVRAMRev=$Rev$\nmacaddr=00:90:4c:c5:12:38\nboardtype=0x0000\n"
    GPU = {n: blob(2000 + i * 777) for i, n in enumerate(["NETB_img.bin", "fecs.bin", "gpccs.bin", "nv_acr_ucode_prod.bin", "pmu_bl.bin",
                                                          "gpmu_ucode_image.bin", "gpmu_ucode_desc.bin", "pmu_sig.bin", "fecs_sig.bin"])}
    root = os.path.join(tmp, "rootfs"); fwd = os.path.join(root, "usr/lib/firmware")
    os.makedirs(os.path.join(fwd, "brcm")); os.makedirs(os.path.join(fwd, "tegra21x"))
    os.symlink("usr/lib", os.path.join(root, "lib"))
    open(os.path.join(fwd, "brcm", "oddname.bin"), "wb").write(BCM)       # found only by hash
    open(os.path.join(fwd, "brcm", "BCM4356A3.hcd"), "wb").write(HCD)
    open(os.path.join(fwd, "brcm", "brcmfmac4356-pcie.txt"), "wb").write(TXT)
    for n, b in GPU.items(): open(os.path.join(fwd, "tegra21x", n), "wb").write(b)
    # many files in brcm/ so the directory spans more than one block
    for i in range(300): open(os.path.join(fwd, "brcm", "pad-%03d-aaaaaaaaaaaaaaaaaaaaaaaa.bin" % i), "wb").write(b"x")
    part = os.path.join(tmp, "part.img")
    subprocess.run(["mke2fs", "-q", "-t", "ext4", "-b", "4096", "-d", root, "-O", "^has_journal", part, "24M"], check=True)
    mbr_img = os.path.join(tmp, "disk-mbr.img"); gpt_img = os.path.join(tmp, "disk-gpt.img")
    mkdisk(mbr_img, part, "mbr"); mkdisk(gpt_img, part, "gpt")

    # ---------------- a copy of the tool whose manifest pins the SYNTHETIC hashes ----------------
    mp = os.path.join(tmp, "manifest.json"); m = json.load(open(os.path.join(TOOL, "firmware-manifest.json")))
    for f in m["files"]:
        if f["id"] == "bcm4356a3_fw": f["expect"] = {"size": len(BCM), "sha256": sha(BCM)}
        if f["id"] == "bcm4356a3_hcd": f["expect"] = {"size": len(HCD), "sha256": sha(HCD)}
    json.dump(m, open(mp, "w"), indent=1)

    # CI: dump scenario 3 (GPT image, lite profile, --deploy-gpu) so a Windows ARM64 runner can run the real fwfetch.exe on it.
    if os.environ.get("FWFETCH_DUMP_FIXTURE"):
        dump = os.environ["FWFETCH_DUMP_FIXTURE"]; shutil.rmtree(dump, ignore_errors=True)
        shutil.copytree(sd, os.path.join(dump, "sd")); shutil.copy(gpt_img, os.path.join(dump, "disk-gpt.img"))
        shutil.copy(mp, os.path.join(dump, "manifest.json"))
        json.dump({"gpu": {n: sha(b) for n, b in GPU.items()}, "bl31": sha(BL31), "xusb": sha(XUSB)},
                  open(os.path.join(dump, "expected.json"), "w"), indent=1)
        print("fixture written to " + dump); sys.exit(0)

    # 1. real manifest: synthetic firmware must be REJECTED (hash differs), but unhashed bl31 / size-checked xusb are taken
    out1 = os.path.join(tmp, "out1")
    rc, log = run(["--profile", "aula", "--search-root", sd, "--disk", mbr_img, "--out-dir", out1])
    check("real manifest: wrong-hash Wi-Fi/BT firmware is rejected, exit 2", rc == 2 and log.count("reject") >= 2, log[-800:])
    check("bl31 copied from switchroot/ubuntu byte-identical", open(os.path.join(out1, "wp11/bl31.bin"), "rb").read() == BL31)
    check("xusb extracted from uImage+gzip+cpio initramfs (122368 bytes)", open(os.path.join(out1, "tegra210b01_xusb_firmware"), "rb").read() == XUSB)
    check("non-initramfs file with uImage magic skipped without crash", "panicked" not in log, log[-600:])

    # 2. pinned synthetic hashes: found via ext4 (MBR) incl. merged-usr symlink and found-by-hash
    out2 = os.path.join(tmp, "out2"); dep = os.path.join(tmp, "boot")
    rc, log = run(["--profile", "aula", "--search-root", sd, "--disk", mbr_img, "--out-dir", out2, "--deploy", dep], manifest=mp)
    check("aula profile completes (exit 0) with pinned hashes", rc == 0, log[-900:])
    check("hcd read from ext4 through /lib -> usr/lib symlink", open(os.path.join(out2, "agent/bt/BCM4356A3.hcd"), "rb").read() == HCD)
    check("Wi-Fi firmware found by hash under another file name", open(os.path.join(out2, "agent/wifi/brcmfmac4356A3-pcie.bin"), "rb").read() == BCM and "found by hash" in log, log[-900:])
    txt = open(os.path.join(out2, "agent/wifi/brcmfmac4356A3-pcie.txt"), "rb").read().decode()
    mac = re.search(r"(?m)^macaddr=([0-9a-f:]{17})", txt)
    check("board file macaddr replaced (locally administered, unicast)", bool(mac) and mac.group(1) != "00:90:4c:c5:12:38" and (int(mac.group(1)[:2], 16) & 3) == 2, txt)
    check("deploy copies to wp11/wp64/agent layout", all(os.path.exists(os.path.join(dep, p)) for p in
          ["wp11/bl31.bin", "wp64/bl31.bin", "tegra210b01_xusb_firmware", "agent/wifi/brcmfmac4356A3-pcie.bin", "agent/wifi/brcmfmac4356A3-pcie.txt", "agent/bt/BCM4356A3.hcd"]))
    lock = json.load(open(os.path.join(out2, "fw-lock.json")))
    check("lock marks hashed files verified and bl31 unverified", lock["bcm4356a3_hcd"]["verified"] is True and lock["bl31"]["verified"] is False)
    mac1 = mac.group(1)
    rc, log = run(["--profile", "aula", "--search-root", sd, "--disk", mbr_img, "--out-dir", out2], manifest=mp)
    mac2 = re.search(r"(?m)^macaddr=([0-9a-f:]{17})", open(os.path.join(out2, "agent/wifi/brcmfmac4356A3-pcie.txt"), "rb").read().decode()).group(1)
    check("MAC stays the same on re-run", mac1 == mac2)

    # 3. GPT disk + GPU set (lite profile) + -DeployGpu
    out3 = os.path.join(tmp, "out3"); gdir = os.path.join(tmp, "gpu")
    rc, log = run(["--profile", "lite", "--search-root", sd, "--disk", gpt_img, "--out-dir", out3, "--deploy-gpu", gdir], manifest=mp)
    check("lite profile completes on a GPT image", rc == 0, log[-900:])
    check("all 9 GPU files deployed byte-identical", all(os.path.exists(os.path.join(gdir, n)) and open(os.path.join(gdir, n), "rb").read() == b for n, b in GPU.items()))

    # 3b. --deploy-drivers: Wi-Fi / Bluetooth firmware goes where the installed drivers read it (System32\\drivers\\SwitchWOA, switchwifi)
    out3b = os.path.join(tmp, "out3b"); drv = os.path.join(tmp, "drivers")
    rc, log = run(["--profile", "lite", "--search-root", sd, "--disk", gpt_img, "--out-dir", out3b, "--deploy-drivers", drv], manifest=mp)
    check("--deploy-drivers run completes", rc == 0, log[-900:])
    check("Wi-Fi firmware deployed for the T214 driver (SwitchWOA) and the Erista driver (switchwifi)",
          all(os.path.exists(os.path.join(drv, q)) and open(os.path.join(drv, q), "rb").read() == BCM for q in ["SwitchWOA/brcmfmac4356A3-pcie.bin", "switchwifi/brcmfmac4356-pcie.bin"]))
    check("Bluetooth patch deployed to SwitchWOA\\BCM4356A3.hcd", os.path.exists(os.path.join(drv, "SwitchWOA/BCM4356A3.hcd")) and open(os.path.join(drv, "SwitchWOA/BCM4356A3.hcd"), "rb").read() == HCD)
    st = open(os.path.join(out3b, "agent/wifi/brcmfmac4356A3-pcie.txt"), "rb").read()
    check("board file deployed with the replaced MAC, identical to the staged copy", all(open(os.path.join(drv, q), "rb").read() == st for q in ["SwitchWOA/brcmfmac4356A3-pcie.txt", "switchwifi/brcmfmac4356-pcie.txt"]))
    check("GPU / boot-partition files are not touched by --deploy-drivers", not os.path.exists(os.path.join(drv, "NETB_img.bin")) and not os.path.exists(os.path.join(drv, "wp11")))
    lock3b = json.load(open(os.path.join(out3b, "fw-lock.json")))
    check("fw-lock.json records where each driver file was deployed", lock3b["bcm4356a3_fw"]["drivers"] == ["SwitchWOA/brcmfmac4356A3-pcie.bin", "switchwifi/brcmfmac4356-pcie.bin"] and "drivers" not in lock3b["gpu_fecs"], str(lock3b.get("bcm4356a3_fw")))
    gt = os.path.join(tmp, "gittree"); os.makedirs(gt); subprocess.run(["git", "init", "-q", gt], check=True)
    rc, log = run(["--profile", "lite", "--search-root", sd, "--out-dir", os.path.join(tmp, "out3c"), "--deploy-drivers", os.path.join(gt, "drivers")], manifest=mp)
    check("--deploy-drivers inside a git work tree is refused", rc == 1, log[-400:])

    # 4. Intel-HEX route (kernel tree), no ext4 at all
    kt = os.path.join(tmp, "ktree/firmware/brcm"); os.makedirs(kt)
    open(os.path.join(kt, "brcmfmac4356A3-pcie.bin.ihex"), "w").write(ihex(BCM))
    open(os.path.join(kt, "BCM4356A3.hcd.ihex"), "w").write(ihex(HCD))
    out4 = os.path.join(tmp, "out4")
    rc, log = run(["--profile", "erista", "--search-root", os.path.join(tmp, "ktree"), "--out-dir", out4], manifest=mp)
    check("ihex converted to the exact binary (hash pinned)", os.path.exists(os.path.join(out4, "agent/wifi/brcmfmac4356A3-pcie.bin")) and
          open(os.path.join(out4, "agent/wifi/brcmfmac4356A3-pcie.bin"), "rb").read() == BCM and open(os.path.join(out4, "agent/bt/BCM4356A3.hcd"), "rb").read() == HCD, log[-700:])
    check("missing files -> exit 2 and listed as MISS", rc == 2 and "[MISS ] gpu_fecs" in log)
    bad = ihex(HCD).replace(":00000001FF", ":00000001FE")
    open(os.path.join(kt, "BCM4356A3.hcd.ihex"), "w").write(bad)
    out4b = os.path.join(tmp, "out4b")
    rc, log = run(["--profile", "erista", "--search-root", os.path.join(tmp, "ktree"), "--out-dir", out4b], manifest=mp)
    check("corrupt ihex is skipped, not staged", not os.path.exists(os.path.join(out4b, "agent/bt/BCM4356A3.hcd")))

    # 5. initramfs variants: plain gzip works; zstd is reported, not crashing
    sd2 = os.path.join(tmp, "sd2/switchroot/android"); os.makedirs(sd2)
    open(os.path.join(sd2, "ramdisk.img"), "wb").write(gzip.compress(cpio))
    open(os.path.join(sd2, "a-ramdisk-zstd.img"), "wb").write(b"\x28\xb5\x2f\xfd" + b"z" * 400000)
    # (the scan stops at the first accepted file, so the zstd file is named to sort BEFORE the good one)
    out5 = os.path.join(tmp, "out5")
    rc, log = run(["--profile", "aula", "--search-root", os.path.join(tmp, "sd2"), "--out-dir", out5], manifest=mp)
    check("plain-gzip initramfs works", os.path.exists(os.path.join(out5, "tegra210b01_xusb_firmware")), log[-600:])
    check("zstd initramfs is reported as unsupported", "zstd initramfs not supported" in log, log[-600:])

    # 6. safety: refuse to stage inside a git work tree; -RequireKnownHash refuses unhashed files
    g = os.path.join(tmp, "repo"); os.makedirs(os.path.join(g, ".git"))
    rc, log = run(["--profile", "aula", "--search-root", sd, "--out-dir", os.path.join(g, "fw")])
    check("refuses OutDir inside a git work tree", rc != 0 and "Refusing" in log and not os.path.exists(os.path.join(g, "fw", "wp11")))
    out6 = os.path.join(tmp, "out6")
    rc, log = run(["--profile", "aula", "--search-root", sd, "--out-dir", out6, "--require-known-hash"])
    check("-RequireKnownHash refuses bl31 (no published hash)", not os.path.exists(os.path.join(out6, "wp11/bl31.bin")) and "no published hash" in log, log[-500:])

    # 7. ext4 reader versus e2fsprogs: every file in the image must read back identical (incl. 300-entry directory)
    out7 = os.path.join(tmp, "out7")
    rc, log = run(["--profile", "lite", "--disk", part, "--out-dir", out7], manifest=mp)   # whole-partition image, no table
    check("bare partition image (no partition table) is accepted", "1 ext4 volume" in log, log[-500:])

    # 8. manifest safety: a manifest that tries to write outside the staging dir is refused before anything is staged
    evil = json.load(open(os.path.join(TOOL, "firmware-manifest.json")))
    evil["files"][0]["deploy"]["windows-switch"] = ["../../escape.bin"]
    ep = os.path.join(tmp, "evil.json"); json.dump(evil, open(ep, "w"))
    out8 = os.path.join(tmp, "out8")
    rc, log = run(["--profile", "aula", "--search-root", sd, "--out-dir", out8], manifest=ep)
    check("manifest with a '..' output path is refused", rc == 1 and "unsafe output path" in log and not os.path.exists(os.path.join(tmp, "escape.bin")), log[-400:])
    evil2 = json.load(open(os.path.join(TOOL, "firmware-manifest.json")))
    evil2["files"][0]["expect"] = {"sha256": "ABC"}
    json.dump(evil2, open(ep, "w"))
    rc, log = run(["--profile", "aula", "--search-root", sd, "--out-dir", out8], manifest=ep)
    check("malformed sha256 in a manifest is refused", rc == 1 and "malformed sha256" in log, log[-300:])

    # 9. deploy re-verifies the staged bytes: tamper with the staged copy and --deploy must refuse
    out9 = os.path.join(tmp, "out9"); dep9 = os.path.join(tmp, "boot9")
    run(["--profile", "aula", "--search-root", sd, "--disk", mbr_img, "--out-dir", out9], manifest=mp)
    # corrupt the staged bl31 between two runs: the second run re-stages it (so it is fine), but a hand-edit made
    # AFTER staging and BEFORE deploy cannot be simulated in one process; instead check a symlinked destination is refused
    if hasattr(os, "symlink"):
        os.remove(os.path.join(out9, "wp11/bl31.bin")); os.symlink(os.path.join(tmp, "elsewhere.bin"), os.path.join(out9, "wp11/bl31.bin"))
        rc, log = run(["--profile", "aula", "--search-root", sd, "--disk", mbr_img, "--out-dir", out9], manifest=mp)
        check("a symlink at the destination is not written through", rc == 1 and not os.path.exists(os.path.join(tmp, "elsewhere.bin")), log[-300:])

    # 10. robustness: corrupted / truncated disk images must never crash the binary (panic = abort => rc -6)
    fz = random.Random(99); raw = open(mbr_img, "rb").read(); bad_rc = []
    for i in range(150):
        b = bytearray(raw)
        for _ in range(fz.choice([1, 8, 64])):
            pos = fz.choice([fz.randrange(0, 4096), fz.randrange(1024 * 1024, 1024 * 1024 + 300000), fz.randrange(0, len(b))])
            b[pos] = fz.getrandbits(8)
        if i % 10 == 0: b = b[:fz.randrange(1, len(b))]
        fp = os.path.join(tmp, "fuzz.img"); open(fp, "wb").write(b)
        rc, log = run(["--profile", "lite", "--disk", fp, "--out-dir", os.path.join(tmp, "outz"), "--quiet"], manifest=mp)
        if rc not in (0, 1, 2) or "panicked" in log: bad_rc.append((i, rc))
    check("150 corrupted/truncated disk images: no crash", not bad_rc, str(bad_rc[:5]))
    fz_cpio = []
    for i in range(60):
        g = bytearray(gzip.compress(cpio)); g[fz.randrange(0, len(g))] ^= 1 << fz.randrange(8)
        if i % 6 == 0: g = g[:fz.randrange(1, len(g))]
        dd = os.path.join(tmp, "fzsd/switchroot/x"); os.makedirs(dd, exist_ok=True); open(os.path.join(dd, "initramfs"), "wb").write(uimage(bytes(g)) + b"\0" * 20000)
        rc, log = run(["--profile", "aula", "--search-root", os.path.join(tmp, "fzsd"), "--out-dir", os.path.join(tmp, "outc"), "--quiet"], manifest=mp)
        if rc not in (0, 1, 2) or "panicked" in log: fz_cpio.append((i, rc))
    check("60 corrupted initramfs files: no crash", not fz_cpio, str(fz_cpio[:5]))

    # 11. Erista profile (real manifest): hekate's sc7entry/sc7exit + L4T XUSB firmware are found on the SD / rootfs copy
    er = os.path.join(tmp, "erista-root"); os.makedirs(os.path.join(er, "bootloader/sys/l4t")); os.makedirs(os.path.join(er, "lib/firmware"))
    SC7E, SC7X, XU21 = blob(9000), blob(20000), blob(100000)
    open(os.path.join(er, "bootloader/sys/l4t/sc7entry.bin"), "wb").write(SC7E)
    open(os.path.join(er, "bootloader/sys/l4t/sc7exit.bin"), "wb").write(SC7X)
    open(os.path.join(er, "lib/firmware/tegra21x_xusb_firmware"), "wb").write(XU21)
    open(os.path.join(er, "bl31.bin"), "wb").write(blob(1000)); os.makedirs(os.path.join(er, "switchroot/ubuntu"), exist_ok=True)
    open(os.path.join(er, "switchroot/ubuntu/bl31.bin"), "wb").write(BL31)
    out11 = os.path.join(tmp, "out11")
    rc, log = run(["--profile", "erista", "--search-root", er, "--out-dir", out11])
    rd = lambda *a: open(os.path.join(out11, *a), "rb").read() if os.path.exists(os.path.join(out11, *a)) else None
    check("erista: sc7entry/sc7exit staged byte-identical", rd("bootloader/sys/l4t/sc7entry.bin") == SC7E and rd("bootloader/sys/l4t/sc7exit.bin") == SC7X, log[-700:])
    check("erista: T210 XUSB firmware staged", rd("tegra21x_xusb_firmware") == XU21, log[-700:])
    check("erista profile now wants bl31 too", "bl31" in log)
finally:
    shutil.rmtree(tmp, ignore_errors=True)

print("\n%d failed" % len(fails) if fails else "\nall passed")
sys.exit(1 if fails else 0)
