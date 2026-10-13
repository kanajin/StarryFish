"""Prepare the project's public bookmark defaults; never access a browser/profile.

This is development tooling, not a live capture/restore adapter. Its fixed source
and observed script revision must be reviewed again before packaging or use.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import stat
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT.parent / 'GBF-Bookmark-Config.json'
OUTPUT = ROOT / '.build/windows/bookmark-initial-storage.json'
SOURCE_SHA256 = 'a3ccc704034302836636561e4008c639065819e700ceb64e0976cdaffcd1bc0f'
OBSERVED_SCRIPT = '41f13f7df7009d1bc17f0084100b85980d5fc85d'
LIMIT = 1024 * 1024
DATA_KEY = 'gbf-bookmark:data'
CONFIG_KEY = 'gbf-bookmark:config'


def safe_path(path):
    path = Path(path).absolute()
    for item in (path, *path.parents):
        try:
            info = item.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
            raise ValueError('link/reparse path rejected')
    return path


def read_bounded(path):
    path = safe_path(path)
    with path.open('rb') as stream:
        if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
            raise ValueError('regular file required')
        raw = stream.read(LIMIT + 1)
    if not raw or len(raw) > LIMIT:
        raise ValueError('empty/oversize configuration')
    return raw


def strict_json(raw):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError('duplicate JSON property')
            result[key] = value
        return result

    def invalid_constant(_):
        raise ValueError('non-JSON numeric constant')

    def finite_float(value):
        result = float(value)
        if not math.isfinite(result):
            raise ValueError('nonfinite JSON number')
        return result

    if isinstance(raw, bytes):
        raw = raw.decode('utf-8', errors='strict')
    if type(raw) is not str or not raw or len(raw.encode('utf-8')) > LIMIT:
        raise ValueError('empty/oversize JSON')
    try:
        result = json.loads(raw, object_pairs_hook=pairs, parse_constant=invalid_constant,
                            parse_float=finite_float)
        pending = [(result, 0)]
        while pending:
            item, depth = pending.pop()
            if depth > 64:
                raise ValueError('deep JSON rejected')
            if isinstance(item, str):
                item.encode('utf-8', errors='strict')
            elif isinstance(item, dict):
                pending.extend((child, depth + 1) for child in item.values())
                pending.extend((key, depth + 1) for key in item)
            elif isinstance(item, list):
                pending.extend((child, depth + 1) for child in item)
        return result
    except (RecursionError, json.JSONDecodeError) as error:
        raise ValueError('invalid/deep JSON') from error


def compare_structure(expected, actual):
    """Object order is immaterial; types, keys, array order and values are exact."""
    if type(expected) is not type(actual):
        raise ValueError('configuration type mismatch')
    if isinstance(expected, dict):
        if expected.keys() != actual.keys():
            raise ValueError('configuration property mismatch')
        for key in expected:
            compare_structure(expected[key], actual[key])
    elif isinstance(expected, list):
        if len(expected) != len(actual):
            raise ValueError('configuration array length mismatch')
        for left, right in zip(expected, actual):
            compare_structure(left, right)
    elif expected != actual:
        raise ValueError('configuration value mismatch')


def validate_config(document):
    if type(document) is not dict or set(document) != {'data', 'config'}:
        raise ValueError('expected data/config object')
    entries, settings = document['data'], document['config']
    if type(entries) is not list or not 1 <= len(entries) <= 1000:
        raise ValueError('invalid bookmark list')
    roots, groups, positions = {}, {}, set()
    required = {'name', 'url', 'index', 'background', '_index'}
    for offset, entry in enumerate(entries):
        if (type(entry) is not dict or not required <= set(entry)
                or set(entry) - required - {'parent', 'color'}):
            raise ValueError('unsupported bookmark properties')
        for key in ('name', 'url'):
            value = entry[key]
            if type(value) is not str or not value or len(value) > 4096:
                raise ValueError('invalid bookmark text')
            value.encode('utf-8', errors='strict')
        for key in ('background', 'color'):
            if key in entry and (type(entry[key]) is not str or not re.fullmatch(
                    r'#[0-9a-fA-F]{3}(?:[0-9a-fA-F]{3})?', entry[key])):
                raise ValueError('invalid bookmark color')
        index = entry['index']
        parent = entry.get('parent', 0)
        if type(index) is not int or not 1 <= index <= 100:
            raise ValueError('index outside observed script display range')
        if type(parent) is not int or not 0 <= parent <= 100:
            raise ValueError('invalid parent index')
        if type(entry['_index']) is not int or entry['_index'] != offset:
            raise ValueError('_index differs from observed script array identity')
        if (parent, index) in positions:
            raise ValueError('duplicate position within bookmark group')
        positions.add((parent, index))
        if parent == 0:  # Missing parent and parent:0 are roots; preserve both.
            roots[index] = offset
        else:
            groups.setdefault(parent, []).append(offset)
    if any(parent not in roots for parent in groups):
        raise ValueError('child refers to missing root index')
    if type(settings) is not dict or set(settings) != {
            'position', 'hideDelay', 'animation', 'margin', 'size', 'align', 'mixed'}:
        raise ValueError('unsupported script settings')
    if (settings['position'] not in ('left', 'right') or
            settings['align'] not in ('left', 'center', 'right') or
            settings['mixed'] not in ('yes', 'no') or type(settings['animation']) is not bool):
        raise ValueError('invalid script settings')
    for key in ('hideDelay', 'margin', 'size'):
        if type(settings[key]) is not int or not -(2**31) <= settings[key] < 2**31:
            raise ValueError('invalid integer script setting')
    if settings['size'] not in (1, 2, 3):
        raise ValueError('invalid script size')
    # makeList inserts the root before children; a child at parent.index replaces
    # that group header slot. Report the observed overlap, never rewrite data.
    overlaps = [parent for parent, offsets in groups.items()
                if any(entries[offset]['index'] == parent for offset in offsets)]
    return {'entries': len(entries), 'roots': len(roots),
            'groups': [{'parent_index': parent, 'root_array_offset': roots[parent],
                        'child_array_offsets': offsets, 'children': len(offsets)}
                       for parent, offsets in groups.items()],
            'group_header_slot_overlaps': overlaps}


def load_source():
    raw = read_bounded(SOURCE)
    if hashlib.sha256(raw).hexdigest() != SOURCE_SHA256:
        raise ValueError('project source changed; review required before preparation')
    document = strict_json(raw)
    validate_config(document)
    return document


def storage_values(document):
    validate_config(document)
    # JSON strings only, no executable JS, HTML, GM state or transient time key.
    values = {DATA_KEY: json.dumps(document['data'], ensure_ascii=False, separators=(',', ':')),
              CONFIG_KEY: json.dumps(document['config'], ensure_ascii=False, separators=(',', ':'))}
    verify_values(document, values)
    return values


def verify_values(document, values):
    if type(values) is not dict or set(values) != {DATA_KEY, CONFIG_KEY}:
        raise ValueError('unexpected/missing storage key')
    for value in values.values():
        if type(value) is not str or len(value.encode('utf-8')) > LIMIT:
            raise ValueError('invalid storage value')
    restored = {'data': strict_json(values[DATA_KEY]), 'config': strict_json(values[CONFIG_KEY])}
    validate_config(restored)
    compare_structure(document, restored)


def merge_initial_values(document, existing):
    """Pure development candidate from an explicit string mapping, no live I/O.

    Add only missing initial keys. Existing initial values must match the full
    parsed structure; preserve their original strings and every unrelated key.
    The caller supplies document; use load_source() for digest-checked defaults.
    This is not a storage lease, migration adapter or credential projection.
    """
    defaults = storage_values(document)
    if type(existing) is not dict or len(existing) > 1024:
        raise ValueError('bounded string storage snapshot required')
    candidate = dict(existing)

    def check_budget(values):
        total = 0
        for key, value in values.items():
            if type(key) is not str or type(value) is not str or len(key) + len(value) > LIMIT:
                raise ValueError('invalid string storage snapshot')
            total += len(key.encode('utf-8', errors='strict')) + len(value.encode('utf-8', errors='strict'))
            if total > LIMIT:
                raise ValueError('storage snapshot exceeds development budget')

    check_budget(candidate)
    # Validate BOTH initial keys before adding either, including partial snapshots.
    for key, field in ((DATA_KEY, 'data'), (CONFIG_KEY, 'config')):
        if key in candidate:
            compare_structure(document[field], strict_json(candidate[key]))
    changed = any(key not in candidate for key in defaults)
    for key, value in defaults.items():
        if key not in candidate:
            candidate[key] = value
    if len(candidate) > 1024:
        raise ValueError('merged storage exceeds key budget')
    check_budget(candidate)
    return candidate, changed


def make_preparation(document):
    return {'schema': 1, 'scope': 'DEVELOPMENT_PREPARATION_ONLY',
            'source_sha256': SOURCE_SHA256, 'observed_script_commit': OBSERVED_SCRIPT,
            'storage': 'page-localStorage', 'values': storage_values(document)}


def verify_preparation(document, packet):
    expected = make_preparation(document)
    if type(packet) is not dict or packet.keys() != expected.keys():
        raise ValueError('invalid preparation properties')
    for key in expected:
        if key != 'values':
            compare_structure(expected[key], packet[key])
    verify_values(document, packet['values'])


def prepare(document):
    output = safe_path(OUTPUT)
    if not output.parent.is_dir():
        raise ValueError('existing development output directory required')
    packet = make_preparation(document)
    raw = (json.dumps(packet, ensure_ascii=False, indent=2) + '\n').encode('utf-8')
    if len(raw) > LIMIT:
        raise ValueError('preparation output too large')
    # No overwrite/retry or profile access. A failed write leaves evidence here;
    # it must not be used until verify-prepared succeeds.
    with output.open('xb') as stream:
        if stream.write(raw) != len(raw):
            raise ValueError('short preparation write')
        stream.flush()
        os.fsync(stream.fileno())
    verify_preparation(document, strict_json(read_bounded(output)))
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument('command', choices=('check', 'prepare', 'verify-prepared'), nargs='?', default='check')
    arguments = parser.parse_args()
    document = load_source()
    if arguments.command == 'prepare':
        output = prepare(document)
        print('Prepared public initial values:', output)
    elif arguments.command == 'verify-prepared':
        verify_preparation(document, strict_json(read_bounded(OUTPUT)))
    else:
        storage_values(document)
    print(json.dumps(validate_config(document), ensure_ascii=True, sort_keys=True))
    if arguments.command == 'check':
        print('PASS: in-memory initial mapping; prepared file NOT_VERIFIED')
    else:
        print('PASS: prepared file verified against exact initial configuration')
    print('FR11 runtime NOT_RUN; no restore capability registered')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, UnicodeError) as error:
        print('STOP:', str(error), file=sys.stderr)
        raise SystemExit(1)
