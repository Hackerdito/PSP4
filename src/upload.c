// upload.c - servidor HTTP de "Subir desde el celular". API: web/README.md.
//
// Reglas que vienen de la consola (las mismas que ya siguen netpc.c y library.c):
//   * Nada de stat(): el kernel llena un struct stat de FreeBSD 9 con st_size en otro lugar.
//     Los tamaños salen de path_size() (lseek al final).
//   * Las constantes de sockets del SDK ya son las de FreeBSD (SOL_SOCKET 0xffff), salvo
//     MSG_NOSIGNAL (trae la de Linux): por eso no se usa, y SIGPIPE se ignora.
//   * El espacio libre se pide con _fstatfs de libkernel y el struct statfs de FreeBSD 9;
//     el statvfs de la libc es el de musl/Linux y lee basura. Si la respuesta no es creíble se
//     informa 0 y la web dice "no disponible".
#include "upload.h"

#include "disc.h"
#include "library.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "web_assets.h"

#ifdef VAULT_HOST
#include <sys/statvfs.h>
#define GAMES_DIR   "testdata/data/vault/games"      // como /data/vault/games en la consola
#define TITLES_FILE "testdata/data/vault/titles.txt"
#define UPLOAD_LOG  "build/host/upload.log"
#else
#define GAMES_DIR   "/data/vault/games"
#define TITLES_FILE "/data/vault/titles.txt"
#define UPLOAD_LOG  "/data/vault/upload.log"
int sceNetInit(void);
int _fstatfs(int fd, void *buf);
#define SAM_SO_NOSIGPIPE 0x0800   // FreeBSD
#endif

#define STAGE_DIR    GAMES_DIR "/.subidas"
#define APP_VERSION  "1.9"
#define CHUNK_SIZE   (8u * 1024u * 1024u)
#define MAX_CHUNK    (16u * 1024u * 1024u)
#define MAX_WORKERS  6
#define MAX_TOKENS   8
#define MAX_SESSIONS 32
#define MAX_GAMES_API 512
#define HEADER_MAX   16384
#define JSON_MAX     8192

static UploadStatus g_st;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile int g_stop;
static int g_listen = -1;
static volatile int g_workers;

// ------------------------------------------------------------------ utilidades

static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static FILE *g_log;
static void ulog(const char *fmt, ...) {
    pthread_mutex_lock(&g_lock);
    if (!g_log) g_log = fopen(UPLOAD_LOG, "a");
    if (g_log) {
        va_list ap;
        va_start(ap, fmt);
        fprintf(g_log, "[%llu] ", (unsigned long long)(now_us() / 1000));
        vfprintf(g_log, fmt, ap);
        fputc('\n', g_log);
        fflush(g_log);
        va_end(ap);
    }
    pthread_mutex_unlock(&g_lock);
}

// splitmix64: suficiente para un PIN y un token en la red de casa (no protege secretos).
static uint64_t g_rng;
static uint64_t rnd(void) {
    if (!g_rng) g_rng = now_us() ^ ((uint64_t)(uintptr_t)&g_rng << 13) ^ 0x9E3779B97F4A7C15ull;
    uint64_t z = (g_rng += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static uint64_t fnv1a(const char *s) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (; *s; s++) { h ^= (uint8_t)*s; h *= 0x100000001b3ull; }
    return h;
}

static uint32_t g_crc_table[256];
static void crc_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        g_crc_table[i] = c;
    }
}
static uint32_t crc32_buf(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) c = g_crc_table[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static void mkdirs(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(tmp, 0777); *p = '/'; }
    mkdir(tmp, 0777);
}

static int ends_ci(const char *s, const char *ext) {
    size_t a = strlen(s), b = strlen(ext);
    return a >= b && strcasecmp(s + a - b, ext) == 0;
}

// Un nombre de archivo seguro: sin carpetas, sin "..", sin caracteres de control.
static int safe_name(const char *in, char *out, size_t out_len) {
    const char *base = in;
    for (const char *p = in; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
    size_t o = 0;
    for (const char *p = base; *p && o + 1 < out_len; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch < 0x20 || ch == 0x7f || strchr(":*?\"<>|", ch)) continue;
        out[o++] = (char)ch;
    }
    out[o] = 0;
    while (o && (out[o - 1] == ' ' || out[o - 1] == '.')) out[--o] = 0;
    return o > 0 && strcmp(out, ".") != 0 && strcmp(out, "..") != 0 && out[0] != '.';
}

// ------------------------------------------------------------------ espacio libre

static void disk_space(uint64_t *free_b, uint64_t *total_b) {
    *free_b = *total_b = 0;
#ifdef VAULT_HOST
    struct statvfs v;
    if (statvfs(GAMES_DIR, &v) == 0) {
        *free_b = (uint64_t)v.f_bavail * v.f_frsize;
        *total_b = (uint64_t)v.f_blocks * v.f_frsize;
    }
#else
    // struct statfs de FreeBSD 9: f_bsize en +16, f_blocks en +32, f_bavail en +48.
    // ⚠ POR VERIFICAR EN CONSOLA. Si los números no son creíbles se informa 0 (no disponible).
    int fd = open(GAMES_DIR, O_RDONLY);
    if (fd < 0) return;
    uint64_t buf[128];
    memset(buf, 0, sizeof buf);
    int rc = _fstatfs(fd, buf);
    close(fd);
    if (rc != 0) return;
    uint64_t bsize = buf[2], blocks = buf[4];
    int64_t bavail = (int64_t)buf[6];
    if (bsize < 512 || bsize > (1u << 20) || (bsize & (bsize - 1)) || !blocks || bavail < 0 || (uint64_t)bavail > blocks) return;
    *free_b = (uint64_t)bavail * bsize;
    *total_b = blocks * bsize;
#endif
}

// ------------------------------------------------------------------ títulos personalizados

