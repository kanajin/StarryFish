"""Read-only structural inspection of an explicitly selected local extension ZIP.

This does NOT authenticate its source, approve redistribution, extract/install
code, or prove CEF compatibility. No real release package is adopted here.
"""
import argparse
import ctypes
import hashlib
import io
import json
import os
from pathlib import Path
import re
import stat
import struct
import sys
import zipfile
import zlib

sys.dont_write_bytecode = True
from bookmark_config import strict_json

ARCHIVE_LIMIT = 64 * 1024 * 1024
TOTAL_LIMIT = 128 * 1024 * 1024
FILE_LIMIT = 32 * 1024 * 1024
MANIFEST_LIMIT = 256 * 1024
ENTRY_LIMIT = 4096
RATIO_LIMIT = 200
CHUNK = 64 * 1024
COMPONENTS = ('tarou', 'arcarum-dungeon', 'zeroomega')
# ZeroOmega's fixed MV3 ZIP may now be prepared as an unselected candidate.
# Tampermonkey remains inspection-only: this does not relax CRX proof checks
# or turn a structurally checked package into an installed/approved extension.
INSPECTION_COMPONENTS = COMPONENTS + ('tampermonkey',)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def local_source(path, suffix):
    path = Path(path)
    require(path.is_absolute() and len(path.drive) == 2 and path.drive[1] == ':' and
            path.drive[0].isascii() and path.drive[0].isalpha() and
            len(path.name) > len(suffix) and path.name.lower().endswith(suffix),
            f'select a standard absolute local {suffix} file')
    require(os.name == 'nt', 'local source path inspection requires Windows')
    drive_type = ctypes.WinDLL('kernel32', use_last_error=True).GetDriveTypeW
    drive_type.argtypes, drive_type.restype = [ctypes.c_wchar_p], ctypes.c_uint
    require(drive_type(path.anchor) in (2, 3), 'network/non-local source drives rejected')
    for part in path.parts[1:]:
        check_part(part)
    for item in (path, *path.parents):
        info = item.lstat()
        require(not stat.S_ISLNK(info.st_mode) and not getattr(info, 'st_file_attributes', 0) & 0x400,
                'link/reparse source path rejected')
    require(path.is_file(), 'regular source file required')
    return path


def member_path(name):
    require(type(name) is str and 0 < len(name) <= 240 and not name.startswith('/') and
            '\\' not in name and all(32 <= ord(c) < 127 for c in name),
            'unsupported archive path encoding, separator or length')
    parts = name.split('/')
    require(len(parts) <= 16, 'archive path is too deep')
    for part in parts:
        check_part(part)
    return parts


def check_part(part):
    require(part and len(part) <= 255 and part not in ('.', '..') and part[-1] not in '. ' and
            not any(ord(c) < 32 or c in '<>:"|?*\\/' for c in part), 'unsafe Windows path component')
    device = part.split('.')[0].rstrip(' ').upper()
    require(device not in ('CON', 'PRN', 'AUX', 'NUL', 'CONIN$', 'CONOUT$') and
            not re.fullmatch(r'(?:COM|LPT)[1-9¹²³]', device), 'Windows device path rejected')


def read_local_source(path, suffix, limit, minimum):
    path = local_source(path, suffix)
    with path.open('rb') as stream:
        before = os.fstat(stream.fileno())
        require(stat.S_ISREG(before.st_mode) and minimum <= before.st_size <= limit,
                'invalid or oversized source')
        raw = stream.read(limit + 1)
        after = os.fstat(stream.fileno())
    require(len(raw) == before.st_size == after.st_size and before.st_mtime_ns == after.st_mtime_ns,
            'source changed while reading; inspection stopped')
    # The captured bytes are immutable for inspection. This is not a guarantee
    # against a same-user writer restoring timestamps or replacing ancestors.
    return raw


def read_archive(path):
    return read_local_source(path, '.zip', ARCHIVE_LIMIT, 22)


def check_extra(raw):
    # Do not let another extractor reinterpret an ASCII path via an alternate
    # name/link/ZIP64 field. Only inert timestamp and UID/GID metadata is allowed;
    # this tool does not apply either to the filesystem.
    position, seen = 0, set()
    while position < len(raw):
        require(position + 4 <= len(raw), 'truncated ZIP extra field')
        kind, size = struct.unpack_from('<HH', raw, position)
        position += 4
        require(kind not in seen and position + size <= len(raw), 'duplicate/truncated ZIP extra field')
        seen.add(kind)
        data = raw[position:position + size]
        require(kind in (0x5455, 0x7875), 'unsupported path/ZIP64/other ZIP extra field')
        if kind == 0x5455:
            require(1 <= len(data) <= 13 and not data[0] & ~7 and (len(data) - 1) % 4 == 0,
                    'unsupported ZIP timestamp metadata')
        else:
            require(len(data) >= 3 and data[0] == 1 and 1 <= data[1] <= 8 and 2 + data[1] < len(data),
                    'unsupported ZIP UID/GID metadata')
            gid_size = data[2 + data[1]]
            require(1 <= gid_size <= 8 and len(data) == 3 + data[1] + gid_size,
                    'unsupported ZIP UID/GID metadata')
        position += size


