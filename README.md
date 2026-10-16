# CactPkg

<p align="center">
  <img src="https://img.shields.io/badge/license-GPLv3-blue.svg?style=for-the-badge" alt="License: GPLv3">
  <img src="https://img.shields.io/badge/arch-i686-red.svg?style=for-the-badge" alt="Arch: i686">
  <img src="https://img.shields.io/badge/language-C%2FPython-orange.svg?style=for-the-badge" alt="Language: C/Python">
  <img src="https://img.shields.io/badge/status-2.0.0-yellow.svg?style=for-the-badge" alt="Status: 2.0.0">
</p>

**The package manager of CactOS.** It runs entirely inside the OS. A package
source is either a repository directory (in the image:
`/usr/lib/cactpkg/repo`) or a **remote repository over HTTPS** — the index and
`.cpkg` artifacts are downloaded into a local cache (TLS 1.3 and certificate
verification come from libc, keys stay in the process). Plain `http://` is
refused.

## Status (2.0.0)

- [x] Directory packages (`pkg/<name>/` = `manifest` + `payload/`)
- [x] `.cpkg` v1 artifact + host-side tool `tools/cpkg.py`
- [x] Repository index (`index`) and the offline repository baked into the image
- [x] Base-system programs packaged automatically: the image build wraps every
      tool in `/usr/bin` and `/usr/sbin` as a `.cpkg` (`cpkg.py sysrepo`), so
      `cactpkg search`/`update` lists the whole base system, not just add-ons
- [x] Linux-style command set: `update`, `install` (with dependencies),
      `remove` (with a dependents check), `list`, `search`, `info`, `files`
- [x] Persistent installs into a prefix (default `/usr/local`), config in
      `/etc/cactpkg.conf`, database in `/var/lib/cactpkg`
- [x] Remote repositories over HTTPS: the index is fetched by `update`,
      artifacts on install, both cached under `/var/cache/cactpkg`
- [ ] `.cpkg` integrity checks (CRC/signature)
- [ ] Versions and conflicts

## Building

Requires built sibling trees of **CactLibc-x86_32** (`clibc.so`, `ld.so`,
`start.o`) - auto-detected by default:

```sh
ninja -C build-meson             # build-meson/cactpkg + build-meson/hello (demo)
ninja -C build-meson repo        # build the personal offline repository in repo/ (.cpkg + index)
ninja -C build-meson check       # validate packages and dump .cpkg
ninja -C build-meson stage       # bake cactpkg and repo/ into the LocalRepoCactOS tree
```

`ninja -C build-meson stage` places:

- `/usr/sbin/cactpkg` - the manager,
- `/usr/lib/cactpkg/repo/{*.cpkg,index}` - the personal offline repository.

The regular image build then packs them into `cctkfs.img`:

```sh
ninja -C CactOS-x86_32/build-meson iso   # or: ninja -C LocalRepoCactOS-x86_32/build-meson stage
```

Since the LocalRepo build gained a `cactpkg` step, the manager and its
offline repository are re-baked into the image on every build, even after a
`ninja -C build-meson clean`.

## Usage inside CactOS

The manager lives in `/usr/sbin`, and the offline repository is available by
default (`/usr/lib/cactpkg/repo`):

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

A remote repository is a `repo` entry that is an **https://** URL (or `-R <url>`);
it must serve an `index` and the `.cpkg` files it names. Plain `http://` is
refused — an unauthenticated index or artifact would be a trivial MITM:

```
cactpkg update -R https://packages.example.org/i686          # fetch the index over the network
cactpkg install -R https://packages.example.org/i686 hello   # download and install a package
```

Files are installed into the prefix (default `/usr/local`) and persist on
disk. Configuration: `/etc/cactpkg.conf` (`prefix`, `db`, `cache`, repeatable
`repo`, `arch`), created with documented defaults on first run; overridable with
`--prefix`, `-R/--repo`, `--cache`, `-C/--conf`.

## Layout

```
CactPkg-x86_32/
├── meson.build
├── docs/FORMAT.md      # formats: directory-package, .cpkg v1, index, db
├── include/cactpkg.h
├── src/                # the manager (C, CactOS ELF)
│   ├── main.c          # CLI
│   ├── util.c          # files, paths, mkdir -p
│   ├── config.c        # /etc/cactpkg.conf + defaults
│   ├── manifest.c      # manifest parser
│   ├── cpkg_io.c       # .cpkg v1 reader
│   ├── repo.c          # repository index (local dir or remote URL)
│   ├── net.c           # HTTP(S) GET for remote repositories
│   ├── db.c            # installed-packages database
│   └── cmd_*.c         # install / remove / query
├── tools/cpkg.py       # host-side tool (build/index/sysrepo/extract/check/dump)
├── examples/hello.c    # demo application for the first package
├── pkg/                # the personal package set (directory packages)
│   ├── hello/          #   demo package
│   ├── cactpkg/        #   the manager itself as a package (bootstrap)
│   └── cact-headers/   #   libc headers (payload filled from CactLibc include/)
├── repo/               # generated: the personal offline repository
└── link.ld
```

## Adding a package to the personal set

1. Create a `pkg/<name>/` directory with a `manifest` (name, version, arch,
   depends, ...) and a `payload/` that mirrors the installation root.
2. `ninja -C build-meson repo` - builds the `.cpkg` and regenerates `index`.
3. `ninja -C build-meson stage` - and the package rides into the image under
   `/usr/lib/cactpkg/repo`.

## License

GPLv3 - see `LICENSE`.