const char *upload_games_dir(void) { return GAMES_DIR; }
const char *upload_titles_file(void) { return TITLES_FILE; }

int upload_title_override(const char *path, char *out, int out_len) {
    FILE *f = fopen(TITLES_FILE, "r");
    if (!f) return 0;
    char line[1024];
    int found = 0;
    size_t pl = strlen(path);
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (strncmp(line, path, pl) == 0 && line[pl] == '\t') {
            snprintf(out, (size_t)out_len, "%s", line + pl + 1);
            found = 1;   // la última línea gana
        }
    }
    fclose(f);
    return found;
}

// Reescribe el archivo sin la ruta y, si title no es NULL, con su nuevo título al final.
static void titles_set(const char *path, const char *title) {
    char tmp[600];
    snprintf(tmp, sizeof tmp, "%s.tmp", TITLES_FILE);
    FILE *in = fopen(TITLES_FILE, "r"), *out = fopen(tmp, "w");
    if (!out) { if (in) fclose(in); return; }
    size_t pl = strlen(path);
    if (in) {
        char line[1024];
        while (fgets(line, sizeof line, in)) {
            if (strncmp(line, path, pl) == 0 && line[pl] == '\t') continue;
            fputs(line, out);
        }
        fclose(in);
    }
    if (title) fprintf(out, "%s\t%s\n", path, title);
    fclose(out);
    rename(tmp, TITLES_FILE);
}

// ------------------------------------------------------------------ juegos en la carpeta

typedef struct {
    char id[20];
    char path[512];
    char title[160];
    char title_id[16];
    int ps1;
    const char *format;   // "iso" "cso" "pbp" "chd" "bincue"
    uint64_t size;        // bytes en disco (un .cue suma sus .bin)
} ApiGame;

// Archivos .bin que nombra un .cue (en su misma carpeta).
static int cue_bins(const char *cue_path, char bins[][256], int max) {
    FILE *f = fopen(cue_path, "r");
    if (!f) return 0;
    int n = 0;
    char line[512];
    while (n < max && fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (strncasecmp(p, "FILE", 4) != 0) continue;
        char *q1 = strchr(p, '"'), *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
        if (!q1 || !q2) continue;
        *q2 = 0;
        char clean[256];
        if (safe_name(q1 + 1, clean, sizeof clean)) snprintf(bins[n++], 256, "%s", clean);
    }
    fclose(f);
    return n;
}

static void dir_of(const char *path, char *out, size_t len) {
    snprintf(out, len, "%s", path);
    char *s = strrchr(out, '/');
    if (s) *s = 0;
}

static void base_no_ext(const char *path, char *out, size_t len) {
    const char *b = strrchr(path, '/');
    snprintf(out, len, "%s", b ? b + 1 : path);
    char *dot = strrchr(out, '.');
    if (dot) *dot = 0;
}

static void inspect(const char *path, ApiGame *g) {
    memset(g, 0, sizeof *g);
    // La misma lectura que la biblioteca de la TV: títulos, ID y sección coinciden en las dos.
    Game *lg = calloc(1, sizeof *lg);
    snprintf(g->path, sizeof g->path, "%s", path);
    snprintf(g->id, sizeof g->id, "%016llx", (unsigned long long)fnv1a(path));
    if (!lg) return;
    library_probe(path, lg);
    snprintf(g->title, sizeof g->title, "%s", lg->title);
    snprintf(g->title_id, sizeof g->title_id, "%s", lg->disc_id);
    g->ps1 = lg->is_ps1;
    g->size = lg->file_size;
    switch (lg->fmt) {
    case DISC_CSO: g->format = "cso"; break;
    case DISC_PBP: g->format = "pbp"; break;
    case DISC_CHD: g->format = "chd"; break;
    case DISC_CUE: case DISC_BIN: g->format = "bincue"; break;
    default: g->format = "iso"; break;
    }
    if (lg->fmt == DISC_CUE) {   // el tamaño de todo el juego: el .cue y sus pistas
        char bins[16][256], dir[512], full[800];
        int n = cue_bins(path, bins, 16);
        dir_of(path, dir, sizeof dir);
        g->size = path_size(path);
        for (int i = 0; i < n; i++) {
            snprintf(full, sizeof full, "%s/%s", dir, bins[i]);
            g->size += path_size(full);
        }
    }
    free(lg);
}

// Recorre la carpeta de juegos (2 niveles, igual que la biblioteca) y llama a cb por juego.
typedef int (*GameCb)(const char *path, void *user);

static int dir_has_cue(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    int found = 0;
    while (!found && (e = readdir(d))) found = ends_ci(e->d_name, ".cue");
    closedir(d);
    return found;
}

static int walk(const char *dir, int depth, GameCb cb, void *user) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    int cue_here = dir_has_cue(dir);
    struct dirent *e;
    int stop = 0;
    while (!stop && (e = readdir(d))) {
        if (e->d_name[0] == '.') continue;   // también .subidas
        char full[800];
        snprintf(full, sizeof full, "%s/%s", dir, e->d_name);
        if (path_is_dir(full)) {
            if (depth < 2) stop = walk(full, depth + 1, cb, user);
            continue;
        }
        int fmt = disc_format_from_name(e->d_name);
        if (fmt < 0 || strcasecmp(e->d_name, "PARAM.PBP") == 0) continue;   // PARAM.PBP: datos de una actualización
        if (fmt == DISC_BIN && cue_here) continue;   // va con su .cue
        stop = cb(full, user);
    }
    closedir(d);
    return stop;
}

typedef struct { ApiGame *games; int count, max; } GameList;
static int collect_cb(const char *path, void *user) {
    GameList *l = user;
    if (l->count >= l->max) return 1;
    inspect(path, &l->games[l->count++]);
    return 0;
}

