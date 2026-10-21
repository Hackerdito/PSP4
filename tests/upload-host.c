// tests/upload-host.c - el servidor de "Subir desde el celular" solo, en la PC, para probar la web
// y la API sin consola:  make upload-host && build/host/upload-host
// Juegos en testdata/data/vault/games (como /data/vault/games en la consola). Imprime la URL y el PIN, y termina con Ctrl+C.
#include "../src/upload.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static volatile int g_quit;
static void on_sig(int s) { (void)s; g_quit = 1; }

int main(void) {
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    if (!upload_start()) { fprintf(stderr, "no arrancó: %s\n", upload_status()->error); return 1; }
    const UploadStatus *st = upload_status();
    // SAMPSP4_PIN=123456 fija el PIN para las pruebas automáticas.
    printf("URL %s\nPIN %s\n", st->url[0] ? st->url : "http://127.0.0.1:8080", st->pin);
    fflush(stdout);
    int last = -1;
    while (!g_quit) {
        if (st->games_added != last) { last = st->games_added; printf("juegos agregados: %d (%s)\n", last, st->last_added); fflush(stdout); }
        usleep(100000);
    }
    upload_stop();
    return 0;
}
