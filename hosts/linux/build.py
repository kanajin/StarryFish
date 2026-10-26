#!/usr/bin/env python3
"""Linux host entry point. Never installs packages or launches the browser."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.parse
import urllib.request
import unittest

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[1]
CDN = "https://cef-builds.spotifycdn.com/"
INDEX = CDN + "index.json"
MAX_ARCHIVE = 3 * 1024**3
MAX_EXPANDED = 8 * 1024**3


def require(ok, message):
    if not ok:
        raise ValueError(message)


def digest(path, algorithm="sha256"):
    result = hashlib.new(algorithm)
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def atomic_json(path, value):
    path = Path(path)
    require(not path.is_symlink(), "Refusing a linked JSON destination")
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix=".write-", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as stream:
            json.dump(value, stream, indent=2, ensure_ascii=False)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(name, path)
    finally:
        if os.path.exists(name):
            os.unlink(name)


def official_url(url):
    parsed = urllib.parse.urlsplit(url)
    require(parsed.scheme == "https" and parsed.netloc == "cef-builds.spotifycdn.com"
            and not parsed.query and not parsed.fragment,
            "Only the official HTTPS CEF distribution host is allowed")
    return url


class OfficialRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        official_url(newurl)
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def download(url, destination, maximum):
    official_url(url)
    request = urllib.request.Request(url, headers={"User-Agent": "StarryFish-Linux-build/1"})
    opener = urllib.request.build_opener(OfficialRedirect())
    with opener.open(request, timeout=120) as response, Path(destination).open("xb") as output:
        official_url(response.url)
        total = 0
        while chunk := response.read(1024 * 1024):
            total += len(chunk)
            require(total <= maximum, "CEF download exceeds the byte limit")
            output.write(chunk)
    return total


def select_release(index, version):
    require(bool(re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+\+g[0-9a-f]+\+chromium-[0-9.]+", version)),
            "Specify the complete CEF version, not latest or a major version")
    matches = [v for v in index["linux64"]["versions"]
               if v["cef_version"] == version and v["channel"] == "stable"]
    require(len(matches) == 1, "Exact stable Linux64 version is absent or ambiguous")
    files = [f for f in matches[0]["files"] if f["type"] == "standard"]
    require(len(files) == 1, "Expected one standard distribution")
    file = files[0]
    name = file["name"]
    require(name == f"cef_binary_{version}_linux64.tar.bz2", "Unexpected CEF archive name")
    require(re.fullmatch(r"[0-9a-f]{40}", file["sha1"]) is not None, "Invalid upstream SHA-1")
    require(type(file["size"]) is int and 0 < file["size"] <= MAX_ARCHIVE, "Invalid archive size")
    return {"schema": 1, "platform": "linux64", "channel": "stable", "type": "standard",
            "version": version, "archive": name, "url": CDN + urllib.parse.quote(name),
            "size": file["size"], "upstream_sha1": file["sha1"],
            "root": name.removesuffix(".tar.bz2")}


def read_lock(path):
    require(not Path(path).is_symlink(), "Linked dependency lock")
    lock = json.loads(Path(path).read_text(encoding="utf-8"))
    require(lock.get("schema") == 1 and lock.get("cef") is not None,
            "CEF is not pinned yet. On CachyOS run lock --version <exact stable version>; review and commit cef.lock.json.")
    value = lock["cef"]
    require(value.get("platform") == "linux64" and value.get("channel") == "stable"
            and value.get("type") == "standard", "Invalid CEF platform/channel/type")
    require(re.fullmatch(r"[0-9a-f]{64}", value.get("sha256", "")) is not None, "Missing CEF SHA-256")
    reconstructed = select_release({"linux64": {"versions": [{
        "cef_version": value["version"], "channel": "stable", "files": [{
            "type": "standard", "name": value["archive"], "size": value["size"],
            "sha1": value["upstream_sha1"]}]}]}}, value["version"])
    require(all(value.get(k) == v for k, v in reconstructed.items()), "Inconsistent CEF lock")
    official_url(value["url"])
    return value


def check_archive(path, lock):
    require(not Path(path).is_symlink(), "Linked dependency archive")
    require(Path(path).stat().st_size == lock["size"], "CEF archive size mismatch")
    require(digest(path) == lock["sha256"], "CEF archive SHA-256 mismatch")


def acquire_archive(lock, deps):
    deps.mkdir(parents=True, exist_ok=True)
    destination = deps / lock["archive"]
    if destination.exists() or destination.is_symlink():
        check_archive(destination, lock)
        return destination
    with tempfile.TemporaryDirectory(prefix="download-", dir=deps) as temporary:
        candidate = Path(temporary) / "archive.tar.bz2"
        download(lock["url"], candidate, lock["size"])
        check_archive(candidate, lock)
        require(not destination.exists(), "Dependency appeared during download")
        os.rename(candidate, destination)
    return destination


def inventory(root, exclude=()):
    root = Path(root)
    require(root.is_dir() and not root.is_symlink(), "Missing or linked inventory root")
    result = {}
    for path in sorted(root.rglob("*")):
        name = path.relative_to(root).as_posix()
        require(not path.is_symlink(), "Linked file in inventory: " + name)
        if path.is_file() and name not in exclude:
            result[name] = digest(path)
        else:
            require(path.is_file() or path.is_dir(), "Non-regular inventory entry")
    return result


def unpack(archive, destination, expected_root):
    """Validate the whole tar before writing; never trust tar ownership or links."""
    require(not destination.exists(), "Never overwrite an extracted dependency")
    with tarfile.open(archive, "r:bz2") as tar:
        members = []
        total, seen = 0, set()
        for member in tar:
            require(len(members) < 100000, "Too many archive entries")
            members.append(member)
            name = member.name.rstrip("/")
            parts = name.split("/")
            require(name and not name.startswith("/") and "\\" not in name and ":" not in name
                    and all(p not in ("", ".", "..") for p in parts)
                    and parts[0] == expected_root, "Unsafe archive path")
            key = name.casefold()
            require(key not in seen, "Duplicate archive entry")
            seen.add(key)
            require(member.isdir() or member.isfile(), "Archive links/devices are forbidden")
            total += member.size
            require(0 <= member.size <= MAX_EXPANDED and total <= MAX_EXPANDED, "Expanded archive limit")
        destination.mkdir()
        for member in members:
            target = destination.joinpath(*PurePosixPath(member.name).parts)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            with tar.extractfile(member) as source, target.open("xb") as output:
                shutil.copyfileobj(source, output)
            # Strip setuid/setgid and all archive ownership. No sudo in builds.
            target.chmod(0o755 if member.mode & 0o111 else 0o644)


def dependency(lock, deps, fetch=False):
    destination = deps / lock["root"]
    receipt = destination / ".starryfish-cef.json"
    require(not destination.is_symlink() and not receipt.is_symlink(), "Linked CEF dependency or receipt")
    if not destination.exists():
        require(fetch, "Run fetch before build")
        archive = acquire_archive(lock, deps)
        with tempfile.TemporaryDirectory(prefix="extract-", dir=deps) as temporary:
            staging = Path(temporary) / "staging"
            unpack(archive, staging, lock["root"])
            extracted = staging / lock["root"]
            atomic_json(extracted / receipt.name, {"lock": lock, "files": inventory(extracted)})
            require(not destination.exists(), "Dependency appeared during extraction")
            os.rename(extracted, destination)
    recorded = json.loads(receipt.read_text(encoding="utf-8"))
    require(recorded["lock"] == lock, "Extracted CEF lock mismatch")
    require(recorded["files"] == inventory(destination, (receipt.name,)), "Extracted CEF files changed")
    return destination


def source_manifest(root=ROOT):
    result = {}
    for path in sorted(Path(root).rglob("*")):
        relative = path.relative_to(root)
        if any(p.startswith(".") or p == "__pycache__" for p in relative.parts):
            continue
        require(not path.is_symlink(), "Linked host source")
        if path.is_file() and path.suffix != ".pyc":
            result[relative.as_posix()] = digest(path)
    return result


def fingerprint(manifest):
    return hashlib.sha256(json.dumps(manifest, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def run(arguments, **kwargs):
    subprocess.run([str(a) for a in arguments], check=True, **kwargs)


def linux_only():
    require(sys.platform == "linux" and platform.machine().lower() in ("x86_64", "amd64"),
            "A real Linux x86_64 host is required; Windows checks are not a Linux build")
    require(os.geteuid() != 0, "Run the build as a normal user, not root")


def preflight():
    linux_only()
    for executable in ("cmake", "ninja", "c++", "pkg-config"):
        require(shutil.which(executable), "Missing build tool: " + executable)


def output_files(build_dir):
    files = inventory(build_dir / "out")
    require("StarryFish" in files and "libcef.so" in files and "icudtl.dat" in files,
            "Incomplete CEF application output")
    return files


def verify(build_dir, lock):
    linux_only()
    receipt = json.loads((build_dir / "build-receipt.json").read_text(encoding="utf-8"))
    require(receipt["schema"] == 1 and receipt["platform"] == "linux-x86_64", "Wrong build receipt")
    require(receipt["sources"] == source_manifest(), "Host sources changed; rebuild required")
    require(receipt["cef"] == lock, "CEF lock changed; rebuild required")
    dependency(lock, ROOT / ".deps")
    require(receipt["outputs"] == output_files(build_dir), "Build output mismatch")
    return receipt


def doctor():
    tools = {name: bool(shutil.which(name)) for name in
             ("cmake", "ninja", "c++", "pkg-config", "Xwayland", "unshare")}
    report = {"platform": sys.platform, "architecture": platform.machine(), "tools": tools,
              "session": os.environ.get("XDG_SESSION_TYPE", "unknown"),
              "desktop": os.environ.get("XDG_CURRENT_DESKTOP", "unknown"),
              "x11_display_available": bool(os.environ.get("DISPLAY")),
              "runtime_directory_set": bool(os.environ.get("XDG_RUNTIME_DIR")),
              "gpu_devices_present": Path("/dev/dri").is_dir() if sys.platform == "linux" else False,
              "sandbox_runtime": "pending: must be verified in the running CEF process",
              "scaling_and_input": "pending: KDE/XWayland visual verification required"}
    if sys.platform == "linux":
        report["root_user"] = os.geteuid() == 0
        report["packages"] = {}
        if tools["pkg-config"]:
            for package in ("gtk+-3.0", "x11", "xtst", "openssl", "libsecret-1"):
                completed = subprocess.run(["pkg-config", "--modversion", package], capture_output=True, text=True)
                report["packages"][package] = completed.stdout.strip() if completed.returncode == 0 else "missing"
    try:
        report["cef_lock"] = read_lock(ROOT / "cef.lock.json")["version"]
    except (ValueError, KeyError, OSError):
        report["cef_lock"] = "not configured"
    print(json.dumps(report, indent=2, ensure_ascii=False))
    return 0 if sys.platform == "linux" and platform.machine() == "x86_64" and all(
        tools[t] for t in ("cmake", "ninja", "c++", "pkg-config")) else 2


def pin(version):
    linux_only()
    path = ROOT / "cef.lock.json"
    require(json.loads(path.read_text(encoding="utf-8")).get("cef") is None,
            "CEF is already pinned. Dependency upgrades require a reviewed lock change")
    deps = ROOT / ".deps"
    deps.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="pin-", dir=deps) as temporary:
        metadata = Path(temporary) / "index.json"
        download(INDEX, metadata, 32 * 1024**2)
        candidate = select_release(json.loads(metadata.read_text(encoding="utf-8")), version)
        archive = Path(temporary) / candidate["archive"]
        download(candidate["url"], archive, candidate["size"])
        require(archive.stat().st_size == candidate["size"], "Upstream archive size mismatch")
        require(digest(archive, "sha1") == candidate["upstream_sha1"], "Upstream SHA-1 mismatch")
        candidate["sha256"] = digest(archive)
        destination = deps / candidate["archive"]
        if destination.exists():
            check_archive(destination, candidate)
        else:
            os.rename(archive, destination)
        atomic_json(path, {"schema": 1, "cef": candidate})
    print("Pinned exact official CEF archive; review and commit cef.lock.json before building.")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("doctor", "lock", "fetch", "build", "test", "verify"))
    parser.add_argument("--version", help="Exact stable CEF version, only with lock")
    parser.add_argument("--portable", action="store_true", help="Only Python build-tool checks (test only)")
    args = parser.parse_args(argv)
    require(not args.version or args.command == "lock", "--version is only valid with lock")
    require(not args.portable or args.command == "test", "--portable is only valid with test")
    if args.command == "doctor":
        return doctor()
    if args.command == "lock":
        require(args.version, "lock requires --version; latest is never selected implicitly")
        pin(args.version)
        return 0
    if args.command == "test":
        suite = unittest.defaultTestLoader.discover(str(ROOT / "checks"), pattern="test_*.py")
        require(unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful(), "Build-tool checks failed")
        if args.portable:
            print("PASS portable build-tool checks ONLY; Linux native/runtime acceptance remains pending")
            return 0
    preflight()
    lock = read_lock(ROOT / "cef.lock.json")
    deps, build_dir = ROOT / ".deps", ROOT / ".build"
    if args.command == "fetch":
        dependency(lock, deps, fetch=True)
        print("PASS pinned CEF archive and extracted file identities")
    elif args.command == "build":
        cef = dependency(lock, deps)
        before = source_manifest()
        run(["cmake", "-S", ROOT, "-B", build_dir / "native", "-G", "Ninja",
             "-DCMAKE_BUILD_TYPE=Release", "-DUSE_SANDBOX=ON", f"-DCEF_ROOT={cef}",
             f"-DSTARRYFISH_OUTPUT_DIR={build_dir / 'out'}",
             f"-DSTARRYFISH_INPUT_FINGERPRINT={fingerprint(before)}"])
        run(["cmake", "--build", build_dir / "native", "--parallel", str(min(os.cpu_count() or 2, 8))])
        require(before == source_manifest(), "Source changed during build; no valid receipt")
        dependency(lock, deps)
        atomic_json(build_dir / "build-receipt.json", {
            "schema": 1, "platform": "linux-x86_64", "sources": before,
            "fingerprint": fingerprint(before), "cef": lock,
            "outputs": output_files(build_dir), "runtime_acceptance": "pending"})
        print("PASS build identity recorded; browser runtime has not been tested")
    elif args.command == "verify":
        verify(build_dir, lock)
        print("PASS source/dependency/output identity; not runtime acceptance")
    elif args.command == "test":
        verify(build_dir, lock)
        run(["ctest", "--test-dir", build_dir / "native", "--output-on-failure"])
        print("PASS isolated Linux native checks; browser runtime has not been tested")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError, tarfile.TarError) as error:
        print("STOP:", error, file=sys.stderr)
        sys.exit(1)