typedef struct { const char *id; char path[800]; int found; } FindId;
static int find_cb(const char *path, void *user) {
    FindId *f = user;
    char id[20];
    snprintf(id, sizeof id, "%016llx", (unsigned long long)fnv1a(path));
    if (strcmp(id, f->id) == 0) { snprintf(f->path, sizeof f->path, "%s", path); f->found = 1; return 1; }
    return 0;
}
static int find_game(const char *id, char *path, size_t len) {
    FindId f = { .id = id };
    walk(GAMES_DIR, 0, find_cb, &f);
    if (f.found) snprintf(path, len, "%s", f.path);
    return f.found;
}

// ------------------------------------------------------------------ JSON mínimo

typedef struct { char *buf; size_t len, cap; } Str;
static void sput(Str *s, const char *t, size_t n) {
    if (s->len + n + 1 > s->cap) {
        size_t nc = s->cap ? s->cap * 2 : 1024;
        while (nc < s->len + n + 1) nc *= 2;
        char *nb = realloc(s->buf, nc);
        if (!nb) return;
        s->buf = nb;
        s->cap = nc;
    }
    memcpy(s->buf + s->len, t, n);
    s->len += n;
    s->buf[s->len] = 0;
}
static void sprintf_s(Str *s, const char *fmt, ...) {
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n > 0) sput(s, tmp, (size_t)n < sizeof tmp ? (size_t)n : sizeof tmp - 1);
}
static void sjson(Str *s, const char *v) {
    sput(s, "\"", 1);
    for (const unsigned char *p = (const unsigned char *)v; *p; p++) {
        char esc[8];
        if (*p == '"' || *p == '\\') { esc[0] = '\\'; esc[1] = (char)*p; sput(s, esc, 2); }
        else if (*p < 0x20) { snprintf(esc, sizeof esc, "\\u%04x", *p); sput(s, esc, 6); }
        else sput(s, (const char *)p, 1);
    }
    sput(s, "\"", 1);
}

// Lee un valor de texto de un objeto JSON plano: {"clave": "valor", ...}. 1 si lo encontró.
static int json_str(const char *body, const char *key, char *out, size_t out_len) {
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(body, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p++ != ':') return 0;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p++ != '"') return 0;
    size_t o = 0;
    while (*p && *p != '"' && o + 4 < out_len) {
        if (*p == '\\' && p[1]) {
            p++;
            if (*p == 'u' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2]) &&
                isxdigit((unsigned char)p[3]) && isxdigit((unsigned char)p[4])) {
                char hx[5] = { p[1], p[2], p[3], p[4], 0 };
                unsigned cp = (unsigned)strtoul(hx, NULL, 16);
                p += 5;
                if (cp < 0x80) out[o++] = (char)cp;
                else if (cp < 0x800) { out[o++] = (char)(0xC0 | (cp >> 6)); out[o++] = (char)(0x80 | (cp & 0x3F)); }
                else { out[o++] = (char)(0xE0 | (cp >> 12)); out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[o++] = (char)(0x80 | (cp & 0x3F)); }
                continue;
            }
            char c = *p++;
            out[o++] = c == 'n' ? '\n' : c == 't' ? '\t' : c == 'r' ? '\r' : c;
            continue;
        }
        out[o++] = *p++;
    }
    out[o] = 0;
    return *p == '"';
}

static int json_u64(const char *body, const char *key, uint64_t *out) {
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(body, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == ':' || *p == '\t') p++;
    if (!isdigit((unsigned char)*p)) return 0;
    *out = strtoull(p, NULL, 10);
    return 1;
}

static void game_json(Str *s, const ApiGame *g) {
    sput(s, "{\"id\":", 6); sjson(s, g->id);
    sput(s, ",\"title\":", 9); sjson(s, g->title);
    sput(s, ",\"titleId\":", 11); sjson(s, g->title_id);
    sprintf_s(s, ",\"system\":\"%s\",\"format\":\"%s\",\"sizeBytes\":%llu", g->ps1 ? "ps1" : "psp", g->format,
              (unsigned long long)g->size);
    sprintf_s(s, ",\"coverUrl\":\"/api/games/%s/cover\",\"addedAt\":\"\"}", g->id);
}

// ------------------------------------------------------------------ HTTP

typedef struct {
    int fd;
    char method[8];
    char path[600];
    char query[200];
    uint64_t content_length;
    char auth[80];
    char crc[16];
    int gzip_ok;
    char *head_extra;      // bytes del cuerpo que llegaron junto con las cabeceras
    size_t head_extra_len;
    uint64_t body_left;    // bytes del cuerpo que todavía no se leyeron
} Req;

static void drain_body(Req *r);

static int send_all(int fd, const void *buf, size_t n) {
    const char *p = buf;
    while (n) {
        ssize_t w = send(fd, p, n, 0);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return 0;
        p += w;
        n -= (size_t)w;
    }
    return 1;
}

static const char *status_text(int code) {
    switch (code) {
    case 200: return "OK"; case 204: return "No Content"; case 400: return "Bad Request";
    case 401: return "Unauthorized"; case 404: return "Not Found"; case 405: return "Method Not Allowed";
    case 409: return "Conflict"; case 413: return "Payload Too Large"; case 415: return "Unsupported Media Type";
    case 422: return "Unprocessable Entity"; case 429: return "Too Many Requests"; case 500: return "Internal Server Error";
    case 503: return "Service Unavailable"; case 507: return "Insufficient Storage";
    default: return "OK";
    }
}

static void respond(Req *r, int code, const char *type, const void *body, size_t len, const char *extra_headers) {
    // Si se responde antes de leer el cuerpo (un 409 a un bloque, por ejemplo), hay que leerlo y
    // descartarlo: cerrar con datos sin leer manda un RST y el navegador ve "error de red" en vez
    // de la respuesta.
    drain_body(r);
    char h[512];
    int n = snprintf(h, sizeof h,
                     "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\n"
                     "Connection: close\r\n%s\r\n",
                     code, status_text(code), type ? type : "application/octet-stream", len,
                     extra_headers ? extra_headers : "");
    send_all(r->fd, h, (size_t)n);
    if (len && body && strcmp(r->method, "HEAD") != 0) send_all(r->fd, body, len);
}

