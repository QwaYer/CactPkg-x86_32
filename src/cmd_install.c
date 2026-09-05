/* cmd_install.c - package installation: from a repository by name, or from a .cpkg. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cactpkg.h"

/* ---- growable string buffer ---- */
struct strbuf {
    char *s;
    unsigned len;
    unsigned cap;
};

static int sb_add(struct strbuf *sb, const char *data, unsigned len)
{
    if (sb->len + len + 1 > sb->cap) {
        unsigned newcap = sb->cap ? sb->cap * 2 : 256;
        while (newcap < sb->len + len + 1)
            newcap *= 2;
        char *ns = (char *)realloc(sb->s, newcap);
        if (!ns)
            return -1;
        sb->s = ns;
        sb->cap = newcap;
    }
    memcpy(sb->s + sb->len, data, len);
    sb->len += len;
    sb->s[sb->len] = '\0';
    return 0;
}

static void sb_free(struct strbuf *sb)
{
    free(sb->s);
    sb->s = NULL;
    sb->len = sb->cap = 0;
}

/* ---- archive checks ---- */
static int check_arch(const struct cp_config *cfg, const struct cp_manifest *m,
                      int force)
{
    if (strcmp(m->arch, cfg->arch) == 0)
        return 0;
    if (force) {
        cp_err("cactpkg: warning: %s: arch %s != %s (forced)\n",
               m->name, m->arch, cfg->arch);
        return 0;
    }
    cp_err("cactpkg: %s: arch %s does not match %s\n",
           m->name, m->arch, cfg->arch);
    return -1;
}

/* extract a .cpkg under the prefix; return 0/-1; file list into files */
static int extract_to_prefix(struct cpkg *p, const struct cp_config *cfg,
                             struct strbuf *files)
{
    for (int i = 0; i < p->nentries; i++) {
        struct cpkg_entry *e = &p->entries[i];

        char *dest = cp_join(cfg->prefix, e->name);
        if (!dest) {
            cp_err("cactpkg: %s: out of memory\n", p->meta.name);
            return -1;
        }

        if (cp_mkdir_p(dest, 0755) != 0) {
            cp_err("cactpkg: %s: cannot create directories for %s\n",
                   p->meta.name, dest);
            free(dest);
            return -1;
        }

        unsigned mode = e->mode ? e->mode : 0644u;
        if (cp_write_file(dest, e->data, e->size, mode) != 0) {
            cp_err("cactpkg: %s: cannot write %s\n",
                   p->meta.name, dest);
            free(dest);
            return -1;
        }

        if (sb_add(files, dest, (unsigned)strlen(dest)) != 0 ||
            sb_add(files, "\n", 1) != 0) {
            cp_err("cactpkg: %s: out of memory\n", p->meta.name);
            free(dest);
            return -1;
        }
        free(dest);
    }
    return 0;
}

/* install one concrete .cpkg file */
static int install_file(struct cpkg *p, const struct cp_config *cfg,
                        int force, int quiet)
{
    if (check_arch(cfg, &p->meta, force) != 0)
        return -1;

    struct strbuf files = { NULL, 0, 0 };
    if (extract_to_prefix(p, cfg, &files) != 0) {
        sb_free(&files);
        return -1;
    }

    if (db_record(cfg, p->meta.name, p->manifest_raw, files.s) != 0) {
        cp_err("cactpkg: %s: cannot update package database\n", p->meta.name);
        sb_free(&files);
        return -1;
    }
    sb_free(&files);

    if (!quiet) {
        cp_out("cactpkg: installed: %s %s (%d file(s))\n",
               p->meta.name, p->meta.version, p->nentries);
    }
    return 0;
}

static int dep_install(const struct cp_config *cfg, const char *name, int depth,
                       int force, int yes, int quiet);

