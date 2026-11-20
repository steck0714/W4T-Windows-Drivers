#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""What the Windows drivers expect the UEFI (EDK II / winx BL33) to have done before Windows starts - and what is checked.

  python3 -I uefi/tools/list_uefi_requirements.py            # table
  python3 -I uefi/tools/list_uefi_requirements.py --strict   # exit 1 if a driver source names a newer UEFI than the manifest says
  python3 -I uefi/tools/list_uefi_requirements.py --winx PATH   # also check a steck0714/winx checkout (branch usually): do the build flags and
                                                                # UEFI modules that each model's Windows drivers rely on exist in NintendoSwitch.dsc?

Everything here is static (source comments, ACPI comments, the SwitchDrivers manifest). It finds the "UEFI R<n>" revisions the
driver sources name, compares the newest one with `minUefi` of reference/switchdrivers-manifest-*.json (that manifest describes the
SHIPPED binaries, so a newer source is normal drift, not a bug: it tells you the manifest needs a new row), and says whether the repository
has a machine check for that hand-over (today only the GPU one: uefi/tools/check_uefi_contract.py). It does not run EDK II and does not
prove a UEFI build provides anything: the UEFI sources (winx) are not in this repository.
"""
import glob, json, os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
REV = re.compile(r"UEFI\s*R(\d{3})")
# service name in the manifest -> source folder name (under drivers/t210-icosa/drivers and drivers/t214-hoag)
SERVICE_DIR = {"tegra210gpio": "Tegra210Gpio", "tegra210i2c": "Tegra210I2c", "switchbattery": "SwitchBattery", "switchfan": "SwitchFan",
               "switchtouch": "SwitchTouch", "switchpwrbtn": "SwitchPowerButton", "switchcpu": "SwitchCpuClock", "switchrtc": "SwitchRtc",
               "switchsensor": "SwitchSensor", "switchbt": "SwitchBluetooth", "switchaudio": "SwitchAudio", "switchwlan": "SwitchWlan",
               "switchwifi": "SwitchWifi", "nvtsdhc": "Tegra210Sdhc"}
# what the UEFI must hand over, from the driver headers / DSDT comments (hand-written, one line per driver)
HANDOVER = {
    "SwitchBattery": "BM92T USB-C PD on I2C1 0x18 left running, XUSB left running (R378), charger configured (R363)",
    "SwitchFan": "fan 5V EN (PA5) on, PWM clocked and routed, duty 76; LCD_BL_PWM (PV0) on PWM0 (R376)",
    "SwitchTouch": "panel powered (LDO6 2.9 V, reset PJ7 high), I2C3 clock on",
    "SwitchCpuClock": "CPU at PLLX 1017.6 MHz, 1025 mV (default profile)",
    "SwitchSensor": "SPI4 and I2C2 clocked (reads stall while the clock is gated)",
    "SwitchBluetooth": "UART-D pinmux, clock and chip power (PH4); R391 leaves the 3 Mbaud override",
    "SwitchAudio": "APE clocks on, ALC5639 codec set up, I2S1 started",
    "SwitchWlan": "PCIe link up, endpoint enumerated (ID, memory enable, BAR0), resources in ACPI (R404/R405)",
    "SwitchWifiProbe": "same as SwitchWlan",
    "Tegra210I2c": "I2C1 and I2C5 clock/reset on",
    "Tegra210Sdhc": "SDMMC1 clock left as the card needed it",
    "SwitchRtc": "none (UEFI RTC is virtual; the driver restores the offset)",
}
CHECKED = {"TegraGpu": "uefi/tools/check_uefi_contract.py (CAR RESET_X bit24 + PMC 0x7000e6d4 bit0, HIDREV, BUSY rule) + GpuHandoff host test"}


# UEFI differs per model: each family has its own UEFI build (board flag) and hand-over
FAMILY = {"drivers/t210-icosa/drivers": "t210 (Erista, HAC-001, SWITCH_BOARD_ERISTA)", "drivers/t214-hoag": "t214 (Lite, HDH-001, SWITCH_LITE_*)"}


def revs_by_dir():
    out = {}
    for base in ("drivers/t210-icosa/drivers", "drivers/t214-hoag"):
        for d, _, fs in os.walk(os.path.join(ROOT, base)):
            for f in fs:
                if os.path.splitext(f)[1] not in (".c", ".h", ".inf", ".cpp"):
                    continue
                try:
                    t = open(os.path.join(d, f), errors="replace").read()
                except OSError:
                    continue
                found = {int(x) for x in REV.findall(t)}
                if found:
                    top = os.path.relpath(os.path.join(d, f), os.path.join(ROOT, base)).split(os.sep)[0]
                    out.setdefault((FAMILY[base], top), set()).update(found)
    return out


# Per model: Windows driver -> (winx DSC flag that builds the UEFI hand-over, module path under Drivers/ or "" ). From NintendoSwitch.dsc of winx `usually`.
# The OLED (Aula) UEFI is NOT winx: DanilKhmyrov/windows-switch carries its own patch series over imbushuo NintendoSwitchPkg (uefi/ + patches/).
MODEL_UEFI = {
    "Erista (HAC-001)": [("board", "SWITCH_BOARD_ERISTA", ""),
                         ("USB-C PD/charger, CPU clock, fan, backlight, touch power, SPI4/I2C2 clocks (R363-R379)", "SWITCH_WOA", "UsbPowerAuditDxe/UsbPowerAudit.inf"),
                         ("Wi-Fi (PCIe) hand-over", "SWITCH_WIFI_ERISTA", "WifiBtApp/WifiPrepare.inf"),
                         ("GPU hand-over", "SWITCH_ERISTA_GPU", "GpuHandoffApp/GpuHandoffApp.inf")],
    "Lite (HDH-001)": [("board", "SWITCH_LITE_TOUCH", ""),
                       ("fan", "SWITCH_LITE_FAN", "FanDxe/LiteFanPower.inf"),
                       ("Bluetooth (UART-D)", "SWITCH_LITE_BLUETOOTH", "UsbPowerAuditDxe/LiteBluetooth.inf"),
                       ("audio (I2S1/ALC5639)", "SWITCH_LITE_AUDIO", "UsbPowerAuditDxe/LiteAudio.inf"),
                       ("XUSB prepare", "SWITCH_LITE_XUSB", "UsbPowerAuditDxe/LiteUsbPrepare.inf"),
                       ("CPU/EMC performance", "SWITCH_LITE_PERFORMANCE", "ClockManagementDxe/t214/LitePerformanceDxe.inf"),
                       ("reset", "SWITCH_LITE_RESET", "PmicDxe/t214/LiteResetDxe.inf"),
                       ("Wi-Fi prepare", "SWITCH_WIFI_RUNTIME", "WifiBtApp/WifiPrepare.inf"),
                       ("GPU hand-over", "SWITCH_LITE_GPU", "GpuHandoffApp/GpuHandoffApp.inf")],
    "OLED (HEG-001)": [("board", "SWITCH_BOARD_OLED", ""),
                       ("GPU hand-over (the only OLED-specific item in winx)", "SWITCH_OLED_GPU", "GpuHandoffApp/GpuHandoffApp.inf")],
    "Iowa (HAC-001-01)": [("board", "SWITCH_BOARD_MARIKO", "")],
}


def check_winx(path):
    dsc = os.path.join(path, "NintendoSwitch.dsc")
    if not os.path.isfile(dsc):
        sys.exit("not a winx checkout (no NintendoSwitch.dsc): " + path)
    text = open(dsc, errors="replace").read()
    bad = 0
    print("\nwinx check (%s): does the DSC build what each model's drivers rely on?" % path)
    for model, rows in MODEL_UEFI.items():
        print("\n== %s ==" % model)
        for what, flag, mod in rows:
            flag_ok = re.search(r"\b%s\b" % flag, text) is not None
            mod_ok = (not mod) or os.path.isfile(os.path.join(path, "Drivers", mod)) or mod in text
            ok = flag_ok and mod_ok
            bad += 0 if ok else 1
            print("  %-4s %-26s %s%s" % ("ok" if ok else "MISS", flag, what, "" if ok else "  (flag %s, module %s)" % ("found" if flag_ok else "missing", "found" if mod_ok else "missing")))
    print("\nThis only shows the flag and module exist in the DSC. It does not show a UEFI build was made with the flag, or that the hand-over works.")
    return bad


def main(argv):
    manifests = sorted(glob.glob(os.path.join(ROOT, "reference", "switchdrivers-manifest-*.json")) or glob.glob(os.path.join(ROOT, "switchdrivers-manifest-*.json")))
    if not manifests:
        sys.exit("no switchdrivers manifest found")
    items = json.load(open(manifests[-1], encoding="utf-8"))["drivers"]["items"]
    min_uefi = {i["service"]: int(i["minUefi"].lstrip("R")) for i in items if i.get("minUefi", "").startswith("R")}
    revs = revs_by_dir()
    behind, rows = [], []
    print("NOTE: the UEFI build differs per model (Erista / Lite / OLED / Iowa). The manifest is for t210 (Erista); Lite and OLED have their own UEFI.")
    for fam in FAMILY.values():
        print("\n== %s ==" % fam)
        print("%-18s %-9s %-8s %-18s %s" % ("driver", "manifest", "source", "finding", "UEFI must have done"))
        for svc, dname in sorted(SERVICE_DIR.items(), key=lambda kv: kv[1]):
            if (fam, dname) not in revs and not fam.startswith("t210"):
                continue
            newest = max(revs.get((fam, dname), {0}))
            declared = min_uefi.get(svc) if fam.startswith("t210") else None
            flag = ""
            if declared is not None and newest > declared:
                flag = "source newer: R%d>R%d" % (newest, declared)
                behind.append((svc, declared, newest))
            r = (dname, "R%d" % declared if declared else "-", "R%d" % newest if newest else "-", flag, HANDOVER.get(dname, ""))
            rows.append(r)
            print("%-18s %-9s %-8s %-18s %s" % r)
    print("\nMachine-checked hand-overs in this repository:")
    for k, v in CHECKED.items():
        print("  %-10s %s" % (k, v))
    unchecked = sorted({r[0] for r in rows if r[4] and r[0] not in CHECKED and not r[4].startswith("none")})
    print("\nNot machine-checked (only comments + the manifest): %s" % ", ".join(unchecked))
    if behind:
        print("\nThe manifest asks for an older UEFI than the source comments name (add a manifest row for the new driver version):")
        for svc, a, b in behind:
            print("  %s: minUefi R%d, but the source refers to R%d" % (svc, a, b))
    bad = check_winx(argv[argv.index("--winx") + 1]) if "--winx" in argv else 0
    return 1 if ((behind and "--strict" in argv) or bad) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
