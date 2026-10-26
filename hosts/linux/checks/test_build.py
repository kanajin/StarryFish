"""Offline fixtures only; no user data, browser launch or dependency downloads."""
import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location("linux_build", Path(__file__).parents[1] / "build.py")
build = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(build)

VERSION = "144.0.1+g123abcd+chromium-144.0.7559.0"
NAME = f"cef_binary_{VERSION}_linux64.tar.bz2"


def index(channel="stable"):
    return {"linux64": {"versions": [{"cef_version": VERSION, "channel": channel, "files": [{
        "type": "standard", "name": NAME, "size": 10, "sha1": "a" * 40}]}]}}


class BuildChecks(unittest.TestCase):
    def setUp(self):
        fixtures = Path(__file__).parents[1] / ".build" / "checks"
        fixtures.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="fixture-", dir=fixtures)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def lock(self):
        value = build.select_release(index(), VERSION)
        value["sha256"] = "b" * 64
        return value

    def write_lock(self, value):
        path = self.root / "lock.json"
        build.atomic_json(path, {"schema": 1, "cef": value})
        return path

    def tar(self, entries):
        archive = self.root / "fixture.tar.bz2"
        with tarfile.open(archive, "w:bz2") as tar:
            for name, kind in entries:
                info = tarfile.TarInfo(name)
                info.type = kind
                info.mode = 0o6755
                info.linkname = "outside"
                info.size = 3 if kind == tarfile.REGTYPE else 0
                tar.addfile(info, io.BytesIO(b"abc") if info.size else None)
        return archive

    def test_exact_stable_selection(self):
        self.assertEqual(build.select_release(index(), VERSION)["archive"], NAME)
        for version in ("latest", "144", VERSION + "/../escape"):
            with self.assertRaises(ValueError):
                build.select_release(index(), version)

    def test_beta_is_not_stable(self):
        with self.assertRaises(ValueError):
            build.select_release(index("beta"), VERSION)

    def test_ambiguous_standard_is_rejected(self):
        data = index()
        data["linux64"]["versions"][0]["files"] *= 2
        with self.assertRaises(ValueError):
            build.select_release(data, VERSION)

    def test_lock_requires_sha256(self):
        self.assertEqual(build.read_lock(self.write_lock(self.lock())), self.lock())
        for value in (None, {**self.lock(), "sha256": ""}, {**self.lock(), "root": "../escape"},
                      {**self.lock(), "url": "https://example.org/archive"}):
            with self.subTest(value=value), self.assertRaises(ValueError):
                build.read_lock(self.write_lock(value))

    def test_download_host_and_redirect_policy(self):
        for url in ("http://cef-builds.spotifycdn.com/file", "https://evil.test/file",
                    "https://cef-builds.spotifycdn.com@evil.test/file",
                    "https://cef-builds.spotifycdn.com/file?redirect=evil"):
            with self.subTest(url=url), self.assertRaises(ValueError):
                build.official_url(url)
        with self.assertRaises(ValueError):
            build.OfficialRedirect().redirect_request(None, None, 302, "", {}, "https://evil.test/file")

    def test_archive_hash_and_size(self):
        path = self.root / "archive"
        path.write_bytes(b"safe")
        value = {"size": 4, "sha256": hashlib.sha256(b"safe").hexdigest()}
        build.check_archive(path, value)
        path.write_bytes(b"evil")
        with self.assertRaises(ValueError):
            build.check_archive(path, value)
        path.write_bytes(b"longer")
        with self.assertRaises(ValueError):
            build.check_archive(path, value)

    def test_cached_archive_is_not_silently_replaced(self):
        value = self.lock()
        path = self.root / value["archive"]
        path.write_bytes(b"wrong")
        with mock.patch.object(build, "download") as download, self.assertRaises(ValueError):
            build.acquire_archive(value, self.root)
        download.assert_not_called()
        self.assertEqual(path.read_bytes(), b"wrong")

    def test_extract_regular_files_without_setuid(self):
        archive = self.tar([("cef", tarfile.DIRTYPE), ("cef/bin", tarfile.REGTYPE)])
        target = self.root / "out"
        build.unpack(archive, target, "cef")
        self.assertEqual((target / "cef/bin").read_bytes(), b"abc")
        self.assertEqual((target / "cef/bin").stat().st_mode & 0o6000, 0)

    def test_unsafe_archive_paths_are_rejected_before_writes(self):
        for name in ("../escape", "/cef/file", "cef/../escape", "cef/./file", "cef//file",
                     "cef\\file", "cef/C:drive", "other/file"):
            with self.subTest(name=name):
                archive = self.tar([("cef/valid", tarfile.REGTYPE), (name, tarfile.REGTYPE)])
                target = self.root / "out"
                with self.assertRaises(ValueError):
                    build.unpack(archive, target, "cef")
                self.assertFalse(target.exists())

    def test_links_devices_and_duplicate_names_rejected(self):
        for kind in (tarfile.SYMTYPE, tarfile.LNKTYPE, tarfile.CHRTYPE, tarfile.FIFOTYPE):
            archive = self.tar([("cef/file", kind)])
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                build.unpack(archive, self.root / "out", "cef")
        archive = self.tar([("cef/File", tarfile.REGTYPE), ("cef/file", tarfile.REGTYPE)])
        with self.assertRaises(ValueError):
            build.unpack(archive, self.root / "out", "cef")

    def test_existing_extraction_preserved(self):
        target = self.root / "out"
        target.mkdir()
        (target / "evidence").write_text("keep", encoding="utf-8")
        archive = self.tar([("cef/bin", tarfile.REGTYPE)])
        with self.assertRaises(ValueError):
            build.unpack(archive, target, "cef")
        self.assertEqual((target / "evidence").read_text(), "keep")

    def test_expansion_budget(self):
        archive = self.tar([("cef/bin", tarfile.REGTYPE)])
        with mock.patch.object(build, "MAX_EXPANDED", 2), self.assertRaises(ValueError):
            build.unpack(archive, self.root / "out", "cef")

    def test_inventory_detects_added_modified_and_removed_files(self):
        (self.root / "one").write_bytes(b"1")
        before = build.inventory(self.root)
        (self.root / "one").write_bytes(b"2")
        self.assertNotEqual(before, build.inventory(self.root))
        (self.root / "one").write_bytes(b"1")
        (self.root / "extra").write_bytes(b"3")
        self.assertNotEqual(before, build.inventory(self.root))
        (self.root / "one").unlink()
        self.assertNotEqual(before, build.inventory(self.root))

    def test_dependency_tree_mutation_rejected(self):
        value = self.lock()
        extracted = self.root / value["root"]
        extracted.mkdir()
        (extracted / "libcef.so").write_bytes(b"fixture")
        build.atomic_json(extracted / ".starryfish-cef.json", {
            "lock": value, "files": build.inventory(extracted)})
        self.assertEqual(build.dependency(value, self.root), extracted)
        (extracted / "libcef.so").write_bytes(b"changed")
        with self.assertRaises(ValueError):
            build.dependency(value, self.root)

    def test_missing_dependency_requires_explicit_fetch(self):
        with mock.patch.object(build, "download") as download, self.assertRaises(ValueError):
            build.dependency(self.lock(), self.root)
        download.assert_not_called()

    def test_source_fingerprint_ignores_only_build_residue(self):
        (self.root / "src").mkdir()
        source = self.root / "src/main.cpp"
        source.write_text("one", encoding="utf-8")
        (self.root / ".build").mkdir()
        (self.root / ".build/receipt.json").write_text("ignored", encoding="utf-8")
        before = build.source_manifest(self.root)
        self.assertEqual(set(before), {"src/main.cpp"})
        source.write_text("two", encoding="utf-8")
        self.assertNotEqual(build.fingerprint(before), build.fingerprint(build.source_manifest(self.root)))

    def test_receipt_rejects_stale_source_and_outputs(self):
        build_dir = self.root / ".build"
        build_dir.mkdir()
        value = self.lock()
        receipt = {"schema": 1, "platform": "linux-x86_64", "sources": {"a": "one"},
                   "cef": value, "outputs": {"StarryFish": "one"}}
        build.atomic_json(build_dir / "build-receipt.json", receipt)
        with mock.patch.object(build, "linux_only"), mock.patch.object(build, "dependency"), \
             mock.patch.object(build, "source_manifest", return_value={"a": "one"}), \
             mock.patch.object(build, "output_files", return_value={"StarryFish": "one"}):
            self.assertEqual(build.verify(build_dir, value), receipt)
            changed = copy.deepcopy(receipt)
            changed["sources"]["a"] = "old"
            build.atomic_json(build_dir / "build-receipt.json", changed)
            with self.assertRaises(ValueError):
                build.verify(build_dir, value)
            changed = copy.deepcopy(receipt)
            changed["outputs"]["StarryFish"] = "old"
            build.atomic_json(build_dir / "build-receipt.json", changed)
            with self.assertRaises(ValueError):
                build.verify(build_dir, value)

    def test_atomic_json_replaces_complete_document(self):
        path = self.root / "state.json"
        build.atomic_json(path, {"value": "first"})
        build.atomic_json(path, {"value": "second"})
        self.assertEqual(json.loads(path.read_text()), {"value": "second"})
        self.assertEqual(sorted(p.name for p in self.root.iterdir()), ["state.json"])

    def test_platform_guard_prevents_native_build_on_windows(self):
        with mock.patch.object(build.sys, "platform", "win32"), self.assertRaises(ValueError):
            build.linux_only()


if __name__ == "__main__":
    unittest.main()
