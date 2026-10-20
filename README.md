# VAULT: launcher de juegos PSP para PS4

VAULT es un homebrew para PS4 con jailbreak (GoldHEN). Lee tus respaldos de PSP en formato `.iso` y `.cso` y los muestra en una biblioteca moderna: portada, título real, ID de disco, tamaño y nivel de compresión.

> **Estado: fase 2.** VAULT es la biblioteca y el motor de juego es **[M33](https://github.com/iHaiDeeZ/M33)** (PPSSPP para PS4, GPL-2.0+). Con ✕, VAULT abre M33 con el juego que elegiste.

## Instalar en tu PS4

1. Necesitas una PS4 con **GoldHEN** activo. El firmware solo tiene que poder hackearse, de 5.05 a 13.x.
2. Copia `VAULT.pkg` a una USB en **exFAT**.
3. Instálalo desde **Ajustes → Debug Settings → Game → Package Installer**.
4. Instala también el motor **M33 v1.03 o más nuevo** ([releases](https://github.com/iHaiDeeZ/M33/releases/latest)) de la misma forma.
5. Abre **VAULT** desde el menú principal.

## Dónde poner tus juegos

| Lugar | Carpeta | Cómo copiar |
| --- | --- | --- |
| **Recomendado** | `/data/March33/games/` | FTP. Es la carpeta de M33: tus juegos aparecen en las dos apps |
| Disco interno | `/data/vault/PSP/GAME/` | FTP de GoldHEN, puerto 2121. VAULT crea la carpeta al abrirse por primera vez |
| USB | `/vault/PSP/GAME/` en la raíz de la memoria | Desde tu PC |
| USB (estilo PSP) | `/PSP/GAME/` en la raíz de la memoria | Desde tu PC |

Formatos: `.iso` y `.cso` (CISO v1). VAULT saca de cada disco el título, el ID, `ICON0.PNG` (miniatura) y `PIC1.PNG` (portada grande).

## Controles

| Botón | Acción |
| --- | --- |
| ← → (cruceta o stick) | Moverse por la biblioteca |
| L1 / R1 | Filtro: Todos · ISO · CSO |
| ✕ | Jugar (abre M33 con el juego) |
| △ | Detalles del archivo |
| □ | Volver a escanear |
| ○ | Cerrar ventana |

## Cómo se abre un juego

Al presionar ✕, VAULT:

1. Escribe la ruta del juego en `/data/March33/launch.txt`.
2. Abre M33 (`MRCH00033`) con `sceLncUtilLaunchApp`, pasándole la ruta como argumento.

M33 1.03 todavía no lee ninguna de las dos cosas: se abre en su propia lista de juegos y ahí eliges el juego. Por eso conviene guardarlos en `/data/March33/games/`. El cambio para que M33 arranque el juego directo está en [`engine/m33-launch-args.patch`](engine/m33-launch-args.patch): son 24 líneas en `ps4/PS4Main.cpp`, listas para proponérselas al autor de M33.

Para volver a VAULT: menú rápido de M33 (L3 + R3) → Salir, o el botón PS.

## Compilar

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
- [x] **Fase 2:** jugar con el motor M33 (PPSSPP para PS4) instalado aparte.
- [ ] **Fase 2b:** que M33 arranque el juego directo (`engine/m33-launch-args.patch`).
- [ ] **Fase 3:** guardados, favoritos, recientes y ajustes por juego.

## Créditos y licencias

- VAULT: GPL-3.0-or-later (ver `LICENSE`).
- [M33](https://github.com/iHaiDeeZ/M33) de ShiroKlein: el motor PSP (PPSSPP), GPL-2.0-or-later. VAULT no lo incluye; solo lo abre.
- [OpenOrbis PS4 Toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain), musl, create-fself, create-gp4 y LibOrbisPkg: GPL-3.0 / MIT.
- [GoldHEN Plugins SDK](https://github.com/GoldHEN/GoldHEN_Plugins_SDK): MIT (llamada de jailbreak).
- [stb](https://github.com/nothings/stb): dominio público / MIT.
- Space Grotesk y JetBrains Mono: SIL Open Font License 1.1.
- Nombres de símbolos de [ps4libdoc](https://github.com/idc/ps4libdoc).

Usa VAULT solo con respaldos de juegos que sean tuyos.
