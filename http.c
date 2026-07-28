/* darkstat 3
 * copyright (c) 2001-2016 Emil Mikulic.
 *
 * http.c: embedded webserver.
 * This borrows a lot of code from darkhttpd.
 *
 * You may use, modify and redistribute this file under the terms of the
 * GNU General Public License version 2. (see COPYING.GPL)
 */

#include "cdefs.h"
#include "config.h"
#include "conv.h"
#include "err.h"
#include "graph_db.h"
#include "hosts_db.h"
#include "http.h"
#include "now.h"
#include "opt.h"
#include "queue.h"
#include "str.h"

#include <sys/uio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>

static char *http_base_url = NULL;
static int http_base_len = 0;

static const char mime_type_xml[] = "text/xml";
static const char mime_type_html[] = "text/html; charset=us-ascii";
static const char mime_type_text_prometheus[] = "text/plain; version=0.0.4";
static const char mime_type_text_json[] = "application/json";
static const char mime_type_css[] = "text/css";
static const char mime_type_js[] = "text/javascript";
static const char mime_type_png[] = "image/png";
static const char encoding_identity[] = "identity";
static const char encoding_gzip[] = "gzip";

static const char server[] = PACKAGE_NAME "/" PACKAGE_VERSION;
static int idletime = 60;
#define MAX_REQUEST_LENGTH 4000

struct http_listener {
    int sock;
    int auth_required;
};

static struct http_listener *insocks = NULL;
static unsigned int insock_num = 0;

struct connection {
    LIST_ENTRY(connection) entries;

    int socket;
    struct sockaddr_storage client;
    time_t last_active_mono;
    enum {
        RECV_REQUEST,          /* receiving request */
        SEND_HEADER_AND_REPLY, /* try to send header+reply together */
        SEND_HEADER,           /* sending generated header */
        SEND_REPLY,            /* sending reply */
        DONE                   /* conn closed, need to remove from queue */
        } state;

    /* char request[request_length+1] is null-terminated */
    char *request;
    size_t request_length;
    int accept_gzip;
    int auth_required;

    /* request fields */
    char *method, *uri, *query; /* query can be NULL */

    char *header;
    const char *mime_type, *encoding, *header_extra;
    size_t header_length, header_sent;
    int header_dont_free, header_only, http_code;

    char *reply;
    int reply_dont_free;
    size_t reply_length, reply_sent;

    unsigned int total_sent; /* header + body = total, for logging */
};

static LIST_HEAD(conn_list_head, connection) connlist =
    LIST_HEAD_INITIALIZER(conn_list_head);

struct bindaddr_entry {
    STAILQ_ENTRY(bindaddr_entry) entries;
    const char *s;
};
static STAILQ_HEAD(bindaddrs_head, bindaddr_entry) bindaddrs =
    STAILQ_HEAD_INITIALIZER(bindaddrs);

static char *parse_field(const struct connection *conn, const char *field);

struct md5_ctx {
    uint32_t state[4];
    uint64_t bits;
    uint8_t buf[64];
};

static uint32_t md5_rotl(uint32_t x, uint32_t n)
{
    return (x << n) | (x >> (32 - n));
}

static uint32_t md5_f(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) | (~x & z);
}

static uint32_t md5_g(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & z) | (y & ~z);
}

static uint32_t md5_h(uint32_t x, uint32_t y, uint32_t z)
{
    return x ^ y ^ z;
}

static uint32_t md5_i(uint32_t x, uint32_t y, uint32_t z)
{
    return y ^ (x | ~z);
}

static uint32_t md5_load32(const uint8_t *p)
{
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void md5_store32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff);
    p[3] = (uint8_t)((v >> 24) & 0xff);
}

