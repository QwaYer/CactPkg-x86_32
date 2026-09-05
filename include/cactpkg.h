/*
 * cactpkg.h - common definitions of the CactPkg package manager.
 *
 * CactPkg: the local package manager of CactOS. It works without any
 * external repository (those will be added later): the package source is
 * a repository directory holding .cpkg files and an index file.
 */

#ifndef CACTPKG_H
#define CACTPKG_H

#include <stddef.h>
#include <stdint.h>

#define CP_VERSION       "0.1.0"
#define CP_ARCH_DEFAULT  "i686"

#define CP_MAGIC         "CKPK"
#define CP_ARCH_VERSION  1u
#define CP_HDR_LEN       32u

#define CP_NAME_MAX      64
#define CP_PATH_MAX      256
#define CP_MAX_REPOS     8
#define CP_MAX_DEPTH     16

/* ---- package manifest ---- */
struct cp_manifest {
    char name[CP_NAME_MAX];
    char version[64];
    char arch[16];
    char *description;   /* strdup; may be NULL */
    char *depends;       /* raw string "a, b"; may be NULL */
};

/* ---- configuration ---- */
struct cp_config {
    char prefix[CP_PATH_MAX];           /* installation root */
    char db[CP_PATH_MAX];               /* installed-packages db dir */
    char repo[CP_MAX_REPOS][CP_PATH_MAX];
    int  nrepos;
    char arch[16];
};

/* ---- .cpkg archive ---- */
struct cpkg_entry {
    char name[CP_PATH_MAX];
    unsigned mode;
    unsigned size;
    const unsigned char *data;
};

struct cpkg {
    struct cp_manifest meta;
    char *manifest_raw;                 /* manifest text */
    struct cpkg_entry *entries;
    int nentries;
    unsigned char *buf;                 /* whole file held in memory */
    unsigned buflen;
    char src[CP_PATH_MAX];              /* file this package was loaded from */
};

/* ---- repository index ---- */
struct repo_pkg {
    char name[CP_NAME_MAX];
    char version[64];
    char file[CP_PATH_MAX];
    char *deps;                         /* raw string; may be NULL */
};

struct repo_index {
    char dir[CP_PATH_MAX];
    struct repo_pkg *pkgs;
    int npkgs;
    int cap;
};

/* util.c */
void cp_out(const char *fmt, ...);
void cp_err(const char *fmt, ...);
void *cp_read_file(const char *path, unsigned *out_len);
int  cp_write_file(const char *path, const void *data, unsigned len, unsigned mode);
int  cp_mkdir_p(const char *path, unsigned mode);
int  cp_dir_names(const char *path, char (*out)[CP_PATH_MAX], int max);
char *cp_join(const char *a, const char *b);          /* malloc: a + "/" + b */
void cp_trim(char *s);
int  cp_has_suffix(const char *s, const char *suffix);
int  cp_name_ok(const char *name);

/* config.c */
void cfg_defaults(struct cp_config *cfg);
int  cfg_load(struct cp_config *cfg, const char *path);   /* 0 ok, -1 parse error */
void cfg_add_repo(struct cp_config *cfg, const char *dir);

/* manifest.c */
void meta_init(struct cp_manifest *m);
void meta_free(struct cp_manifest *m);
int  meta_parse(struct cp_manifest *m, const char *text);  /* 0 ok, -1 error */

/* cpkg_io.c */
int  cpkg_load(const char *path, struct cpkg *p);
void cpkg_unload(struct cpkg *p);

/* repo.c */
int  repo_load(const char *dir, struct repo_index *ri);
void repo_unload(struct repo_index *ri);
const struct repo_pkg *repo_find(const struct repo_index *ri, const char *name);

/* db.c */
int  db_has(const struct cp_config *cfg, const char *name);
int  db_record(const struct cp_config *cfg, const char *name,
               const char *manifest_raw, const char *files_text);
int  db_forget(const struct cp_config *cfg, const char *name);
int  db_read_manifest(const struct cp_config *cfg, const char *name, char **raw);
int  db_read_files(const struct cp_config *cfg, const char *name, char **files_text);
int  db_list(const struct cp_config *cfg, char (*out)[CP_NAME_MAX], int max);

/* cmd_query.c */
int cmd_update(const struct cp_config *cfg);
int cmd_list(const struct cp_config *cfg);
int cmd_search(const struct cp_config *cfg, const char *needle);
int cmd_info(const struct cp_config *cfg, const char *name);
int cmd_files(const struct cp_config *cfg, const char *name);

/* cmd_install.c */
int cmd_install(const struct cp_config *cfg, int argc, char **argv,
                int force, int yes, int quiet);

/* cmd_remove.c */
int cmd_remove(const struct cp_config *cfg, int argc, char **argv,
               int force, int quiet);

#endif /* CACTPKG_H */
