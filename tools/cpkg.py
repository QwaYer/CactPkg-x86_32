#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
cpkg.py — host-side tool for CactPkg.

Operates on directory-packages (pkg/<name>/manifest + payload/)
and .cpkg v1 artifacts.

Commands:
  init   <repodir>                 create an empty repository directory
  build  <pkgdir> [--repo <dir>]   build a .cpkg from a directory-package
          [-o <file>]              (with --repo: <dir>/<name>-<version>.cpkg)
  index  <repodir>                 rebuild index from *.cpkg in a directory
  sysrepo <userland> <repodir>     generate one .cpkg per executable in
          [--version <ver>]        <userland>/{bin,sbin} (the base image) and
          [--arch <arch>]          re-index; skips names already packaged
  check  <pkgdir>                  validate a directory-package
  extract <file.cpkg> <dest>       unpack a .cpkg (for host-side testing)
  dump   <file.cpkg>               show the manifest and entries of a .cpkg

Format is described in docs/FORMAT.md.
"""

import os
import struct
import sys

MAGIC = b"CKPK"
VERSION = 1
HDR = struct.Struct("<4sBBHIIIIII")   # 32 bytes
NAME_MAX = 255

REQUIRED = ("name", "version", "arch")

# Generated base-system packages carry this manifest tag so a rebuild can drop
# the previous batch without touching hand-written packages (hello, cactpkg).
SYS_ORIGIN = "system"
SYS_VERSION = "2.0.0"


class CpkgError(Exception):
    pass


# ---- manifest ----
def parse_manifest(text):
    meta = {}
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if ":" in line:
            key, _, val = line.partition(":")
        elif "=" in line:
            key, _, val = line.partition("=")
        else:
            continue
        key = key.strip().lower()
        val = val.strip()
        if not key or not val:
            continue
        meta[key] = val
    for k in REQUIRED:
        if k not in meta:
            raise CpkgError(f"manifest: missing required field '{k}'")
    return meta


def name_ok(name):
    if not name:
        return False
    allowed = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")
    return all(c in allowed for c in name)


def rel_safe(rel):
    if not rel or rel.startswith("/") or rel.startswith("./"):
        return False
    parts = rel.split("/")
    return ".." not in parts and "" not in parts


# ---- .cpkg read/write ----
def write_cpkg(manifest_text, files, out):
    """files: list of (relpath, mode, data)."""
    mlen = len(manifest_text.encode("utf-8"))
    buf = bytearray()
    buf += HDR.pack(MAGIC, VERSION, 0, 0, len(files), mlen, 0, 0, 0, 0)
    buf += manifest_text.encode("utf-8")
    for rel, mode, data in files:
        if len(rel) > NAME_MAX:
            raise CpkgError(f"name too long: {rel}")
        nb = rel.encode("utf-8")
        buf += struct.pack("<HHI", len(nb), mode & 0xFFFF, len(data))
        buf += nb
        buf += data
    with open(out, "wb") as f:
        f.write(buf)


def read_cpkg(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < HDR.size:
        raise CpkgError(f"{path}: file shorter than header")
    magic, ver, _, _fl, nfiles, mlen, _crc, *_ = HDR.unpack_from(data, 0)
    if magic != MAGIC:
        raise CpkgError(f"{path}: not a .cpkg (no CKPK magic)")
    if ver != VERSION:
        raise CpkgError(f"{path}: unsupported version {ver}")
    end_manifest = HDR.size + mlen
    if end_manifest > len(data):
        raise CpkgError(f"{path}: corrupted manifest")
    manifest_text = data[HDR.size:end_manifest].decode("utf-8", "replace")
    meta = parse_manifest(manifest_text)

    entries = []
    off = end_manifest
    for _ in range(nfiles):
        if off + 8 > len(data):
            raise CpkgError(f"{path}: corrupted file list")
        name_len, mode, size = struct.unpack_from("<HHI", data, off)
        off += 8
        if off + name_len + size > len(data):
            raise CpkgError(f"{path}: corrupted file entry")
        rel = data[off:off + name_len].decode("utf-8", "replace")
        off += name_len
        payload = data[off:off + size]
        off += size
        if not rel_safe(rel):
            raise CpkgError(f"{path}: unsafe path in package: {rel!r}")
        entries.append((rel, mode, payload))
    return manifest_text, meta, entries


# ---- directory-package ----
def load_pkgdir(pkgdir):
    man_path = os.path.join(pkgdir, "manifest")
    if not os.path.isfile(man_path):
        raise CpkgError(f"{pkgdir}: no manifest file")
    with open(man_path, encoding="utf-8") as f:
        manifest_text = f.read()
    meta = parse_manifest(manifest_text)
    if not name_ok(meta["name"]):
        raise CpkgError(f"invalid package name: {meta['name']!r}")

    payload = os.path.join(pkgdir, "payload")
    files = []
    if os.path.isdir(payload):
        for root, dirs, names in os.walk(payload):
            dirs.sort()
            for nm in sorted(names):
                full = os.path.join(root, nm)
                rel = os.path.relpath(full, payload).replace(os.sep, "/")
                if not rel_safe(rel):
                    raise CpkgError(f"unsafe path: {rel}")
                with open(full, "rb") as f:
                    data = f.read()
                mode = os.stat(full).st_mode & 0o7777
                files.append((rel, mode, data))
    return manifest_text, meta, files


def build_pkg(pkgdir, out):
    manifest_text, meta, files = load_pkgdir(pkgdir)
    if os.path.exists(out):
        raise CpkgError(f"{out}: already exists (remove it, or use -o with a new name)")
    write_cpkg(manifest_text, files, out)
    return meta, len(files)


# ---- commands ----
def cmd_init(repodir):
    os.makedirs(repodir, exist_ok=True)
    print(f"cactpkg: repository ready: {repodir}")


def cmd_build(argv):
    if not argv:
        raise CpkgError("build <pkgdir> [--repo <dir>] [-o <file>]")
    pkgdir = argv[0]
    repo = None
    out = None
    i = 1
    while i < len(argv):
        if argv[i] == "--repo" and i + 1 < len(argv):
            repo = argv[i + 1]
            i += 2
        elif argv[i] == "-o" and i + 1 < len(argv):
            out = argv[i + 1]
            i += 2
        else:
            raise CpkgError(f"unknown argument: {argv[i]}")
    if not out:
        if not repo:
            raise CpkgError("need --repo <dir> or -o <file>")
        _, meta, _ = load_pkgdir(pkgdir)
        out = os.path.join(repo, f"{meta['name']}-{meta['version']}.cpkg")
        if os.path.exists(out):
            raise CpkgError(f"{out}: already exists (remove it first)")
    meta, nfiles = build_pkg(pkgdir, out)
    print(f"cactpkg: {out}: {meta['name']} {meta['version']}, {nfiles} file(s)")


def cmd_index(repodir):
    if not os.path.isdir(repodir):
        raise CpkgError(f"{repodir}: not a directory")
    rows = []
    for fn in sorted(os.listdir(repodir)):
        if not fn.endswith(".cpkg"):
            continue
        _, meta, _ = read_cpkg(os.path.join(repodir, fn))
        deps = meta.get("depends", "")
        rows.append(f"{meta['name']}\t{meta['version']}\t{fn}\t{deps}")
    rows.sort(key=lambda r: r.split("\t", 1)[0])
    with open(os.path.join(repodir, "index"), "w", encoding="utf-8") as f:
        f.write("# CactPkg repository index v1\n")
        for r in rows:
            f.write(r + "\n")
    print(f"cactpkg: index: {len(rows)} package(s) in {repodir}")


def cmd_sysrepo(argv):
    """Generate a .cpkg for every executable in <userland>/{bin,sbin}.

    The base image's userland is not made of packages, so cactpkg cannot see
    it.  This wraps each staged binary as its own package (version = the CactOS
    release) and re-indexes, so `cactpkg update/search` lists the base system
    and every entry is installable.  Hand-written packages already in the repo
    win: a tool whose name is taken is skipped.
    """
    if len(argv) < 2:
        raise CpkgError("sysrepo <userland> <repodir> [--version <ver>] [--arch <arch>]")
    root, repodir = argv[0], argv[1]
    ver, arch = SYS_VERSION, "i686"
    i = 2
    while i < len(argv):
        if argv[i] == "--version" and i + 1 < len(argv):
            ver = argv[i + 1]; i += 2
        elif argv[i] == "--arch" and i + 1 < len(argv):
            arch = argv[i + 1]; i += 2
        else:
            raise CpkgError(f"unknown argument: {argv[i]}")
    if not os.path.isdir(repodir):
        raise CpkgError(f"{repodir}: not a directory")

    # Drop the previous generated batch, and remember names already packaged.
    taken = set()
    for fn in sorted(os.listdir(repodir)):
        if not fn.endswith(".cpkg"):
            continue
        path = os.path.join(repodir, fn)
        try:
            _, meta, _ = read_cpkg(path)
        except CpkgError:
            continue
        if meta.get("origin") == SYS_ORIGIN:
            os.remove(path)
        else:
            taken.add(meta["name"])

    made = 0
    for sub in ("bin", "sbin"):
        d = os.path.join(root, sub)
        if not os.path.isdir(d):
            continue
        for nm in sorted(os.listdir(d)):
            full = os.path.join(d, nm)
            if nm in taken or not name_ok(nm):
                continue
            if not os.path.isfile(full) or not os.access(full, os.X_OK):
                continue
            with open(full, "rb") as f:
                data = f.read()
            mode = os.stat(full).st_mode & 0o7777
            manifest = (f"name: {nm}\n"
                        f"version: {ver}\n"
                        f"arch: {arch}\n"
                        f"description: CactOS base-system program (usr/{sub})\n"
                        f"depends:\n"
                        f"origin: {SYS_ORIGIN}\n")
            write_cpkg(manifest, [(f"{sub}/{nm}", mode, data)],
                       os.path.join(repodir, f"{nm}-{ver}.cpkg"))
            made += 1

    print(f"cactpkg: sysrepo: {made} base-system package(s), version {ver}")
    cmd_index(repodir)


def cmd_check(pkgdir):
    manifest_text, meta, files = load_pkgdir(pkgdir)
    print(f"ok: {meta['name']} {meta['version']} arch={meta['arch']}")
    print(f"    depends: {meta.get('depends', '-')}")
    print(f"    description: {meta.get('description', '-')}")
    for rel, mode, _data in files:
        print(f"    {rel} ({mode:o})")


def cmd_extract(path, dest):
    _, _meta, entries = read_cpkg(path)
    for rel, mode, data in entries:
        full = os.path.join(dest, rel)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "wb") as f:
            f.write(data)
        os.chmod(full, mode)
    print(f"cactpkg: extracted {len(entries)} file(s) into {dest}")


def cmd_dump(path):
    manifest_text, meta, entries = read_cpkg(path)
    print(manifest_text.rstrip())
    print(f"-- {len(entries)} file(s):")
    for rel, mode, data in entries:
        print(f"   {rel}  mode={mode:o}  size={len(data)}")


def main():
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"):
        print(__doc__)
        return 1
    cmd = sys.argv[1]
    argv = sys.argv[2:]
    try:
        if cmd == "init":
            if len(argv) != 1:
                raise CpkgError("init <repodir>")
            cmd_init(argv[0])
        elif cmd == "build":
            cmd_build(argv)
        elif cmd == "index":
            if len(argv) != 1:
                raise CpkgError("index <repodir>")
            cmd_index(argv[0])
        elif cmd == "sysrepo":
            cmd_sysrepo(argv)
        elif cmd == "check":
            if len(argv) != 1:
                raise CpkgError("check <pkgdir>")
            cmd_check(argv[0])
        elif cmd == "extract":
            if len(argv) != 2:
                raise CpkgError("extract <file.cpkg> <dest>")
            cmd_extract(argv[0], argv[1])
        elif cmd == "dump":
            if len(argv) != 1:
                raise CpkgError("dump <file.cpkg>")
            cmd_dump(argv[0])
        else:
            raise CpkgError(f"unknown command: {cmd}")
    except CpkgError as e:
        print(f"cpkg: error: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())


