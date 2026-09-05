ROOT := $(abspath .)

CACTLIB ?= $(abspath ../CactLibc-x86_32)
LR_SBIN ?= $(abspath ../LocalRepoCactOS-x86_32/lib/sbin)
LR_LIB  ?= $(abspath ../LocalRepoCactOS-x86_32/lib)
PY      ?= python3
CPKG    := $(PY) $(ROOT)/tools/cpkg.py

CC      := gcc
LD      := ld
START_O := $(CACTLIB)/build/pic/start.o
LIBC_SO := $(CACTLIB)/clibc.so

CFLAGS := -m32 -ffreestanding -fPIE -fno-stack-protector -nostdlib \
          -ffunction-sections -fdata-sections \
          -I$(ROOT)/include -I$(CACTLIB)/include -Wall -Wextra

LDFLAGS := -m elf_i386 -pie --dynamic-linker=/lib/ld.so --hash-style=both \
           -nostdlib --gc-sections -T $(ROOT)/link.ld

BUILDD  := $(ROOT)/build/bin
OBJD    := $(ROOT)/build/obj

SRCS    := $(wildcard $(ROOT)/src/*.c)
OBJS    := $(patsubst $(ROOT)/src/%.c,$(OBJD)/%.o,$(SRCS))

PKGS    := hello cactpkg
PKGDIRS := $(patsubst %,$(ROOT)/pkg/%,$(PKGS))
REPO    := $(ROOT)/repo

.PHONY: all cactpkg hello stage repo install clean check

all: cactpkg hello

$(LIBC_SO) $(START_O):
	@test -f $(LIBC_SO) && test -f $(START_O) || \
		(echo >&2 "Missing libc — build CactLibc-x86_32 first (CACTLIB=$(CACTLIB))"; exit 1)

$(OBJD) $(BUILDD):
	mkdir -p $@

$(OBJD)/%.o: $(ROOT)/src/%.c | $(OBJD)
	$(CC) $(CFLAGS) -c $< -o $@

cactpkg: $(BUILDD)/cactpkg
$(BUILDD)/cactpkg: $(OBJS) $(START_O) $(LIBC_SO) | $(BUILDD)
	$(LD) $(LDFLAGS) $(START_O) $(OBJS) $(LIBC_SO) -o $@

hello: $(BUILDD)/hello
$(BUILDD)/hello: $(ROOT)/examples/hello.c $(START_O) $(LIBC_SO) | $(BUILDD)
	$(CC) $(CFLAGS) -c $< -o $(OBJD)/hello.o
	$(LD) $(LDFLAGS) $(START_O) $(OBJD)/hello.o $(LIBC_SO) -o $@

# fill the payload/ dirs of directory-packages with built ELFs
stage: cactpkg hello
	@mkdir -p $(ROOT)/pkg/cactpkg/payload/sbin
	@mkdir -p $(ROOT)/pkg/hello/payload/bin
	cp -f $(BUILDD)/cactpkg $(ROOT)/pkg/cactpkg/payload/sbin/cactpkg
	cp -f $(BUILDD)/hello   $(ROOT)/pkg/hello/payload/bin/hello

# personal (offline) repository: .cpkg + index
repo: stage
	rm -rf $(REPO)
	mkdir -p $(REPO)
	$(CPKG) build $(ROOT)/pkg/hello   --repo $(REPO)
	$(CPKG) build $(ROOT)/pkg/cactpkg --repo $(REPO)
	$(CPKG) index $(REPO)

# bake cactpkg and its repository into the LocalRepo tree (then make -C LocalRepo)
install: repo
	@mkdir -p $(LR_SBIN) $(LR_LIB)/cactpkg/repo
	cp -f $(BUILDD)/cactpkg $(LR_SBIN)/cactpkg
	cp -f $(REPO)/*.cpkg $(REPO)/index $(LR_LIB)/cactpkg/repo/

check: repo
	$(CPKG) check $(ROOT)/pkg/hello
	$(CPKG) check $(ROOT)/pkg/cactpkg
	$(CPKG) dump $(REPO)/hello-1.0.0.cpkg
	$(CPKG) dump $(REPO)/cactpkg-0.1.0.cpkg

clean:
	rm -rf $(BUILDD) $(OBJD) $(REPO)
	rm -rf $(ROOT)/pkg/hello/payload $(ROOT)/pkg/cactpkg/payload
