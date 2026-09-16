# ==============================================================================
# CactPkg — Meson build system
# ==============================================================================
#
#   meson setup build-meson --cross-file cross/i686-cact-clang.ini \
#       -Dcactlib=../CactLibc-x86_32
#   ninja -C build-meson             # build-meson/cactpkg + build-meson/hello
#   ninja -C build-meson stage       # payloads → offline repo → LocalRepo
#   ninja -C build-meson repo        # only rebuild repo/ with tools/cpkg.py
#   ninja -C build-meson check       # cpkg check/dump on the built packages
#
# Two PIEs link against CactLibc: the package manager itself (src/) and the
# example payload (examples/hello.c).  The repository is produced by the
# repo-local python tool, so those steps are run_targets that shell out to it,
# exactly as the Makefile did.
# ==============================================================================

project('cactpkg', 'c',
  meson_version: '>=1.0.0',
  default_options: [
    'c_std=gnu17',
    'warning_level=2',        # -Wall -Wextra, as in the Makefile
    'buildtype=plain',
    'b_staticpic=false',
  ])

fs = import('fs')
bash = find_program('bash')
python3 = find_program('python3')
ld = find_program('ld')

src_root = meson.project_source_root()
build_root = meson.current_build_dir()

# Path options are relative to this repo's root unless absolute.
_cactlib = get_option('cactlib')
cactlib = _cactlib.startswith('/') ? _cactlib : src_root / _cactlib
_lr_sbin = get_option('lr_sbin')
lr_sbin = _lr_sbin.startswith('/') ? _lr_sbin : src_root / _lr_sbin
_lr_lib = get_option('lr_lib')
lr_lib = _lr_lib.startswith('/') ? _lr_lib : src_root / _lr_lib

start_o = cactlib / 'build-meson' / 'start.o'
clibc_so = cactlib / 'build-meson' / 'clibc.so'

if not fs.exists(start_o) or not fs.exists(clibc_so)
  error('CactLibc is not built: expected ' + start_o +
        '  —  build it first:  ninja -C ' + cactlib / 'build-meson')
endif

# Every object in this repo is position independent: both tools are PIEs.
add_project_arguments('-fPIE', language: 'c')

c_args = [
  '-ffreestanding',
  '-fno-stack-protector',
  '-nostdlib',
  '-ffunction-sections',
  '-fdata-sections',
  '-I' + (src_root / 'include'),
  '-I' + (cactlib / 'include'),
]

pkg_lib = static_library('cactpkg',
  files(
    'src/cmd_install.c',
    'src/cmd_query.c',
    'src/cmd_remove.c',
    'src/config.c',
    'src/cpkg_io.c',
    'src/db.c',
    'src/main.c',
    'src/manifest.c',
    'src/repo.c',
    'src/util.c',
  ),
  c_args: c_args)

hello_lib = static_library('hello', 'examples/hello.c', c_args: c_args)

_link_prefix = [
  ld, '-m', 'elf_i386', '-pie', '--dynamic-linker=/lib/ld.so',
  '--hash-style=both', '-nostdlib', '--gc-sections',
  '-T', src_root / 'link.ld', '-o', '@OUTPUT@',
  start_o,
]

cactpkg_bin = custom_target('cactpkg',
  output: 'cactpkg',
  input: pkg_lib,
  command: _link_prefix +
           ['--whole-archive', pkg_lib, '--no-whole-archive', clibc_so],
  depend_files: files('link.ld'),
  build_by_default: true)

hello_bin = custom_target('hello',
  output: 'hello',
  input: hello_lib,
  command: _link_prefix +
           ['--whole-archive', hello_lib, '--no-whole-archive', clibc_so],
  depend_files: files('link.ld'),
  build_by_default: true)

# ------------------------------------------------------------------------------
# Packages and the offline repository (the Makefile's stage/repo/install)
# ------------------------------------------------------------------------------

cpkg = src_root / 'tools/cpkg.py'
pkg_root = src_root / 'pkg'
repo_dir = src_root / 'repo'

# Fill the payload dirs of the directory-packages, then rebuild repo/ with the
# repo-local tool.  Shared by repo/check/stage below.
_prep_script = (
  'set -e; ' +
  'mkdir -p "' + pkg_root + '/cactpkg/payload/sbin" "' + pkg_root + '/hello/payload/bin"; ' +
  'cp -f "$1" "' + pkg_root + '/cactpkg/payload/sbin/cactpkg"; ' +
  'cp -f "$2" "' + pkg_root + '/hello/payload/bin/hello"; ' +
  'rm -rf "' + repo_dir + '"; mkdir -p "' + repo_dir + '"; ' +
  python3.full_path() + ' "' + cpkg + '" build "' + pkg_root + '/hello" --repo "' + repo_dir + '"; ' +
  python3.full_path() + ' "' + cpkg + '" build "' + pkg_root + '/cactpkg" --repo "' + repo_dir + '"; ' +
  python3.full_path() + ' "' + cpkg + '" index "' + repo_dir + '"'
)

# The Makefile's `stage`: payload dirs only.
run_target('stage-payload',
  command: [bash, '-c',
            'set -e; mkdir -p "' + pkg_root + '/cactpkg/payload/sbin" "' + pkg_root + '/hello/payload/bin"; ' +
            'cp -f "$1" "' + pkg_root + '/cactpkg/payload/sbin/cactpkg"; ' +
            'cp -f "$2" "' + pkg_root + '/hello/payload/bin/hello"',
            'bash', cactpkg_bin, hello_bin])

# The Makefile's `repo`: payloads + offline repository.
run_target('repo',
  command: [bash, '-c', _prep_script, 'bash', cactpkg_bin, hello_bin])

# The Makefile's `check`.
run_target('check',
  command: [bash, '-c', _prep_script +
            '; ' + python3.full_path() + ' "' + cpkg + '" check "' + pkg_root + '/hello"' +
            '; ' + python3.full_path() + ' "' + cpkg + '" check "' + pkg_root + '/cactpkg"' +
            '; ' + python3.full_path() + ' "' + cpkg + '" dump "' + repo_dir + '/hello-1.0.0.cpkg"' +
            '; ' + python3.full_path() + ' "' + cpkg + '" dump "' + repo_dir + '/cactpkg-0.1.0.cpkg"',
            'bash', cactpkg_bin, hello_bin])

# The Makefile's `install`: bake the binary and the repository into LocalRepo
# (run -C LocalRepoCactOS afterwards).  Meson reserves "install", hence "stage".
run_target('stage',
  command: [bash, '-c', _prep_script +
            '; mkdir -p "' + lr_sbin + '" "' + lr_lib + '/cactpkg/repo"' +
            '; cp -f "$1" "' + lr_sbin + '/cactpkg"' +
            '; cp -f "' + repo_dir + '"/*.cpkg "' + repo_dir + '"/index "' + lr_lib + '/cactpkg/repo/"',
            'bash', cactpkg_bin, hello_bin])