def directory_budget(raw):
    require(22 <= len(raw) <= ARCHIVE_LIMIT and raw.startswith(b'PK\x03\x04'),
            'standard non-executable ZIP required')
    end = raw.rfind(b'PK\x05\x06', max(0, len(raw) - 65557))
    require(end >= 0 and end + 22 <= len(raw), 'ZIP end record missing')
    disk, start_disk, on_disk, count, size, offset, comment = struct.unpack_from('<4H2IH', raw, end + 4)
    require(disk == start_disk == 0 and on_disk == count and
            0 < count <= ENTRY_LIMIT and offset + size == end and end + 22 + comment == len(raw),
            'unsupported split/ZIP64 directory or trailing ZIP data')
    # ZipFile does not use the advertised EOCD count as an allocation budget.
    # Bound the actual central records before letting it materialize ZipInfos.
    position, actual = offset, 0
    while position < end:
        require(actual < ENTRY_LIMIT and position + 46 <= end and raw[position:position + 4] == b'PK\x01\x02',
                'central directory record budget/format rejected')
        name, extra, note = struct.unpack_from('<3H', raw, position + 28)
        require(0 < name <= 241 and position + 46 + name + extra + note <= end,
                'central directory member name/length budget rejected')
        require(all(struct.unpack_from('<I', raw, position + field)[0] != 0xffffffff for field in (20, 24, 42)) and
                struct.unpack_from('<H', raw, position + 34)[0] == 0, 'ZIP64/split member layout rejected')
        check_extra(raw[position + 46 + name:position + 46 + name + extra])
        position += 46 + name + extra + note
        actual += 1
    require(position == end and actual == count, 'central directory count mismatch')
    return offset, count


def check_directory(raw, entries, offset, count):
    require(count == len(entries), 'parsed ZIP member count mismatch')
    require(min(info.header_offset for info in entries) == 0, 'prepended ZIP data rejected')
    seen, explicit, total, compressed, intervals = {}, set(), 0, 0, []
    for info in entries:
        require(info.orig_filename == info.filename, 'NUL/truncated archive member name rejected')
        directory = info.is_dir()
        name = info.filename[:-1] if directory else info.filename
        parts = member_path(name)
        kind = stat.S_IFMT(info.external_attr >> 16)
        require(kind in (0, stat.S_IFDIR if directory else stat.S_IFREG) and
                not info.external_attr & 0x400 and (directory or not info.external_attr & 0x10),
                'archive link, special file or inconsistent attributes rejected')
        require(not info.flag_bits & (1 | 32 | 64) and
                info.compress_type in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED),
                'encrypted/patched/unsupported compression rejected')
        require(0 <= info.file_size <= FILE_LIMIT and 0 <= info.compress_size <= ARCHIVE_LIMIT and
                info.file_size <= max(1, info.compress_size) * RATIO_LIMIT and
                (not directory or info.file_size == 0), 'member size/compression budget exceeded')
        require(info.file_size != 0 or info.compress_size <= 64, 'empty member compression budget exceeded')
        require(info.compress_type != zipfile.ZIP_STORED or info.compress_size == info.file_size,
                'stored member length mismatch')
        compressed += info.compress_size
        require(compressed <= min(ARCHIVE_LIMIT, len(raw)), 'aggregate compressed budget exceeded')
        total += info.file_size
        require(total <= TOTAL_LIMIT, 'aggregate uncompressed budget exceeded')
        start = info.header_offset
        require(0 <= start and start + 30 <= offset and raw[start:start + 4] == b'PK\x03\x04',
                'invalid ZIP local-header offset/signature')
        flags, method = struct.unpack_from('<2H', raw, start + 6)
        crc, packed, unpacked = struct.unpack_from('<3I', raw, start + 14)
        name_size, extra_size = struct.unpack_from('<2H', raw, start + 26)
        body = start + 30 + name_size + extra_size
        end = body + info.compress_size
        require(flags == info.flag_bits and method == info.compress_type and 0 < name_size <= 241 and
                end <= offset and raw[start + 30:start + 30 + name_size] == info.orig_filename.encode('ascii'),
                'local/central ZIP header or data range mismatch')
        check_extra(raw[start + 30 + name_size:body])
        if flags & 8:
            require(crc in (0, info.CRC) and packed in (0, info.compress_size) and unpacked in (0, info.file_size),
                    'unsupported local ZIP64/descriptor metadata')
            descriptor = end + (4 if raw[end:end + 4] == b'PK\x07\x08' else 0)
            require(descriptor + 12 <= offset and struct.unpack_from('<3I', raw, descriptor) ==
                    (info.CRC, info.compress_size, info.file_size), 'invalid ZIP data descriptor')
            end = descriptor + 12
        else:
            require((crc, packed, unpacked) == (info.CRC, info.compress_size, info.file_size),
                    'local/central ZIP sizes or CRC disagree')
        intervals.append((start, end))
        for index in range(1, len(parts) + 1):
            current = '/'.join(parts[:index])
            is_dir = index < len(parts) or directory
            key = current.casefold()
            if key in seen:
                require(seen[key] == (current, is_dir), 'case or file/directory collision rejected')
            seen[key] = (current, is_dir)
        key = name.casefold()
        require(key not in explicit, 'duplicate archive member rejected')
        explicit.add(key)
    previous_end = 0
    for start, end in sorted(intervals):
        require(start >= previous_end, 'overlapping ZIP local headers/data rejected')
        previous_end = end
    return total, {name for name, is_directory in seen.values() if is_directory}


