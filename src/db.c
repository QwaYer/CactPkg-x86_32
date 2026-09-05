/* db.c - database of installed packages in <db>/installed/. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stat.h>

#include "cactpkg.h"

static char *db_installed_dir(const struct cp_config *cfg)
{
    return cp_join(cfg->db, "installed");
}

static char *db_path(const struct cp_config *cfg, const char *name,
                     const char *suffix)
{
    char *dir = db_installed_dir(cfg);
    if (!dir)
        return NULL;
    char *stem = cp_join(dir, name);
    free(dir);
    if (!stem)
        return NULL;

    char *with_suffix = (char *)malloc(strlen(stem) + strlen(suffix) + 1);
    if (with_suffix) {
        strcpy(with_suffix, stem);
        strcat(with_suffix, suffix);
    }
    free(stem);
    return with_suffix;
}

static char *read_text(const char *path)
{
    unsigned len = 0;
    void *buf = cp_read_file(path, &len);
    if (!buf)
        return NULL;
    char *text = (char *)malloc(len + 1);
    if (!text) {
        free(buf);
        return NULL;
    }
    memcpy(text, buf, len);
    text[len] = '\0';
    free(buf);
    return text;
}

int db_has(const struct cp_config *cfg, const char *name)
{
    char *path = db_path(cfg, name, ".manifest");
    if (!path)
        return 0;
    int ok = (access(path, F_OK) == 0);
    free(path);
    return ok;
}

int db_record(const struct cp_config *cfg, const char *name,
              const char *manifest_raw, const char *files_text)
{
    char *dir = db_installed_dir(cfg);
    if (!dir)
        return -1;
    int rc = cp_mkdir_p(dir, 0755);
    if (rc == 0) {
        /* cp_mkdir_p creates parent components, but not the last one */
        if (mkdir(dir, 0755) != 0) {
            struct stat st;
            if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode))
                rc = -1;
        }
    }
    free(dir);
    if (rc != 0)
        return -1;

    char *mpath = db_path(cfg, name, ".manifest");
    char *fpath = db_path(cfg, name, ".files");
    if (!mpath || !fpath) {
        free(mpath);
        free(fpath);
        return -1;
    }

    int ok = cp_write_file(mpath, manifest_raw, (unsigned)strlen(manifest_raw), 0644);
    if (ok == 0)
        ok = cp_write_file(fpath, files_text, (unsigned)strlen(files_text), 0644);

    free(mpath);
    free(fpath);
    return ok;
}

int db_forget(const struct cp_config *cfg, const char *name)
{
    char *mpath = db_path(cfg, name, ".manifest");
    char *fpath = db_path(cfg, name, ".files");
    if (!mpath || !fpath) {
        free(mpath);
        free(fpath);
        return -1;
    }
    unlink(mpath);
    unlink(fpath);
    free(mpath);
    free(fpath);
    return 0;
}

int db_read_manifest(const struct cp_config *cfg, const char *name, char **raw)
{
    char *path = db_path(cfg, name, ".manifest");
    if (!path)
        return -1;
    char *text = read_text(path);
    free(path);
    if (!text)
        return -1;
    *raw = text;
    return 0;
}

int db_read_files(const struct cp_config *cfg, const char *name, char **files_text)
{
    char *path = db_path(cfg, name, ".files");
    if (!path)
        return -1;
    char *text = read_text(path);
    free(path);
    if (!text)
        return -1;
    *files_text = text;
    return 0;
}

int db_list(const struct cp_config *cfg, char (*out)[CP_NAME_MAX], int max)
{
    char *dir = db_installed_dir(cfg);
    if (!dir)
        return 0;

    char names[64][CP_PATH_MAX];
    int n = cp_dir_names(dir, names, 64);
    free(dir);
    if (n < 0)
        n = 0;

    int count = 0;
    for (int i = 0; i < n && count < max; i++) {
        const char *suffix = ".manifest";
        size_t ls = strlen(names[i]);
        size_t lf = strlen(suffix);
        if (ls <= lf || strcmp(names[i] + ls - lf, suffix) != 0)
            continue;
        size_t stem = ls - lf;
        memcpy(out[count], names[i], stem);
        out[count][stem] = '\0';
        count++;
    }
    return count;
}
