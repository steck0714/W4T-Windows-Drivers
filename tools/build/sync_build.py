#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Fill build/<MODEL>/ from the finished GitHub Actions run - one command, no manual download or copying.

The workflow `build-all-drivers` already commits the driver sets to build/<model>/ after every push (git pull is enough then).
This script is for the other cases: a branch that has not committed yet, a fork without write access, or a run you want to
take without committing it. It does what the workflow's commit step does, on your clone:

  1. finds the newest successful run of build-all-drivers on a branch (gh CLI, `gh auth login` once), or takes --run-id;
  2. downloads its artifacts build-<model> (and apps-<model> with --apps);
  3. replaces build/<model>/{drivers,agent,fwfetch,test-signing} (and apps/) with them, refusing private keys;
  4. runs tools/check_no_proprietary.py.

  python tools/build/sync_build.py --branch test                  # all models, drivers only
  python tools/build/sync_build.py --models HAC-001,HDH-001 --apps
  python tools/build/sync_build.py --run-id 1234567890
  python tools/build/sync_build.py --artifacts-dir C:\\dl            # artifacts you downloaded yourself (folders build-<model> / apps-<model>)

Nothing here proves a driver works on a console: the sets are test-signed builds, exactly what the workflow produced.
"""
import argparse, json, os, shutil, subprocess, sys, tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FORBIDDEN_EXT = {".pfx", ".p12", ".pvk", ".key", ".pem", ".snk"}
OWNED = ("drivers", "agent", "fwfetch", "test-signing")   # what a driver-set artifact replaces (same list as the workflow)
WORKFLOW = "build-all-drivers.yml"


def gh(*args, capture=True):
    exe = os.environ.get("GH", "gh")
    try:
        p = subprocess.run([exe, *args], capture_output=capture, text=True)
    except FileNotFoundError:
        sys.exit("the GitHub CLI (gh) was not found. Install it from https://cli.github.com/ and run `gh auth login`, "
                 "or download the artifacts by hand and use --artifacts-dir.")
    if p.returncode != 0:
        sys.exit("gh %s failed:\n%s" % (" ".join(args), (p.stderr or p.stdout or "").strip()))
    return p.stdout if capture else ""


def refuse_keys(folder):
    for d, _, fs in os.walk(folder):
        for f in fs:
            if os.path.splitext(f)[1].lower() in FORBIDDEN_EXT:
                sys.exit("refusing: private key material in the artifacts: " + os.path.join(d, f))


def place(art_dir, out, models, with_apps):
    """Copy build-<model> / apps-<model> folders from art_dir into out/<model>/. Returns the list of what was placed."""
    placed = []
    for name in sorted(os.listdir(art_dir)):
        src = os.path.join(art_dir, name)
        if not os.path.isdir(src):
            continue
        kind, _, model = name.partition("-")
        if kind not in ("build", "apps") or model not in models or (kind == "apps" and not with_apps):
            continue
        refuse_keys(src)
        dest = os.path.join(out, model)
        os.makedirs(dest, exist_ok=True)
        if kind == "build":
            for sub in OWNED:
                shutil.rmtree(os.path.join(dest, sub), ignore_errors=True)
            target = dest
        else:
            target = os.path.join(dest, "apps")
            shutil.rmtree(target, ignore_errors=True)
            os.makedirs(target)
        shutil.copytree(src, target, dirs_exist_ok=True)
        placed.append("%s -> %s" % (name, os.path.relpath(target, ROOT) if target.startswith(ROOT) else target))
    return placed


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--branch", default="test", help="branch whose newest successful run is used (default: test)")
    ap.add_argument("--run-id", help="use this run instead of the newest successful one")
    ap.add_argument("--repo", help="OWNER/REPO (default: the repository of the current directory, as gh sees it)")
    ap.add_argument("--models", default="all", help="HAC-001,HDH-001,HEG-001 or all (default)")
    ap.add_argument("--apps", action="store_true", help="also place the apps (SwitchCenter / SwitchPatcher, large)")
    ap.add_argument("--artifacts-dir", help="folder with already downloaded artifacts (build-<model>, apps-<model>); skips gh")
    ap.add_argument("--out", default=os.path.join(ROOT, "build"))
    a = ap.parse_args(argv)

    all_models = json.load(open(os.path.join(ROOT, "build", "models.json"), encoding="utf-8"))["models"]
    models = set(all_models) if a.models.lower() == "all" else {m.strip() for m in a.models.split(",") if m.strip()}
    unknown = models - set(all_models)
    if unknown:
        sys.exit("unknown model(s): %s (have: %s)" % (", ".join(sorted(unknown)), ", ".join(all_models)))

    tmp = None
    try:
        if a.artifacts_dir:
            art = os.path.abspath(a.artifacts_dir)
            if not os.path.isdir(art):
                sys.exit("not a folder: " + art)
        else:
            repo = ["--repo", a.repo] if a.repo else []
            run_id = a.run_id
            if not run_id:
                rows = json.loads(gh("run", "list", *repo, "--workflow", WORKFLOW, "--branch", a.branch, "--status", "success",
                                     "--limit", "1", "--json", "databaseId,headSha,createdAt") or "[]")
                if not rows:
                    sys.exit("no successful %s run on branch %r. Run it from Actions first, or pass --run-id / --branch." % (WORKFLOW, a.branch))
                run_id = str(rows[0]["databaseId"])
                print("run %s (commit %s, %s)" % (run_id, rows[0]["headSha"][:8], rows[0]["createdAt"]))
            tmp = tempfile.mkdtemp(prefix="w4t-artifacts-")
            art = os.path.join(tmp, "dl")
            patterns = ["build-*"] + (["apps-*"] if a.apps else [])
            args = ["run", "download", run_id, *repo, "--dir", art]
            for pat in patterns:
                args += ["--pattern", pat]
            gh(*args, capture=False)
            if not os.path.isdir(art):
                sys.exit("run %s has no build-<model> artifact (the model jobs failed, or the artifacts expired)" % run_id)
        placed = place(art, os.path.abspath(a.out), models, a.apps)
    finally:
        if tmp:
            shutil.rmtree(tmp, ignore_errors=True)
    if not placed:
        sys.exit("nothing placed: no build-<model> artifact for %s in the run" % ", ".join(sorted(models)))
    for line in placed:
        print("placed", line)
    if os.path.abspath(a.out) == os.path.join(ROOT, "build"):
        rc = subprocess.run([sys.executable, "-I", os.path.join(ROOT, "tools", "check_no_proprietary.py")]).returncode
        if rc != 0:
            sys.exit("check_no_proprietary.py failed: remove what it lists before committing")


if __name__ == "__main__":
    main()
