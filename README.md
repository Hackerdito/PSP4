# VAULT: launcher de juegos PSP para PS4

VAULT es un homebrew para PS4 con jailbreak (GoldHEN). Lee tus respaldos de PSP en formato `.iso` y `.cso` y los muestra en una biblioteca moderna: portada, título real, ID de disco, tamaño y nivel de compresión.

> **Estado: v1.0, con motor integrado.** VAULT trae su propio emulador de PSP dentro del mismo PKG: PPSSPP con el port a PS4 de [M33](https://github.com/iHaiDeeZ/M33). No hace falta instalar otra app. Detalles en [`engine/`](engine/README.md).

## Instalar en tu PS4

1. Necesitas una PS4 con **GoldHEN** activo. El firmware solo tiene que poder hackearse, de 5.05 a 13.x.
2. Copia `VAULT.pkg` a una USB en **exFAT**.
3. Instálalo desde **Ajustes → Debug Settings → Game → Package Installer**.
4. Abre **VAULT** desde el menú principal.

## Dónde poner tus juegos

| Lugar | Carpeta | Cómo copiar |
| --- | --- | --- |
| **Recomendado** | `/data/vault/games/` | FTP de GoldHEN, puerto 2121 |
| Disco interno | `/data/vault/PSP/GAME/` | FTP de GoldHEN, puerto 2121 |
| USB | **cualquier carpeta**, en cualquier puerto (`usb0` a `usb7`) | VAULT revisa la memoria completa, hasta 3 carpetas de profundidad |

Formatos: `.iso` y `.cso` (CISO v1). VAULT saca de cada disco el título, el ID, `ICON0.PNG` (miniatura) y `PIC1.PNG` (portada grande).

## Controles

| Botón | Acción |
| --- | --- |
| ← → (cruceta o stick) | Moverse por la biblioteca |
| L1 / R1 | Filtro: Todos · ISO · CSO |
| ✕ | Jugar |
| △ | Detalles del archivo |
| □ | **Explorar**: navega USB y disco interno |
| OPTIONS | Volver a escanear |

### Explorar

Muestra las unidades conectadas (USB0 a USB7, juegos de M33, disco interno y todo el sistema).

| Botón | Acción |
| --- | --- |
| ↑ ↓ | Moverse |
| L1 / R1 | Página arriba o abajo |
| ✕ | Abrir una carpeta, o jugar un `.iso` / `.cso` |
| △ | Agregar la carpeta actual a tu biblioteca (se guarda en `/data/vault/folders.txt`) |
| ○ | Subir una carpeta; en la lista de unidades, cerrar |

## Cómo se abre un juego

Al presionar ✕, la pantalla de VAULT se cierra y el motor arranca el juego en la misma app.

**Durante el juego** (controles del port de M33):

| DualShock 4 | PSP |
| --- | --- |
| ✕ ○ □ △, cruceta, stick izquierdo | Igual que en la PSP |
| L1 / R1 | L / R |
| OPTIONS | START |
| Clic en el touchpad | SELECT |
| L2 (mantener) | Rebobinar |
| R2 | Avance rápido |
| **L3 + R3** | Menú rápido: guardar y cargar estado, trucos, resolución, **Salir al inicio** |

Tus partidas quedan en `/data/vault/psp/PSP/SAVEDATA`. Si algo falla, `/data/vault/psp/boot-trace.log` y `crash.log` dicen hasta dónde llegó.

## Compilar

**Con el motor (el PKG que se instala):** ver [`engine/README.md`](engine/README.md) (`engine/build-engine.sh` y `engine/package-vault.sh`).

**Solo la pantalla de VAULT** (sin motor, para probar la interfaz):

En Linux o WSL (Ubuntu):

```bash
sudo apt install clang lld make golang python3 dotnet-sdk-8.0
./tools/setup-sdk.sh                 # arma el SDK de OpenOrbis desde el código fuente en ./sdk
export OO_PS4_TOOLCHAIN=$PWD/sdk
make                                 # genera VAULT.pkg
```

También sirve un release oficial del [OpenOrbis PS4 Toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain) con `OO_PS4_TOOLCHAIN` apuntando a él.

### Probar en la PC (sin consola)

```bash
pip install pycdlib pillow
python3 tools/make-testdata.py       # crea discos PSP falsos en ./testdata
make host && ./build/host/vault-host # renderiza capturas en build/host/*.png
```

## Estructura

```
src/
  main.c            ciclo principal
  ui.c              interfaz (héroe, carrusel, filtros, ventanas)
  gfx.c             render por software: formas con antialias, imágenes y texto (stb)
  disc.c            lector de ISO/CSO + ISO9660 + PARAM.SFO
  library.c         escaneo de carpetas y metadatos
  platform_ps4.c    video, control, reloj y salida del sandbox con GoldHEN
  platform_host.c   versión de prueba para PC (capturas PNG)
assets/fonts/       Space Grotesk y JetBrains Mono (OFL)
sce_sys/            ícono y fondo del menú de la PS4
sce_module/         módulos que exige el SDK de OpenOrbis
tools/              SDK, stubs, datos de prueba, arte del paquete
```

## Plan

- [x] **Fase 1:** paquete instalable, biblioteca ISO/CSO, portadas, filtros y detalles.
- [x] **Fase 2:** jugar con el motor M33 instalado aparte.
- [x] **Fase 3:** motor PSP integrado en VAULT (un solo PKG).
- [ ] **Fase 4:** favoritos, recientes, volver a VAULT al salir de un juego.

## Créditos y licencias

- VAULT: GPL-3.0-or-later (ver `LICENSE`).
- [PPSSPP](https://github.com/hrydgard/ppsspp) (Henrik Rydgård y colaboradores) y su port a PS4 [M33](https://github.com/iHaiDeeZ/M33) de ShiroKlein, con la base de Resonance, DolphinPS4 y Crema: el motor, GPL-2.0-or-later. Mesa (RADV), MIT. FFmpeg, LGPL.
- [OpenOrbis PS4 Toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain), musl, create-fself, create-gp4 y LibOrbisPkg: GPL-3.0 / MIT.
- [GoldHEN Plugins SDK](https://github.com/GoldHEN/GoldHEN_Plugins_SDK): MIT (llamada de jailbreak).
- [stb](https://github.com/nothings/stb): dominio público / MIT.
- Space Grotesk y JetBrains Mono: SIL Open Font License 1.1.
- Nombres de símbolos de [ps4libdoc](https://github.com/idc/ps4libdoc).

Usa VAULT solo con respaldos de juegos que sean tuyos.
