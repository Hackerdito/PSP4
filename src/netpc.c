// netpc.c - "Mi PC": browse a computer's shared folder over SMB (libsmb2) and copy games to the
// PS4. Windows shares and macOS "File Sharing" (SMB) both work, with the computer's own user
// name and password. Every network call runs on a worker thread (pc_job_start).
#include "netpc.h"

#include "disc.h"
#include "library.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include <smb2/libsmb2-raw.h>
#include <smb2/libsmb2-dcerpc-srvsvc.h>

#ifdef VAULT_HOST
#define PC_CONFIG_FILE "build/host/pc.txt"
#else
#define PC_CONFIG_FILE "/data/vault/pc.txt"
int sceNetInit(void);
#endif

static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + ts.tv_nsec / 1000;
}

// ---------------------------------------------------------------- settings

int pc_config_load(PcConfig *c) {
    memset(c, 0, sizeof *c);
    FILE *f = fopen(PC_CONFIG_FILE, "r");
    if (!f) return 0;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char *v = eq + 1;
        if (!strcmp(line, "host")) snprintf(c->host, sizeof c->host, "%s", v);
        else if (!strcmp(line, "user")) snprintf(c->user, sizeof c->user, "%s", v);
        else if (!strcmp(line, "pass")) snprintf(c->pass, sizeof c->pass, "%s", v);
        else if (!strcmp(line, "share")) snprintf(c->share, sizeof c->share, "%s", v);
    }
    fclose(f);
    return c->host[0] != 0;
}

void pc_config_save(const PcConfig *c) {
#ifndef VAULT_HOST
    mkdir("/data/vault", 0777);
#endif
    FILE *f = fopen(PC_CONFIG_FILE, "w");
    if (!f) return;
    fprintf(f, "host=%s\nuser=%s\npass=%s\nshare=%s\n", c->host, c->user, c->pass, c->share);
    fclose(f);
}

// ---------------------------------------------------------------- helpers

int pc_is_game_name(const char *name) {
    return disc_format_from_name(name) >= 0 && strcasecmp(name, "PARAM.PBP") != 0;
}

void pc_explain(int err, const char *detail, char *out, int out_len) {
    const char *d = detail ? detail : "";
    if (strstr(d, "LOGON_FAILURE") || strstr(d, "ACCESS_DENIED") || err == -EACCES || err == -EPERM)
        snprintf(out, out_len, "La PC rechaz\xC3\xB3 el usuario o la contrase\xC3\xB1" "a");
    else if (strstr(d, "BAD_NETWORK_NAME"))
        snprintf(out, out_len, "No existe esa carpeta compartida en la PC");
    else if (err == -ETIMEDOUT || strstr(d, "imeout"))
        snprintf(out, out_len, "La PC no respondi\xC3\xB3 (\xC2\xBF" "est\xC3\xA1 encendida y en la misma red?)");
    else if (err == -ECONNREFUSED || strstr(d, "refused"))
        snprintf(out, out_len, "La PC no tiene activado compartir archivos (SMB)");
    else if (err == -EHOSTUNREACH || err == -ENETUNREACH || strstr(d, "unreachable"))
        snprintf(out, out_len, "No se lleg\xC3\xB3 a la PC: revisa la IP y que la PS4 tenga red");
    else if (d[0])
        snprintf(out, out_len, "Error de red: %.150s", d);
    else
        snprintf(out, out_len, "Error de red (%d)", err);
}

static struct smb2_context *pc_connect(const PcConfig *c, const char *share, char *err, int err_len) {
    struct smb2_context *smb2 = smb2_init_context();
    if (!smb2) { snprintf(err, err_len, "Sin memoria para la conexi\xC3\xB3n"); return NULL; }
    smb2_set_timeout(smb2, 8);
    smb2_set_security_mode(smb2, SMB2_NEGOTIATE_SIGNING_ENABLED);
    if (c->user[0]) smb2_set_user(smb2, c->user);
    if (c->pass[0]) smb2_set_password(smb2, c->pass);
    smb2_set_workstation(smb2, "SAMPSP4");
    int rc = smb2_connect_share(smb2, c->host, share, c->user[0] ? c->user : "guest");
    if (rc < 0) {
        pc_explain(rc, smb2_get_error(smb2), err, err_len);
        smb2_destroy_context(smb2);
        return NULL;
    }
    return smb2;
}

