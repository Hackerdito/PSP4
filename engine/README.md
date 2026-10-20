# Motor PSP de VAULT

VAULT trae su propio emulador de PSP dentro del mismo PKG. Al elegir un juego, la pantalla de VAULT se cierra y el motor arranca ese juego en el mismo proceso. No hace falta instalar otra app.

El motor es **PPSSPP** con el port a PS4 de **[M33](https://github.com/iHaiDeeZ/M33)** de ShiroKlein:

| Pieza | Qué hace |
| --- | --- |
| CPU | El JIT x86-64 de PPSSPP traduce el código MIPS de la PSP al CPU de la PS4 |
| Gráficos | Vulkan de PPSSPP sobre RADV (Mesa), con un back end que envía directo a la GPU de la PS4 |
| Video y audio | FFmpeg 7.1 (H.264, ATRAC3/3+, MP3, AAC) y sceAudioOut |
| Control | DualShock 4; **L3 + R3** abre el menú rápido (guardar/cargar estado, trucos, salir) |

## Archivos

| Archivo | Qué es |
| --- | --- |
| `build-engine.sh` | Compila todo desde el código fuente: toolchain (clang 20, libc++ 21, musl), dependencias, Mesa y PPSSPP |
| `build-deps.sh` | zlib, OpenSSL, curl, libsmb2, libnfs, libpng, zstd, libzip y FFmpeg para la PS4 |
| `vault-engine.patch` | Cambios de VAULT sobre PPSSPP + parche de M33: abrir la pantalla de VAULT primero, carpetas en `/data/vault`, salir al menú de la PS4 |
| `vault-port.patch` | Cambios de VAULT al runtime de PS4 de M33 (rutas de los registros) |
| `package-vault.sh` | Arma el PKG final: motor + pantalla de VAULT + assets |

## Compilar

```bash
engine/build-engine.sh           # varias horas la primera vez
engine/package-vault.sh 01.00    # deja out/VAULT-v01.00.pkg
```

## En la consola

| Ruta | Qué |
| --- | --- |
| `/data/vault/games` | Carpeta principal de juegos |
| `/data/vault/psp` | La memory stick: `PSP/SAVEDATA` (partidas), `PSP/PPSSPP_STATE` (estados), ajustes |
| `/data/vault/psp/boot-trace.log`, `crash.log` | Registros para reportar errores |

## Licencias

PPSSPP y el port de M33 son GPL-2.0-or-later; Mesa es MIT; FFmpeg es LGPL/GPL. VAULT es GPL-3.0-or-later, y el PKG combinado se distribuye bajo GPL-3.0. No incluye juegos ni firmware de Sony.