static void md5_transform(uint32_t state[4], const uint8_t block[64])
{
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t x[16];
    int i;

    for (i = 0; i < 16; i++)
        x[i] = md5_load32(block + i * 4);

#define MD5_STEP(f, a, b, c, d, x, t, s) \
    do { \
        a += f(b, c, d) + x + (uint32_t)(t); \
        a = md5_rotl(a, s); \
        a += b; \
    } while (0)

    MD5_STEP(md5_f, a, b, c, d, x[ 0], 0xd76aa478, 7);
    MD5_STEP(md5_f, d, a, b, c, x[ 1], 0xe8c7b756, 12);
    MD5_STEP(md5_f, c, d, a, b, x[ 2], 0x242070db, 17);
    MD5_STEP(md5_f, b, c, d, a, x[ 3], 0xc1bdceee, 22);
    MD5_STEP(md5_f, a, b, c, d, x[ 4], 0xf57c0faf, 7);
    MD5_STEP(md5_f, d, a, b, c, x[ 5], 0x4787c62a, 12);
    MD5_STEP(md5_f, c, d, a, b, x[ 6], 0xa8304613, 17);
    MD5_STEP(md5_f, b, c, d, a, x[ 7], 0xfd469501, 22);
    MD5_STEP(md5_f, a, b, c, d, x[ 8], 0x698098d8, 7);
    MD5_STEP(md5_f, d, a, b, c, x[ 9], 0x8b44f7af, 12);
    MD5_STEP(md5_f, c, d, a, b, x[10], 0xffff5bb1, 17);
    MD5_STEP(md5_f, b, c, d, a, x[11], 0x895cd7be, 22);
    MD5_STEP(md5_f, a, b, c, d, x[12], 0x6b901122, 7);
    MD5_STEP(md5_f, d, a, b, c, x[13], 0xfd987193, 12);
    MD5_STEP(md5_f, c, d, a, b, x[14], 0xa679438e, 17);
    MD5_STEP(md5_f, b, c, d, a, x[15], 0x49b40821, 22);

    MD5_STEP(md5_g, a, b, c, d, x[ 1], 0xf61e2562, 5);
    MD5_STEP(md5_g, d, a, b, c, x[ 6], 0xc040b340, 9);
    MD5_STEP(md5_g, c, d, a, b, x[11], 0x265e5a51, 14);
    MD5_STEP(md5_g, b, c, d, a, x[ 0], 0xe9b6c7aa, 20);
    MD5_STEP(md5_g, a, b, c, d, x[ 5], 0xd62f105d, 5);
    MD5_STEP(md5_g, d, a, b, c, x[10], 0x02441453, 9);
    MD5_STEP(md5_g, c, d, a, b, x[15], 0xd8a1e681, 14);
    MD5_STEP(md5_g, b, c, d, a, x[ 4], 0xe7d3fbc8, 20);
    MD5_STEP(md5_g, a, b, c, d, x[ 9], 0x21e1cde6, 5);
    MD5_STEP(md5_g, d, a, b, c, x[14], 0xc33707d6, 9);
    MD5_STEP(md5_g, c, d, a, b, x[ 3], 0xf4d50d87, 14);
    MD5_STEP(md5_g, b, c, d, a, x[ 8], 0x455a14ed, 20);
    MD5_STEP(md5_g, a, b, c, d, x[13], 0xa9e3e905, 5);
    MD5_STEP(md5_g, d, a, b, c, x[ 2], 0xfcefa3f8, 9);
    MD5_STEP(md5_g, c, d, a, b, x[ 7], 0x676f02d9, 14);
    MD5_STEP(md5_g, b, c, d, a, x[12], 0x8d2a4c8a, 20);

    MD5_STEP(md5_h, a, b, c, d, x[ 5], 0xfffa3942, 4);
    MD5_STEP(md5_h, d, a, b, c, x[ 8], 0x8771f681, 11);
    MD5_STEP(md5_h, c, d, a, b, x[11], 0x6d9d6122, 16);
    MD5_STEP(md5_h, b, c, d, a, x[14], 0xfde5380c, 23);
    MD5_STEP(md5_h, a, b, c, d, x[ 1], 0xa4beea44, 4);
    MD5_STEP(md5_h, d, a, b, c, x[ 4], 0x4bdecfa9, 11);
    MD5_STEP(md5_h, c, d, a, b, x[ 7], 0xf6bb4b60, 16);
    MD5_STEP(md5_h, b, c, d, a, x[10], 0xbebfbc70, 23);
    MD5_STEP(md5_h, a, b, c, d, x[13], 0x289b7ec6, 4);
    MD5_STEP(md5_h, d, a, b, c, x[ 0], 0xeaa127fa, 11);
    MD5_STEP(md5_h, c, d, a, b, x[ 3], 0xd4ef3085, 16);
    MD5_STEP(md5_h, b, c, d, a, x[ 6], 0x04881d05, 23);
    MD5_STEP(md5_h, a, b, c, d, x[ 9], 0xd9d4d039, 4);
    MD5_STEP(md5_h, d, a, b, c, x[12], 0xe6db99e5, 11);
    MD5_STEP(md5_h, c, d, a, b, x[15], 0x1fa27cf8, 16);
    MD5_STEP(md5_h, b, c, d, a, x[ 2], 0xc4ac5665, 23);

    MD5_STEP(md5_i, a, b, c, d, x[ 0], 0xf4292244, 6);
    MD5_STEP(md5_i, d, a, b, c, x[ 7], 0x432aff97, 10);
    MD5_STEP(md5_i, c, d, a, b, x[14], 0xab9423a7, 15);
    MD5_STEP(md5_i, b, c, d, a, x[ 5], 0xfc93a039, 21);
    MD5_STEP(md5_i, a, b, c, d, x[12], 0x655b59c3, 6);
    MD5_STEP(md5_i, d, a, b, c, x[ 3], 0x8f0ccc92, 10);
    MD5_STEP(md5_i, c, d, a, b, x[10], 0xffeff47d, 15);
    MD5_STEP(md5_i, b, c, d, a, x[ 1], 0x85845dd1, 21);
    MD5_STEP(md5_i, a, b, c, d, x[ 8], 0x6fa87e4f, 6);
    MD5_STEP(md5_i, d, a, b, c, x[15], 0xfe2ce6e0, 10);
    MD5_STEP(md5_i, c, d, a, b, x[ 6], 0xa3014314, 15);
    MD5_STEP(md5_i, b, c, d, a, x[13], 0x4e0811a1, 21);
    MD5_STEP(md5_i, a, b, c, d, x[ 4], 0xf7537e82, 6);
    MD5_STEP(md5_i, d, a, b, c, x[11], 0xbd3af235, 10);
    MD5_STEP(md5_i, c, d, a, b, x[ 2], 0x2ad7d2bb, 15);
    MD5_STEP(md5_i, b, c, d, a, x[ 9], 0xeb86d391, 21);

#undef MD5_STEP

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

static void md5_init(struct md5_ctx *ctx)
{
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
    ctx->bits = 0;
}

static void md5_update(struct md5_ctx *ctx, const uint8_t *data, size_t len)
{
    size_t have = (size_t)((ctx->bits >> 3) & 63);
    size_t need = 64 - have;

    ctx->bits += (uint64_t)len << 3;
    if (have != 0 && len >= need) {
        memcpy(ctx->buf + have, data, need);
        md5_transform(ctx->state, ctx->buf);
        data += need;
        len -= need;
        have = 0;
    }
    while (len >= 64) {
        md5_transform(ctx->state, data);
        data += 64;
        len -= 64;
    }
    if (len != 0)
        memcpy(ctx->buf + have, data, len);
}

static void md5_final(struct md5_ctx *ctx, uint8_t out[16])
{
    size_t have = (size_t)((ctx->bits >> 3) & 63);
    size_t pad = (have < 56) ? (56 - have) : (120 - have);
    uint8_t tail[128];
    uint64_t bits = ctx->bits;
    size_t i;

    tail[0] = 0x80;
    memset(tail + 1, 0, pad - 1);
    md5_update(ctx, tail, pad);

    for (i = 0; i < 8; i++)
        tail[i] = (uint8_t)((bits >> (8 * i)) & 0xff);
    md5_update(ctx, tail, 8);

    for (i = 0; i < 4; i++)
        md5_store32(out + i * 4, ctx->state[i]);
}

static void md5_hex(const char *s, char out[33])
{
    static const char hexdig[] = "0123456789abcdef";
    struct md5_ctx ctx;
    uint8_t digest[16];
    size_t i;

    md5_init(&ctx);
    md5_update(&ctx, (const uint8_t *)s, strlen(s));
    md5_final(&ctx, digest);
    for (i = 0; i < sizeof(digest); i++) {
        out[i * 2] = hexdig[digest[i] >> 4];
        out[i * 2 + 1] = hexdig[digest[i] & 0x0f];
    }
    out[32] = '\0';
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int consttime_eq(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    size_t i, n = MAX(la, lb);
    unsigned char diff = (unsigned char)(la ^ lb);

    for (i = 0; i < n; i++) {
        unsigned char ca = (i < la) ? (unsigned char)a[i] : 0;
        unsigned char cb = (i < lb) ? (unsigned char)b[i] : 0;
        diff |= (unsigned char)(ca ^ cb);
    }
    return diff == 0;
}

static int sockaddr_is_private_bindaddr(const struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        const struct sockaddr_in *sin = (const struct sockaddr_in *)sa;
        uint32_t ip = ntohl(sin->sin_addr.s_addr);

        if (ip == INADDR_ANY)
            return 0;
        if ((ip >> 24) == 127)
            return 1;
        if ((ip >> 24) == 10)
            return 1;
        if ((ip >> 16) == ((172 << 8) | 16) || (ip >> 16) == ((172 << 8) | 17) ||
            (ip >> 16) == ((172 << 8) | 18) || (ip >> 16) == ((172 << 8) | 19) ||
            (ip >> 16) == ((172 << 8) | 20) || (ip >> 16) == ((172 << 8) | 21) ||
            (ip >> 16) == ((172 << 8) | 22) || (ip >> 16) == ((172 << 8) | 23) ||
            (ip >> 16) == ((172 << 8) | 24) || (ip >> 16) == ((172 << 8) | 25) ||
            (ip >> 16) == ((172 << 8) | 26) || (ip >> 16) == ((172 << 8) | 27) ||
            (ip >> 16) == ((172 << 8) | 28) || (ip >> 16) == ((172 << 8) | 29) ||
            (ip >> 16) == ((172 << 8) | 30) || (ip >> 16) == ((172 << 8) | 31))
            return 1;
        if ((ip >> 16) == ((192 << 8) | 168))
            return 1;
        if ((ip >> 16) == ((169 << 8) | 254))
            return 1;
        return 0;
    }
    if (sa->sa_family == AF_INET6) {
        const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)sa;
        const uint8_t *addr = sin6->sin6_addr.s6_addr;

        if (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr))
            return 0;
        if (IN6_IS_ADDR_LOOPBACK(&sin6->sin6_addr))
            return 1;
        if ((addr[0] & 0xfe) == 0xfc)
            return 1; /* fc00::/7 unique local */
        if (addr[0] == 0xfe && (addr[1] & 0xc0) == 0x80)
            return 1; /* fe80::/10 link-local */
        return 0;
    }
    return 0;
}

