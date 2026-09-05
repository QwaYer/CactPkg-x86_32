/* repo.c - reading the index of a local repository. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cactpkg.h"

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
    if (!cp_name_ok(fields[0]) || !*fields[1] || !*fields[2])
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

int repo_load(const char *dir, struct repo_index *ri)
{
    memset(ri, 0, sizeof(*ri));
    strncpy(ri->dir, dir, CP_PATH_MAX - 1);
    ri->dir[CP_PATH_MAX - 1] = '\0';

    char *idxpath = cp_join(dir, "index");
    if (!idxpath)
        return -1;

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