static void respond_json(Req *r, int code, const char *json) {
    respond(r, code, "application/json; charset=utf-8", json, strlen(json), NULL);
}
static void respond_err(Req *r, int code, const char *err) {
    char b[160];
    snprintf(b, sizeof b, "{\"error\":\"%s\"}", err);
    respond_json(r, code, b);
}

// Lee exactamente n bytes del cuerpo (primero lo que llegó con las cabeceras).
static int recv_body(Req *r, uint8_t *dst, size_t n) {
    size_t got = 0;
    if (n > r->body_left) return 0;
    r->body_left -= n;
    if (r->head_extra_len) {
        size_t take = r->head_extra_len < n ? r->head_extra_len : n;
        memcpy(dst, r->head_extra, take);
        r->head_extra += take;
        r->head_extra_len -= take;
        got = take;
    }
    while (got < n) {
        ssize_t k = recv(r->fd, dst + got, n - got, 0);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) return 0;
        got += (size_t)k;
    }
    return 1;
}

static void drain_body(Req *r) {
    if (!r->body_left || r->body_left > MAX_CHUNK + 1024u * 1024u) return;   // más grande: se cierra y ya
    uint8_t tmp[16384];
    while (r->body_left) {
        size_t take = r->body_left < sizeof tmp ? (size_t)r->body_left : sizeof tmp;
        if (!recv_body(r, tmp, take)) { r->body_left = 0; return; }
    }
}

static int read_json_body(Req *r, char *out, size_t cap) {
    if (r->content_length >= cap) return 0;
    if (!recv_body(r, (uint8_t *)out, (size_t)r->content_length)) return 0;
    out[r->content_length] = 0;
    return 1;
}

// ------------------------------------------------------------------ sesiones

static char g_tokens[MAX_TOKENS][36];
static int g_bad_pins;
static uint64_t g_pin_locked_until;

typedef struct {
    int used;
    char id[20];
    char name[256];       // nombre limpio del archivo
    uint64_t size;
    int ps1;
    char part[600];       // archivo temporal
} Session;
static Session g_sess[MAX_SESSIONS];

static int authorized(Req *r) {
    if (strncmp(r->auth, "Bearer ", 7) != 0) return 0;
    const char *t = r->auth + 7;
    pthread_mutex_lock(&g_lock);
    int ok = 0;
    for (int i = 0; i < MAX_TOKENS && !ok; i++) ok = g_tokens[i][0] && strcmp(g_tokens[i], t) == 0;
    pthread_mutex_unlock(&g_lock);
    return ok;
}

static Session *session_find(const char *id) {
    for (int i = 0; i < MAX_SESSIONS; i++)
        if (g_sess[i].used && strcmp(g_sess[i].id, id) == 0) return &g_sess[i];
    return NULL;
}

// Dónde queda el juego al completar. Un EBOOT.PBP va en su propia carpeta (la biblioteca y el
// motor lo esperan así); todo lo demás, suelto en la carpeta de juegos, con sus .bin al lado del .cue.
static void final_path(const Session *s, char *out, size_t len) {
    if (ends_ci(s->name, ".pbp")) {
        char base[256];
        base_no_ext(s->name, base, sizeof base);
        if (strcasecmp(base, "EBOOT") == 0) {
            // Sin nombre útil: la carpeta toma el ID del juego (DISC_ID) del propio archivo.
            size_t sl = 0;
            uint8_t *sfo = pbp_read_entry(s->part, PBP_SFO, &sl, 64 * 1024);
            char id[16] = "";
            if (sfo) { sfo_get_string(sfo, sl, "DISC_ID", id, sizeof id); free(sfo); }
            char clean[32];
            if (id[0] && safe_name(id, clean, sizeof clean)) snprintf(base, sizeof base, "%s", clean);
            else snprintf(base, sizeof base, "JUEGO_%s", s->id + 8);
        }
        snprintf(out, len, "%s/%s/EBOOT.PBP", GAMES_DIR, base);
    } else {
        snprintf(out, len, "%s/%s", GAMES_DIR, s->name);
    }
}

// ------------------------------------------------------------------ rutas de la API

static void api_pair(Req *r) {
    char body[JSON_MAX], pin[16];
    if (!read_json_body(r, body, sizeof body)) { respond_err(r, 400, "cuerpo_invalido"); return; }
    uint64_t t = now_us();
    pthread_mutex_lock(&g_lock);
    if (t < g_pin_locked_until) {
        pthread_mutex_unlock(&g_lock);
        respond_err(r, 429, "demasiados_intentos");
        return;
    }
    int ok = json_str(body, "pin", pin, sizeof pin) && strcmp(pin, g_st.pin) == 0;
    char token[36] = "";
    if (ok) {
        g_bad_pins = 0;
        snprintf(token, sizeof token, "%016llx%016llx", (unsigned long long)rnd(), (unsigned long long)rnd());
        int slot = 0;
        for (int i = 0; i < MAX_TOKENS; i++) if (!g_tokens[i][0]) { slot = i; break; }
        if (g_tokens[slot][0]) { memmove(g_tokens[0], g_tokens[1], sizeof g_tokens[0] * (MAX_TOKENS - 1)); slot = MAX_TOKENS - 1; }
        snprintf(g_tokens[slot], sizeof g_tokens[slot], "%s", token);
        g_st.paired++;
    } else if (++g_bad_pins >= 5) {
        g_bad_pins = 0;
        g_pin_locked_until = t + 30u * 1000000u;   // 30 s tras 5 intentos
    }
    pthread_mutex_unlock(&g_lock);
    if (!ok) { respond_err(r, 401, "pin_invalido"); return; }
    char out[96];
    snprintf(out, sizeof out, "{\"token\":\"%s\"}", token);
    respond_json(r, 200, out);
    ulog("emparejado un dispositivo");
}