static const char *parse_authorization_basic_password(const struct connection *conn)
{
    char *auth = parse_field(conn, "Authorization: Basic ");
    char *decoded, *colon, *password;
    size_t len, out_len, i, pos;
    int bad = 0;

    if (auth == NULL)
        return NULL;

    len = strlen(auth);
    out_len = (len / 4) * 3 + 3;
    decoded = xmalloc(out_len + 1);

    /* Small base64 decoder for Basic auth. */
    {
        int table[256];
        const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        int seen_pad = 0;
        memset(table, -1, sizeof(table));
        for (i = 0; alphabet[i] != '\0'; i++)
            table[(unsigned char)alphabet[i]] = (int)i;
        {
            unsigned int acc = 0, bits = 0;
            pos = 0;
            for (i = 0; i < len; i++) {
                unsigned char ch = (unsigned char)auth[i];
                int val;
                if (isspace(ch))
                    continue;
                if (ch == '=') {
                    seen_pad = 1;
                    break;
                }
                val = table[ch];
                if (val < 0 || seen_pad) {
                    bad = 1;
                    break;
                }
                acc = (acc << 6) | (unsigned int)val;
                bits += 6;
                if (bits >= 8) {
                    bits -= 8;
                    decoded[pos++] = (char)((acc >> bits) & 0xff);
                }
            }
            if (!bad) {
                for (; i < len; i++) {
                    unsigned char ch = (unsigned char)auth[i];
                    if (!isspace(ch) && ch != '=') {
                        bad = 1;
                        break;
                    }
                }
            }
            decoded[pos] = '\0';
        }
    }
    free(auth);

    if (bad) {
        free(decoded);
        return NULL;
    }

    colon = strchr(decoded, ':');
    if (colon == NULL) {
        free(decoded);
        return NULL;
    }
    password = xstrdup(colon + 1);
    free(decoded);
    return password;
}

static int http_check_auth(const struct connection *conn)
{
    char hashed[33];
    char *password;

    if (!conn->auth_required)
        return 1;
    if (opt_api_key_md5 == NULL || opt_api_key_md5[0] == '\0')
        return 0;

    password = (char *)parse_authorization_basic_password(conn);
    if (password == NULL)
        return 0;

    md5_hex(password, hashed);
    free(password);
    return consttime_eq(hashed, opt_api_key_md5);
}

/* ---------------------------------------------------------------------------
 * Decode URL by converting %XX (where XX are hexadecimal digits) to the
 * character it represents.  Don't forget to free the return value.
 */
static char *urldecode(const char *url)
{
    size_t i, len = strlen(url);
    char *out = xmalloc(len+1);
    int pos;

    for (i=0, pos=0; i<len; i++)
    {
        if (url[i] == '%' && i+2 < len &&
            isxdigit(url[i+1]) && isxdigit(url[i+2]))
        {
            /* decode %XX */
            #define HEX_TO_DIGIT(hex) ( \
                ((hex) >= 'A' && (hex) <= 'F') ? ((hex)-'A'+10): \
                ((hex) >= 'a' && (hex) <= 'f') ? ((hex)-'a'+10): \
                ((hex)-'0') )

            out[pos++] = HEX_TO_DIGIT(url[i+1]) * 16 +
                         HEX_TO_DIGIT(url[i+2]);
            i += 2;

            #undef HEX_TO_DIGIT
        }
        else
        {
            /* straight copy */
            out[pos++] = url[i];
        }
    }
    out[pos] = 0;
#if 0
    /* don't really need to realloc here - it's probably a performance hit */
    out = xrealloc(out, strlen(out)+1);  /* dealloc what we don't need */
#endif
    return (out);
}



/* ---------------------------------------------------------------------------
 * Consolidate slashes in-place by shifting parts of the string over repeated
 * slashes.
 */
static void consolidate_slashes(char *s)
{
    size_t left = 0, right = 0;
    int saw_slash = 0;

    assert(s != NULL);

    while (s[right] != '\0')
    {
        if (saw_slash)
        {
            if (s[right] == '/') right++;
            else
            {
                saw_slash = 0;
                s[left++] = s[right++];
            }
        }
        else
        {
            if (s[right] == '/') saw_slash++;
            s[left++] = s[right++];
        }
    }
    s[left] = '\0';
}



