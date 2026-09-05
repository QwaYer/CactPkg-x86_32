/* util.c - low-level helpers: output, file read/write, paths, directories. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <stat.h>
#include <dirent.h>

#include "cactpkg.h"

void cp_out(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
}

void cp_err(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void *cp_read_file(const char *path, unsigned *out_len)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return NULL;

    int fd = open(path, O_RDONLY, 0);
    if (fd < 0)
        return NULL;

    unsigned len = (unsigned)st.st_size;
    unsigned char *buf = (unsigned char *)malloc(len ? len : 1);
    if (!buf) {
        close(fd);
        return NULL;
    }

    unsigned got = 0;
    while (got < len) {
        int n = (int)read(fd, buf + got, len - got);
        if (n <= 0) {
            free(buf);
            close(fd);
            return NULL;
        }
        got += (unsigned)n;
    }
    close(fd);

    if (out_len)
        *out_len = got;
    return buf;
}

int cp_write_file(const char *path, const void *data, unsigned len, unsigned mode)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0)
        return -1;

    unsigned written = 0;
    while (written < len) {
        int n = (int)write(fd, (const unsigned char *)data + written, len - written);
        if (n <= 0) {
            close(fd);
            return -1;
        }
        written += (unsigned)n;
    }
    close(fd);
    if (chmod(path, (int)mode) != 0)
        return -1;
    return 0;
}

/* create every component of an (absolute) path except the last one */
int cp_mkdir_p(const char *path, unsigned mode)
{
    if (!path || !*path)
        return -1;

    char tmp[CP_PATH_MAX];
    if ((size_t)strlen(path) >= sizeof(tmp))
        return -1;
    strcpy(tmp, path);

    size_t len = strlen(tmp);
    while (len > 1 && tmp[len - 1] == '/')
        tmp[--len] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (tmp[0] && mkdir(tmp, mode) != 0) {
                struct stat st;
                if (stat(tmp, &st) != 0 || !S_ISDIR(st.st_mode)) {
                    *p = '/';
                    return -1;
                }
            }
            *p = '/';
        }
    }
    return 0;
}

/* directory entry names (excluding "." and "..") */
int cp_dir_names(const char *path, char (*out)[CP_PATH_MAX], int max)
{
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0)
        return -1;

    struct dirent buf[16];
    int count = 0;

    for (;;) {
        int n = getdents(fd, buf, sizeof(buf));
        if (n <= 0)
            break;
        int entries = n / (int)sizeof(struct dirent);
        for (int i = 0; i < entries; i++) {
            const char *name = buf[i].d_name;
            if (!name[0] || strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
                continue;
            if (count < max) {
                strncpy(out[count], name, CP_PATH_MAX - 1);
                out[count][CP_PATH_MAX - 1] = '\0';
                count++;
            }
        }
    }
    close(fd);
    return count;
}

/* a + "/" + b; a may be empty */
char *cp_join(const char *a, const char *b)
{
    size_t la = a ? strlen(a) : 0;
    size_t lb = b ? strlen(b) : 0;
    int slash = (la > 0 && a[la - 1] == '/') ? 0 : (la > 0 ? 1 : 0);

    char *res = (char *)malloc(la + lb + (size_t)slash + 1);
    if (!res)
        return NULL;

    char *p = res;
    if (la) {
        memcpy(p, a, la);
        p += la;
    }
    if (slash)
        *p++ = '/';
    if (lb) {
        memcpy(p, b, lb);
        p += lb;
    }
    *p = '\0';
    return res;
}

void cp_trim(char *s)
{
    char *p = s;
    while (*p == ' ' || *p == '\t')
        p++;
    if (p != s)
        memmove(s, p, strlen(p) + 1);

    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
                       s[len - 1] == '\r' || s[len - 1] == '\n'))
        s[--len] = '\0';
}

int cp_has_suffix(const char *s, const char *suffix)
{
    size_t ls = strlen(s);
    size_t lf = strlen(suffix);
    if (lf > ls)
        return 0;
    return strcmp(s + ls - lf, suffix) == 0;
}

int cp_name_ok(const char *name)
{
    if (!name || !*name)
        return 0;
    for (const char *p = name; *p; p++) {
        char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'))
            return 0;
    }
    return 1;
}