static void api_info(Req *r) {
    uint64_t fb, tb;
    disk_space(&fb, &tb);
    char out[200];
    snprintf(out, sizeof out, "{\"app\":\"SAMPSP4\",\"version\":\"%s\",\"freeBytes\":%llu,\"totalBytes\":%llu}",
             APP_VERSION, (unsigned long long)fb, (unsigned long long)tb);
    respond_json(r, 200, out);
}

static void api_games(Req *r) {
    GameList l = { .max = MAX_GAMES_API };
    l.games = calloc(MAX_GAMES_API, sizeof(ApiGame));
    if (!l.games) { respond_err(r, 500, "sin_memoria"); return; }
    walk(GAMES_DIR, 0, collect_cb, &l);
    Str s = { 0 };
    sput(&s, "[", 1);
    for (int i = 0; i < l.count; i++) {
        if (i) sput(&s, ",", 1);
        game_json(&s, &l.games[i]);
    }
    sput(&s, "]", 1);
    respond(r, 200, "application/json; charset=utf-8", s.buf, s.len, NULL);
    free(s.buf);
    free(l.games);
}

static void api_cover(Req *r, const char *id) {
    char path[800];
    if (!find_game(id, path, sizeof path)) { respond_err(r, 404, "no_existe"); return; }
    size_t len = 0;
    uint8_t *png = NULL;
    if (disc_format_from_name(path) == DISC_PBP) {
        png = pbp_read_entry(path, PBP_ICON0, &len, 2 * 1024 * 1024);
    } else {
        Disc *d = disc_open(path);
        if (d) { png = disc_read_file(d, "PSP_GAME/ICON0.PNG", &len, 2 * 1024 * 1024); disc_close(d); }
    }
    if (!png) { respond_err(r, 404, "sin_portada"); return; }
    respond(r, 200, "image/png", png, len, NULL);
    free(png);
}

static void api_game_patch(Req *r, const char *id) {
    char body[JSON_MAX], title[160], path[800];
    if (!read_json_body(r, body, sizeof body) || !json_str(body, "title", title, sizeof title)) { respond_err(r, 400, "cuerpo_invalido"); return; }
    if (!find_game(id, path, sizeof path)) { respond_err(r, 404, "no_existe"); return; }
    for (char *p = title; *p; p++) if (*p == '\t' || *p == '\n' || *p == '\r') *p = ' ';
    pthread_mutex_lock(&g_lock);
    titles_set(path, title[0] ? title : NULL);
    pthread_mutex_unlock(&g_lock);
    ApiGame g;
    inspect(path, &g);
    Str s = { 0 };
    game_json(&s, &g);
    respond(r, 200, "application/json; charset=utf-8", s.buf, s.len, NULL);
    free(s.buf);
    __atomic_add_fetch(&g_st.games_added, 1, __ATOMIC_SEQ_CST);   // la biblioteca se actualiza
}

static void api_game_delete(Req *r, const char *id) {
    char path[800];
    if (!find_game(id, path, sizeof path)) { respond_err(r, 404, "no_existe"); return; }
    char dir[800];
    dir_of(path, dir, sizeof dir);
    int fmt = disc_format_from_name(path);
    if (fmt == DISC_CUE) {
        char bins[16][256], full[1100];
        int n = cue_bins(path, bins, 16);
        for (int i = 0; i < n; i++) { snprintf(full, sizeof full, "%s/%s", dir, bins[i]); unlink(full); }
    }
    int ok = unlink(path) == 0;
    // Un EBOOT.PBP vive en su carpeta: se borra la carpeta solo si quedó vacía.
    if (fmt == DISC_PBP && strcmp(dir, GAMES_DIR) != 0) rmdir(dir);
    pthread_mutex_lock(&g_lock);
    titles_set(path, NULL);
    pthread_mutex_unlock(&g_lock);
    if (!ok) { respond_err(r, 500, "no_se_pudo_borrar"); return; }
    ulog("borrado %s", path);
    __atomic_add_fetch(&g_st.games_added, 1, __ATOMIC_SEQ_CST);
    respond(r, 204, NULL, NULL, 0, NULL);
}

static void upload_json(char *out, size_t len, const Session *s, uint64_t received) {
    snprintf(out, len, "{\"uploadId\":\"%s\",\"chunkSize\":%u,\"receivedBytes\":%llu}", s->id, CHUNK_SIZE,
             (unsigned long long)received);
}