def manifest_report(raw, files, root):
    require(0 < len(raw) <= MANIFEST_LIMIT, 'manifest size budget exceeded')
    manifest = strict_json(raw)
    require(type(manifest) is dict, 'manifest must be an object')
    mv, name, version = (manifest.get(key) for key in ('manifest_version', 'name', 'version'))
    require(type(mv) is int and mv in (2, 3), 'unsupported declared manifest version')
    require(type(name) is str and 0 < len(name) <= 256 and not any(ord(c) < 32 for c in name),
            'invalid declared extension name')
    require(type(version) is str and re.fullmatch(r'(?:0|[1-9][0-9]{0,4})(?:\.(?:0|[1-9][0-9]{0,4})){0,3}', version)
            and all(int(part) <= 65535 for part in version.split('.')) and any(int(part) for part in version.split('.')),
            'invalid declared extension version')
    permissions = {}
    for key in ('permissions', 'optional_permissions', 'host_permissions', 'optional_host_permissions'):
        values = manifest.get(key, [])
        require(type(values) is list and len(values) <= 256 and
                all(type(value) is str and 0 < len(value) <= 2048 and not any(ord(c) < 32 for c in value)
                    for value in values), 'invalid declared permission list')
        require(len(set(values)) == len(values), 'duplicate declared permissions')
        permissions[key] = values
    referenced = set()

    def asset(value):
        # A manifest reference may start with one explicit current-directory
        # prefix. Archive member names remain canonical and reject all dot
        # components; never normalize nested dots, traversal or URL encoding.
        if type(value) is str and value.startswith('./'):
            value = value[2:]
        member_path(value)
        require('%' not in value and '#' not in value, 'encoded/fragment asset references are unsupported')
        require(root + value in files, 'declared local entry file is absent or has different case')
        referenced.add(value)

    def assets(values):
        require(type(values) is list and len(values) <= ENTRY_LIMIT, 'invalid declared asset list')
        for value in values:
            asset(value)

    def icon(value):
        if type(value) is str:
            asset(value)
        else:
            require(type(value) is dict and len(value) <= 128, 'invalid declared icon table')
            for item in value.values():
                asset(item)

    if 'icons' in manifest:
        icon(manifest['icons'])
    if 'background' in manifest:
        background = manifest['background']
        require(type(background) is dict, 'invalid background declaration')
        for key in ('service_worker', 'page'):
            if key in background:
                asset(background[key])
        if 'scripts' in background:
            assets(background['scripts'])
    if 'content_scripts' in manifest:
        scripts = manifest['content_scripts']
        require(type(scripts) is list and len(scripts) <= 256, 'invalid content scripts declaration')
        for script in scripts:
            require(type(script) is dict, 'invalid content script entry')
            for key in ('js', 'css'):
                if key in script:
                    assets(script[key])
    if 'side_panel' in manifest:
        panel = manifest['side_panel']
        require(mv == 3 and type(panel) is dict, 'invalid side panel declaration')
        if 'default_path' in panel:
            asset(panel['default_path'])
        # An absent default path is not guessed from scripts or permissions.
        # Runtime setOptions and complete Chrome schema validation remain outside scope.
    for key in ('action', 'browser_action', 'page_action', 'options_ui', 'devtools_page', 'options_page'):
        if key not in manifest:
            continue
        value = manifest[key]
        if key.endswith('_page'):
            asset(value)
        else:
            require(type(value) is dict, 'invalid action/options declaration')
            page = 'page' if key == 'options_ui' else 'default_popup'
            if page in value and value[page] != '':
                asset(value[page])
            if 'default_icon' in value:
                icon(value['default_icon'])
    # These are selected static entry references, not a full Chrome schema
    # validator, JavaScript/CSS dependency scanner or permission risk approval.
    return {'declared_name': name, 'declared_version': version, 'manifest_version': mv,
            'declared_permissions': permissions, 'checked_local_entry_count': len(referenced)}