static void join_path(char *out, size_t out_len, const char *a, const char *b) {
    if (a && a[0]) snprintf(out, out_len, "%s/%s", a, b);
    else snprintf(out, out_len, "%s", b);
}

// ---------------------------------------------------------------- shares

static int g_enum_done;
static void share_cb(struct smb2_context *smb2, int status, void *data, void *priv) {
    PcJob *job = priv;
    if (status) {
        pc_explain(status, smb2_get_error(smb2), job->error, sizeof job->error);
        g_enum_done = -1;
        return;
    }
    struct srvsvc_NetrShareEnum_rep *rep = data;
    struct srvsvc_SHARE_INFO_1_CONTAINER *c1 = &rep->ses.ShareInfo.Level1;
    for (uint32_t i = 0; i < c1->EntriesRead && job->count < PC_MAX_ENTRIES; i++) {
        struct srvsvc_SHARE_INFO_1 *s = &c1->Buffer->share_info_1[i];
        if ((s->type & 3) != SHARE_TYPE_DISKTREE || (s->type & SHARE_TYPE_HIDDEN)) continue;
        const char *name = s->netname.utf8;
        if (!name || !name[0] || name[strlen(name) - 1] == '$') continue;   // C$, ADMIN$, ...
        PcEntry *e = &job->entries[job->count++];
        memset(e, 0, sizeof *e);
        snprintf(e->name, sizeof e->name, "%s", name);
        e->kind = PC_KIND_SHARE;
    }
    smb2_free_data(smb2, rep);
    g_enum_done = 1;
}

static int job_shares(PcJob *job) {
    struct smb2_context *smb2 = pc_connect(&job->cfg, "IPC$", job->error, sizeof job->error);
    if (!smb2) return 0;
    g_enum_done = 0;
    if (smb2_share_enum_async(smb2, SHARE_INFO_1, share_cb, job) != 0) {
        pc_explain(-EIO, smb2_get_error(smb2), job->error, sizeof job->error);
        smb2_destroy_context(smb2);
        return 0;
    }
    uint64_t deadline = now_us() + 10000000;
    while (!g_enum_done && now_us() < deadline) {
        struct pollfd pfd = { .fd = smb2_get_fd(smb2), .events = (short)smb2_which_events(smb2) };
        if (poll(&pfd, 1, 500) < 0) break;
        if (smb2_service(smb2, pfd.revents) < 0) {
            pc_explain(-EIO, smb2_get_error(smb2), job->error, sizeof job->error);
            g_enum_done = -1;
        }
    }
    if (!g_enum_done) snprintf(job->error, sizeof job->error, "La PC no mand\xC3\xB3 su lista de carpetas");
    smb2_disconnect_share(smb2);
    smb2_destroy_context(smb2);
    return g_enum_done == 1;
}

// ---------------------------------------------------------------- folder listing

static int cmp_entry(const void *pa, const void *pb) {
    const PcEntry *a = pa, *b = pb;
    if (a->kind != b->kind) return a->kind - b->kind;   // folders, games, other files
    return strcasecmp(a->name, b->name);
}

static int job_list(PcJob *job) {
    struct smb2_context *smb2 = pc_connect(&job->cfg, job->cfg.share, job->error, sizeof job->error);
    if (!smb2) return 0;
    struct smb2dir *dir = smb2_opendir(smb2, job->dir);
    if (!dir) {
        pc_explain(-ENOENT, smb2_get_error(smb2), job->error, sizeof job->error);
        smb2_destroy_context(smb2);
        return 0;
    }
    int has_cue = 0;
    struct smb2dirent *e;
    while ((e = smb2_readdir(smb2, dir)) && job->count < PC_MAX_ENTRIES) {
        if (e->name[0] == '.') continue;
        if (e->st.smb2_type == SMB2_TYPE_LINK) continue;
        PcEntry *p = &job->entries[job->count++];
        memset(p, 0, sizeof *p);
        snprintf(p->name, sizeof p->name, "%s", e->name);
        p->size = e->st.smb2_size;
        if (e->st.smb2_type == SMB2_TYPE_DIRECTORY) p->kind = PC_KIND_DIR;
        else p->kind = pc_is_game_name(e->name) ? PC_KIND_GAME : PC_KIND_FILE;
        if (disc_format_from_name(e->name) == DISC_CUE) has_cue = 1;
    }
    smb2_closedir(smb2, dir);
    smb2_disconnect_share(smb2);
    smb2_destroy_context(smb2);
    // A .bin next to a .cue is one of its tracks: the .cue is the game.
    if (has_cue)
        for (int i = 0; i < job->count; i++)
            if (job->entries[i].kind == PC_KIND_GAME && disc_format_from_name(job->entries[i].name) == DISC_BIN)
                job->entries[i].kind = PC_KIND_FILE;
    qsort(job->entries, job->count, sizeof(PcEntry), cmp_entry);
    return 1;
}

