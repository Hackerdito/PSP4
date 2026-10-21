// upload.h - "Subir desde el celular": un servidor HTTP en la red de casa que sirve la web de
// SAMPSP4 Upload (embebida en el eboot) y recibe juegos por bloques con CRC32.
//
// Contrato de la API: web/README.md. Todo corre en hilos propios; la pantalla solo lee
// upload_status() en cada cuadro.
#pragma once
#include <stdint.h>

#define UPLOAD_PORT 8080

typedef struct {
    volatile int running;          // el servidor está escuchando
    char url[64];                  // "http://192.168.1.50:8080", "" si no hay red
    char pin[8];                   // 6 dígitos, nuevo cada vez que se abre la pantalla
    char error[160];               // por qué no arrancó
    volatile int paired;           // celulares emparejados en esta sesión
    // La subida en curso (una a la vez desde la web)
    volatile int active;
    char current[200];             // nombre del archivo
    volatile uint64_t done, total; // bytes
    // Cada juego completado suma 1: la pantalla vuelve a escanear la biblioteca al cerrarse.
    volatile int games_added;
    char last_added[200];
    volatile uint64_t last_activity_us;
} UploadStatus;

// Arranca el servidor en UPLOAD_PORT con un PIN nuevo. 1 si quedó escuchando (o ya lo estaba).
int upload_start(void);
// Deja de aceptar conexiones; una subida en curso se puede reanudar después.
void upload_stop(void);
const UploadStatus *upload_status(void);

// Carpeta donde se guardan los juegos subidos (la primera de la biblioteca).
const char *upload_games_dir(void);
// Títulos que el usuario cambió desde la web: "ruta\ttítulo" por línea. library.c los aplica.
const char *upload_titles_file(void);
// Busca el título personalizado de una ruta. 1 si hay uno.
int upload_title_override(const char *path, char *out, int out_len);
