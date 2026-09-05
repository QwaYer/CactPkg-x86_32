/* manifest.c - parser for the text manifest of a package. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cactpkg.h"

void meta_init(struct cp_manifest *m)
{
    memset(m, 0, sizeof(*m));
}

void meta_free(struct cp_manifest *m)
{
    free(m->description);
    free(m->depends);
    m->description = NULL;
    m->depends = NULL;
}

static int meta_required(const struct cp_manifest *m)
{
    if (!m->name[0] || !m->version[0] || !m->arch[0])
        return -1;
    if (!cp_name_ok(m->name))
        return -1;
    return 0;
}

int meta_parse(struct cp_manifest *m, const char *text)
{
    meta_init(m);

    char *copy = strdup(text);
    if (!copy)
        return -1;

    char *save = NULL;
    int rc = -1;

    for (char *line = strtok_r(copy, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        cp_trim(line);
        if (!*line || *line == '#')
            continue;

        char *colon = strchr(line, ':');
        char *eq = strchr(line, '=');
        char *sep = NULL;
        if (colon && (!eq || colon < eq))
            sep = colon;
        else if (eq)
            sep = eq;
        if (!sep)
            goto out;

        *sep = '\0';
        char *key = line;
        char *value = sep + 1;
        cp_trim(key);
        cp_trim(value);
        if (!*key)
            goto out;
        if (!*value)
            continue;               /* empty value: field is skipped */

        if (strcmp(key, "name") == 0) {
            if (strlen(value) >= CP_NAME_MAX)
                goto out;
            strcpy(m->name, value);
        } else if (strcmp(key, "version") == 0) {
            if ((size_t)strlen(value) >= sizeof(m->version))
                goto out;
            strcpy(m->version, value);
        } else if (strcmp(key, "arch") == 0) {
            if ((size_t)strlen(value) >= sizeof(m->arch))
                goto out;
            strcpy(m->arch, value);
        } else if (strcmp(key, "description") == 0) {
            free(m->description);
            m->description = strdup(value);
            if (!m->description)
                goto out;
        } else if (strcmp(key, "depends") == 0) {
            free(m->depends);
            m->depends = strdup(value);
            if (!m->depends)
                goto out;
        }
    }

    if (meta_required(m) != 0)
        goto out;
    rc = 0;

out:
    free(copy);
    if (rc != 0)
        meta_free(m);
    return rc;
}