/* ---------------------------------------------------------------------------
 * Resolve /./ and /../ in a URI, returing a new, safe URI, or NULL if the URI
 * is invalid/unsafe.  Returned buffer needs to be deallocated.
 */
static char *make_safe_uri(char *uri)
{
    char **elem, *out;
    unsigned int slashes = 0, elements = 0;
    size_t urilen, i, j, pos;

    assert(uri != NULL);
    if (uri[0] != '/')
        return (NULL);
    consolidate_slashes(uri);
    urilen = strlen(uri);

    /* count the slashes */
    for (i=0, slashes=0; i<urilen; i++)
        if (uri[i] == '/') slashes++;

    /* make an array for the URI elements */
    elem = xmalloc(sizeof(*elem) * slashes);
    for (i=0; i<slashes; i++)
        elem[i] = (NULL);

    /* split by slashes and build elem[] array */
    for (i=1; i<urilen;)
    {
        /* look for the next slash */
        for (j=i; j<urilen && uri[j] != '/'; j++)
            ;

        /* process uri[i,j) */
        if ((j == i+1) && (uri[i] == '.'))
            /* "." */;
        else if ((j == i+2) && (uri[i] == '.') && (uri[i+1] == '.'))
        {
            /* ".." */
            if (elements == 0)
            {
                /*
                 * Unsafe string so free elem[].  All its elements are free
                 * at this point.
                 */
                free(elem);
                return (NULL);
            }
            else
            {
                elements--;
                free(elem[elements]);
            }
        }
        else elem[elements++] = split_string(uri, i, j);

        i = j + 1; /* uri[j] is a slash - move along one */
    }

    /* reassemble */
    out = xmalloc(urilen+1); /* it won't expand */
    pos = 0;
    for (i=0; i<elements; i++)
    {
        size_t delta = strlen(elem[i]);

        assert(pos <= urilen);
        out[pos++] = '/';

        assert(pos+delta <= urilen);
        memcpy(out+pos, elem[i], delta);
        free(elem[i]);
        pos += delta;
    }
    free(elem);

    if ((elements == 0) || (uri[urilen-1] == '/')) out[pos++] = '/';
    assert(pos <= urilen);
    out[pos] = '\0';

#if 0
    /* don't really need to do this and it's probably a performance hit: */
    /* shorten buffer if necessary */
    if (pos != urilen) out = xrealloc(out, strlen(out)+1);
#endif
    return (out);
}

/* ---------------------------------------------------------------------------
 * Allocate and initialize an empty connection.
 */
static struct connection *new_connection(void)
{
    struct connection *conn = xmalloc(sizeof(*conn));

    conn->socket = -1;
    memset(&conn->client, 0, sizeof(conn->client));
    conn->last_active_mono = now_mono();
    conn->request = NULL;
    conn->request_length = 0;
    conn->accept_gzip = 0;
    conn->auth_required = 0;
    conn->method = NULL;
    conn->uri = NULL;
    conn->query = NULL;
    conn->header = NULL;
    conn->mime_type = NULL;
    conn->encoding = NULL;
    conn->header_extra = "";
    conn->header_length = 0;
    conn->header_sent = 0;
    conn->header_dont_free = 0;
    conn->header_only = 0;
    conn->http_code = 0;
    conn->reply = NULL;
    conn->reply_dont_free = 0;
    conn->reply_length = 0;
    conn->reply_sent = 0;
    conn->total_sent = 0;

    /* Make it harmless so it gets garbage-collected if it should, for some
     * reason, fail to be correctly filled out.
     */
    conn->state = DONE;

    return (conn);
}



/* ---------------------------------------------------------------------------
 * Accept a connection from sockin and add it to the connection queue.
 */
static void accept_connection(const struct http_listener *listener)
{
    struct sockaddr_storage addrin;
    socklen_t sin_size;
    struct connection *conn;
    char ipaddr[INET6_ADDRSTRLEN], portstr[12];
    int sock;

    sin_size = (socklen_t)sizeof(addrin);
    sock = accept(listener->sock, (struct sockaddr *)&addrin, &sin_size);
    if (sock == -1)
    {
        if (errno == ECONNABORTED || errno == EINTR)
        {
            verbosef("accept() failed: %s", strerror(errno));
            return;
        }
        /* else */ err(1, "accept()");
    }

    fd_set_nonblock(sock);

    /* allocate and initialise struct connection */
    conn = new_connection();
    conn->socket = sock;
    conn->state = RECV_REQUEST;
    conn->auth_required = listener->auth_required;
    memcpy(&conn->client, &addrin, sizeof(conn->client));
    LIST_INSERT_HEAD(&connlist, conn, entries);

    getnameinfo((struct sockaddr *) &addrin, sin_size,
            ipaddr, sizeof(ipaddr), portstr, sizeof(portstr),
            NI_NUMERICHOST | NI_NUMERICSERV);
    verbosef("accepted connection from %s:%s", ipaddr, portstr);
}



/* ---------------------------------------------------------------------------
 * Log a connection, then cleanly deallocate its internals.
 */
static void free_connection(struct connection *conn)
{
    dverbosef("free_connection(%d)", conn->socket);
    if (conn->socket != -1)
        close(conn->socket);
    free(conn->request);
    free(conn->method);
    free(conn->uri);
    free(conn->query);
    if (!conn->header_dont_free)
        free(conn->header);
    if (!conn->reply_dont_free)
        free(conn->reply);
}



/* ---------------------------------------------------------------------------
 * Format [when] as an RFC1123 date, stored in the specified buffer.  The same
 * buffer is returned for convenience.
 */
#define DATE_LEN 30 /* strlen("Fri, 28 Feb 2003 00:02:08 GMT")+1 */
static char *rfc1123_date(char *dest, time_t when) {
    if (strftime(dest, DATE_LEN,
        "%a, %d %b %Y %H:%M:%S %Z", gmtime(&when) ) == 0)
            errx(1, "strftime() failed [%s]", dest);
    return dest;
}

