/* cmd_query.c — update / list / search / info / files. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cactpkg.h"

static const char *or_dash(const char *s)
{
    return (s && *s) ? s : "-";
}

static void print_manifest(const struct cp_manifest *m)
{
    cp_out("name:        %s\n", m->name);
    cp_out("version:     %s\n", m->version);
    cp_out("arch:        %s\n", m->arch);
    cp_out("description: %s\n", or_dash(m->description));
    cp_out("depends:     %s\n", or_dash(m->depends));
}

int cmd_update(const struct cp_config *cfg)
{
    if (cfg->nrepos == 0) {
        cp_err("cactpkg: no repositories configured\n");
        return -1;
    }

    int rc = 0;
    for (int i = 0; i < cfg->nrepos; i++) {
        struct repo_index ri;
        if (repo_load(cfg->repo[i], &ri) != 0) {
            cp_err("cactpkg: repository %s: no index (run 'make repo' first)\n",
                   cfg->repo[i]);
            rc = -1;
            continue;
        }
        cp_out("cactpkg: repository %s: %d package(s)\n",
               cfg->repo[i], ri.npkgs);
        repo_unload(&ri);
    }
    return rc;
}

int cmd_list(const struct cp_config *cfg)
{
    char names[64][CP_NAME_MAX];
    int n = db_list(cfg, names, 64);
    if (n == 0) {
        cp_out("(no packages installed)\n");
        return 0;
    }
    for (int i = 0; i < n; i++) {
        char *raw = NULL;
        if (db_read_manifest(cfg, names[i], &raw) != 0)
            continue;
        struct cp_manifest m;
        if (meta_parse(&m, raw) == 0) {
            cp_out("%-16s %-10s %s\n", m.name, m.version, or_dash(m.description));
            meta_free(&m);
        }
        free(raw);
    }
    return 0;
}

int cmd_search(const struct cp_config *cfg, const char *needle)
{
    if (!needle || !*needle) {
        cp_err("usage: cactpkg search <pattern>\n");
        return -1;
    }

    char names[64][CP_NAME_MAX];
    int n = db_list(cfg, names, 64);
    for (int i = 0; i < n; i++) {
        char *raw = NULL;
        if (db_read_manifest(cfg, names[i], &raw) != 0)
            continue;
        struct cp_manifest m;
        if (meta_parse(&m, raw) == 0) {
            if (strstr(m.name, needle) ||
                (m.description && strstr(m.description, needle)))
                cp_out("i %-16s %-10s %s\n", m.name, m.version,
                       or_dash(m.description));
            meta_free(&m);
        }
        free(raw);
    }

    for (int r = 0; r < cfg->nrepos; r++) {
        struct repo_index ri;
        if (repo_load(cfg->repo[r], &ri) != 0)
            continue;
        for (int i = 0; i < ri.npkgs; i++) {
            struct repo_pkg *p = &ri.pkgs[i];
            if (strstr(p->name, needle) || strstr(p->file, needle))
                cp_out("r %-16s %-10s %s\n", p->name, p->version, ri.dir);
        }
        repo_unload(&ri);
    }
    return 0;
}

int cmd_info(const struct cp_config *cfg, const char *name)
{
    char *raw = NULL;
    if (db_read_manifest(cfg, name, &raw) == 0) {
        struct cp_manifest m;
        if (meta_parse(&m, raw) == 0) {
            print_manifest(&m);
            cp_out("status:      installed\n");
            meta_free(&m);
        }
        free(raw);
        return 0;
    }

    for (int r = 0; r < cfg->nrepos; r++) {
        struct repo_index ri;
        if (repo_load(cfg->repo[r], &ri) != 0)
            continue;
        const struct repo_pkg *p = repo_find(&ri, name);
        if (p) {
            cp_out("name:        %s\n", p->name);
            cp_out("version:     %s\n", p->version);
            cp_out("file:        %s\n", p->file);
            cp_out("depends:     %s\n", or_dash(p->deps));
            cp_out("repo:        %s\n", ri.dir);
            cp_out("status:      available\n");
            repo_unload(&ri);
            return 0;
        }
        repo_unload(&ri);
    }

    cp_err("cactpkg: package %s not found (not installed and not in repositories)\n",
           name);
    return -1;
}

int cmd_files(const struct cp_config *cfg, const char *name)
{
    if (!db_has(cfg, name)) {
        cp_err("cactpkg: package %s is not installed\n", name);
        return -1;
    }
    char *files_text = NULL;
    if (db_read_files(cfg, name, &files_text) != 0) {
        cp_err("cactpkg: %s: no file list in database\n", name);
        return -1;
    }
    if (!*files_text) {
        cp_out("(no files)\n");
    } else {
        char *save = NULL;
        for (char *line = strtok_r(files_text, "\n", &save); line;
             line = strtok_r(NULL, "\n", &save))
            if (*line)
                cp_out("%s\n", line);
    }
    free(files_text);
    return 0;
}
