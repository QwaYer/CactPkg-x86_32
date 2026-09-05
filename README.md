# CactPkg

<p align="center">
  <img src="https://img.shields.io/badge/license-GPLv3-blue.svg?style=for-the-badge" alt="License: GPLv3">
  <img src="https://img.shields.io/badge/arch-i686-red.svg?style=for-the-badge" alt="Arch: i686">
  <img src="https://img.shields.io/badge/language-C%2FPython-orange.svg?style=for-the-badge" alt="Language: C/Python">
  <img src="https://img.shields.io/badge/status-0.1.0-yellow.svg?style=for-the-badge" alt="Status: 0.1.0">
</p>

**The local package manager of CactOS.** It works entirely inside the OS and
does not reach outside: the package source is a repository directory (in the
image: `/lib/cactpkg/repo`). Network repositories will be added later; the
command and format design already accounts for them.

## Status (0.1.0)

- [x] Directory packages (`pkg/<name>/` = `manifest` + `payload/`)
- [x] `.cpkg` v1 artifact + host-side tool `tools/cpkg.py`
- [x] Repository index (`index`) and the offline repository baked into the image
- [x] Linux-style command set: `update`, `install` (with dependencies),
      `remove` (with a dependents check), `list`, `search`, `info`, `files`
- [x] Persistent installs into a prefix (default `/usr/local`), config in
      `/etc/cactpkg.conf`, database in `/var/lib/cactpkg`
- [ ] `.cpkg` integrity checks (CRC/signature)
- [ ] Remote repository (fetch/socket), versions and conflicts

## Building

Requires built sibling trees of **CactLibc-x86_32** (`clibc.so`, `ld.so`,
`start.o`) - auto-detected by default:

```sh
make            # build/bin/cactpkg + build/bin/hello (demo)
make repo       # build the personal offline repository in repo/ (.cpkg + index)
make check      # validate packages and dump .cpkg
make install    # bake cactpkg and repo/ into the LocalRepoCactOS tree
```

`make install` places:

- `/sbin/cactpkg` - the manager,
- `/lib/cactpkg/repo/{*.cpkg,index}` - the personal offline repository.

The regular image build then packs them into `cctkfs.img`:

```sh
make -C CactOS-x86_32 iso     # or: make -C LocalRepoCactOS-x86_32
```

Since the LocalRepo `Makefile` gained a `cactpkg` step, the manager and its
offline repository are re-baked into the image on every build, even after a
`make clean`.

## Usage inside CactOS

The manager lives in `/sbin`, and the offline repository is available by
default (`/lib/cactpkg/repo`):

```
cactpkg update                  # refresh repository indexes
cactpkg search hello            # search
cactpkg install hello           # install a package from the repository (with dependencies)
cactpkg install ./pkg.cpkg      # install from a file
cactpkg list                    # installed packages
cactpkg info cactpkg            # package details
cactpkg files hello             # files of an installed package
cactpkg remove hello            # remove
```

Files are installed into the prefix (default `/usr/local`) and persist on
disk. Configuration: `/etc/cactpkg.conf` (`prefix`, `db`, repeatable `repo`,
`arch`); overridable with `--prefix`, `-R/--repo`, `-C/--conf`.

## Layout

```
CactPkg-x86_32/
├── Makefile
├── docs/FORMAT.md      # formats: directory-package, .cpkg v1, index, db
├── include/cactpkg.h
├── src/                # the manager (C, CactOS ELF)
│   ├── main.c          # CLI
│   ├── util.c          # files, paths, mkdir -p
│   ├── config.c        # /etc/cactpkg.conf + defaults
│   ├── manifest.c      # manifest parser
│   ├── cpkg_io.c       # .cpkg v1 reader
│   ├── repo.c          # repository index
│   ├── db.c            # installed-packages database
│   └── cmd_*.c         # install / remove / query
├── tools/cpkg.py       # host-side tool (build/index/extract/check/dump)
├── examples/hello.c    # demo application for the first package
├── pkg/                # the personal package set (directory packages)
│   ├── hello/          #   demo package
│   └── cactpkg/        #   the manager itself as a package (bootstrap)
├── repo/               # generated: the personal offline repository
└── link.ld
```

## Adding a package to the personal set

1. Create a `pkg/<name>/` directory with a `manifest` (name, version, arch,
   depends, ...) and a `payload/` that mirrors the installation root.
2. `make repo` - builds the `.cpkg` and regenerates `index`.
3. `make install` - and the package rides into the image under
   `/lib/cactpkg/repo`.

## License

GPLv3 - see `LICENSE`.