// ---------------------------------------------------------------- download

static void mkdirs(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(tmp, 0777); *p = '/'; }
    mkdir(tmp, 0777);
}

typedef struct { char remote[512]; char local[512]; uint64_t size; } Copy;

static int remote_size(struct smb2_context *smb2, const char *path, uint64_t *size) {
    struct smb2_stat_64 st;
    if (smb2_stat(smb2, path, &st) < 0) return 0;
    *size = st.smb2_size;
    return 1;
}

// The cue sheet's FILE entries (track files), as names inside the cue's folder.
static int cue_files(struct smb2_context *smb2, const char *cue, char names[][256], int max) {
    struct smb2fh *fh = smb2_open(smb2, cue, O_RDONLY);
    if (!fh) return 0;
    static uint8_t text[65536];
    int n = 0, got;
    while (n < (int)sizeof text - 1 && (got = smb2_read(smb2, fh, text + n, sizeof text - 1 - n)) > 0) n += got;
    smb2_close(smb2, fh);
    text[n] = 0;
    int count = 0;
    char *line = (char *)text;
    while (line && *line && count < max) {
        char *next = strpbrk(line, "\r\n");
        if (next) { *next = 0; next++; while (*next == '\r' || *next == '\n') next++; }
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (!strncasecmp(p, "FILE", 4)) {
            p += 4;
            while (*p == ' ' || *p == '\t') p++;
            char *start = p, *end;
            if (*p == '"') { start = p + 1; end = strchr(start, '"'); }
            else { end = start; while (*end && *end != ' ' && *end != '\t') end++; }
            if (end && end > start) {
                snprintf(names[count], 256, "%.*s", (int)(end - start), start);
                // Only a plain name: never a path that climbs out of the folder.
                if (!strchr(names[count], '/') && !strchr(names[count], '\\') && strcmp(names[count], ".."))
                    count++;
            }
        }
        line = next;
    }
    return count;
}

