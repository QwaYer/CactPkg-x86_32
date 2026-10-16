/* repo.c - reading a repository index, local or remote. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stat.h>

#include "cactpkg.h"

/* Short deterministic key for a source string: FNV-1a plus the length, so a
 * remote repository gets its own cache subdirectory. */
static void src_cache_key(const char *src, char out[20])
{
    uint32_t h = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)src; *p; p++) {
        h ^= *p;
        h *= 16777619u;
    }
    snprintf(out, 20, "%08x%08x", h, (unsigned)strlen(src));
}

/* <cache>/<key-of-src>; malloc'd */
static char *repo_cache_dir(const char *cache, const char *src)
{
    char key[20];
    src_cache_key(src, key);
    return cp_join(cache, key);
}

/* mkdir -p, including the final component */
static int ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0)
        return S_ISDIR(st.st_mode) ? 0 : -1;
    if (cp_mkdir_p(path, 0755) != 0)
        return -1;
    if (mkdir(path, 0755) != 0) {
        if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode))
            return -1;
    }
    return 0;
}

void repo_unload(struct repo_index *ri)
{
    for (int i = 0; i < ri->npkgs; i++)
        free(ri->pkgs[i].deps);
    free(ri->pkgs);
    ri->pkgs = NULL;
    ri->npkgs = 0;
    ri->cap = 0;
}

/* split a line into up to 4 fields separated by '\t'; returns the number found */
static int split_tsv(char *line, char *fields[4])
{
    int n = 0;
    char *p = line;
    while (*p && n < 4) {
        char *tab = strchr(p, '\t');
        if (tab)
            *tab = '\0';
        fields[n++] = p;
        if (!tab)
            break;
        p = tab + 1;
    }
    return n;
}

static int repo_add_line(struct repo_index *ri, char *line)
{
    cp_trim(line);
    if (!*line || *line == '#')
        return 0;

    char *fields[4] = { NULL, NULL, NULL, NULL };
    int n = split_tsv(line, fields);
    if (n < 3)
        return -1;
    /* a remote index is untrusted: never accept a path, only a plain file name */
    if (!cp_name_ok(fields[0]) || !*fields[1] || !cp_name_ok(fields[2]) ||
        strcmp(fields[2], "..") == 0)
        return -1;

    if (ri->npkgs == ri->cap) {
        int newcap = ri->cap ? ri->cap * 2 : 16;
        struct repo_pkg *np = (struct repo_pkg *)realloc(
            ri->pkgs, (size_t)newcap * sizeof(struct repo_pkg));
        if (!np)
            return -1;
        ri->pkgs = np;
        ri->cap = newcap;
    }

    struct repo_pkg *p = &ri->pkgs[ri->npkgs];
    memset(p, 0, sizeof(*p));
    strncpy(p->name, fields[0], CP_NAME_MAX - 1);
    p->name[CP_NAME_MAX - 1] = '\0';
    strncpy(p->version, fields[1], sizeof(p->version) - 1);
    p->version[sizeof(p->version) - 1] = '\0';
    strncpy(p->file, fields[2], CP_PATH_MAX - 1);
    p->file[CP_PATH_MAX - 1] = '\0';
    if (n >= 4 && *fields[3]) {
        p->deps = strdup(fields[3]);
        if (!p->deps)
            return -1;
    }
    ri->npkgs++;
    return 0;
}

int repo_load(const char *cache, const char *src, struct repo_index *ri,
              int refresh)
{
    memset(ri, 0, sizeof(*ri));
    strncpy(ri->dir, src, CP_PATH_MAX - 1);
    ri->dir[CP_PATH_MAX - 1] = '\0';

    char *idxpath;
    if (net_is_url(src)) {
        char *dir = repo_cache_dir(cache, src);
        if (!dir)
            return -1;
        if (ensure_dir(dir) != 0) {
            cp_err("cactpkg: cannot create cache %s\n", dir);
            free(dir);
            return -1;
        }
        idxpath = cp_join(dir, "index");
        free(dir);
        if (!idxpath)
            return -1;

        struct stat st;
        if (refresh || stat(idxpath, &st) != 0) {
            char *url = cp_join(src, "index");
            if (!url) {
                free(idxpath);
                return -1;
            }
            int frc = net_fetch(url, idxpath);
            free(url);
            if (frc != 0) {
                free(idxpath);
                return -1;
            }
        }
    } else {
        idxpath = cp_join(src, "index");
        if (!idxpath)
            return -1;
    }

    unsigned len = 0;
    unsigned char *buf = (unsigned char *)cp_read_file(idxpath, &len);
    free(idxpath);
    if (!buf)
        return -1;

    char *text = (char *)malloc(len + 1);
    if (!text) {
        free(buf);
        return -1;
    }
    memcpy(text, buf, len);
    text[len] = '\0';
    free(buf);

    int rc = 0;
    char *save = NULL;
    for (char *line = strtok_r(text, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        if (repo_add_line(ri, line) != 0) {
            rc = -1;
            break;
        }
    }
    free(text);
    if (rc != 0)
        repo_unload(ri);
    return rc;
}

const struct repo_pkg *repo_find(const struct repo_index *ri, const char *name)
{
    for (int i = 0; i < ri->npkgs; i++) {
        if (strcmp(ri->pkgs[i].name, name) == 0)
            return &ri->pkgs[i];
    }
    return NULL;
}

/* Local path of `file` inside `src`: just src/file for a directory source,
 * download-into-cache for a URL (reused from the cache unless refresh is set).
 * malloc'd; NULL on error. */
char *repo_local_file(const char *cache, const char *src, const char *file,
                      int refresh)
{
    if (!net_is_url(src))
        return cp_join(src, file);

    char *dir = repo_cache_dir(cache, src);
    if (!dir)
        return NULL;
    if (ensure_dir(dir) != 0) {
        cp_err("cactpkg: cannot create cache %s\n", dir);
        free(dir);
        return NULL;
    }
    char *dest = cp_join(dir, file);
    free(dir);
    if (!dest)
        return NULL;

    struct stat st;
    if (!refresh && stat(dest, &st) == 0)
        return dest;                       /* already cached */

    char *url = cp_join(src, file);
    if (!url) {
        free(dest);
        return NULL;
    }
    cp_out("cactpkg: fetching %s\n", url);
    int rc = net_fetch(url, dest);
    free(url);
    if (rc != 0) {
        free(dest);
        return NULL;
    }
    return dest;
}