def _inspect_bytes(raw, component):
    require(component in INSPECTION_COMPONENTS, 'unsupported requested component')
    require(type(raw) is bytes and 22 <= len(raw) <= ARCHIVE_LIMIT, 'invalid ZIP input budget')
    try:
        offset, count = directory_budget(raw)
        with zipfile.ZipFile(io.BytesIO(raw)) as archive:
            entries = archive.infolist()
            require(0 < len(entries) <= ENTRY_LIMIT, 'archive entry budget exceeded')
            total, directories = check_directory(raw, entries, offset, count)
            files = {}
            manifests = [info for info in entries if not info.is_dir() and
                         info.filename.rsplit('/', 1)[-1].casefold() == 'manifest.json']
            require(len(manifests) == 1 and manifests[0].filename.endswith('manifest.json'),
                    'one unambiguous lowercase manifest.json required')
            require(0 < manifests[0].file_size <= MANIFEST_LIMIT, 'manifest size budget exceeded')
            manifest_name = manifests[0].filename
            root = manifest_name[:-len('manifest.json')]
            manifest_bytes, actual_total = None, 0
            for info in entries:
                digest, count, chunks = hashlib.sha256(), 0, []
                with archive.open(info) as stream:
                    while True:
                        chunk = stream.read(CHUNK)
                        if not chunk:
                            break
                        count += len(chunk)
                        actual_total += len(chunk)
                        require(count <= info.file_size and actual_total <= TOTAL_LIMIT,
                                'actual decompression budget exceeded')
                        digest.update(chunk)
                        if info.filename == manifest_name:
                            chunks.append(chunk)
                require(count == info.file_size, 'member content size mismatch')
                if info.filename == manifest_name:
                    manifest_bytes = b''.join(chunks)
                if not info.is_dir():
                    files[info.filename] = (count, digest.hexdigest())
            require(actual_total == total, 'aggregate member content size mismatch')
            report = manifest_report(manifest_bytes, files, root)
            hashes = [(name, digest) for name, (_, digest) in files.items()]
            tree = json.dumps(sorted(hashes), ensure_ascii=True, separators=(',', ':')).encode('ascii')
            licenses = sorted(name for name in files if re.fullmatch(
                r'(?:license|licence|copying)(?:\.(?:txt|md))?', name.rsplit('/', 1)[-1], re.I))
            report.update({'schema': 1, 'status': 'STRUCTURAL_INSPECTION_ONLY',
                           'requested_component': component, 'package_sha256': hashlib.sha256(raw).hexdigest(),
                           'file_tree_sha256': hashlib.sha256(tree).hexdigest(), 'manifest_path': manifest_name,
                           'archive_bytes': len(raw), 'uncompressed_bytes': total, 'entry_count': len(entries),
                           'file_count': len(files), 'files_outside_manifest_root': sum(not name.startswith(root) for name in files),
                           'license_evidence': [{'path': name, 'sha256': files[name][1]} for name in licenses],
                           'source_identity': 'NOT_EVALUATED', 'redistribution': 'NOT_EVALUATED',
                           'signature': 'NOT_EVALUATED', 'version_approval': 'NOT_EVALUATED',
                           'cef_compatibility': 'NOT_EVALUATED', 'installability': 'NOT_EVALUATED',
                           'installation': 'NOT_PERFORMED'})
            return report, files, directories
    except (zipfile.BadZipFile, NotImplementedError, RuntimeError, zlib.error) as error:
        raise ValueError('ZIP content/CRC/compression validation failed') from error


def inspect_package(path, component):
    return inspect_bytes(read_archive(path), component)


def inspect_bytes(raw, component):
    # Keep the report-only public/CLI contract; staging shares the validated
    # inventory from the same pass instead of re-expanding and rehashing the ZIP.
    return _inspect_bytes(raw, component)[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument('--component', choices=INSPECTION_COMPONENTS, required=True,
                        help='requested target label only; does not prove the package identity')
    parser.add_argument('zip', type=Path, help='absolute local ZIP; no extraction or installation')
    args = parser.parse_args()
    try:
        report = inspect_package(args.zip, args.component)
        print(json.dumps(report, ensure_ascii=True, indent=2))
    except (ValueError, OSError, UnicodeError, OverflowError) as error:
        print('STOP: ' + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
