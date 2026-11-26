import os, sys, tempfile, subprocess
HERE = os.path.dirname(os.path.abspath(__file__)); TOOL = os.path.join(HERE, "..", "stamp_driver_ver.py")
sys.path.insert(0, os.path.join(HERE, ".."))
import stamp_driver_ver as s
assert s.stamp_text("DriverVer = 05/01/2022,1.0.0.19\r\nX=1", 5, "11/19/2026") == "DriverVer = 11/19/2026,1.0.0.24\r\nX=1"
assert "1.0.0.65535" in s.stamp_text("DriverVer=1/1/2020,1.0.0.65530", 99, "11/19/2026")
with tempfile.TemporaryDirectory() as t:
    for d in ("drivers/A", "drivers/upstream-src/B", "drivers/kit/C"):
        os.makedirs(os.path.join(t, d))
        open(os.path.join(t, d, "x.inf"), "w").write("[Version]\nDriverVer=05/01/2022,0.1.0.60\n")
    out = subprocess.run([sys.executable, TOOL, "--root", os.path.join(t, "drivers"), "--run", "7", "--date", "11/19/2026"], capture_output=True, text=True, check=True).stdout
    assert "1 INF" in out, out
    assert "0.1.0.67" in open(os.path.join(t, "drivers/A/x.inf")).read()
    assert "0.1.0.60" in open(os.path.join(t, "drivers/kit/C/x.inf")).read()
print("stamp_driver_ver tests passed")
