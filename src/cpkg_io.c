/* cpkg_io.c - reading a .cpkg v1 artifact from a file (whole file in memory). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cactpkg.h"

static uint16_t rd16(const unsigned char *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int name_safe(const char *name)
{
    if (!name || !*name || name[0] == '/')
        return 0;
    if (strstr(name, "../") || strcmp(name, "..") == 0 ||
        strstr(name, "/../"))
        return 0;
    return 1;
}

static int cpkg_parse(struct cpkg *p)
{
    const unsigned char *b = p->buf;
    unsigned len = p->buflen;

    if (len < CP_HDR_LEN)
        return -1;
    if (memcmp(b, CP_MAGIC, 4) != 0)
        return -1;
    if (b[4] != CP_ARCH_VERSION)
        return -1;

    uint32_t n_files = rd32(b + 8);
    uint32_t mlen = rd32(b + 12);

    if (n_files > 65536u)
        return -1;
    if (mlen > len || CP_HDR_LEN + mlen > len)
        return -1;

    /* manifest */
    char *raw = (char *)malloc(mlen + 1);
    if (!raw)
        return -1;
    memcpy(raw, b + CP_HDR_LEN, mlen);
    raw[mlen] = '\0';
    p->manifest_raw = raw;

    if (meta_parse(&p->meta, raw) != 0) {
        free(raw);
        p->manifest_raw = NULL;
        return -1;
    }

    /* file entries */
    p->entries = (struct cpkg_entry *)calloc(n_files ? n_files : 1,
                                             sizeof(struct cpkg_entry));
    if (!p->entries) {
        p->nentries = 0;
        return -1;
    }

    const unsigned char *cur = b + CP_HDR_LEN + mlen;
    const unsigned char *end = b + len;
    uint32_t i;
    int rc = -1;

    for (i = 0; i < n_files; i++) {
        if ((size_t)(end - cur) < 8)
            goto fail;
        uint16_t name_len = rd16(cur);
        uint16_t mode = rd16(cur + 2);
        uint32_t size = rd32(cur + 4);
        cur += 8;

        if (name_len == 0 || name_len >= CP_PATH_MAX)
            goto fail;
        if ((size_t)(end - cur) < (size_t)name_len + size)
            goto fail;

        memcpy(p->entries[i].name, cur, name_len);
        p->entries[i].name[name_len] = '\0';
        if (!name_safe(p->entries[i].name))
            goto fail;

        p->entries[i].mode = mode;
        p->entries[i].size = size;
        p->entries[i].data = cur + name_len;

        cur += (size_t)name_len + size;
    }
    p->nentries = (int)n_files;
    return 0;

fail:
    if (raw) {
        free(raw);
        p->manifest_raw = NULL;
    }
    meta_free(&p->meta);
    free(p->entries);
    p->entries = NULL;
    p->nentries = 0;
    return rc;
}

int cpkg_load(const char *path, struct cpkg *p)
{
    memset(p, 0, sizeof(*p));

    unsigned len = 0;
    p->buf = (unsigned char *)cp_read_file(path, &len);
    if (!p->buf)
        return -1;
    p->buflen = len;
    strncpy(p->src, path, CP_PATH_MAX - 1);
    p->src[CP_PATH_MAX - 1] = '\0';

    if (cpkg_parse(p) != 0) {
        cpkg_unload(p);
        return -1;
    }
    return 0;
}

void cpkg_unload(struct cpkg *p)
{
    if (p->manifest_raw) {
        free(p->manifest_raw);
        p->manifest_raw = NULL;
    }
    meta_free(&p->meta);
    if (p->entries) {
        free(p->entries);
        p->entries = NULL;
    }
    if (p->buf) {
        free(p->buf);
        p->buf = NULL;
    }
    p->nentries = 0;
    p->buflen = 0;
}
