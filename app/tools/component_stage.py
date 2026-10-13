"""Development-only component candidate staging; no CLI, activation or CEF load.

Only trusted calling code supplies expectations and a ComponentArtifacts store.
Expectations compare bytes/declarations, not source identity or rights. Reopen
requires a complete independent inventory and a held cooperative writer lock.
There is no overwrite, selection, reload or cleanup interface.
"""
import ctypes
from ctypes import wintypes
from dataclasses import dataclass
import hashlib
import io
import json
import os
from pathlib import Path
import re
import secrets
import stat
import sys
import threading
import zipfile

sys.dont_write_bytecode = True
import component_package as package

PATH_LIMIT = 240  # UTF-16 units of each full destination path, including store.
NODE_LIMIT = 8192
RECEIPT_LIMIT = 1024 * 1024
STORE_CONTENT_LIMIT = 512 * 1024 * 1024
VERSION_LIMIT = 32
OWNER_FILE = 'owner.lock'
EMPTY_HASH = hashlib.sha256(b'').hexdigest()


class _ObservationStopped(ValueError):
    """Unsafe paths, exceeded I/O bounds or observed filesystem identity drift."""


def _require_stable(condition, message):
    if not condition:
        raise _ObservationStopped(message)


class _FileInformation(ctypes.Structure):
    _fields_ = [('attributes', wintypes.DWORD), ('creation', wintypes.FILETIME),
                ('access', wintypes.FILETIME), ('write', wintypes.FILETIME),
                ('volume', wintypes.DWORD), ('size_high', wintypes.DWORD),
                ('size_low', wintypes.DWORD), ('links', wintypes.DWORD),
                ('index_high', wintypes.DWORD), ('index_low', wintypes.DWORD)]


