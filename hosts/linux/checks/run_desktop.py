#!/usr/bin/env python3
"""Launch one verified build with newly allocated synthetic XDG directories.

This is an explicit interactive check, not part of build.py test. Close the
window normally. Evidence and synthetic profiles are retained under .build.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("linux_build", ROOT / "build.py")
build = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(build)


def process(pid):
    """Read only processes descended from the browser created by this check."""
    root = Path("/proc") / str(pid)
    try:
        fields = dict(line.split(":", 1) for line in (root / "status").read_text().splitlines())
        if int(fields["Uid"].split()[0]) != os.getuid():
            return None
        # Chromium may rewrite argv into a single process-title string.
        command = (root / "cmdline").read_bytes().replace(b"\0", b" ").decode(errors="replace")
        role = re.search(r"(?:^| )--type=([^ ]+)", command)
        start = (root / "stat").read_text().rsplit(")", 1)[1].split()[19]
        return {"pid": pid, "start_ticks": start,
                "role": role.group(1) if role else (root / "comm").read_text().strip(),
                "no_new_privs": int(fields.get("NoNewPrivs", "0")),
                "seccomp": int(fields.get("Seccomp", "0")),
                "user_namespace": os.readlink(root / "ns/user"),
                "extension_process": "--extension-process" in command}
    except (OSError, ValueError, KeyError, IndexError):
        return None


def descendants(pid):
    result, pending, seen = [], [pid], set()
    while pending and len(seen) < 256:
        current = pending.pop()
        if current in seen:
            continue
        seen.add(current)
        info = process(current)
        if not info:
            continue
        result.append(info)
        try:
            children = Path(f"/proc/{current}/task/{current}/children").read_text()
            pending.extend(int(child) for child in children.split())
        except OSError:
            pass
    return result


def still_running(records):
    return [old["pid"] for old in records
            if (current := process(old["pid"])) and current["start_ticks"] == old["start_ticks"]]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--page", choices=("blank", "gpu", "sandbox", "input"), default="blank")
    parser.add_argument("--check-lock", action="store_true",
                        help="Try a second blank instance using this check's synthetic directories")
    args = parser.parse_args()
    build.linux_only()
    build.require(bool(os.environ.get("DISPLAY")), "Run from the KDE/XWayland desktop session")
    runtime = Path(os.environ.get("XDG_RUNTIME_DIR", ""))
    build.require(runtime.is_absolute() and runtime.is_dir() and not runtime.is_symlink(),
                  "An absolute desktop XDG_RUNTIME_DIR is required")
    info = runtime.stat()
    build.require(info.st_uid == os.getuid() and stat.S_IMODE(info.st_mode) & 0o077 == 0,
                  "The desktop runtime directory must be private and owned by this user")
    receipt = build.verify(ROOT / ".build", build.read_lock(ROOT / "cef.lock.json"))
    root = Path(tempfile.mkdtemp(prefix="smoke-", dir=ROOT / ".build"))
    isolated_runtime = Path(tempfile.mkdtemp(prefix="starryfish-check.", dir=runtime))
    environment = os.environ.copy()
    for key, name in (("XDG_CONFIG_HOME", "config"), ("XDG_DATA_HOME", "data"),
                      ("XDG_CACHE_HOME", "cache")):
        environment[key] = str(root / name)
    environment["XDG_RUNTIME_DIR"] = str(isolated_runtime)
    executable = ROOT / ".build/out/StarryFish"
    argument = "--smoke-test" if args.page == "blank" else "--diagnostic-page=" + args.page
    build.atomic_json(root / "build-receipt.json", receipt)
    with (root / "browser.log").open("xb") as log:
        browser = subprocess.Popen([str(executable), argument], env=environment,
                                   stdout=log, stderr=log, start_new_session=True)
        launch = {"schema": 1, "pid": browser.pid, "root": str(root), "page": args.page,
                  "runtime": str(isolated_runtime), "exe": str(executable),
                  "xdg": {key: environment[key] for key in
                          ("XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_RUNTIME_DIR")},
                  "process": process(browser.pid), "fingerprint": receipt["fingerprint"]}
        build.atomic_json(root / "launch.json", launch)
        print("Evidence:", root, flush=True)
        print("Browser PID:", browser.pid, "— close its window normally when finished.", flush=True)
        deadline = time.monotonic() + 5
        records = {}
        while browser.poll() is None and time.monotonic() < deadline:
            for item in descendants(browser.pid):
                records[item["pid"]] = item
            time.sleep(0.2)
        build.atomic_json(root / "processes.json", list(records.values()))
        lock_ok = None
        if args.check_lock and browser.poll() is None:
            with (root / "second-instance.log").open("xb") as second_log:
                second = subprocess.Popen([str(executable), "--smoke-test"], env=environment,
                                          stdout=second_log, stderr=second_log, start_new_session=True)
                try:
                    code = second.wait(timeout=15)
                    lock_ok = code == 3 and b"already in use" in (root / "second-instance.log").read_bytes()
                    build.atomic_json(root / "lock-result.json", {"exit_code": code, "passed": lock_ok})
                    print("Second-instance profile lock:", "PASS" if lock_ok else "FAIL", flush=True)
                except subprocess.TimeoutExpired:
                    build.atomic_json(root / "lock-result.json", {"passed": False, "still_running_pid": second.pid})
                    print("Second instance did not exit; retained for inspection, PID", second.pid, flush=True)
                    lock_ok = False
        while browser.poll() is None:
            for item in descendants(browser.pid):
                records[item["pid"]] = item
            time.sleep(0.5)
        code = browser.wait()
        build.atomic_json(root / "processes.json", list(records.values()))
    deadline = time.monotonic() + 5
    remaining = still_running(records.values())
    while remaining and time.monotonic() < deadline:
        time.sleep(0.1)
        remaining = still_running(records.values())
    result = {"exit_code": code, "observed_processes_remaining": remaining,
              "lock_check": lock_ok, "gpu_input_scaling_user_acceptance": "pending"}
    build.atomic_json(root / "exit.json", result)
    print(json.dumps(result, indent=2))
    return 0 if code == 0 and not remaining and lock_ok is not False else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("Check interrupted. Browser and evidence retained; close the test window normally.")
        raise SystemExit(130)
    except (OSError, ValueError) as error:
        print("STOP:", error)
        raise SystemExit(1)