static int job_download(PcJob *job) {
    struct smb2_context *smb2 = pc_connect(&job->cfg, job->cfg.share, job->error, sizeof job->error);
    if (!smb2) return 0;
    static Copy copies[64];
    int n = 0;
    char folder[512] = "";
    const char *slash = strrchr(job->remote, '/');
    const char *file = slash ? slash + 1 : job->remote;
    if (slash) snprintf(folder, sizeof folder, "%.*s", (int)(slash - job->remote), job->remote);
    int fmt = disc_format_from_name(file);
    char local_dir[512];

    if (fmt == DISC_PBP) {
        // EBOOT.PBP lives in its own folder (and may have company: PARAM.PBP, DATA.DNP...): copy it all.
        const char *dir_name = strrchr(folder, '/') ? strrchr(folder, '/') + 1 : folder[0] ? folder : "EBOOT";
        join_path(local_dir, sizeof local_dir, job->dest_dir, dir_name);
        struct smb2dir *dir = smb2_opendir(smb2, folder);
        struct smb2dirent *e;
        while (dir && (e = smb2_readdir(smb2, dir)) && n < 64) {
            if (e->name[0] == '.' || e->st.smb2_type != SMB2_TYPE_FILE) continue;
            join_path(copies[n].remote, sizeof copies[n].remote, folder, e->name);
            join_path(copies[n].local, sizeof copies[n].local, local_dir, e->name);
            copies[n].size = e->st.smb2_size;
            n++;
        }
        if (dir) smb2_closedir(smb2, dir);
        join_path(job->dest_path, sizeof job->dest_path, local_dir, file);
    } else if (fmt == DISC_CUE) {
        // A PS1 disc: the .cue and its tracks, together in a folder named after the game.
        char base[256];
        snprintf(base, sizeof base, "%s", file);
        char *dot = strrchr(base, '.');
        if (dot) *dot = 0;
        join_path(local_dir, sizeof local_dir, job->dest_dir, base);
        static char tracks[32][256];
        int nt = cue_files(smb2, job->remote, tracks, 32);
        join_path(copies[n].remote, sizeof copies[n].remote, folder, file);
        join_path(copies[n].local, sizeof copies[n].local, local_dir, file);
        remote_size(smb2, copies[n].remote, &copies[n].size);
        n++;
        for (int i = 0; i < nt && n < 64; i++) {
            join_path(copies[n].remote, sizeof copies[n].remote, folder, tracks[i]);
            join_path(copies[n].local, sizeof copies[n].local, local_dir, tracks[i]);
            if (!remote_size(smb2, copies[n].remote, &copies[n].size)) {
                snprintf(job->error, sizeof job->error, "Falta %s (lo pide el .cue)", tracks[i]);
                smb2_destroy_context(smb2);
                return 0;
            }
            n++;
        }
        join_path(job->dest_path, sizeof job->dest_path, local_dir, file);
    } else {
        snprintf(local_dir, sizeof local_dir, "%s", job->dest_dir);
        snprintf(copies[0].remote, sizeof copies[0].remote, "%s", job->remote);
        join_path(copies[0].local, sizeof copies[0].local, local_dir, file);
        remote_size(smb2, job->remote, &copies[0].size);
        n = 1;
        snprintf(job->dest_path, sizeof job->dest_path, "%s", copies[0].local);
    }

    job->total = 0;
    for (int i = 0; i < n; i++) job->total += copies[i].size;
    job->file_count = n;
    mkdirs(local_dir);

    uint32_t chunk = smb2_get_max_read_size(smb2);
    if (chunk == 0 || chunk > 8u << 20) chunk = 1u << 20;
    uint8_t *buf = malloc(chunk);
    int ok = buf != NULL;
    for (int i = 0; ok && i < n; i++) {
        job->file_index = i;
        const char *name = strrchr(copies[i].local, '/') ? strrchr(copies[i].local, '/') + 1 : copies[i].local;
        snprintf(job->current, sizeof job->current, "%s", name);
        // Already there, same size: nothing to do.
        if (path_exists(copies[i].local) && path_size(copies[i].local) == copies[i].size) {
            job->done += copies[i].size;
            continue;
        }
        struct smb2fh *fh = smb2_open(smb2, copies[i].remote, O_RDONLY);
        if (!fh) { pc_explain(-ENOENT, smb2_get_error(smb2), job->error, sizeof job->error); ok = 0; break; }
        char part[520];
        snprintf(part, sizeof part, "%s.part", copies[i].local);
        FILE *out = fopen(part, "wb");
        if (!out) {
            snprintf(job->error, sizeof job->error, "No se pudo escribir en %s", local_dir);
            smb2_close(smb2, fh);
            ok = 0;
            break;
        }
        uint64_t left = copies[i].size;
        while (left > 0 && !job->cancel) {
            uint32_t want = left < chunk ? (uint32_t)left : chunk;
            int got = smb2_read(smb2, fh, buf, want);
            if (got <= 0) {
                pc_explain(got < 0 ? got : -EIO, smb2_get_error(smb2), job->error, sizeof job->error);
                ok = 0;
                break;
            }
            if (fwrite(buf, 1, (size_t)got, out) != (size_t)got) {
                snprintf(job->error, sizeof job->error, "Se llen\xC3\xB3 el disco o no se pudo escribir");
                ok = 0;
                break;
            }
            left -= (uint64_t)got;
            job->done += (uint64_t)got;
        }
        smb2_close(smb2, fh);
        if (fclose(out) != 0) ok = 0;
        if (job->cancel) { snprintf(job->error, sizeof job->error, "Descarga cancelada"); ok = 0; }
        if (ok) rename(part, copies[i].local);
        else remove(part);
    }
    free(buf);
    smb2_disconnect_share(smb2);
    smb2_destroy_context(smb2);
    return ok;
}

// ---------------------------------------------------------------- finding computers