class _Owner:
    """A held Windows share-mode lock, not authority from a file's existence."""
    def __init__(self, root, create):
        self.handle = None
        self.path = root / OWNER_FILE
        _path_budget(self.path)
        self.api = ctypes.WinDLL('kernel32', use_last_error=True)
        self.api.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                        ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
        self.api.CreateFileW.restype = wintypes.HANDLE
        self.api.GetFileInformationByHandle.argtypes = [wintypes.HANDLE, ctypes.POINTER(_FileInformation)]
        self.api.GetFileInformationByHandle.restype = wintypes.BOOL
        self.api.GetFileType.argtypes, self.api.GetFileType.restype = [wintypes.HANDLE], wintypes.DWORD
        self.api.CloseHandle.argtypes, self.api.CloseHandle.restype = [wintypes.HANDLE], wintypes.BOOL
        # READ|WRITE, share READ only: other cooperative writers and deletion
        # fail until close. Never truncate, repair, follow a reparse or inherit.
        handle = self.api.CreateFileW(str(self.path), 0xC0000000, 1, None,
                                      1 if create else 3, 0x00200080, None)
        if handle == ctypes.c_void_p(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        self.handle = handle
        try:
            self.identity = self._information()
            self.state = _file_state(self.path.lstat())
            self.check()
        except BaseException:
            self.close()
            raise

    def _information(self):
        info = _FileInformation()
        if not self.api.GetFileInformationByHandle(self.handle, ctypes.byref(info)):
            raise ctypes.WinError(ctypes.get_last_error())
        package.require(self.api.GetFileType(self.handle) == 1 and not info.attributes & (0x10 | 0x400)
                        and info.links == 1 and info.size_high == info.size_low == 0,
                        'ordinary zero-byte single-link owner file required')
        return info.volume, info.index_high, info.index_low

    def check(self):
        package.require(self.handle is not None, 'closed owner handle')
        info = self.path.lstat()
        package.require(stat.S_ISREG(info.st_mode) and not stat.S_ISLNK(info.st_mode) and
                        not getattr(info, 'st_file_attributes', 0) & 0x400 and info.st_nlink == 1 and
                        info.st_size == 0 and _file_state(info) == self.state and
                        info.st_ino == (self.identity[1] << 32 | self.identity[2]) and
                        self._information() == self.identity, 'owner file identity changed')

    def close(self):
        if self.handle is not None:
            if not self.api.CloseHandle(self.handle):
                raise ctypes.WinError(ctypes.get_last_error())
            self.handle = None


@dataclass(frozen=True)
class PackageExpectation:
    component: str
    package_sha256: str
    declared_name: str
    declared_version: str


@dataclass(frozen=True)
class CandidateArtifact:
    # A frozen value object, not filesystem immutability. Returned paths remain
    # writable by their owner; consumers MUST revalidate before use or load.
    directory: Path
    manifest: Path
    package_sha256: str
    status: str = 'UNAPPROVED_CANDIDATE_ONLY'


def _directory_identity(path):
    info = path.lstat()
    _require_stable(stat.S_ISDIR(info.st_mode) and not stat.S_ISLNK(info.st_mode) and
                    not getattr(info, 'st_file_attributes', 0) & 0x400,
                    'regular non-reparse directory required')
    return info.st_dev, info.st_ino


def _path_budget(path):
    _require_stable(len(str(path).encode('utf-16-le')) // 2 <= PATH_LIMIT,
                    'full candidate destination path budget exceeded')


def _local_path(value):
    package.require(os.name == 'nt', 'component artifacts require Windows')
    text = os.fspath(value)
    package.require(type(text) is str and re.match(r'^[A-Za-z]:[\\/]', text) and
                    not text.endswith(('\\', '/')), 'standard absolute local store path required')
    parts = re.split(r'[\\/]', text[3:])
    for part in parts:
        package.check_part(part)
    path = Path(text)
    _path_budget(path)
    drive_type = ctypes.WinDLL('kernel32', use_last_error=True).GetDriveTypeW
    drive_type.argtypes, drive_type.restype = [ctypes.c_wchar_p], ctypes.c_uint
    package.require(drive_type(path.anchor) in (2, 3), 'network/non-local store drives rejected')
    return path


def _new_store_path(value):
    path = _local_path(value)
    package.require(path.name == 'ComponentArtifacts', 'new ComponentArtifacts store required')
    # No missing parents are created. Reuse requires the separate reopen API.
    identities = {item: _directory_identity(item) for item in (path.parent, *path.parent.parents)}
    try:
        path.lstat()
    except FileNotFoundError:
        return path, identities
    raise ValueError('existing store rejected by new-store constructor')


def _expectation(expected):
    package.require(type(expected) is PackageExpectation and
                    type(expected.component) is str and expected.component in package.COMPONENTS and
                    type(expected.package_sha256) is str and
                    re.fullmatch('[0-9a-f]{64}', expected.package_sha256) and
                    type(expected.declared_name) is str and type(expected.declared_version) is str,
                    'explicit package digest/name/version expectations required')


def _inventory(inventory):
    package.require(type(inventory) is dict and len(inventory) <= VERSION_LIMIT,
                    'bounded complete trusted candidate inventory required')
    wanted = dict(inventory)
    for name, expected in wanted.items():
        _expectation(expected)
        package.require(type(name) is str and
                        re.fullmatch(re.escape(expected.component) + '-[0-9a-f]{32}', name),
                        'inventory candidate name/component/token mismatch')
    return wanted


def _preflight(raw, expected):
    _expectation(expected)
    package.require(type(raw) is bytes and 22 <= len(raw) <= package.ARCHIVE_LIMIT,
                    'bounded immutable ZIP bytes required')
    package.require(hashlib.sha256(raw).hexdigest() == expected.package_sha256,
                    'expected package digest mismatch')
    report, files, directories = package._inspect_bytes(raw, expected.component)
    package.require(report['declared_name'] == expected.declared_name and
                    report['declared_version'] == expected.declared_version,
                    'expected manifest name/version mismatch')
    # This inventory comes from the same validated bytes/pass as the report,
    # never from a supplied receipt. Names and archive attributes are not applied.
    package.require(len(files) + len(directories) <= NODE_LIMIT, 'candidate node budget exceeded')
    receipt = json.dumps({'schema': 1, 'status': 'UNAPPROVED_CANDIDATE_ONLY',
                          'inspection': report, 'activation': 'NOT_PERFORMED'},
                         ensure_ascii=True, sort_keys=True, separators=(',', ':')).encode('ascii')
    package.require(len(receipt) <= RECEIPT_LIMIT, 'candidate receipt budget exceeded')
    return report, files, directories, receipt


def _write_chunks(path, chunks, expected_size, expected_hash):
    digest, size = hashlib.sha256(), 0
    with path.open('xb') as output:
        for chunk in chunks:
            size += len(chunk)
            package.require(size <= expected_size, 'actual write budget exceeded')
            package.require(output.write(chunk) == len(chunk), 'short candidate write')
            digest.update(chunk)
        package.require(size == expected_size and digest.hexdigest() == expected_hash,
                        'written candidate content mismatch')
        output.flush()
        os.fsync(output.fileno())


def _chunks(raw):
    for offset in range(0, len(raw), package.CHUNK):
        yield raw[offset:offset + package.CHUNK]


def _file_state(info):
    # Windows lstat guesses executable bits from a .exe/.bat suffix while
    # fstat does not. Compare the real file type and Win32 attributes instead;
    # READONLY/reparse, link count, identity, size and mtime still participate.
    return (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns,
            info.st_nlink, stat.S_IFMT(info.st_mode), getattr(info, 'st_file_attributes', 0))


def _read_regular(path, limit, expected_hash, expected_size=None, capture=False):
    before = path.lstat()
    _require_stable(stat.S_ISREG(before.st_mode) and not stat.S_ISLNK(before.st_mode) and
                    not getattr(before, 'st_file_attributes', 0) & 0x400 and before.st_nlink == 1,
                    'candidate link/special/reparse file rejected')
    _require_stable(0 <= before.st_size <= limit, 'candidate file size budget exceeded')
    package.require(expected_size is None or before.st_size == expected_size, 'candidate file size mismatch')
    with path.open('rb') as stream:
        opened = os.fstat(stream.fileno())
        _require_stable(_file_state(opened) == _file_state(before),
                        'candidate file identity/size changed')
        digest, size, chunks = hashlib.sha256(), 0, []
        while chunk := stream.read(min(package.CHUNK, before.st_size - size + 1)):
            size += len(chunk)
            _require_stable(size <= before.st_size, 'candidate readback budget exceeded')
            digest.update(chunk)
            if capture:
                chunks.append(chunk)
        after = os.fstat(stream.fileno())
    final = path.lstat()
    _require_stable(_file_state(before) == _file_state(after) == _file_state(final),
                    'candidate readback metadata changed')
    package.require(size == before.st_size and digest.hexdigest() == expected_hash,
                    'candidate readback content mismatch')
    return b''.join(chunks) if capture else None


def _verify_tree(candidate, files, directories, receipt, raw, check):
    expected_files = {'payload/' + name: identity for name, identity in files.items()}
    expected_files['package.zip'] = (len(raw), hashlib.sha256(raw).hexdigest())
    expected_files['receipt.json'] = (len(receipt), hashlib.sha256(receipt).hexdigest())
    expected_directories = {'payload', *('payload/' + name for name in directories)}
    children = {name: set() for name in ('', *expected_directories)}
    for name in (*expected_files, *expected_directories):
        parent, _, leaf = name.rpartition('/')
        children[parent].add(leaf)
        _path_budget(candidate / name)
    # Only enumerate expected directories, with a bounded count; no traversal
    # via a disk receipt, unknown name or link. Shared by publish and verify.
    for name, wanted in children.items():
        directory = candidate / name
        check(directory)
        actual = set()
        with os.scandir(directory) as entries:
            for entry in entries:
                package.require(len(actual) < len(wanted), 'unknown candidate tree entry')
                actual.add(entry.name)
        package.require(actual == wanted, 'candidate tree set mismatch')
    for name, (size, digest) in expected_files.items():
        target = candidate / name
        check(target.parent)
        _read_regular(target, size, digest, size)
    check(candidate)


def _content_size(raw, report, receipt):
    return len(raw) + report['uncompressed_bytes'] + len(receipt)


def _names(directory, limit):
    names = set()
    with os.scandir(directory) as entries:
        for entry in entries:
            _require_stable(len(names) < limit, 'unknown/excess component store entry')
            names.add(entry.name)
    return names


def _store_layout(root, legacy=False):
    names = _names(root, 3)
    old = {'Pending', 'Versions'}
    _require_stable(names == old | {OWNER_FILE} or legacy and names == old,
                    'component store root layout mismatch')
    owner_state = None
    if OWNER_FILE in names:
        owner = root / OWNER_FILE
        _path_budget(owner)
        owner_state = _file_state(owner.lstat())
        _read_regular(owner, 0, EMPTY_HASH, 0)
        _require_stable(_file_state(owner.lstat()) == owner_state, 'owner file identity changed')
    return names, owner_state


def _recheck_candidate(directory, expected):
    _expectation(expected)
    candidate = _local_path(directory)
    root, versions = candidate.parent.parent, candidate.parent
    package.require(root.name == 'ComponentArtifacts' and versions.name == 'Versions' and
                    re.fullmatch(re.escape(expected.component) + '-[0-9a-f]{32}', candidate.name),
                    'published candidate layout/component/token required; Pending is not consumable')
    identities = {}
    # Inspect parents first; never follow a junction while discovering children.
    for path in reversed((candidate, *candidate.parents)):
        identities[path] = _directory_identity(path)

    def check(directory):
        for path in (directory, *directory.parents):
            _require_stable(path in identities and _directory_identity(path) == identities[path],
                            'candidate directory identity changed')
        _require_stable(_directory_identity(root / 'Pending') == identities[root / 'Pending'],
                        'candidate store Pending directory changed')

    check_pending = root / 'Pending'
    identities[check_pending] = _directory_identity(check_pending)
    _require_stable(identities[root][0] == identities[versions][0] == identities[check_pending][0] ==
                    identities[candidate][0], 'candidate store layout must remain on one volume')
    check(root)
    layout = _store_layout(root, legacy=True)
    source = candidate / 'package.zip'
    _path_budget(source)
    source_identity = _file_state(source.lstat())
    raw = _read_regular(source, package.ARCHIVE_LIMIT, expected.package_sha256, capture=True)
    _require_stable(_file_state(source.lstat()) == source_identity, 'candidate source identity changed')
    report, files, directories, receipt = _preflight(raw, expected)
    payload_directories = {'payload', *('payload/' + name for name in directories)}
    for name in sorted(payload_directories, key=lambda value: (value.count('/'), value)):
        target = candidate / name
        _path_budget(target)
        check(target.parent)
        identities[target] = _directory_identity(target)
    _verify_tree(candidate, files, directories, receipt, raw, check)
    check(candidate)
    _require_stable(_store_layout(root, legacy=True) == layout, 'component store layout/owner changed')
    _require_stable(_file_state(source.lstat()) == source_identity, 'candidate source identity changed')
    return (CandidateArtifact(candidate, candidate / 'payload' / report['manifest_path'], report['package_sha256']),
            _content_size(raw, report, receipt))


def verify_candidate(directory, expected):
    """Read-only recheck with independent expectations, not installation approval.

    Returned paths can still change after this call. Consumers must check close
    to use; lstat checks do not provide handle-relative or same-SID isolation.
    Legacy stores without owner.lock can only use this read-only interface.
    """
    return _recheck_candidate(directory, expected)[0]


def diagnose_store(path, inventory):
    """Bounded read-only observations for future diagnostics, never a write permit.

    Unknown versions and Pending evidence are named but never traversed. Known
    candidates are checked only against a copied independent trusted inventory.
    No owner lock is acquired, no writer presence/atomic snapshot is inferred.
    Invalid caller input raises; disk failures yield controlled issue codes.
    """
    wanted = _inventory(inventory)
    root = _local_path(path)
    package.require(root.name == 'ComponentArtifacts', 'ComponentArtifacts store required')
    report = {'schema': 1, 'status': 'UNAPPROVED_CANDIDATE_ONLY', 'assessment': 'STOPPED',
              'activation': 'NOT_PERFORMED', 'writable_reopen': 'NOT_PERFORMED',
              'writer_presence': 'NOT_EVALUATED', 'consistency': 'POINT_IN_TIME_ONLY',
              'layout': 'NOT_EVALUATED', 'issues': [], 'candidates': [], 'pending_entries': [],
              'unknown_versions': [], 'missing_versions': [], 'observed_version_count': None,
              'verified_content_bytes': 0}

    def issue(code, candidate=None):
        item = {'code': code}
        if candidate is not None:
            item['candidate'] = candidate
        report['issues'].append(item)

    identities = {}

    def check():
        for directory, identity in identities.items():
            _require_stable(_directory_identity(directory) == identity,
                            'diagnostic directory identity changed')

    try:
        # Ancestors first: do not discover children through an observed reparse.
        for directory in reversed((root, *root.parents)):
            identities[directory] = _directory_identity(directory)
        check()
    except (ValueError, OSError):
        issue('STORE_UNAVAILABLE_OR_UNSAFE')
        return report
    try:
        root_names = _names(root, 3)
        ordinary = {'Pending', 'Versions'}
        if root_names not in (ordinary, ordinary | {OWNER_FILE}):
            report['layout'] = 'INCOMPLETE_OR_UNKNOWN'
            issue('ROOT_LAYOUT_MISMATCH')
            return report
        check()
        layout = _store_layout(root, legacy=True)
        report['layout'] = 'LOCKED_FORMAT' if OWNER_FILE in root_names else 'LEGACY_READ_ONLY'
        if report['layout'] == 'LEGACY_READ_ONLY':
            issue('LEGACY_READ_ONLY')
        for name in ('Pending', 'Versions'):
            child = root / name
            _path_budget(child)
            identities[child] = _directory_identity(child)
            package.require(identities[child][0] == identities[root][0], 'store must remain on one volume')
        check()
    except (ValueError, OSError):
        issue('ROOT_LAYOUT_UNAVAILABLE_OR_UNSAFE')
        return report
    try:
        pending = _names(root / 'Pending', VERSION_LIMIT)
        check()
        versions = _names(root / 'Versions', VERSION_LIMIT)
        check()
        report['pending_entries'] = sorted(pending)
        report['unknown_versions'] = sorted(versions - wanted.keys())
        report['missing_versions'] = sorted(wanted.keys() - versions)
        report['observed_version_count'] = len(versions)
        if pending:
            issue('PENDING_EVIDENCE')
        if report['unknown_versions']:
            issue('UNKNOWN_VERSIONS')
        if report['missing_versions']:
            issue('EXPECTED_CANDIDATES_MISSING')
    except (ValueError, OSError):
        issue('STORE_ENUMERATION_FAILED_OR_EXCEEDED')
        return report

    def check_snapshot():
        check()
        _require_stable(_store_layout(root, legacy=True) == layout and
                        _names(root / 'Pending', VERSION_LIMIT) == pending and
                        _names(root / 'Versions', VERSION_LIMIT) == versions,
                        'diagnostic layout/owner/inventory changed')
        check()

    for name in sorted(versions & wanted.keys()):
        try:
            check_snapshot()
            candidate = root / 'Versions' / name
            _path_budget(candidate)
            identities[candidate] = _directory_identity(candidate)
            _require_stable(identities[candidate][0] == identities[root][0],
                            'candidate must remain on one volume')
        except (ValueError, OSError):
            issue('CANDIDATE_PATH_UNAVAILABLE_OR_UNSAFE', name)
            return report
    for name in sorted(versions & wanted.keys()):
        # Do not turn errors into traversal of untrusted alternatives or repair.
        try:
            check_snapshot()
        except (ValueError, OSError):
            issue('STORE_CHANGED_OR_UNAVAILABLE')
            return report
        candidate = root / 'Versions' / name
        try:
            artifact, content_bytes = _recheck_candidate(candidate, wanted[name])
            _require_stable(artifact.directory == candidate and _directory_identity(candidate) == identities[candidate],
                            'candidate identity changed during diagnosis')
            report['verified_content_bytes'] += content_bytes
            report['candidates'].append({'name': name, 'state': 'OBSERVED_MATCH'})
        except _ObservationStopped:
            issue('CANDIDATE_UNSAFE_OR_CHANGED', name)
            return report
        except ValueError:
            issue('CANDIDATE_VERIFICATION_FAILED', name)
            report['candidates'].append({'name': name, 'state': 'VERIFY_FAILED'})
        except OSError:
            issue('CANDIDATE_READ_FAILED', name)
            report['candidates'].append({'name': name, 'state': 'READ_FAILED'})
            return report
        try:
            check_snapshot()
        except (ValueError, OSError):
            issue('STORE_CHANGED_OR_UNAVAILABLE')
            return report
        if report['verified_content_bytes'] > STORE_CONTENT_LIMIT:
            issue('VERIFIED_CONTENT_BUDGET_EXCEEDED')
            return report
    try:
        check_snapshot()
    except (ValueError, OSError):
        issue('STORE_CHANGED_OR_UNAVAILABLE')
        return report
    report['assessment'] = 'ISSUES_OBSERVED' if report['issues'] else 'NO_ISSUES_OBSERVED'
    return report


class CandidateStore:
    """Cooperative development store; creation is deferred until preflight.

    Publication only creates NEW unselected candidates. A failure after
    writing starts poisons this instance, preserving evidence without a retry or
    deletion API. Python privacy is not an adversarial capability boundary.
    """
    def __init__(self, path):
        root, parents = _new_store_path(path)
        self._initialize(root, parents)

    def _initialize(self, root, parents):
        self._root, self._parents = root, parents
        self._owned = {}
        self._created, self._poisoned = False, False
        self._closed, self._owner = False, None
        self._versions = {}
        self._content_bytes, self._reservations = 0, 0
        self._lock = threading.Lock()

    @classmethod
    def reopen(cls, path, inventory):
        """Recheck ALL candidates under a held writer lock; never adopt disk receipts.

        inventory is a complete trusted dict of candidate basename -> independent
        PackageExpectation. Pending evidence or unknown versions stops reuse.
        This does not select, execute, repair, delete or approve any component.
        """
        wanted = _inventory(inventory)
        root = _local_path(path)
        package.require(root.name == 'ComponentArtifacts', 'ComponentArtifacts store required')
        parents = {}
        for parent in reversed((root.parent, *root.parent.parents)):
            parents[parent] = _directory_identity(parent)
        owned = {root: _directory_identity(root)}
        store = cls.__new__(cls)
        store._initialize(root, parents)
        try:
            store._owner = _Owner(root, create=False)
            store._owned = owned
            store._check(root)
            _store_layout(root)
            for name in ('Pending', 'Versions'):
                child = root / name
                _path_budget(child)
                store._owned[child] = _directory_identity(child)
                package.require(store._owned[child][0] == owned[root][0], 'store must remain on one volume')
            store._created = True
            store._check(root / 'Pending')
            package.require(not _names(root / 'Pending', 1), 'Pending evidence prevents writable reopen')
            store._check(root / 'Versions')
            package.require(_names(root / 'Versions', VERSION_LIMIT) == set(wanted),
                            'complete candidate inventory mismatch')
            for name, expected in wanted.items():
                candidate = root / 'Versions' / name
                store._check(candidate.parent)
                identity = _directory_identity(candidate)
                artifact, content_bytes = _recheck_candidate(candidate, expected)
                package.require(artifact.directory == candidate and _directory_identity(candidate) == identity,
                                'candidate identity changed during reopen')
                store._content_bytes += content_bytes
                package.require(store._content_bytes <= STORE_CONTENT_LIMIT,
                                'development store content/version budget exceeded')
                store._versions[name] = identity
            store._reservations = len(wanted)
            store._check_store()
            return store
        except BaseException:
            store.close()
            raise

    def close(self):
        package.require(self._lock.acquire(blocking=False), 'candidate store already busy')
        try:
            if self._owner is not None:
                self._owner.close()
            self._closed = True
        finally:
            self._lock.release()

    def __enter__(self):
        package.require(not self._closed, 'closed candidate store')
        return self

    def __exit__(self, *_):
        self.close()

    def __del__(self):
        # Explicit close/context manager is preferred. Never remove evidence.
        owner = getattr(self, '_owner', None)
        if owner is not None:
            owner.close()

    def _check_store(self):
        self._check(self._root / 'Pending')
        self._check(self._root / 'Versions')
        _store_layout(self._root)
        package.require(not _names(self._root / 'Pending', 1), 'Pending evidence prevents publication')
        package.require(_names(self._root / 'Versions', VERSION_LIMIT) == set(self._versions),
                        'complete candidate inventory mismatch')
        for name, identity in self._versions.items():
            package.require(_directory_identity(self._root / 'Versions' / name) == identity,
                            'published candidate directory identity changed')

    def _check(self, directory):
        if self._owner is not None:
            self._owner.check()
        for path, identity in self._parents.items():
            package.require(_directory_identity(path) == identity, 'store ancestor changed')
        for path in (directory, *directory.parents):
            if path in self._parents:
                break
            package.require(path in self._owned and _directory_identity(path) == self._owned[path],
                            'owned directory identity changed')

    def _mkdir(self, path):
        if path != self._root:
            self._check(path.parent)
        else:
            for parent, identity in self._parents.items():
                package.require(_directory_identity(parent) == identity, 'store ancestor changed')
        path.mkdir()  # exclusive; never exist_ok and never mkdir(parents=True).
        self._owned[path] = _directory_identity(path)

    def publish(self, raw, expected):
        package.require(self._lock.acquire(blocking=False), 'candidate store already busy')
        mutated = False
        try:
            package.require(not self._closed, 'closed candidate store')
            package.require(not self._poisoned, 'failed store cannot be reused; preserve its evidence')
            report, files, directories, receipt = _preflight(raw, expected)
            if self._created:
                self._check_store()
            content_bytes = _content_size(raw, report, receipt)
            package.require(self._reservations < VERSION_LIMIT and
                            self._content_bytes + content_bytes <= STORE_CONTENT_LIMIT,
                            'development store content/version budget exceeded')
            identity = expected.component + '-' + secrets.token_hex(16)
            stage, final = self._root / 'Pending' / identity, self._root / 'Versions' / identity
            # Check both names before any creation: publication may have a longer
            # base than Pending. Unsupported full paths stop, never get shortened.
            for base in (stage, final):
                for name in ('package.zip', 'receipt.json', 'payload',
                             *('payload/' + name for name in (*files, *directories))):
                    _path_budget(base / name)
            self._content_bytes += content_bytes
            self._reservations += 1
            mutated = True
            if not self._created:
                self._mkdir(self._root)
                self._owner = _Owner(self._root, create=True)
                self._mkdir(self._root / 'Pending')
                self._mkdir(self._root / 'Versions')
                self._created = True
                self._check_store()
            self._mkdir(stage)
            self._mkdir(stage / 'payload')
            for name in sorted(directories, key=lambda value: (value.count('/'), value)):
                self._mkdir(stage / 'payload' / name)
            for name, content in (('package.zip', raw), ('receipt.json', receipt)):
                self._check(stage)
                _write_chunks(stage / name, _chunks(content), len(content), hashlib.sha256(content).hexdigest())
            with zipfile.ZipFile(io.BytesIO(raw)) as archive:
                for name, (size, digest) in files.items():
                    target = stage / 'payload' / name
                    self._check(target.parent)
                    with archive.open(name) as source:
                        chunks = iter(lambda: source.read(package.CHUNK), b'')
                        _write_chunks(target, chunks, size, digest)
            # All writable handles are closed. Never trust receipt.json instead
            # of the in-memory expectations, exact tree and bytes readback.
            _verify_tree(stage, files, directories, receipt, raw, self._check)
            self._check(stage)
            self._check(final.parent)
            package.require(self._owned[stage][0] == self._owned[final.parent][0],
                            'candidate publication must remain on one volume')
            # Windows rename fails when final already exists. No os.replace or
            # fallback copy/delete. This does not imply power-loss durability;
            # interruption after rename can leave a published but unselected
            # candidate even when the caller never received the return value.
            os.rename(stage, final)
            self._versions[identity] = _directory_identity(final)
            return CandidateArtifact(final, final / 'payload' / report['manifest_path'],
                                     report['package_sha256'])
        except BaseException:
            if mutated:
                self._poisoned = True
            raise
        finally:
            self._lock.release()
