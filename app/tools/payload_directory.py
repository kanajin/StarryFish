"""Shared bounded payload publication for build delivery and initial setup.

No profile I/O, execution or registration. Failure preserves a staging tree;
publication is Windows no-replace rename. Observations do not isolate arbitrary
same-user writers; a runtime consumer still needs its own held source checks.
"""
import hashlib
import os
from pathlib import Path
import secrets
import sys

sys.dont_write_bytecode = True
import component_package as package
import component_stage as observation


def verify_payload(path, files, directories):
    root = observation._local_path(path)
    identities = {entry: observation._directory_identity(entry)
                  for entry in reversed((root, *root.parents))}
    layout = {name: set() for name in ('', *directories)}
    for name in (*files, *directories):
        parent, _, leaf = name.rpartition('/')
        layout[parent].add(leaf)
    for name in layout:
        directory = root / name
        observation._path_budget(directory)
        identities[directory] = observation._directory_identity(directory)

    def check_layout():
        for directory, identity in identities.items():
            package.require(observation._directory_identity(directory) == identity,
                            'packaged payload directory identity changed')
        for name, expected in layout.items():
            package.require(observation._names(root / name, len(expected)) == expected,
                            'packaged payload missing or extra entry')

    check_layout()
    for name, (size, digest) in files.items():
        target = root / name
        observation._path_budget(target)
        observation._read_regular(target, size, digest, size)
    check_layout()
    return {name: digest for name, (_, digest) in files.items()}


def publish_tree(parent, relative, files, directories, read_file, *, reuse_matching=False, read_chunks=None):
    """Publish only a fresh tree, or explicitly reuse an exact fixed build tree.

    The caller owns the source/manifest policy. First setup never sets
    reuse_matching: an existing component is not a request to update it.
    """
    parent = observation._local_path(parent)
    package.require((read_file is None) != (read_chunks is None), 'one payload reader required')
    package.member_path(relative)
    package.require(files and len(files) + len(directories) <= observation.NODE_LIMIT,
                    'bounded nonempty payload inventory required')
    for name in (*files, *directories):
        package.member_path(name)
        prefix = name.rpartition('/')[0]
        package.require(not prefix or prefix in directories, 'missing payload parent inventory')
    package.require(not set(files) & set(directories), 'payload file/directory collision')
    for directory in (parent, *parent.parents):
        observation._directory_identity(directory)
    destination = parent / relative
    observation._path_budget(destination)
    # Inspect each existing ancestor before looking through it, never resolve
    # a reparse target and then treat that resolved location as authorized.
    current = parent
    for part in Path(relative).parts[:-1]:
        current /= part
        try:
            observation._directory_identity(current)
        except FileNotFoundError:
            break
    try:
        destination.lstat()
    except FileNotFoundError:
        pass
    else:
        package.require(reuse_matching, 'existing component retained; manual update only')
        return verify_payload(destination, files, directories)
    staging = parent / ('.component-payload-' + secrets.token_hex(16))
    # Budget every full path before creating any files.
    for base in (staging, destination):
        for name in (*files, *directories):
            observation._path_budget(base / name)
    staging.mkdir()
    for name in sorted(directories, key=lambda value: (value.count('/'), value)):
        (staging / name).mkdir()
    for name, (size, digest) in files.items():
        target = staging / name
        if read_chunks is None:
            content = read_file(name)
            package.require(type(content) is bytes and len(content) == size and
                            hashlib.sha256(content).hexdigest() == digest,
                            'captured payload member changed')
            chunks = observation._chunks(content)
        else:
            # Large CEF files are streamed; the shared writer checks the exact
            # byte count/digest before the unchanged no-replace publication.
            chunks = read_chunks(name)
        observation._write_chunks(target, chunks, size, digest)
    verify_payload(staging, files, directories)
    current = parent
    for part in Path(relative).parts[:-1]:
        observation._directory_identity(current)
        current /= part
        try:
            current.mkdir()
        except FileExistsError:
            pass
        observation._directory_identity(current)
    for directory in (current, *current.parents):
        observation._directory_identity(directory)
    os.rename(staging, destination)  # Windows fails if destination already exists.
    return verify_payload(destination, files, directories)