static void generate_header(struct connection *conn,
    const int code, const char *text)
{
    char date[DATE_LEN];

    assert(conn->header == NULL);
    assert(conn->mime_type != NULL);
    if (conn->encoding == NULL)
        conn->encoding = encoding_identity;

    verbosef("http: %d %s (%s: %zu bytes)",
             code,
             text,
             conn->encoding,
             conn->reply_length);
    conn->header_length = xasprintf(&(conn->header),
        "HTTP/1.1 %d %s\r\n"
        "Date: %s\r\n"
        "Server: %s\r\n"
        "Vary: Accept-Encoding\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %qu\r\n"
        "Content-Encoding: %s\r\n"
        "X-Robots-Tag: noindex, noarchive\r\n"
        "%s"
        "\r\n",
        code, text,
        rfc1123_date(date, now_real()),
        server,
        conn->mime_type,
        (qu)conn->reply_length,
        conn->encoding,
        conn->header_extra);
    conn->http_code = code;
}



/* ---------------------------------------------------------------------------
 * A default reply for any (erroneous) occasion.
 */
static void default_reply(struct connection *conn,
    const int errcode, const char *errname, const char *format, ...)
    _printflike_(4, 5);
static void default_reply(struct connection *conn,
    const int errcode, const char *errname, const char *format, ...)
{
    char *reason;
    va_list va;

    va_start(va, format);
    xvasprintf(&reason, format, va);
    va_end(va);

    conn->reply_length = xasprintf(&(conn->reply),
     "<html><head><title>%d %s</title></head><body>\n"
     "<h1>%s</h1>\n" /* errname */
     "%s\n" /* reason */
     "<hr>\n"
     "Generated by %s"
     "</body></html>\n",
     errcode, errname, errname, reason, server);
    free(reason);

    /* forget any dangling metadata */
    conn->mime_type = mime_type_html;
    conn->encoding = encoding_identity;

    generate_header(conn, errcode, errname);
}



/* ---------------------------------------------------------------------------
 * Parses a single HTTP request field.  Returns string from end of [field] to
 * first \r, \n or end of request string.  Returns NULL if [field] can't be
 * matched.
 *
 * You need to remember to deallocate the result.
 * example: parse_field(conn, "Referer: ");
 */
static char *parse_field(const struct connection *conn, const char *field)
{
    size_t bound1, bound2;
    char *pos;

    /* find start */
    pos = strstr(conn->request, field);
    if (pos == NULL)
        return (NULL);
    bound1 = pos - conn->request + strlen(field);

    /* find end */
    for (bound2 = bound1;
        bound2 < conn->request_length &&
        conn->request[bound2] != '\r'; bound2++)
            ;

    /* copy to buffer */
    return (split_string(conn->request, bound1, bound2));
}



/* ---------------------------------------------------------------------------
 * Parse an HTTP request like "GET /hosts/?sort=in HTTP/1.1" to get the method
 * (GET), the uri (/hosts/), the query (sort=in) and whether the UA will
 * accept gzip encoding.  Remember to deallocate all these buffers.  Query
 * can be NULL.  The method will be returned in uppercase.
 */
static int parse_request(struct connection *conn)
{
    size_t bound1, bound2, mid;
    char *accept_enc;

    /* parse method */
    for (bound1 = 0; bound1 < conn->request_length &&
        conn->request[bound1] != ' '; bound1++)
            ;

    conn->method = split_string(conn->request, 0, bound1);
    strntoupper(conn->method, bound1);

    /* parse uri */
    for (; bound1 < conn->request_length &&
        conn->request[bound1] == ' '; bound1++)
            ;

    if (bound1 == conn->request_length)
        return (0); /* fail */

    for (bound2=bound1+1; bound2 < conn->request_length &&
        conn->request[bound2] != ' ' &&
        conn->request[bound2] != '\r'; bound2++)
            ;

    /* find query string */
    for (mid=bound1; mid<bound2 && conn->request[mid] != '?'; mid++)
        ;

    if (conn->request[mid] == '?') {
        conn->query = split_string(conn->request, mid+1, bound2);
        bound2 = mid;
    }

    conn->uri = split_string(conn->request, bound1, bound2);

    /* parse important fields */
    accept_enc = parse_field(conn, "Accept-Encoding: ");
    if (accept_enc != NULL) {
        if (strstr(accept_enc, "gzip") != NULL)
            conn->accept_gzip = 1;
        free(accept_enc);
    }
    return (1);
}

/* FIXME: maybe we need a smarter way of doing static pages: */

/* ---------------------------------------------------------------------------
 * Web interface: static stylesheet.
 */
static void
static_style_css(struct connection *conn)
{
#include "stylecss.h"

    conn->reply = (char*)style_css;
    conn->reply_length = style_css_len;
    conn->reply_dont_free = 1;
    conn->mime_type = mime_type_css;
}

/* ---------------------------------------------------------------------------
 * Web interface: static JavaScript.
 */
static void
static_graph_js(struct connection *conn)
{
#include "graphjs.h"

    conn->reply = (char*)graph_js;
    conn->reply_length = graph_js_len;
    conn->reply_dont_free = 1;
    conn->mime_type = mime_type_js;
}

/* ---------------------------------------------------------------------------
 * Web interface: favicon.
 */
static void
static_favicon(struct connection *conn)
{
#include "favicon.h"

    conn->reply = (char*)favicon_png;
    conn->reply_length = sizeof(favicon_png);
    conn->reply_dont_free = 1;
    conn->mime_type = mime_type_png;
}

/* ---------------------------------------------------------------------------
 * gzip a reply, if requested and possible.  Don't bother with a minimum
 * length requirement, I've never seen a page fail to compress.
 */
static void
process_gzip(struct connection *conn)
{
    char *buf;
    size_t len;
    z_stream zs;

    if (!conn->accept_gzip)
        return;

    buf = xmalloc(conn->reply_length);
    len = conn->reply_length;

    zs.zalloc = Z_NULL;
    zs.zfree = Z_NULL;
    zs.opaque = Z_NULL;

    if (deflateInit2(&zs,
                     Z_BEST_COMPRESSION,
                     Z_DEFLATED,
                     15+16, /* 15 = biggest window,
                               16 = add gzip header+trailer */
                     8 /* default */,
                     Z_DEFAULT_STRATEGY) != Z_OK) {
        free(buf);
        return;
    }

    zs.avail_in = conn->reply_length;
    zs.next_in = (unsigned char *)conn->reply;

    zs.avail_out = conn->reply_length;
    zs.next_out = (unsigned char *)buf;

    if (deflate(&zs, Z_FINISH) != Z_STREAM_END) {
        deflateEnd(&zs);
        free(buf);
        verbosef("failed to compress %zu bytes", len);
        return;
    }

    if (conn->reply_dont_free)
        conn->reply_dont_free = 0;
    else
        free(conn->reply);
    conn->reply = buf;
    conn->reply_length -= zs.avail_out;
    conn->encoding = encoding_gzip;
    deflateEnd(&zs);
}

