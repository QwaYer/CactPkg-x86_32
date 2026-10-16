/* net.c - minimal HTTP/1.1 GET client for remote CactPkg repositories.
 *
 * The repository index and .cpkg artifacts are pulled over the same path wget
 * uses: a TCP connection wrapped in a libc TLS 1.3 session (cact_tls_connect)
 * whose keys stay in this process - the kernel only verifies the server
 * certificate against the system CA bundle.  Remote repositories must be
 * https://; a plain http:// URL (or a redirect that drops to http) is refused,
 * because an unauthenticated index/artifact is a trivial MITM.  Bodies are
 * handled in Content-Length, chunked and read-to-close forms, and up to a few
 * redirects are followed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <socket.h>
#include <tls.h>

#include "cactpkg.h"

#define NET_UA         "cactpkg/" CP_VERSION
#define NET_REDIR_MAX  5
#define NET_BUF_SIZE   (16 * 1024)

int net_is_url(const char *s)
{
    return s && (strncmp(s, "http://", 7) == 0 ||
                 strncmp(s, "https://", 8) == 0);
}

/* "10.0.2.15" -> host byte order; 0 = ok, -1 = error. */
static int parse_ipv4(const char *s, uint32_t *out)
{
    uint32_t r = 0;
    int dots = 0, val = 0, has_digit = 0;
    while (*s) {
        if (*s >= '0' && *s <= '9') {
            val = val * 10 + (*s - '0');
            if (val > 255) return -1;
            has_digit = 1;
        } else if (*s == '.') {
            if (!has_digit) return -1;
            r = (r << 8) | (uint32_t)val;
            val = 0; has_digit = 0;
            if (++dots > 3) return -1;
        } else {
            return -1;
        }
        s++;
    }
    if (!has_digit || dots != 3) return -1;
    *out = (r << 8) | (uint32_t)val;
    return 0;
}

static void fill_sin(struct sockaddr_in *a, uint32_t ip_h, uint16_t port_h)
{
    memset(a, 0, sizeof(*a));
    a->sin_family = AF_INET;
    a->sin_port   = htons(port_h);
    a->sin_addr   = htonl(ip_h);
}

/* split "http[s]://HOST[:PORT][/PATH]"; 0 = ok. */
static int url_parse(const char *s, char *host, size_t hostsz, int *port,
                     int *is_tls, char *path, size_t pathsz)
{
    if (!s || !*s) return -1;
    int tls = 0;
    const char *p = s;
    if (strncmp(p, "https://", 8) == 0) { tls = 1; p += 8; }
    else if (strncmp(p, "http://", 7) == 0) { p += 7; }

    size_t hn = 0;
    while (*p && *p != ':' && *p != '/' && hn < hostsz - 1)
        host[hn++] = *p++;
    host[hn] = '\0';
    if (hn == 0) return -1;

    *is_tls = tls;
    *port = tls ? 443 : 80;
    if (*p == ':') {
        p++;
        int v = 0;
        while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
        if (v <= 0 || v > 65535) return -1;
        *port = v;
    }
    if (*p == '/') {
        size_t pn = 0;
        while (*p && pn < pathsz - 1) path[pn++] = *p++;
        path[pn] = '\0';
    } else {
        path[0] = '/'; path[1] = '\0';
    }
    return 0;
}

/* transport: a plain socket or a TLS session on top of it */
typedef struct { int fd; cact_tls_t *tls; } tr_t;

static int tr_read(tr_t *t, void *buf, int len)
{
    if (t->tls) {
        long n = cact_tls_read(t->tls, buf, (size_t)len);
        return n < 0 ? -1 : (int)n;
    }
    return recv(t->fd, buf, (size_t)len, 0);
}

static int tr_write_all(tr_t *t, const char *buf, int len)
{
    while (len > 0) {
        long n = t->tls ? cact_tls_write(t->tls, buf, (size_t)len)
                        : write(t->fd, buf, (size_t)len);
        if (n < 0) return -1;
        if (n == 0) {
            struct pollfd p;
            p.fd = t->fd; p.events = POLLOUT; p.revents = 0;
            poll(&p, 1, -1);
            continue;
        }
        buf += n; len -= (int)n;
    }
    return 0;
}