static void api_upload_create(Req *r) {
    char body[JSON_MAX], raw[400], name[256], sys[8] = "psp";
    uint64_t size = 0;
    if (!read_json_body(r, body, sizeof body) || !json_str(body, "fileName", raw, sizeof raw) || !json_u64(body, "sizeBytes", &size)) {
        respond_err(r, 400, "cuerpo_invalido");
        return;
    }
    json_str(body, "system", sys, sizeof sys);
    if (!safe_name(raw, name, sizeof name) || disc_format_from_name(name) < 0 || size == 0) {
        respond_err(r, 415, "formato_no_soportado");
        return;
    }
    Session tmp = { 0 };
    snprintf(tmp.name, sizeof tmp.name, "%s", name);
    char key[400];
    snprintf(key, sizeof key, "%s|%llu", name, (unsigned long long)size);
    snprintf(tmp.id, sizeof tmp.id, "%016llx", (unsigned long long)fnv1a(key));
    tmp.size = size;
    tmp.ps1 = strcmp(sys, "ps1") == 0;
    snprintf(tmp.part, sizeof tmp.part, "%s/%s.part", STAGE_DIR, tmp.id);

    char fin[800];
    if (!ends_ci(name, ".pbp") || strcasecmp(name, "EBOOT.PBP") != 0) {
        final_path(&tmp, fin, sizeof fin);
        if (path_exists(fin)) { respond_err(r, 409, "ya_existe"); return; }
    }

    mkdirs(STAGE_DIR);
    uint64_t received = path_size(tmp.part);   // reanudar: misma sesión (nombre + tamaño), aunque se haya reiniciado la app
    if (received > size) { unlink(tmp.part); received = 0; }

    uint64_t fb, tb;
    disk_space(&fb, &tb);
    if (tb && fb < (size - received) + 64ull * 1024 * 1024) { respond_err(r, 507, "sin_espacio"); return; }

    pthread_mutex_lock(&g_lock);
    Session *s = session_find(tmp.id);
    if (!s) {
        for (int i = 0; i < MAX_SESSIONS && !s; i++) if (!g_sess[i].used) s = &g_sess[i];
        if (!s) { s = &g_sess[0]; }   // la más vieja cede su lugar; su .part sigue en disco
        *s = tmp;
        s->used = 1;
    }
    char out[200];
    upload_json(out, sizeof out, s, received);
    pthread_mutex_unlock(&g_lock);
    ulog("subida %s: %s (%llu bytes), desde %llu", tmp.id, name, (unsigned long long)size, (unsigned long long)received);
    respond_json(r, 200, out);
}

static void api_upload_chunk(Req *r, const char *id) {
    pthread_mutex_lock(&g_lock);
    Session *sp = session_find(id);
    Session s = sp ? *sp : (Session){ 0 };
    pthread_mutex_unlock(&g_lock);
    if (!sp) { respond_err(r, 404, "subida_desconocida"); return; }

    const char *q = strstr(r->query, "offset=");
    if (!q) { respond_err(r, 400, "falta_offset"); return; }
    uint64_t offset = strtoull(q + 7, NULL, 10);
    uint64_t len = r->content_length;
    if (len == 0 || len > MAX_CHUNK || offset + len > s.size) { respond_err(r, 413, "bloque_invalido"); return; }

    uint64_t received = path_size(s.part);
    if (offset != received) {
        char out[120];
        snprintf(out, sizeof out, "{\"error\":\"offset_incorrecto\",\"receivedBytes\":%llu}", (unsigned long long)received);
        respond_json(r, 409, out);
        return;
    }

    uint8_t *buf = malloc((size_t)len);
    if (!buf) { respond_err(r, 503, "sin_memoria"); return; }
    if (!recv_body(r, buf, (size_t)len)) { free(buf); return; }   // la conexión se cayó: el cliente reanuda

    uint32_t want = (uint32_t)strtoul(r->crc, NULL, 16);
    if (!r->crc[0] || crc32_buf(buf, (size_t)len) != want) {
        free(buf);
        respond_err(r, 422, "crc_incorrecto");
        return;
    }

    int fd = open(s.part, O_WRONLY | O_CREAT, 0666);
    int ok = fd >= 0 && lseek(fd, (off_t)offset, SEEK_SET) == (off_t)offset;
    for (uint64_t w = 0; ok && w < len;) {
        ssize_t k = write(fd, buf + w, (size_t)(len - w));
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) ok = 0;
        else w += (uint64_t)k;
    }
    if (fd >= 0) close(fd);
    free(buf);
    if (!ok) { respond_err(r, 507, "sin_espacio"); return; }

    received = offset + len;
    pthread_mutex_lock(&g_lock);
    g_st.active = 1;
    snprintf(g_st.current, sizeof g_st.current, "%s", s.name);
    g_st.done = received;
    g_st.total = s.size;
    g_st.last_activity_us = now_us();
    pthread_mutex_unlock(&g_lock);

    char out[80];
    snprintf(out, sizeof out, "{\"receivedBytes\":%llu}", (unsigned long long)received);
    respond_json(r, 200, out);
}

static void api_upload_status(Req *r, const char *id) {
    pthread_mutex_lock(&g_lock);
    Session *sp = session_find(id);
    Session s = sp ? *sp : (Session){ 0 };
    pthread_mutex_unlock(&g_lock);
    if (!sp) { respond_err(r, 404, "subida_desconocida"); return; }
    char out[120];
    snprintf(out, sizeof out, "{\"receivedBytes\":%llu,\"sizeBytes\":%llu}", (unsigned long long)path_size(s.part),
             (unsigned long long)s.size);
    respond_json(r, 200, out);
}

static void api_upload_complete(Req *r, const char *id) {
    pthread_mutex_lock(&g_lock);
    Session *sp = session_find(id);
    Session s = sp ? *sp : (Session){ 0 };
    pthread_mutex_unlock(&g_lock);
    if (!sp) { respond_err(r, 404, "subida_desconocida"); return; }
    uint64_t received = path_size(s.part);
    if (received != s.size) {
        char out[120];
        snprintf(out, sizeof out, "{\"error\":\"incompleto\",\"receivedBytes\":%llu}", (unsigned long long)received);
        respond_json(r, 409, out);
        return;
    }
    char fin[800], dir[800];
    final_path(&s, fin, sizeof fin);
    if (path_exists(fin)) { respond_err(r, 409, "ya_existe"); return; }
    dir_of(fin, dir, sizeof dir);
    mkdirs(dir);
    if (rename(s.part, fin) != 0) { ulog("rename %s -> %s: errno %d", s.part, fin, errno); respond_err(r, 500, "no_se_pudo_guardar"); return; }

    pthread_mutex_lock(&g_lock);
    sp = session_find(id);
    if (sp) sp->used = 0;
    g_st.active = 0;
    g_st.done = g_st.total = 0;
    snprintf(g_st.last_added, sizeof g_st.last_added, "%s", s.name);
    pthread_mutex_unlock(&g_lock);
    __atomic_add_fetch(&g_st.games_added, 1, __ATOMIC_SEQ_CST);
    ulog("listo %s", fin);

    // Un .bin suelto se informa igual: queda listado hasta que llegue su .cue.
    ApiGame g;
    inspect(fin, &g);
    Str out = { 0 };
    sput(&out, "{\"game\":", 8);
    game_json(&out, &g);
    sput(&out, "}", 1);
    respond(r, 200, "application/json; charset=utf-8", out.buf, out.len, NULL);
    free(out.buf);
}

