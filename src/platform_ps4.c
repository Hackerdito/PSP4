// platform_ps4.c - PS4 video out, DualShock 4 input, clock and GoldHEN sandbox escape.
#include "platform.h"

#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <orbis/libkernel.h>
#include <orbis/Pad.h>
#include <orbis/SystemService.h>
#include <orbis/UserService.h>
#include <orbis/VideoOut.h>

// ---- GoldHEN SDK (MIT, https://github.com/GoldHEN/GoldHEN_Plugins_SDK) ----
struct jailbreak_backup {
    uint32_t cr_uid, cr_ruid, cr_rgid, cr_groups;
    uint64_t cr_paid;
    uint64_t cr_caps[2];
    void *cr_prison, *fd_cdir, *fd_jdir, *fd_rdir;
};
int orbis_syscall(int num, ...);
__asm__(
    ".text\n"
    ".globl orbis_syscall\n"
    "orbis_syscall:\n"
    "  movq $0, %rax\n"
    "  movq %rcx, %r10\n"
    "  syscall\n"
    "  jb 1f\n"
    "  retq\n"
    "1:\n"
    "  movq $-1, %rax\n"
    "  retq\n");
#define GOLDHEN_SDK_CMD_VERSION   0
#define GOLDHEN_SDK_CMD_JAILBREAK 2
// ---------------------------------------------------------------------------

static int g_video = -1;
static void *g_fb[2];
static int g_fb_idx = 0;
static int64_t g_frame = 0;
static OrbisKernelEqueue g_flip_queue;
static void *g_fb_mem;
static off_t g_fb_phys;
static size_t g_fb_total;
static int g_pad = -1;
static int32_t g_user = -1;

const char *plat_app_dir(void) { return "/app0"; }

void plat_shutdown(void) {
    if (g_pad >= 0) { scePadClose(g_pad); g_pad = -1; }
    if (g_video >= 0) {
        sceVideoOutClose(g_video);
        g_video = -1;
    }
    if (g_fb_mem) {
        sceKernelMunmap(g_fb_mem, g_fb_total);
        sceKernelReleaseDirectMemory(g_fb_phys, g_fb_total);
        g_fb_mem = NULL;
    }
    sceKernelDeleteEqueue(g_flip_queue);
}

void plat_log(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    printf("[VAULT] %s\n", buf);
}

static int video_init(void) {
    g_video = sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN, ORBIS_VIDEO_OUT_BUS_MAIN, 0, 0);
    if (g_video < 0) return 0;
    if (sceKernelCreateEqueue(&g_flip_queue, "vault flip") < 0) return 0;
    sceVideoOutAddFlipEvent(g_flip_queue, g_video, 0);

    const size_t align = 0x200000;
    size_t fb_size = ((size_t)SCREEN_W * SCREEN_H * 4 + align - 1) / align * align;
    size_t total = fb_size * 2;
    off_t phys = 0;
    void *mem = NULL;
    if (sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), total, align, 3, &phys) < 0) return 0;
    if (sceKernelMapDirectMemory(&mem, total, 0x33, 0, phys, align) < 0) return 0;
    g_fb_mem = mem; g_fb_phys = phys; g_fb_total = total;
    g_fb[0] = mem;
    g_fb[1] = (char *)mem + fb_size;
    memset(mem, 0, total);

    OrbisVideoOutBufferAttribute attr;
    sceVideoOutSetBufferAttribute(&attr, 0x80000000, 1, 0, SCREEN_W, SCREEN_H, SCREEN_W);
    if (sceVideoOutRegisterBuffers(g_video, 0, g_fb, 2, &attr) != 0) return 0;
    sceVideoOutSetFlipRate(g_video, 0);
    return 1;
}

#ifndef VAULT_EMBEDDED
static void escape_sandbox(void) {
    // Unknown syscalls raise SIGSYS on consoles without GoldHEN; ignore it so we just carry on sandboxed.
    signal(SIGSYS, SIG_IGN);
    struct jailbreak_backup jb;
    memset(&jb, 0, sizeof jb);
    int ver = orbis_syscall(500, GOLDHEN_SDK_CMD_VERSION, NULL);
    if (ver > 0) {
        int rc = orbis_syscall(500, GOLDHEN_SDK_CMD_JAILBREAK, &jb);
        plat_log("GoldHEN SDK 0x%x, jailbreak rc=%d", ver, rc);
    } else {
        plat_log("GoldHEN SDK not found; staying in the sandbox");
    }
}
#endif

int plat_init(void) {
    sceSystemServiceHideSplashScreen();

    OrbisUserServiceInitializeParams up = { .priority = 700 };
    sceUserServiceInitialize(&up);
    sceUserServiceGetInitialUser(&g_user);
    if (scePadInit() == 0) g_pad = scePadOpen(g_user, 0, 0, NULL);

    if (!video_init()) { plat_log("video init failed"); return 0; }
#ifndef VAULT_EMBEDDED
    // Standalone builds escape the sandbox; the built-in engine must keep it, since jailbreaking
    // swaps the process root and /app0 (the engine's assets) would no longer resolve.
    escape_sandbox();
#endif
    return 1;
}