/* iterate dependencies "a, b" one by one */
static int install_depends(const struct cp_config *cfg, const char *deps,
                           int depth, int force, int yes, int quiet)
{
    (void)yes;
    char *copy = strdup(deps);
    if (!copy)
        return -1;

    int rc = 0;
    char *save = NULL;
    for (char *tok = strtok_r(copy, ",", &save); tok;
         tok = strtok_r(NULL, ",", &save)) {
        cp_trim(tok);
        if (!*tok)
            continue;
        if (dep_install(cfg, tok, depth + 1, force, yes, quiet) != 0)
            rc = -1;
    }
    free(copy);
    return rc;
}

static int repo_install_one(const struct cp_config *cfg,
                            const struct repo_index *ri,
                            const struct repo_pkg *rp,
                            int force, int yes, int quiet)
{
    (void)yes;
    if (db_has(cfg, rp->name)) {
        if (!force) {
            if (!quiet)
                cp_out("cactpkg: %s is already installed\n", rp->name);
            return 0;
        }
        cp_out("cactpkg: %s: reinstalling (--force)\n", rp->name);
    }

    char *path = cp_join(ri->dir, rp->file);
    if (!path)
        return -1;

    struct cpkg p;
    if (cpkg_load(path, &p) != 0) {
        cp_err("cactpkg: cannot read %s\n", path);
        free(path);
        return -1;
    }
    free(path);

    int rc = install_file(&p, cfg, force, quiet);
    cpkg_unload(&p);
    return rc;
}

static int dep_install(const struct cp_config *cfg, const char *name, int depth,
                       int force, int yes, int quiet)
{
    (void)yes;
    if (depth > CP_MAX_DEPTH) {
        cp_err("cactpkg: dependency chain too deep at %s\n", name);
        return -1;
    }
    if (db_has(cfg, name)) {
        if (!force) {
            if (!quiet)
                cp_out("cactpkg: %s already installed (dependency)\n", name);
            return 0;
        }
    }

    for (int i = 0; i < cfg->nrepos; i++) {
        struct repo_index ri;
        if (repo_load(cfg->repo[i], &ri) != 0)
            continue;
        const struct repo_pkg *rp = repo_find(&ri, name);
        if (rp) {
            int rc = 0;
            if (rp->deps && *rp->deps)
                rc = install_depends(cfg, rp->deps, depth, force, yes, quiet);
            if (rc == 0)
                rc = repo_install_one(cfg, &ri, rp, force, yes, quiet);
            repo_unload(&ri);
            return rc;
        }
        repo_unload(&ri);
    }

    cp_err("cactpkg: package %s not found in any repository\n", name);
    return -1;
}

static int is_cpkg_path(const char *arg)
{
    return cp_has_suffix(arg, ".cpkg") || strchr(arg, '/') != NULL;
}

int cmd_install(const struct cp_config *cfg, int argc, char **argv,
                int force, int yes, int quiet)
{
    (void)yes;
    if (argc < 1) {
        cp_err("usage: cactpkg install <package... | file.cpkg...> [-y]\n");
        return -1;
    }
    if (cfg->nrepos == 0) {
        cp_err("cactpkg: no repositories configured "
               "(add 'repo = <dir>' to %s, or use --repo)\n", "/etc/cactpkg.conf");
        return -1;
    }

    int rc = 0;
    for (int i = 0; i < argc; i++) {
        const char *arg = argv[i];
        if (is_cpkg_path(arg)) {
            struct cpkg p;
            if (cpkg_load(arg, &p) != 0) {
                cp_err("cactpkg: cannot read package %s\n", arg);
                rc = -1;
                continue;
            }
            if (!db_has(cfg, p.meta.name) || force) {
                int sub = install_depends(cfg, p.meta.depends ? p.meta.depends : "",
                                         0, force, yes, quiet);
                if (sub == 0)
                    sub = install_file(&p, cfg, force, quiet);
                if (sub != 0)
                    rc = -1;
            } else if (!quiet) {
                cp_out("cactpkg: %s is already installed\n", p.meta.name);
            }
            cpkg_unload(&p);
        } else {
            if (dep_install(cfg, arg, 0, force, yes, quiet) != 0)
                rc = -1;
        }
    }
    return rc;
}
