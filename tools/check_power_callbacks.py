#!/usr/bin/env python3
"""Static check: which drivers handle system sleep/resume (D0Exit/D0Entry) and idle power.

Heuristic grep only. It does not prove a driver survives S3/S4 or Modern Standby; it lists
which KMDF drivers register the callbacks Microsoft's power-management docs expect
(WDF_PNPPOWER_EVENT_CALLBACKS.EvtDeviceD0Entry/D0Exit, WdfDeviceAssignS0IdleSettings, ...).
usage: tools/check_power_callbacks.py [--strict]   (exit 1 with --strict if any KMDF driver lacks D0Entry/D0Exit,
       or polls with a WDF timer without stopping it on Dx)
"""
import pathlib, re, sys
ROOT = pathlib.Path(__file__).resolve().parent.parent
CB = {"D0Entry": r"EvtDeviceD0Entry\s*=", "D0Exit": r"EvtDeviceD0Exit\s*=",
      "Prepare": r"EvtDevicePrepareHardware\s*=", "Release": r"EvtDeviceReleaseHardware\s*=",
      "S0Idle": r"WdfDeviceAssignS0IdleSettings", "SxWake": r"WdfDeviceAssignSxWakeSettings",
      "SMIOSusp": r"EvtDeviceSelfManagedIoSuspend\s*=", "SMIORest": r"EvtDeviceSelfManagedIoRestart\s*="}
rows, bad, timer_bad = [], 0, []
for soc in ("t210-icosa/drivers", "t214-hoag"):
    base = ROOT / "drivers" / soc
    if not base.is_dir(): continue
    for d in sorted(p for p in base.iterdir() if p.is_dir()):
        txt = "".join(f.read_text(errors="ignore") for ext in ("*.c", "*.cpp", "*.h")
                      for f in d.rglob(ext) if "tests" not in f.parts)
        kmdf = "WdfDriverCreate" in txt
        found = {k: bool(re.search(v, txt)) for k, v in CB.items()}
        if kmdf and not (found["D0Entry"] and found["D0Exit"]): bad += 1
        # A driver that re-arms a WDF timer which does I/O must stop it when the device leaves D0
        # (EvtDeviceSelfManagedIoSuspend, or D0Exit) -- KMDF does not stop driver timers for you.
        if kmdf and "WdfTimerStart" in txt and not (found["SMIOSusp"] or found["D0Exit"]):
            timer_bad.append(f"{soc.split('/')[0]}/{d.name}")
        rows.append((f"{soc.split('/')[0]}/{d.name}", "KMDF" if kmdf else "other", found))
print(f"{'driver':36} {'type':6} " + " ".join(f"{k:8}" for k in CB))
for name, t, f in rows:
    print(f"{name:36} {t:6} " + " ".join(f"{'yes' if f[k] else '-':8}" for k in CB))
print(f"\nKMDF drivers without D0Entry+D0Exit: {bad}")
print(f"KMDF drivers with a WDF poll timer but no stop on Dx (SelfManagedIoSuspend / D0Exit): {len(timer_bad)}")
for n in timer_bad: print("   ", n)
sys.exit(1 if ((bad or timer_bad) and "--strict" in sys.argv) else 0)