static void api_upload_cancel(Req *r, const char *id) {
    pthread_mutex_lock(&g_lock);
    Session *sp = session_find(id);
    if (sp) { unlink(sp->part); sp->used = 0; }
    g_st.active = 0;
    pthread_mutex_unlock(&g_lock);
    respond(r, 204, NULL, NULL, 0, NULL);
}

// ------------------------------------------------------------------ la web embebida

static void serve_static(Req *r) {
    const char *p = r->path;
    const WebAsset *a = web_asset_find(strcmp(p, "/") == 0 ? "/index.html" : p);
    if (!a) a = web_asset_find("/index.html");   // la web es de una sola página
    if (!a) { respond_err(r, 404, "no_existe"); return; }
    if (a->gzip && r->gzip_ok) {
        respond(r, 200, a->type, a->data, a->len, "Content-Encoding: gzip\r\nVary: Accept-Encoding\r\n");
    } else if (a->gzip) {
        respond_err(r, 415, "el_navegador_no_acepta_gzip");
    } else {
        respond(r, 200, a->type, a->data, a->len, NULL);
    }
}

// ------------------------------------------------------------------ conexión

static int parse_request(Req *r, char *head, size_t head_len) {
    char *end = strstr(head, "\r\n\r\n");
    if (!end) return 0;
    size_t hlen = (size_t)(end - head) + 4;
    r->head_extra = head + hlen;
    r->head_extra_len = head_len - hlen;
    *end = 0;

    char *line = head, *nl = strstr(line, "\r\n");
    if (nl) *nl = 0;
    char target[800] = "";
    if (sscanf(line, "%7s %799s", r->method, target) != 2) return 0;
    char *qm = strchr(target, '?');
    if (qm) { *qm = 0; snprintf(r->query, sizeof r->query, "%s", qm + 1); }
    // %XX en la ruta (ids y archivos de la web no los llevan, pero un navegador puede mandarlos)
    size_t o = 0;
    for (char *p = target; *p && o + 1 < sizeof r->path; p++) {
        if (*p == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
            char hx[3] = { p[1], p[2], 0 };
            r->path[o++] = (char)strtol(hx, NULL, 16);
            p += 2;
        } else r->path[o++] = *p;
    }
    r->path[o] = 0;
    if (strstr(r->path, "..")) return 0;

    for (char *h = nl ? nl + 2 : NULL; h && *h;) {
        char *e = strstr(h, "\r\n");
        if (e) *e = 0;
        char *colon = strchr(h, ':');
        if (colon) {
            *colon = 0;
            char *v = colon + 1;
            while (*v == ' ') v++;
            if (!strcasecmp(h, "Content-Length")) r->body_left = r->content_length = strtoull(v, NULL, 10);
            else if (!strcasecmp(h, "Authorization")) snprintf(r->auth, sizeof r->auth, "%s", v);
            else if (!strcasecmp(h, "X-Chunk-CRC32")) snprintf(r->crc, sizeof r->crc, "%s", v);
            else if (!strcasecmp(h, "Accept-Encoding")) r->gzip_ok = strstr(v, "gzip") != NULL;
        }
        h = e ? e + 2 : NULL;
    }
    return 1;
}

static void route(Req *r) {
    const char *p = r->path;
    int get = !strcmp(r->method, "GET") || !strcmp(r->method, "HEAD");
    if (strncmp(p, "/api/", 5) != 0) {
        if (get) serve_static(r);
        else respond_err(r, 405, "metodo_no_permitido");
        return;
    }
    if (!strcmp(p, "/api/pair") && !strcmp(r->method, "POST")) { api_pair(r); return; }
    if (!strcmp(p, "/api/info") && get) { api_info(r); return; }
    if (!authorized(r)) { respond_err(r, 401, "sesion_expirada"); return; }

    char id[64] = "";
    if (!strcmp(p, "/api/games") && get) { api_games(r); return; }
    if (sscanf(p, "/api/games/%63[^/]/cover", id) == 1 && strstr(p, "/cover") && get) { api_cover(r, id); return; }
    if (sscanf(p, "/api/games/%63[^/]", id) == 1 && !strchr(p + 11, '/')) {
        if (!strcmp(r->method, "PATCH")) { api_game_patch(r, id); return; }
        if (!strcmp(r->method, "DELETE")) { api_game_delete(r, id); return; }
    }
    if (!strcmp(p, "/api/uploads") && !strcmp(r->method, "POST")) { api_upload_create(r); return; }
    if (sscanf(p, "/api/uploads/%63[^/]/chunk", id) == 1 && strstr(p, "/chunk") && !strcmp(r->method, "PUT")) { api_upload_chunk(r, id); return; }
    if (sscanf(p, "/api/uploads/%63[^/]/complete", id) == 1 && strstr(p, "/complete") && !strcmp(r->method, "POST")) { api_upload_complete(r, id); return; }
    if (sscanf(p, "/api/uploads/%63[^/]", id) == 1 && !strchr(p + 13, '/')) {
        if (get) { api_upload_status(r, id); return; }
        if (!strcmp(r->method, "DELETE")) { api_upload_cancel(r, id); return; }
    }
    respond_err(r, 404, "ruta_desconocida");
}

