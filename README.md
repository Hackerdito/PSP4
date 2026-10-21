# SAMPSP4: juegos de PSP y PS1 en tu PS4

SAMPSP4 es un homebrew para PS4 con jailbreak (GoldHEN). Lee tus respaldos de **PSP** (`.iso`, `.cso`, `EBOOT.PBP`, `.chd`) y de **PS1** (`.cue/.bin`, `.chd`, `.iso`, `EBOOT.PBP`) y los muestra en una biblioteca moderna, separada en tres secciones: **ISO / CSO**, **EBOOT y Homebrew** y **PS1**.

> **Estado: v1.9, con dos motores integrados, descargas desde tu PC y subida desde el celular.** PSP: PPSSPP con el port a PS4 de [M33](https://github.com/iHaiDeeZ/M33). PS1: [PCSX-ReARMed](https://github.com/libretro/pcsx_rearmed) con su recompilador lightrec. Todo dentro del mismo PKG; no hace falta instalar otra app. Detalles en [`engine/`](engine/README.md).

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
| USB | **cualquier carpeta**, en cualquier puerto (`usb0` a `usb7`) | SAMPSP4 revisa la memoria completa, hasta 4 carpetas de profundidad |

| Sección | Formatos |
| --- | --- |
| **ISO / CSO** (PSP) | `.iso`, `.cso` (CISO v1), `.chd` de UMD |
| **EBOOT y Homebrew** (PSP) | `EBOOT.PBP`, cada uno en su carpeta (p. ej. `PSP/GAME/NPUH10000/EBOOT.PBP`) |
| **PS1** | `.cue` + `.bin`, `.chd` de CD, `.iso` de PS1, `EBOOT.PBP` de PSone Classics |

De PSP saca el título, el ID, `ICON0.PNG` (miniatura) y `PIC1.PNG` (portada grande). De PS1 lee el ID del disco (`SYSTEM.CNF`); el nombre viene del archivo. Un `.bin` que tiene su `.cue` al lado no se lista aparte.

## Mi PC: juegos desde tu computadora

SAMPSP4 copia juegos desde una **carpeta compartida** de tu PC (Windows o Mac) por la red de tu casa. No hay que instalar nada en la PC: es el "compartir archivos" (SMB) que ya traen los dos sistemas.

**En la PC (una vez):**

- **Windows:** clic derecho en la carpeta de tus juegos → **Propiedades → Compartir → Compartir…**, agrega tu usuario. La IP sale con `ipconfig` (algo como `192.168.1.50`). Si inicias sesión con cuenta de Microsoft, el usuario es tu correo y la contraseña la de esa cuenta.
- **Mac:** **Ajustes del Sistema → General → Compartir → Compartir archivos**, agrega la carpeta; en **Opciones…** activa "Compartir archivos y carpetas mediante SMB" y marca tu usuario. La IP está en **Ajustes → Red**.

**En la PS4:** **L2** en la biblioteca (o **Explorar → Mi PC**). La primera vez escribe la IP (o usa **Buscar PCs en la red**), tu usuario y contraseña; se guardan en `/data/vault/pc.txt`. Luego eliges la carpeta compartida, navegas, y con ✕ sobre un juego eliges dónde guardarlo:

- **Disco duro de la PS4:** `/data/vault/games`
- **Memoria USB:** `USB/SAMPSP4/`

El original **se queda en tu PC**. Un `.cue` baja con sus `.bin`; un `EBOOT.PBP`, con su carpeta. Al cerrar Mi PC la biblioteca se vuelve a escanear y el juego aparece en su sección.

## Subir desde el celular

Sube juegos a la PS4 desde el navegador del celular o de la PC, por el WiFi de tu casa. No hay que instalar nada ni usar FTP.

1. En la biblioteca presiona **R2**. La TV muestra un código QR, la dirección (algo como `http://192.168.1.50:8080`) y un PIN de 6 dígitos.
2. Con el celular en **el mismo WiFi**, escanea el QR (o escribe la dirección) y pon el PIN.
3. Elige tus juegos y toca **Subir**. El progreso se ve en el celular y en la TV.
4. Al cerrar la pantalla con **○**, la biblioteca se vuelve a escanear y los juegos aparecen en su sección.

- **Formatos:** `.iso`, `.cso`, `.pbp`, `.chd` y `.cue` + sus `.bin` (se eligen juntos; la web los agrupa).
- **Dónde quedan:** `/data/vault/games/`. Un `EBOOT.PBP` va en su propia carpeta (`/data/vault/games/<ID>/EBOOT.PBP`); un `.cue`, con sus `.bin` al lado.
- **Si se corta:** cada bloque de 8 MB lleva su CRC32 y la subida sigue donde iba, aunque cierres SAMPSP4 y la vuelvas a abrir (el temporal está en `/data/vault/games/.subidas/`).
- **Desde la web también puedes** ver tu biblioteca, cambiar el nombre de un juego (se guarda en `/data/vault/titles.txt`) o borrarlo.
- **Mientras subes, deja esta pantalla abierta**: una app homebrew no corre en segundo plano. Con cable de red es más rápido que por WiFi, sobre todo en la PS4 original (solo WiFi de 2.4 GHz).
- **Seguridad:** solo funciona en tu red local. El PIN cambia cada vez que abres la pantalla; 5 PIN incorrectos bloquean el emparejamiento 30 segundos.
- Registro: `/data/vault/upload.log`.

## Controles

| Botón | Acción |
| --- | --- |
| ← → (cruceta o stick) | Moverse por la biblioteca |
| L1 / R1 | Sección: ISO / CSO · EBOOT y Homebrew · PS1 |
| ✕ | Jugar |
| △ | Detalles del archivo |
| □ | **Explorar**: navega USB y disco interno |
| OPTIONS | Volver a escanear |
| L2 | **Mi PC**: juegos de tu computadora |
| R2 | **Subir desde el celular** |

### Explorar

Muestra las unidades conectadas (USB0 a USB7, juegos de M33, disco interno y todo el sistema).

| Botón | Acción |
| --- | --- |
| ↑ ↓ | Moverse |
| L1 / R1 | Página arriba o abajo |
| ✕ | Abrir una carpeta, o jugar un `.iso` / `.cso` / `EBOOT.PBP` / `.chd` / `.cue` / `.bin` |
| △ | Agregar la carpeta actual a tu biblioteca (se guarda en `/data/vault/folders.txt`) |
| ○ | Subir una carpeta; en la lista de unidades, cerrar |

## Cómo se abre un juego

Al presionar ✕ suena la intro de SAMPSP4 (logo y sonido propios, unos 2 segundos; ✕ u ○ la saltan) y el motor arranca el juego en la misma app.

**Juegos de PSP** (controles del port de M33):

| DualShock 4 | PSP |
| --- | --- |
| ✕ ○ □ △, cruceta, stick izquierdo | Igual que en la PSP |
| L1 / R1 | L / R |
| OPTIONS | START |
| Clic en el touchpad | SELECT |
| L2 (mantener) | Rebobinar |
| R2 | Avance rápido |
| **L3 + R3** | Menú rápido: guardar y cargar estado, trucos, resolución, **Volver a SAMPSP4** |

Tus partidas quedan en `/data/vault/psp/PSP/SAVEDATA`. Si algo falla, `/data/vault/psp/boot-trace.log` (y `boot-trace.prev.log`, la sesión anterior) y `crash.log` dicen hasta dónde llegó.

**Juegos de PS1:**

| DualShock 4 | PS1 |
| --- | --- |
| ✕ ○ □ △, cruceta, L1 R1 L2 R2 | Igual que en el control de PS1 |
| Stick izquierdo | Cruceta |
| OPTIONS | START |
| Clic en el touchpad | SELECT |
| **L3 + R3** (o touchpad + OPTIONS) | Menú de PS1 (abajo) |

**Menú de PS1** (con ← → cambias cada opción; se guardan en `/data/vault/ps1/ajustes.txt` para todos los juegos de PS1):

| Sección | Opciones |
| --- | --- |
| Partida | Continuar · Guardar estado · Cargar estado · Reiniciar · Salir a SAMPSP4 |
| Pantalla | **Tamaño**: 4:3 original / Pantalla completa / Pixel perfecto · **Filtro**: Nítido / Suave / TV (scanlines) · **Alta resolución** (2× interna) · **Mostrar FPS** |
| Control | **Tipo**: Digital / DualShock (sticks analógicos) · **Vibración** · **Ver controles** |
| Juego | **Saltar cuadros si va lento** |

- Memory card: `/data/vault/ps1/saves/<juego>.srm` (se guarda sola cada 5 s si cambió y al salir; mismo formato que RetroArch).
- Estados: `/data/vault/ps1/states/`.
- BIOS: opcional. Sin BIOS usa la HLE de PCSX-ReARMed; con tu propia `scph1001.bin` (u otra) en `/data/vault/ps1/bios/` la compatibilidad es mayor.
- Registro: `/data/vault/ps1/ps1.log`.

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
python3 tools/make-testdata.py       # crea discos PSP y PS1 falsos en ./testdata
# núcleo PS1 para Linux (una vez): PCSX-ReARMed con el Makefile.libretro, platform=unix STATIC_LINKING=1
# HAVE_PHYSICAL_CDROM=0, empaquetado como engine/build-ps1.sh en build/host/ps1core-host.o
make host && ./build/host/vault-host # renderiza capturas en build/host/*.png
VAULT_SCRIPT=secciones ./build/host/vault-host                     # otras: explorar, eboot, scroll, intro
VAULT_SCRIPT=ps1 VAULT_PS1="testdata/usb0/PS1/Midnight Rally (USA).cue" ./build/host/vault-host
```

"Subir desde el celular" sin consola:

```bash
make test-upload                                # el servidor solo + 34 comprobaciones de la API
make upload-host && SAMPSP4_PIN=123456 ./build/host/upload-host   # abre http://127.0.0.1:8080 en el navegador
python3 tests/upload_shots.py                   # capturas 34-37 de la pantalla, con un "celular" simulado
```

La web está en [`web/`](web/README.md) (React + Vite). `make web` la compila y la embebe en `src/web_assets.c`, que entra en el eboot; ese archivo está en el repositorio, así que `make` no necesita Node.

El disco PS1 de prueba trae un programa MIPS diminuto que pinta barras de color: sirve para comprobar el recompilador, la GPU y el escalado sin un juego real.

## Estructura

```
src/
  main.c            ciclo principal
  ui.c              interfaz (héroe, carrusel, filtros, ventanas)
  gfx.c             render por software: formas con antialias, imágenes y texto (stb)
  disc.c            lector de ISO/CSO/EBOOT.PBP + ISO9660 + PARAM.SFO
  library.c         escaneo de carpetas y metadatos
  upload.c          "Subir desde el celular": servidor HTTP, PIN, subidas por bloques
  ui_upload.inc.c   la pantalla con el QR (R2)
  web_assets.c      la web compilada, embebida (generado por tools/embed-web.py)
  platform_ps4.c    video, control, reloj y salida del sandbox con GoldHEN
  platform_host.c   versión de prueba para PC (capturas PNG)
assets/fonts/       Space Grotesk y JetBrains Mono (OFL)
sce_sys/            ícono y fondo del menú de la PS4
sce_module/         módulos que exige el SDK de OpenOrbis
web/                SAMPSP4 Upload: la web que se abre en el celular (React + Vite)
tests/              servidor en la PC y pruebas de la API
tools/              SDK, stubs, datos de prueba, arte del paquete, embed-web.py
```

## Plan

- [x] **Fase 1:** paquete instalable, biblioteca ISO/CSO, portadas, filtros y detalles.
- [x] **Fase 2:** jugar con el motor M33 instalado aparte.
- [x] **Fase 3:** motor PSP integrado en VAULT (un solo PKG).
- [x] **Fase 4:** volver a SAMPSP4 al salir de un juego, EBOOT.PBP y CHD, secciones, intro.
- [x] **Fase 5:** motor PS1 integrado.
- [x] **Fase 6:** Mi PC (descargas por SMB desde Windows y Mac).
- [x] **Fase 7:** subir juegos desde el celular (web servida por la PS4).
- [ ] **Siguiente:** favoritos y recientes.

## Créditos y licencias

- VAULT: GPL-3.0-or-later (ver `LICENSE`).
- [PCSX-ReARMed](https://github.com/libretro/pcsx_rearmed) (notaz y colaboradores, con lightrec y GNU Lightning): el motor PS1, GPL-2.0-or-later.
- [PPSSPP](https://github.com/hrydgard/ppsspp) (Henrik Rydgård y colaboradores) y su port a PS4 [M33](https://github.com/iHaiDeeZ/M33) de ShiroKlein, con la base de Resonance, DolphinPS4 y Crema: el motor, GPL-2.0-or-later. Mesa (RADV), MIT. FFmpeg, LGPL.
- [OpenOrbis PS4 Toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain), musl, create-fself, create-gp4 y LibOrbisPkg: GPL-3.0 / MIT.
- [libsmb2](https://github.com/sahlberg/libsmb2) (Ronnie Sahlberg): SMB2/3, LGPL-2.1.
- [GoldHEN Plugins SDK](https://github.com/GoldHEN/GoldHEN_Plugins_SDK): MIT (llamada de jailbreak).
- [stb](https://github.com/nothings/stb): dominio público / MIT.
- [QR Code generator](https://github.com/nayuki/QR-Code-generator) (Project Nayuki): el QR de la pantalla, MIT.
- La web: React, Vite, Tailwind CSS y lucide-react, MIT.
- Space Grotesk y JetBrains Mono: SIL Open Font License 1.1.
- Nombres de símbolos de [ps4libdoc](https://github.com/idc/ps4libdoc).

Usa SAMPSP4 solo con respaldos de juegos que sean tuyos.
