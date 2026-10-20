// netpc.h - "Mi PC": games from a shared folder on the player's computer (SMB: Windows and macOS
// file sharing), listed and copied to the PS4. Everything that touches the network runs on a
// worker thread; the screen polls a PcJob.
#pragma once
#include <stdint.h>

typedef struct {
    char host[64];      // IP (or a name the console can resolve)
    char user[64];
    char pass[64];
    char share[96];     // shared folder, "" = not chosen yet
} PcConfig;

int pc_config_load(PcConfig *c);          // 1 if there is a saved one
void pc_config_save(const PcConfig *c);

enum { PC_KIND_DIR, PC_KIND_GAME, PC_KIND_FILE, PC_KIND_SHARE };

typedef struct {
    char name[200];
    int kind;
    uint64_t size;
} PcEntry;

#define PC_MAX_ENTRIES 400
#define PC_MAX_FOUND 16

typedef enum { JOB_IDLE, JOB_RUNNING, JOB_DONE, JOB_FAILED } JobState;
typedef enum { JOB_SCAN, JOB_SHARES, JOB_LIST, JOB_DOWNLOAD } JobKind;

typedef struct {
    volatile JobState state;
    JobKind kind;
    PcConfig cfg;
    char dir[512];               // JOB_LIST: folder inside the share ("" = top)
    char error[200];
    // Results
    PcEntry entries[PC_MAX_ENTRIES];
    int count;
    char found[PC_MAX_FOUND][64];   // JOB_SCAN: computers with file sharing on
    int found_count;
    char local_ip[32];
    // JOB_DOWNLOAD: remote game (share-relative) -> local folder
    char remote[512];
    char dest_dir[256];
    char dest_path[512];         // where the game ended up (its main file)
    volatile uint64_t total, done;
    volatile int file_index, file_count;
    char current[200];
    volatile int cancel;
    uint64_t started_us;
} PcJob;

// Starts `job` (fields for its kind filled in) on a worker thread. 0 if a job is still running.
int pc_job_start(PcJob *job);

// Is this file name a game SAMPSP4 can play (same rules as the library)?
int pc_is_game_name(const char *name);

// Explains an SMB error for the screen ("contraseña incorrecta", ...).
void pc_explain(int err, const char *detail, char *out, int out_len);