// The console's own address on the local network (no packet is sent).
static int local_ipv4(char *out, size_t out_len, uint32_t *addr) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return 0;
    struct sockaddr_in to = { .sin_family = AF_INET, .sin_port = htons(53) };
    inet_pton(AF_INET, "1.1.1.1", &to.sin_addr);
    struct sockaddr_in me;
    socklen_t len = sizeof me;
    int ok = connect(s, (struct sockaddr *)&to, sizeof to) == 0 && getsockname(s, (struct sockaddr *)&me, &len) == 0;
    close(s);
    if (!ok || me.sin_addr.s_addr == 0) return 0;
    *addr = ntohl(me.sin_addr.s_addr);
    inet_ntop(AF_INET, &me.sin_addr, out, (socklen_t)out_len);
    return 1;
}

// Computers on the same /24 network that answer on the SMB port (445).
static int job_scan(PcJob *job) {
    uint32_t me;
    if (!local_ipv4(job->local_ip, sizeof job->local_ip, &me)) {
        snprintf(job->error, sizeof job->error, "La PS4 no est\xC3\xA1 conectada a una red");
        return 0;
    }
    uint32_t net = me & 0xFFFFFF00u;
    for (int start = 1; start < 255 && job->found_count < PC_MAX_FOUND && !job->cancel; start += 64) {
        struct pollfd pfd[64];
        uint32_t ip[64];
        int n = 0;
        for (int h = start; h < start + 64 && h < 255; h++) {
            uint32_t a = net | (uint32_t)h;
            if (a == me) continue;
            int s = socket(AF_INET, SOCK_STREAM, 0);
            if (s < 0) continue;
            fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
            struct sockaddr_in to = { .sin_family = AF_INET, .sin_port = htons(445) };
            to.sin_addr.s_addr = htonl(a);
            int rc = connect(s, (struct sockaddr *)&to, sizeof to);
            if (rc < 0 && errno != EINPROGRESS) { close(s); continue; }
            pfd[n].fd = s; pfd[n].events = POLLOUT; pfd[n].revents = 0;
            ip[n] = a;
            n++;
        }
        uint64_t deadline = now_us() + 700000;
        int open_left = n;
        while (open_left > 0 && now_us() < deadline) {
            int r = poll(pfd, (nfds_t)n, 100);
            if (r <= 0) continue;
            for (int i = 0; i < n; i++) {
                if (pfd[i].fd < 0 || !pfd[i].revents) continue;
                int err = 0;
                socklen_t el = sizeof err;
                getsockopt(pfd[i].fd, SOL_SOCKET, SO_ERROR, &err, &el);
                if (err == 0 && (pfd[i].revents & POLLOUT) && job->found_count < PC_MAX_FOUND) {
                    struct in_addr ia = { .s_addr = htonl(ip[i]) };
                    inet_ntop(AF_INET, &ia, job->found[job->found_count++], 64);
                }
                close(pfd[i].fd);
                pfd[i].fd = -1;
                open_left--;
            }
        }
        for (int i = 0; i < n; i++) if (pfd[i].fd >= 0) close(pfd[i].fd);
    }
    return 1;
}

// ---------------------------------------------------------------- jobs

static void *job_thread(void *arg) {
    PcJob *job = arg;
    int ok = 0;
    switch (job->kind) {
    case JOB_SCAN: ok = job_scan(job); break;
    case JOB_SHARES: ok = job_shares(job); break;
    case JOB_LIST: ok = job_list(job); break;
    case JOB_DOWNLOAD: ok = job_download(job); break;
    }
    __atomic_store_n(&job->state, ok ? JOB_DONE : JOB_FAILED, __ATOMIC_RELEASE);
    return NULL;
}

int pc_job_start(PcJob *job) {
    if (job->state == JOB_RUNNING) return 0;
#ifndef VAULT_HOST
    static int net_ready;
    if (!net_ready) { sceNetInit(); net_ready = 1; }
#endif
    job->error[0] = 0;
    job->count = 0;
    job->found_count = 0;
    job->total = job->done = 0;
    job->file_index = job->file_count = 0;
    job->current[0] = 0;
    job->cancel = 0;
    job->started_us = now_us();
    job->state = JOB_RUNNING;
    pthread_t t;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr, 512 * 1024);
    if (pthread_create(&t, &attr, job_thread, job) != 0) {
        pthread_attr_destroy(&attr);
        snprintf(job->error, sizeof job->error, "No se pudo iniciar la tarea");
        job->state = JOB_FAILED;
        return 1;
    }
    pthread_attr_destroy(&attr);
    return 1;
}
