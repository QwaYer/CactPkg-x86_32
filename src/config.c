/* config.c - CactPkg configuration: /etc/cactpkg.conf + default values. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cactpkg.h"

void cfg_defaults(struct cp_config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    strcpy(cfg->prefix, "/usr/local");
    strcpy(cfg->db, "/var/lib/cactpkg");
    cfg->nrepos = 0;
    strcpy(cfg->arch, CP_ARCH_DEFAULT);
    cfg_add_repo(cfg, "/lib/cactpkg/repo");   /* offline repo baked into the image */
}

void cfg_add_repo(struct cp_config *cfg, const char *dir)
{
    if (!dir || !*dir || cfg->nrepos >= CP_MAX_REPOS)
        return;
    for (int i = 0; i < cfg->nrepos; i++)
        if (strcmp(cfg->repo[i], dir) == 0)
            return;
    strncpy(cfg->repo[cfg->nrepos], dir, CP_PATH_MAX - 1);
    cfg->repo[cfg->nrepos][CP_PATH_MAX - 1] = '\0';
    cfg->nrepos++;
}

static void cfg_set(struct cp_config *cfg, const char *key, const char *value)
{
    if (strcmp(key, "prefix") == 0) {
        strncpy(cfg->prefix, value, CP_PATH_MAX - 1);
        cfg->prefix[CP_PATH_MAX - 1] = '\0';
    } else if (strcmp(key, "db") == 0) {
        strncpy(cfg->db, value, CP_PATH_MAX - 1);
        cfg->db[CP_PATH_MAX - 1] = '\0';
    } else if (strcmp(key, "repo") == 0) {
        cfg_add_repo(cfg, value);
    } else if (strcmp(key, "arch") == 0) {
        strncpy(cfg->arch, value, 15);
        cfg->arch[15] = '\0';
    }
}

int cfg_load(struct cp_config *cfg, const char *path)
{
    unsigned len = 0;
    unsigned char *buf = (unsigned char *)cp_read_file(path, &len);
    if (!buf)
        return 0;               /* missing file is not an error - defaults apply */

    char *text = (char *)malloc(len + 1);
    if (!text) {
        free(buf);
        return -1;
    }
    memcpy(text, buf, len);
    text[len] = '\0';
    free(buf);

    char *save = NULL;
    for (char *line = strtok_r(text, "\n", &save); line;
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
            continue;

        *sep = '\0';
        char *key = line;
        char *value = sep + 1;
        cp_trim(key);
        cp_trim(value);
        if (*key && *value)
            cfg_set(cfg, key, value);
    }
    free(text);
    return 0;
}