/* ---------------------------------------------------------------------------
 * Process a GET/HEAD request
 */
static void process_get(struct connection *conn)
{
    char *safe_url;

    verbosef("http: %s \"%s\" %s", conn->method, conn->uri,
        (conn->query == NULL)?"":conn->query);

    {
        /* Decode the URL being requested. */
        char *decoded_url;
        char *decoded_url_offset;

        decoded_url = urldecode(conn->uri);

        /* Optionally strip the base. */
        decoded_url_offset = decoded_url;
        if (str_starts_with(decoded_url, http_base_url)) {
            decoded_url_offset += http_base_len - 1;
        }

        /* Make sure it's safe. */
        safe_url = make_safe_uri(decoded_url_offset);
        free(decoded_url);
        if (safe_url == NULL) {
            default_reply(conn, 400, "Bad Request",
                    "You requested an invalid URI: %s", conn->uri);
            return;
        }
    }

    if (strcmp(safe_url, "/") == 0) {
        struct str *buf = html_front_page();
        str_extract(buf, &(conn->reply_length), &(conn->reply));
        conn->mime_type = mime_type_html;
    }
    else if (str_starts_with(safe_url, "/hosts/")) {
        /* FIXME here - make this saner */
        struct str *buf = html_hosts(safe_url, conn->query);
        if (buf == NULL) {
            default_reply(conn, 404, "Not Found",
                "The page you requested could not be found.");
            free(safe_url);
            return;
        }
        str_extract(buf, &(conn->reply_length), &(conn->reply));
        conn->mime_type = mime_type_html;
    }
    else if (str_starts_with(safe_url, "/graphs.xml")) {
        struct str *buf = xml_graphs();
        str_extract(buf, &(conn->reply_length), &(conn->reply));
        conn->mime_type = mime_type_xml;
        /* hack around Opera caching the XML */
        conn->header_extra = "Pragma: no-cache\r\n";
    }
    else if (str_starts_with(safe_url, "/metrics")) {
        struct str *buf = text_metrics();
        str_extract(buf, &(conn->reply_length), &(conn->reply));
        conn->mime_type = mime_type_text_prometheus;
    }
    else if (str_starts_with(safe_url, "/json")) {
        struct str *buf = text_json(conn->query);
        str_extract(buf, &(conn->reply_length), &(conn->reply));
        conn->mime_type = mime_type_text_json;
    }
    else if (strcmp(safe_url, "/style.css") == 0)
        static_style_css(conn);
    else if (strcmp(safe_url, "/graph.js") == 0)
        static_graph_js(conn);
    else if (strcmp(safe_url, "/favicon.ico") == 0) {
        /* serves a PNG instead of an ICO, might cause problems for IE6 */
        static_favicon(conn);
    } else {
        default_reply(conn, 404, "Not Found",
            "The page you requested could not be found.");
        free(safe_url);
        return;
    }
    free(safe_url);

    process_gzip(conn);
    assert(conn->mime_type != NULL);
    generate_header(conn, 200, "OK");
}



/* ---------------------------------------------------------------------------
 * Process a request: build the header and reply, advance state.
 */
static void process_request(struct connection *conn)
{
    if (!http_check_auth(conn)) {
        conn->header_extra = "WWW-Authenticate: Basic realm=\"darkstat\"\r\n";
        default_reply(conn, 401, "Unauthorized",
            "Authentication is required to access this interface.");
        conn->state = SEND_HEADER_AND_REPLY;
        return;
    }

    if (!parse_request(conn))
    {
        default_reply(conn, 400, "Bad Request",
            "You sent a request that the server couldn't understand.");
    }
    else if (strcmp(conn->method, "GET") == 0)
    {
        process_get(conn);
    }
    else if (strcmp(conn->method, "HEAD") == 0)
    {
        process_get(conn);
        conn->header_only = 1;
    }
    else
    {
        default_reply(conn, 501, "Not Implemented",
            "The method you specified (%s) is not implemented.",
            conn->method);
    }

    /* advance state */
    if (conn->header_only)
        conn->state = SEND_HEADER;
    else
        conn->state = SEND_HEADER_AND_REPLY;
}



/* ---------------------------------------------------------------------------
 * Receiving request.
 */
static void poll_recv_request(struct connection *conn)
{
    char buf[65536];
    ssize_t recvd;

    recvd = recv(conn->socket, buf, sizeof(buf), 0);
    dverbosef("poll_recv_request(%d) got %d bytes", conn->socket, (int)recvd);
    if (recvd <= 0)
    {
        if (recvd == -1)
            verbosef("recv(%d) error: %s", conn->socket, strerror(errno));
        conn->state = DONE;
        return;
    }
    conn->last_active_mono = now_mono();

    /* append to conn->request */
    conn->request = xrealloc(conn->request, conn->request_length+recvd+1);
    memcpy(conn->request+conn->request_length, buf, (size_t)recvd);
    conn->request_length += recvd;
    conn->request[conn->request_length] = 0;

    /* die if it's too long */
    if (conn->request_length > MAX_REQUEST_LENGTH)
    {
        default_reply(conn, 413, "Request Entity Too Large",
            "Your request was dropped because it was too long.");
        conn->state = SEND_HEADER;
        return;
    }

    /* process request if we have all of it */
    if (conn->request_length > 4 &&
        memcmp(conn->request+conn->request_length-4, "\r\n\r\n", 4) == 0)
    {
        process_request(conn);

        /* request not needed anymore */
        free(conn->request);
        conn->request = NULL; /* important: don't free it again later */
    }
}



/* ---------------------------------------------------------------------------
 * Try to send header and [a part of the] reply in one packet.
 */