static void *conn_thread(void *arg) {
    int fd = (int)(intptr_t)arg;
    char *head = malloc(HEADER_MAX + 1);
    if (head) {
        size_t n = 0;
        int ok = 0;
        while (n < HEADER_MAX) {
            struct pollfd pfd = { fd, POLLIN, 0 };
            if (poll(&pfd, 1, 15000) <= 0) break;   // 15 s sin datos: se cierra
            ssize_t k = recv(fd, head + n, HEADER_MAX - n, 0);
            if (k < 0 && errno == EINTR) continue;
            if (k <= 0) break;
            n += (size_t)k;
            head[n] = 0;
            if (strstr(head, "\r\n\r\n")) { ok = 1; break; }
        }
        Req r;
        memset(&r, 0, sizeof r);
        r.fd = fd;
        if (ok && parse_request(&r, head, n)) route(&r);
        else if (n) { r.fd = fd; strcpy(r.method, "GET"); respond_err(&r, 400, "peticion_invalida"); }
        free(head);
    }
    close(fd);
    __atomic_sub_fetch(&g_workers, 1, __ATOMIC_SEQ_CST);
    return NULL;
}

static void *accept_thread(void *arg) {
    (void)arg;
    while (!g_stop) {
        struct pollfd pfd = { g_listen, POLLIN, 0 };
        if (poll(&pfd, 1, 250) <= 0) continue;
        int fd = accept(g_listen, NULL, NULL);
        if (fd < 0) continue;
#ifndef VAULT_HOST
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SAM_SO_NOSIGPIPE, &one, sizeof one);
#endif
        if (__atomic_add_fetch(&g_workers, 1, __ATOMIC_SEQ_CST) > MAX_WORKERS) {
            __atomic_sub_fetch(&g_workers, 1, __ATOMIC_SEQ_CST);
            static const char busy[] = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send_all(fd, busy, sizeof busy - 1);
            close(fd);
            continue;
        }
        pthread_t t;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_attr_setstacksize(&attr, 256 * 1024);
        if (pthread_create(&t, &attr, conn_thread, (void *)(intptr_t)fd) != 0) {
            __atomic_sub_fetch(&g_workers, 1, __ATOMIC_SEQ_CST);
            close(fd);
        }
        pthread_attr_destroy(&attr);
    }
    close(g_listen);
    g_listen = -1;
    g_st.running = 0;
    return NULL;
}

// La dirección de la consola en la red de casa (no se envía ningún paquete).
static int local_ipv4(char *out, size_t out_len) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return 0;
    struct sockaddr_in to = { .sin_family = AF_INET, .sin_port = htons(53) };
    inet_pton(AF_INET, "1.1.1.1", &to.sin_addr);
    struct sockaddr_in me;
    socklen_t len = sizeof me;
    int ok = connect(s, (struct sockaddr *)&to, sizeof to) == 0 && getsockname(s, (struct sockaddr *)&me, &len) == 0;
    close(s);
    if (!ok || me.sin_addr.s_addr == 0) return 0;
    inet_ntop(AF_INET, &me.sin_addr, out, (socklen_t)out_len);
    return 1;
}

// ------------------------------------------------------------------ API pública

const UploadStatus *upload_status(void) { return &g_st; }

int upload_start(void) {
    if (g_st.running) return 1;
    static int once;
    if (!once) {
        once = 1;
        crc_init();
        signal(SIGPIPE, SIG_IGN);   // un celular que se va a mitad de respuesta no debe tumbar la app
#ifndef VAULT_HOST
        sceNetInit();
#endif
    }
    g_st.error[0] = 0;
    g_st.url[0] = 0;
    mkdirs(GAMES_DIR);
    mkdirs(STAGE_DIR);

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { snprintf(g_st.error, sizeof g_st.error, "No se pudo abrir la red (%d)", errno); return 0; }
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(UPLOAD_PORT), .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (bind(s, (struct sockaddr *)&a, sizeof a) != 0 || listen(s, 16) != 0) {
        snprintf(g_st.error, sizeof g_st.error, "El puerto %d est\xC3\xA1 ocupado (%d)", UPLOAD_PORT, errno);
        close(s);
        return 0;
    }
    g_listen = s;

    pthread_mutex_lock(&g_lock);
    snprintf(g_st.pin, sizeof g_st.pin, "%06u", (unsigned)(rnd() % 1000000u));
#ifdef VAULT_HOST
    if (getenv("SAMPSP4_PIN")) snprintf(g_st.pin, sizeof g_st.pin, "%.6s", getenv("SAMPSP4_PIN"));   // pruebas automáticas
#endif
    memset(g_tokens, 0, sizeof g_tokens);   // un PIN nuevo cierra las sesiones anteriores
    g_bad_pins = 0;
    g_st.paired = 0;
    g_st.active = 0;
    pthread_mutex_unlock(&g_lock);

    char ip[32];
    if (local_ipv4(ip, sizeof ip)) snprintf(g_st.url, sizeof g_st.url, "http://%s:%d", ip, UPLOAD_PORT);
    else snprintf(g_st.error, sizeof g_st.error, "La PS4 no est\xC3\xA1 conectada a una red");

    g_stop = 0;
    g_st.running = 1;
    pthread_t t;
    if (pthread_create(&t, NULL, accept_thread, NULL) != 0) {
        close(s);
        g_listen = -1;
        g_st.running = 0;
        snprintf(g_st.error, sizeof g_st.error, "No se pudo iniciar el servidor");
        return 0;
    }
    pthread_detach(t);
    ulog("escuchando en %s", g_st.url[0] ? g_st.url : "(sin red)");
    return 1;
}

void upload_stop(void) {
    if (!g_st.running) return;
    g_stop = 1;
    for (int i = 0; i < 40 && g_st.running; i++) usleep(25000);   // el hilo cierra el socket
    pthread_mutex_lock(&g_lock);
    memset(g_tokens, 0, sizeof g_tokens);
    g_st.active = 0;
    pthread_mutex_unlock(&g_lock);
    ulog("detenido");
}