static void tr_close(tr_t *t)
{
    if (!t) return;
    if (t->tls) { cact_tls_close(t->tls); t->tls = NULL; }
    if (t->fd >= 0) close(t->fd);
    t->fd = -1;
}

/* Slice buffer over the response: the body surplus hides behind the header
 * end.  The headers (CSP, cookies, report-to) easily outgrow a stack array,
 * so the buffer lives on the heap. */
typedef struct {
    tr_t *tr;
    unsigned char *buf;
    int cap, start, end;
} wbuf_t;

static int wbuf_fill(wbuf_t *b)
{
    if (b->start < b->end) return 1;
    b->start = b->end = 0;
    int n = tr_read(b->tr, b->buf, b->cap);
    if (n <= 0) return n;
    b->end = n;
    return 1;
}

/* pull in more data: move the unread part to the front, then read */
static int wbuf_append(wbuf_t *b)
{
    if (b->start > 0) {
        memmove(b->buf, b->buf + b->start, (size_t)(b->end - b->start));
        b->end -= b->start;
        b->start = 0;
    }
    if (b->end >= b->cap) return -1;   /* header too long */
    int n = tr_read(b->tr, b->buf + b->end, b->cap - b->end);
    if (n <= 0) return n;
    b->end += n;
    return 1;
}

/* read a byte; 1 = ok, 0 = EOF, -1 = error */
static int wbuf_get(wbuf_t *b, unsigned char *out)
{
    if (b->start == b->end) {
        int rc = wbuf_fill(b);
        if (rc <= 0) return rc;
    }
    *out = b->buf[b->start++];
    return 1;
}

/* read a line (up to '\n'), length without CRLF; -1 on error */
static int wbuf_line(wbuf_t *b, char *out, int cap)
{
    int n = 0;
    for (;;) {
        unsigned char c;
        int rc = wbuf_get(b, &c);
        if (rc <= 0) return -1;
        if (c == '\n') return n;
        if (c != '\r' && n < cap - 1) out[n++] = (char)c;
    }
}

static int wbuf_readn(wbuf_t *b, unsigned char *out, int n)
{
    while (n > 0) {
        if (b->start == b->end) {
            int rc = wbuf_fill(b);
            if (rc <= 0) return -1;
        }
        int take = b->end - b->start;
        if (take > n) take = n;
        memcpy(out, b->buf + b->start, (size_t)take);
        b->start += take;
        out += take;
        n -= take;
    }
    return 0;
}