static void poll_send_header_and_reply(struct connection *conn)
{
    ssize_t sent;
    struct iovec iov[2];

    assert(!conn->header_only);
    assert(conn->reply_length > 0);
    assert(conn->header_sent == 0);

    assert(conn->reply_sent == 0);

    /* Fill out iovec */
    iov[0].iov_base = conn->header;
    iov[0].iov_len = conn->header_length;

    iov[1].iov_base = conn->reply;
    iov[1].iov_len = conn->reply_length;

    sent = writev(conn->socket, iov, 2);
    conn->last_active_mono = now_mono();

    /* handle any errors (-1) or closure (0) in send() */
    if (sent < 1) {
        if (sent == -1)
            verbosef("writev(%d) error: %s", conn->socket, strerror(errno));
        conn->state = DONE;
        return;
    }

    /* Figure out what we've sent. */
    conn->total_sent += (unsigned int)sent;
    if (sent < (ssize_t)conn->header_length) {
        verbosef("partially sent header");
        conn->header_sent = sent;
        conn->state = SEND_HEADER;
        return;
    }
    /* else */
    conn->header_sent = conn->header_length;
    sent -= conn->header_length;

    if (sent < (ssize_t)conn->reply_length) {
        verbosef("partially sent reply");
        conn->reply_sent += sent;
        conn->state = SEND_REPLY;
        return;
    }
    /* else */
    conn->reply_sent = conn->reply_length;
    conn->state = DONE;
}

/* ---------------------------------------------------------------------------
 * Sending header.  Assumes conn->header is not NULL.
 */
static void poll_send_header(struct connection *conn)
{
    ssize_t sent;

    sent = send(conn->socket, conn->header + conn->header_sent,
        conn->header_length - conn->header_sent, 0);
    conn->last_active_mono = now_mono();
    dverbosef("poll_send_header(%d) sent %d bytes", conn->socket, (int)sent);

    /* handle any errors (-1) or closure (0) in send() */
    if (sent < 1)
    {
        if (sent == -1)
            verbosef("send(%d) error: %s", conn->socket, strerror(errno));
        conn->state = DONE;
        return;
    }
    conn->header_sent += (unsigned int)sent;
    conn->total_sent += (unsigned int)sent;

    /* check if we're done sending */
    if (conn->header_sent == conn->header_length)
    {
        if (conn->header_only)
            conn->state = DONE;
        else
            conn->state = SEND_REPLY;
    }
}



/* ---------------------------------------------------------------------------
 * Sending reply.
 */
static void poll_send_reply(struct connection *conn)
{
    ssize_t sent;

    sent = send(conn->socket,
        conn->reply + conn->reply_sent,
        conn->reply_length - conn->reply_sent, 0);
    conn->last_active_mono = now_mono();
    dverbosef("poll_send_reply(%d) sent %d: [%d-%d] of %d",
        conn->socket, (int)sent,
        (int)conn->reply_sent,
        (int)(conn->reply_sent + sent - 1),
        (int)conn->reply_length);

    /* handle any errors (-1) or closure (0) in send() */
    if (sent < 1)
    {
        if (sent == -1)
            verbosef("send(%d) error: %s", conn->socket, strerror(errno));
        else if (sent == 0)
            verbosef("send(%d) closure", conn->socket);
        conn->state = DONE;
        return;
    }
    conn->reply_sent += (unsigned int)sent;
    conn->total_sent += (unsigned int)sent;

    /* check if we're done sending */
    if (conn->reply_sent == conn->reply_length) conn->state = DONE;
}



/* --------------------------------------------------------------------------
 * Initialize the base url.
 */
void http_init_base(const char *url) {
    char *slashed_url, *safe_url;
    size_t urllen;

    if (url == NULL) {
        http_base_url = strdup("/");
    } else {
        /* Make sure that the url has leading and trailing slashes. */
        urllen = strlen(url);
        slashed_url = xmalloc(urllen+3);
        slashed_url[0] = '/';
        memcpy(slashed_url+1, url, urllen); /* don't copy NUL */
        slashed_url[urllen+1] = '/';
        slashed_url[urllen+2] = '\0';

        /* Clean the url. */
        safe_url = make_safe_uri(slashed_url);
        free(slashed_url);
        if (safe_url == NULL) {
            verbosef("invalid base \"%s\", ignored", url);
            http_base_url = strdup("/"); /* set to default */
        } else {
            http_base_url = safe_url;
        }
    }
    http_base_len = strlen(http_base_url);
    verbosef("set base url to \"%s\"", http_base_url);
}

/* Use getaddrinfo to figure out what type of socket to create and
 * what to bind it to.  "bindaddr" can be NULL.  Remember to freeaddrinfo()
 * the result.
 */
static struct addrinfo *get_bind_addr(
    const char *bindaddr, const unsigned short bindport)
{
    struct addrinfo hints, *ai;
    char portstr[6];
    int ret;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    snprintf(portstr, sizeof(portstr), "%u", bindport);
    if ((ret = getaddrinfo(bindaddr, portstr, &hints, &ai)))
        err(1, "getaddrinfo(%s, %s) failed: %s",
            bindaddr ? bindaddr : "NULL", portstr, gai_strerror(ret));
    if (ai == NULL)
        err(1, "getaddrinfo() returned NULL pointer");
    return ai;
}

void http_add_bindaddr(const char *bindaddr)
{
    struct bindaddr_entry *ent;

    ent = xmalloc(sizeof(*ent));
    ent->s = bindaddr;
    STAILQ_INSERT_TAIL(&bindaddrs, ent, entries);
}

static void http_listen_one(struct addrinfo *ai,
    const unsigned short bindport)
{
    char ipaddr[INET6_ADDRSTRLEN];
    int sockin, sockopt, ret;

    /* format address into ipaddr string */
    if ((ret = getnameinfo(ai->ai_addr, ai->ai_addrlen, ipaddr,
                           sizeof(ipaddr), NULL, 0, NI_NUMERICHOST)) != 0)
        err(1, "getnameinfo failed: %s", gai_strerror(ret));

    /* create incoming socket */
    if ((sockin = socket(ai->ai_family, ai->ai_socktype,
            ai->ai_protocol)) == -1) {
        warn("http_listen_one(%s, %u): socket(%d (%s), %d, %d) failed",
          ipaddr, (unsigned int)bindport,
          ai->ai_family,
          (ai->ai_family == AF_INET6) ? "AF_INET6" :
          (ai->ai_family == AF_INET) ? "AF_INET" :
          "?",
          ai->ai_socktype,  ai->ai_protocol);
        return;
    }

    fd_set_nonblock(sockin);

    /* reuse address */
    sockopt = 1;
    if (setsockopt(sockin, SOL_SOCKET, SO_REUSEADDR,
            &sockopt, sizeof(sockopt)) == -1)
        err(1, "can't set SO_REUSEADDR");

#ifdef IPV6_V6ONLY
    /* explicitly disallow IPv4 mapped addresses since OpenBSD doesn't allow
     * dual stack sockets under any circumstances
     */
    if (ai->ai_family == AF_INET6) {
        sockopt = 1;
        if (setsockopt(sockin, IPPROTO_IPV6, IPV6_V6ONLY,
                &sockopt, sizeof(sockopt)) == -1)
            err(1, "can't set IPV6_V6ONLY");
    }
#endif

    /* bind socket */
    if (bind(sockin, ai->ai_addr, ai->ai_addrlen) == -1) {
        warn("bind(\"%s\") failed", ipaddr);
        close(sockin);
        return;
    }

    /* listen on socket */
    if (listen(sockin, 128) == -1)
        err(1, "listen() failed");

    verbosef("listening on http://%s%s%s:%u%s",
        (ai->ai_family == AF_INET6) ? "[" : "",
        ipaddr,
        (ai->ai_family == AF_INET6) ? "]" : "",
        bindport,
        http_base_url);

    /* add to insocks */
    insocks = xrealloc(insocks, sizeof(*insocks) * (insock_num + 1));
    insocks[insock_num].sock = sockin;
    insocks[insock_num].auth_required = !sockaddr_is_private_bindaddr(ai->ai_addr);
    insock_num++;
}

