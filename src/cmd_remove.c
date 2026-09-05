/* cmd_remove.c - removal of installed packages with a dependents check. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cactpkg.h"

static int depends_contain(const char *deps, const char *name)
{
    if (!deps || !*deps)
        return 0;
    char *copy = strdup(deps);
    if (!copy)
        return 0;
    int found = 0;
    char *save = NULL;
    for (char *tok = strtok_r(copy, ",", &save); tok;
         tok = strtok_r(NULL, ",", &save)) {
        cp_trim(tok);
        if (*tok && strcmp(tok, name) == 0) {
            found = 1;
            break;
        }
    }
    free(copy);
    return found;
}

/* name of an installed package that depends on target (excluding target itself) */
static const char *find_dependent(const struct cp_config *cfg, const char *target,
                                  char *out, int outsz)
{
    char names[64][CP_NAME_MAX];
    int n = db_list(cfg, names, 64);
    for (int i = 0; i < n; i++) {
        if (strcmp(names[i], target) == 0)
            continue;
        char *raw = NULL;
        if (db_read_manifest(cfg, names[i], &raw) != 0)
            continue;
        struct cp_manifest m;
        if (meta_parse(&m, raw) == 0) {
            if (m.depends && depends_contain(m.depends, target)) {
                strncpy(out, names[i], (size_t)outsz - 1);
                out[outsz - 1] = '\0';
                meta_free(&m);
                free(raw);
                return out;
            }
            meta_free(&m);
        }
        free(raw);
    }
    return NULL;
}

int cmd_remove(const struct cp_config *cfg, int argc, char **argv,
               int force, int quiet)
{
    if (argc < 1) {
        cp_err("usage: cactpkg remove <package...>\n");
        return -1;
    }

    int rc = 0;
    for (int i = 0; i < argc; i++) {
        const char *name = argv[i];

        if (!db_has(cfg, name)) {
            cp_err("cactpkg: package %s is not installed\n", name);
            rc = -1;
            continue;
        }

        if (!force) {
            char dep[CP_NAME_MAX];
            if (find_dependent(cfg, name, dep, sizeof(dep)) != NULL) {
                cp_err("cactpkg: %s: required by %s "
                       "(remove it first, or use --force)\n", name, dep);
                rc = -1;
                continue;
            }
        }

        char *files_text = NULL;
        if (db_read_files(cfg, name, &files_text) != 0) {
            cp_err("cactpkg: %s: no file list in database\n", name);
            rc = -1;
            continue;
        }

        char *save = NULL;
        for (char *line = strtok_r(files_text, "\n", &save); line;
             line = strtok_r(NULL, "\n", &save)) {
            if (!*line)
                continue;
            if (unlink(line) != 0) {
                cp_err("cactpkg: %s: cannot remove %s\n", name, line);
            }
        }
        free(files_text);

        db_forget(cfg, name);
        if (!quiet)
            cp_out("cactpkg: removed: %s\n", name);
    }
    return rc;
}