/* skip the CRLF that closes a chunk */
static int wbuf_skip_crlf(wbuf_t *b)
{
    unsigned char c;
    if (wbuf_get(b, &c) <= 0) return -1;
    if (c == '\r' && wbuf_get(b, &c) <= 0) return -1;
    return 0;
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int write_all(int fd, const unsigned char *buf, int len)
{
    while (len > 0) {
        int n = (int)write(fd, buf, (size_t)len);
        if (n <= 0) return -1;
        buf += n;
        len -= n;
    }
    return 0;
}

/* Read the response headers; fills code/clen/chunked/location.  Right after
 * the headers the buffered body remains.  0 = ok, -1 = error. */
static int read_headers(wbuf_t *b, int *code, long *clen, int *chunked,
                        char *loc, int loccap)
{
    *code = 0; *clen = -1; *chunked = 0; loc[0] = '\0';
    for (;;) {
        int found = -1, term = 0;
        for (int i = b->start; i + 3 < b->end; i++) {
            if (b->buf[i] == '\r' && b->buf[i + 1] == '\n' &&
                b->buf[i + 2] == '\r' && b->buf[i + 3] == '\n') {
                found = i; term = 4; break;
            }
        }
        if (found < 0) {
            for (int i = b->start; i + 1 < b->end; i++) {
                if (b->buf[i] == '\n' && b->buf[i + 1] == '\n') {
                    found = i; term = 2; break;
                }
            }
        }
        if (found >= 0) {
            int p = b->start, first = 1;
            while (p < found) {
                int q = p;
                while (q < found && b->buf[q] != '\n') q++;
                int len = q - p;
                if (len > 0 && b->buf[q - 1] == '\r') len--;
                if (len < 0) len = 0;
                if (first) {
                    int sp = p;                    /* "HTTP/1.x CODE ..." */
                    while (sp < p + len && b->buf[sp] != ' ') sp++;
                    if (sp < p + len)
                        *code = atoi((const char *)b->buf + sp + 1);
                    first = 0;
                } else {
                    char tmp[256];
                    int tl = len < (int)sizeof(tmp) - 1 ? len
                                                        : (int)sizeof(tmp) - 1;
                    memcpy(tmp, b->buf + p, (size_t)tl);
                    tmp[tl] = '\0';
                    char *colonp = strchr(tmp, ':');
                    if (colonp) {
                        *colonp = '\0';
                        char *valp = colonp + 1;
                        while (*valp == ' ') valp++;
                        if (strcasecmp(tmp, "content-length") == 0) {
                            *clen = atol(valp);
                        } else if (strcasecmp(tmp, "transfer-encoding") == 0) {
                            if (strcasestr(valp, "chunked")) *chunked = 1;
                        } else if (strcasecmp(tmp, "location") == 0) {
                            strncpy(loc, valp, (size_t)loccap - 1);
                            loc[loccap - 1] = '\0';
                            int ll = (int)strlen(loc);
                            while (ll > 0 && (loc[ll - 1] == '\r' ||
                                              loc[ll - 1] == ' '))
                                loc[--ll] = '\0';
                        }
                    }
                }
                p = q + 1;
            }
            b->start = found + term;
            return 0;
        }
        int rc = wbuf_append(b);
        if (rc <= 0) return -1;          /* error or EOF mid-headers */
    }
}

static int save_body(wbuf_t *b, int outfd, int chunked, long clen)
{
    if (chunked) {
        for (;;) {
            char line[32];
            int ln = wbuf_line(b, line, sizeof(line));
            if (ln < 0) return -1;
            int sz = 0;
            for (int i = 0; i < ln; i++) {
                int v = hexval((unsigned char)line[i]);
                if (v < 0) break;
                if (sz > 0x0FFFFFFF) return -1;
                sz = sz * 16 + v;
            }
            if (sz == 0) break;
            unsigned char tmp[1024];
            while (sz > 0) {
                int take = sz > (int)sizeof(tmp) ? (int)sizeof(tmp) : sz;
                if (wbuf_readn(b, tmp, take) != 0) return -1;
                if (write_all(outfd, tmp, take) != 0) return -1;
                sz -= take;
            }
            if (wbuf_skip_crlf(b) != 0) return -1;
        }
        return 0;
    }

    if (clen == 0) return 0;               /* empty body */
    unsigned char tmp[1024];
    for (;;) {
        if (b->start == b->end) {
            int rc = wbuf_fill(b);
            if (rc < 0) return -1;
            if (rc == 0) break;            /* read-to-close */
        }
        int take = b->end - b->start;
        if (clen >= 0 && take > clen) take = (int)clen;
        memcpy(tmp, b->buf + b->start, (size_t)take);
        b->start += take;
        if (write_all(outfd, tmp, take) != 0) return -1;
        if (clen >= 0) {
            clen -= take;
            if (clen == 0) break;
        }
    }
    return 0;
}

/* connect and, if needed, bring up a TLS session; NULL on error */
static tr_t *conn_open(const char *host, int port, int use_tls)
{
    uint32_t ip;
    if (parse_ipv4(host, &ip) != 0 && dns_resolve(host, &ip) != 0) {
        cp_err("cactpkg: cannot resolve %s\n", host);
        return NULL;
    }
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) return NULL;
    struct sockaddr_in dst;
    fill_sin(&dst, ip, (uint16_t)port);
    if (connect(fd, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
        close(fd);
        return NULL;
    }

    tr_t *tr = (tr_t *)calloc(1, sizeof(*tr));
    if (!tr) { close(fd); return NULL; }
    tr->fd = fd;
    tr->tls = NULL;

    if (use_tls) {
        tr->tls = cact_tls_connect(fd, host);
        if (!tr->tls) {
            cp_err("cactpkg: TLS with %s failed: %s\n", host,
                   cact_tls_error());
            tr_close(tr);
            free(tr);
            return NULL;
        }
    }
    return tr;
}

static int send_request(tr_t *tr, const char *host, int port, int tls,
                        const char *path)
{
    char portstr[8];
    char req[1600];
    int n;
    if (port != (tls ? 443 : 80)) {
        snprintf(portstr, sizeof(portstr), "%d", port);
        n = snprintf(req, sizeof(req),
                     "GET %s HTTP/1.1\r\n"
                     "Host: %s:%s\r\n"
                     "User-Agent: %s\r\n"
                     "Accept: */*\r\n"
                     "Connection: close\r\n\r\n",
                     path, host, portstr, NET_UA);
    } else {
        n = snprintf(req, sizeof(req),
                     "GET %s HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "User-Agent: %s\r\n"
                     "Accept: */*\r\n"
                     "Connection: close\r\n\r\n",
                     path, host, NET_UA);
    }
    if (n <= 0 || n >= (int)sizeof(req)) return -1;
    return tr_write_all(tr, req, n);
}

/* GET `url` and write the body to `dest` (created/truncated). 0 ok, -1 error.
 * Only https:// is accepted; http:// is refused. */
int net_fetch(const char *url, const char *dest)
{
    char host[160];
    char path[1024];
    int port = 443, tls = 1;

    if (strncmp(url, "https://", 8) != 0) {
        cp_err("cactpkg: refusing insecure URL %s (https required)\n", url);
        return -1;
    }
    if (url_parse(url, host, sizeof(host), &port, &tls, path, sizeof(path)) != 0) {
        cp_err("cactpkg: bad URL: %s\n", url);
        return -1;
    }

    for (int attempt = 0; attempt < NET_REDIR_MAX; attempt++) {
        tr_t *tr = conn_open(host, port, tls);
        if (!tr) {
            cp_err("cactpkg: cannot connect to %s\n", host);
            return -1;
        }
        if (send_request(tr, host, port, tls, path) != 0) {
            cp_err("cactpkg: %s: request failed\n", host);
            tr_close(tr); free(tr);
            return -1;
        }

        wbuf_t b;
        b.tr = tr;
        b.cap = NET_BUF_SIZE;
        b.buf = (unsigned char *)malloc((size_t)b.cap);
        b.start = b.end = 0;
        if (!b.buf) {
            cp_err("cactpkg: out of memory\n");
            tr_close(tr); free(tr);
            return -1;
        }

        int code, chunked;
        long clen;
        char loc[1024];
        if (read_headers(&b, &code, &clen, &chunked, loc, sizeof(loc)) != 0) {
            cp_err("cactpkg: %s: malformed HTTP response\n", host);
            free(b.buf); tr_close(tr); free(tr);
            return -1;
        }

        if (code == 301 || code == 302 || code == 303 ||
            code == 307 || code == 308) {
            free(b.buf); tr_close(tr); free(tr);
            if (!loc[0]) {
                cp_err("cactpkg: redirect without Location\n");
                return -1;
            }
            if (strncmp(loc, "https://", 8) == 0) {
                if (url_parse(loc, host, sizeof(host), &port, &tls,
                              path, sizeof(path)) != 0) {
                    cp_err("cactpkg: bad redirect Location\n");
                    return -1;
                }
            } else if (strncmp(loc, "http://", 7) == 0) {
                cp_err("cactpkg: refusing redirect to insecure %s\n", loc);
                return -1;
            } else if (loc[0] == '/') {
                strncpy(path, loc, sizeof(path) - 1);
                path[sizeof(path) - 1] = '\0';
            } else {
                cp_err("cactpkg: unsupported relative redirect\n");
                return -1;
            }
            continue;
        }

        if (code < 200 || code >= 300) {
            cp_err("cactpkg: %s: HTTP %d\n", host, code);
            free(b.buf); tr_close(tr); free(tr);
            return -1;
        }

        int fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            cp_err("cactpkg: cannot create %s\n", dest);
            free(b.buf); tr_close(tr); free(tr);
            return -1;
        }
        int rc = save_body(&b, fd, chunked, clen);
        close(fd);
        free(b.buf); tr_close(tr); free(tr);
        if (rc != 0) {
            unlink(dest);
            cp_err("cactpkg: %s: body read failed\n", host);
            return -1;
        }
        return 0;
    }

    cp_err("cactpkg: too many redirects for %s\n", url);
    return -1;
}