/* Initialize the http sockets and listen on them. */
void http_listen(const unsigned short bindport)
{
    /* If the user didn't specify any bind addresses, add a NULL.
     * This will become a wildcard.
     */
    if (STAILQ_EMPTY(&bindaddrs))
        http_add_bindaddr(NULL);

    /* Listen on every specified interface. */
    while (!STAILQ_EMPTY(&bindaddrs)) {
        struct bindaddr_entry *bindaddr = STAILQ_FIRST(&bindaddrs);
        struct addrinfo *ai, *ais = get_bind_addr(bindaddr->s, bindport);

        /* There could be multiple addresses returned, handle them all. */
        for (ai = ais; ai; ai = ai->ai_next)
            http_listen_one(ai, bindport);

        freeaddrinfo(ais);

        STAILQ_REMOVE_HEAD(&bindaddrs, entries);
        free(bindaddr);
    }

    if (insocks == NULL)
        errx(1, "was not able to bind any ports for http interface");

    if (opt_api_key_md5 == NULL || opt_api_key_md5[0] == '\0') {
        unsigned int i;
        for (i = 0; i < insock_num; i++) {
            if (insocks[i].auth_required) {
                warnx("authentication is required for public bind addresses, "
                      "but API_KEY is not configured; public listeners will "
                      "return 401 Unauthorized");
                break;
            }
        }
    }

    /* ignore SIGPIPE */
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        err(1, "can't ignore SIGPIPE");
}



/* ---------------------------------------------------------------------------
 * Set recv/send fd_sets and calculate timeout length.
 */
void
http_fd_set(fd_set *recv_set, fd_set *send_set, int *max_fd,
    struct timeval *timeout, int *need_timeout)
{
    struct connection *conn, *next;
    int minidle = idletime + 1;
    unsigned int i;

    #define MAX_FD_SET(sock, fdset) do { \
        FD_SET(sock, fdset); *max_fd = MAX(*max_fd, sock); } while(0)

    for (i=0; i<insock_num; i++)
        MAX_FD_SET(insocks[i].sock, recv_set);

    LIST_FOREACH_SAFE(conn, &connlist, entries, next)
    {
        int idlefor = now_mono() - conn->last_active_mono;

        /* Time out dead connections. */
        if (idlefor >= idletime) {
            char ipaddr[INET6_ADDRSTRLEN];
            /* FIXME: this is too late on FreeBSD, socket is invalid */
            int ret = getnameinfo((struct sockaddr *)&conn->client,
                sizeof(conn->client), ipaddr, sizeof(ipaddr),
                NULL, 0, NI_NUMERICHOST);
            if (ret == 0)
                verbosef("http socket timeout from %s (fd %d)",
                        ipaddr, conn->socket);
            else
                warn("http socket timeout: getnameinfo error: %s",
                    gai_strerror(ret));
            conn->state = DONE;
        }

        /* Connections that need a timeout. */
        if (conn->state != DONE)
            minidle = MIN(minidle, (idletime - idlefor));

        switch (conn->state)
        {
        case DONE:
            /* clean out stale connection */
            LIST_REMOVE(conn, entries);
            free_connection(conn);
            free(conn);
            break;

        case RECV_REQUEST:
            MAX_FD_SET(conn->socket, recv_set);
            break;

        case SEND_HEADER_AND_REPLY:
        case SEND_HEADER:
        case SEND_REPLY:
            MAX_FD_SET(conn->socket, send_set);
            break;

        default: errx(1, "invalid state");
        }
    }
    #undef MAX_FD_SET

    /* Only set timeout if cap hasn't already. */
    if ((*need_timeout == 0) && (minidle <= idletime)) {
        *need_timeout = 1;
        timeout->tv_sec = minidle;
        timeout->tv_usec = 0;
    }
}



/* ---------------------------------------------------------------------------
 * poll connections that select() says need attention
 */
void http_poll(fd_set *recv_set, fd_set *send_set)
{
    struct connection *conn;
    unsigned int i;

    for (i=0; i<insock_num; i++)
        if (FD_ISSET(insocks[i].sock, recv_set))
            accept_connection(&insocks[i]);

    LIST_FOREACH(conn, &connlist, entries)
    switch (conn->state)
    {
    case RECV_REQUEST:
        if (FD_ISSET(conn->socket, recv_set)) poll_recv_request(conn);
        break;

    case SEND_HEADER_AND_REPLY:
        if (FD_ISSET(conn->socket, send_set)) poll_send_header_and_reply(conn);
        break;

    case SEND_HEADER:
        if (FD_ISSET(conn->socket, send_set)) poll_send_header(conn);
        break;

    case SEND_REPLY:
        if (FD_ISSET(conn->socket, send_set)) poll_send_reply(conn);
        break;

    case DONE: /* fallthrough */
    default: errx(1, "invalid state");
    }
}

void http_stop(void) {
    struct connection *conn;
    struct connection *next;
    unsigned int i;

    free(http_base_url);

    /* Close listening sockets. */
    for (i=0; i<insock_num; i++)
        close(insocks[i].sock);
    free(insocks);
    insocks = NULL;

    /* Close in-flight connections. */
    LIST_FOREACH_SAFE(conn, &connlist, entries, next) {
        LIST_REMOVE(conn, entries);
        free_connection(conn);
        free(conn);
    }
}

/* vim:set ts=4 sw=4 et tw=78: */