uint32_t plat_buttons(void) {
    if (g_pad < 0) return 0;
    OrbisPadData d;
    if (scePadReadState(g_pad, &d) != 0 || !d.connected) return 0;
    uint32_t b = d.buttons, out = 0;
    if (b & ORBIS_PAD_BUTTON_UP) out |= BTN_UP;
    if (b & ORBIS_PAD_BUTTON_DOWN) out |= BTN_DOWN;
    if (b & ORBIS_PAD_BUTTON_LEFT) out |= BTN_LEFT;
    if (b & ORBIS_PAD_BUTTON_RIGHT) out |= BTN_RIGHT;
    if (b & ORBIS_PAD_BUTTON_CROSS) out |= BTN_CROSS;
    if (b & ORBIS_PAD_BUTTON_CIRCLE) out |= BTN_CIRCLE;
    if (b & ORBIS_PAD_BUTTON_TRIANGLE) out |= BTN_TRIANGLE;
    if (b & ORBIS_PAD_BUTTON_SQUARE) out |= BTN_SQUARE;
    if (b & ORBIS_PAD_BUTTON_L1) out |= BTN_L1;
    if (b & ORBIS_PAD_BUTTON_R1) out |= BTN_R1;
    if (b & ORBIS_PAD_BUTTON_OPTIONS) out |= BTN_OPTIONS;
    // Left stick as d-pad.
    if (d.leftStick.x < 40) out |= BTN_LEFT;
    if (d.leftStick.x > 215) out |= BTN_RIGHT;
    if (d.leftStick.y < 40) out |= BTN_UP;
    if (d.leftStick.y > 215) out |= BTN_DOWN;
    return out;
}

void plat_present(const Canvas *c) {
    uint32_t *dst = g_fb[g_fb_idx];
    const uint32_t *src = c->px;
    for (int i = 0, n = c->w * c->h; i < n; i++) dst[i] = src[i] | 0xFF000000u;

    g_frame++;
    sceVideoOutSubmitFlip(g_video, g_fb_idx, ORBIS_VIDEO_OUT_FLIP_VSYNC, g_frame);
    for (;;) {
        OrbisVideoOutFlipStatus st;
        sceVideoOutGetFlipStatus(g_video, &st);
        if (st.flipArg >= g_frame) break;
        OrbisKernelEvent ev;
        int count;
        if (sceKernelWaitEqueue(g_flip_queue, &ev, 1, &count, 0) != 0) break;
    }
    g_fb_idx ^= 1;
}

void plat_idle(void) { sceKernelUsleep(16000); }

void plat_clock(int *hh, int *mm) {
    time_t utc = time(NULL), local = utc;
    uint8_t tz[16];
    int dst = 0;
    typedef int (*conv_fn)(time_t, time_t *, void *, int *);
    if (((conv_fn)sceKernelConvertUtcToLocaltime)(utc, &local, tz, &dst) != 0) local = utc;
    struct tm t;
    gmtime_r(&local, &t);
    *hh = t.tm_hour;
    *mm = t.tm_min;
}

#ifdef VAULT_EMBEDDED
// The engine is built into this eboot: main.c hands the game over directly.
int plat_engine_installed(void) { return 1; }
int plat_launch_engine(const char *game_path) { (void)game_path; return -1; }
#else
// ---- PSP engine (M33) ------------------------------------------------------

#define ENGINE_LAUNCH_FILE "/data/March33/launch.txt"

// Only a hint: installs can live elsewhere (extended storage), so a launch is always attempted.
int plat_engine_installed(void) {
    static const char *const paths[] = {
        "/user/app/" ENGINE_TITLE_ID, "/system_data/priv/appmeta/" ENGINE_TITLE_ID,
        "/user/appmeta/" ENGINE_TITLE_ID, "/mnt/ext0/user/app/" ENGINE_TITLE_ID, NULL,
    };
    for (int i = 0; paths[i]; i++)
        if (access(paths[i], F_OK) == 0) return 1;
    return 0;
}

int plat_launch_engine(const char *game_path) {
    // M33 reads the game from its first argument; launch.txt covers consoles where arguments
    // don't reach the app (see engine/m33-launch-args.patch).
    mkdir("/data/March33", 0777);
    FILE *f = fopen(ENGINE_LAUNCH_FILE, "w");
    if (f) { fprintf(f, "%s\n", game_path); fclose(f); }

    const char *argv[] = { game_path, NULL };
    LncAppParam p;
    memset(&p, 0, sizeof p);
    p.size = sizeof p;
    p.user_id = (uint32_t)g_user;
    p.app_opt = 0;
    p.crash_report = 0;
    p.LaunchAppCheck_flag = LaunchApp_SkipSystemUpdate;
    int rc = sceLncUtilLaunchApp(ENGINE_TITLE_ID, argv, &p);
    plat_log("launch %s (%s): 0x%08x", ENGINE_TITLE_ID, game_path, (unsigned)rc);
    if (rc < 0) {
        int rc2 = sceSystemServiceLaunchApp(ENGINE_TITLE_ID, argv, &p);
        plat_log("sceSystemServiceLaunchApp: 0x%08x", (unsigned)rc2);
        if (rc2 >= 0) return 0;
        remove(ENGINE_LAUNCH_FILE);   // don't let a stale request start a game later
        return rc;
    }
    return 0;
}

#endif  // VAULT_EMBEDDED
