# SAMPSP4 Upload

**SAMPSP4 Upload** es una aplicación web *mobile-first* diseñada para transferir juegos de PlayStation Portable (PSP) y PlayStation 1 (PS1) desde un teléfono móvil, tablet o PC hacia la aplicación homebrew **SAMPSP4** ejecutada en una PlayStation 4, a través de la red local WiFi.

---

## 📺 Contexto Técnico

1. **Servidor Local Embebido en PS4**:
   - La web no se hospeda en internet. La sirve la propia PS4 desde el servidor de SAMPSP4 (`src/upload.c`, en C) en `http://IP-DE-LA-PS4:8080`.
   - `make web` (en la raíz del repositorio) la compila y la embebe comprimida en `src/web_assets.c`, dentro del eboot.
   - El usuario escanea un código QR en el televisor que abre esta aplicación en el navegador del celular.
2. **Contexto HTTP sin HTTPS (Inseguro)**:
   - `crypto.subtle` **no existe** en contextos HTTP no seguros. Se incluye una implementación propia y optimizada de **CRC32** (estándar IEEE 802.3 con tabla precomputada de 256 entradas).
   - No se emplean Service Workers ni PWA.
3. **Cero Dependencias Externas (Aislamiento Total)**:
   - Funciona sin conexión a internet: no utiliza CDNs, ni Google Fonts, ni scripts de analítica.
   - Íconos SVG empaquetados mediante *tree-shaking* de `lucide-react`.
   - Tipografía del sistema (`system-ui`, `-apple-system`, `SF Pro`, `Segoe UI`, `Roboto`).
4. **Bundle Ultraligero**:
   - Menor a 100 KB gzipped para ser empaquetado directamente dentro del archivo PKG homebrew.
   - Rutas relativas (`base: './'`) para poder servirse desde cualquier directorio de la PS4.
5. **Screen Wake Lock API**:
   - Mantiene activa la pantalla del celular durante las transferencias para evitar interrupciones de WiFi en segundo plano.

---

## 🚀 Comandos de Ejecución

| Comando | Descripción |
| :--- | :--- |
| `npm install` | Instala las dependencias del proyecto. |
| `npm run dev` | Inicia el servidor de desarrollo local de Vite en el puerto `3000`. |
| `npm run dev:mock` | Inicia la web en **Modo Simulado** (solo existe en desarrollo: el build que va en el PKG habla siempre con la PS4) con backend en memoria (simula 40 GB libres, velocidad de 5 MB/s, y corte de red). |
| `npm test` | Ejecuta las pruebas unitarias con **Vitest** (CRC32, bloques, recuperación de cortes y reintentos 409/422). |
| `npm run build` | Compila el bundle de producción en rutas relativas dentro de la carpeta `dist/`. |

---

## 📡 Tabla de la API de SAMPSP4

Todas las respuestas son en formato JSON. A excepción de `/api/pair` e `/api/info`, todas las rutas requieren el encabezado:
```http
Authorization: Bearer <token>
```

| Método | Endpoint | Cabeceras adicionales / Cuerpo | Respuesta Exitosa | Códigos y Errores |
| :--- | :--- | :--- | :--- | :--- |
| **POST** | `/api/pair` | Body: `{ "pin": "123456" }` | `200 { "token": "abc..." }` | `401 { "error": "pin_invalido" }` |
| **POST** | `/api/pair` (5 fallos) | — | — | `429 { "error": "demasiados_intentos" }` durante 30 s |
| **GET** | `/api/info` | — | `200 { "app": "SAMPSP4", "version": "1.9", "freeBytes": N, "totalBytes": N }` | `totalBytes: 0` = la consola no pudo medir el disco (la web dice "no disponible") |
| **GET** | `/api/games` | — | `200 [ { "id": "<16 hex>", "title": "...", "titleId": "ULUS10041", "system": "psp", "format": "iso", "sizeBytes": 0, "coverUrl": "/api/games/<id>/cover", "addedAt": "" } ]` | `401 { "error": "sesion_expirada" }` |
| **GET** | `/api/games/:id/cover` | — | `200 image/png` (o SVG) | `404` (la web muestra placeholder estilizado) |
| **PATCH** | `/api/games/:id` | Body: `{ "title": "Nuevo nombre" }` | `200 { ...juegoActualizado }` | `404` No encontrado |
| **DELETE** | `/api/games/:id` | — | `204 No Content` | `404` No encontrado |
| **POST** | `/api/uploads` | Body: `{ "fileName": "juego.iso", "sizeBytes": 1234567, "system": "psp" \| "ps1" }` | `200 { "uploadId": "u1", "chunkSize": 8388608, "receivedBytes": 0 }` | `409 { "error": "ya_existe" }`<br>`507 { "error": "sin_espacio" }`<br>`415 { "error": "formato_no_soportado" }` |
| **PUT** | `/api/uploads/:id/chunk?offset=N` | `Content-Type: application/octet-stream`<br>`X-Chunk-CRC32: <hex>`<br>Body: Bytes crudos del bloque | `200 { "receivedBytes": N }` | `409 { "error": "offset_incorrecto", "receivedBytes": N }`<br>`422 { "error": "crc_incorrecto" }` |
| **GET** | `/api/uploads/:id` | — | `200 { "receivedBytes": N, "sizeBytes": N }` | `404` Subida no encontrada |
| **POST** | `/api/uploads/:id/complete` | — | `200 { "game": { ...juego } }` | `409 { "error": "incompleto", "receivedBytes": N }` |
| **DELETE** | `/api/uploads/:id` | — | `204 No Content` | Cancelar y purgar archivo temporal |

---

### Cómo lo implementa la PS4 (`src/upload.c`)

- Los juegos de la API son los de `/data/vault/games` (2 niveles), leídos con la misma función que la biblioteca de la TV (`library_probe`), así que títulos, IDs y secciones coinciden. El `id` es un hash de la ruta.
- Una subida se identifica por nombre + tamaño: si se crea otra vez (por ejemplo, tras cerrar y abrir SAMPSP4), devuelve el mismo `uploadId` y el `receivedBytes` del archivo temporal en `/data/vault/games/.subidas/`.
- Un bloque se escribe solo completo y con su CRC32 correcto; se puede reanudar desde cualquier byte.
- Los nombres se limpian (sin `/`, `..` ni caracteres de control): nada se escribe fuera de la carpeta de juegos.
- Un `.cue` + `.bin` son subidas separadas: la web manda primero los `.bin` y al final el `.cue`, y completa cada una.
- Pruebas: `make test-upload` (34 comprobaciones contra el servidor real) y `src/tests/contract.server.test.ts` (este cliente contra ese servidor).

## 🧩 Formatos y Reglas de Subida

- **PSP**: `.iso`, `.cso`, `.pbp`.
- **PS1**: `.chd`, `.pbp`, y `.cue` + `.bin` juntos.
  - La aplicación detecta automáticamente archivos `.cue` junto a sus archivos de pistas `.bin` y los agrupa en un solo elemento visual de subida.
- **Lectura por bloques sin carga en memoria**: Se utiliza `File.slice(offset, end)` para que juegos de hasta 4 GB se transmitan en fragmentos (por defecto de 8 MB) sin desbordar la memoria RAM del navegador del celular.
- **Verificación CRC32**: Cada bloque genera su suma de comprobación antes del envío. Si el servidor responde `422 crc_incorrecto`, se reintenta dicho bloque con retroceso exponencial (*backoff*).
- **Recuperación ante fallos de conexión (Resuming)**:
  - Si la red WiFi se desconecta, el motor sondea `GET /api/uploads/:id` al volver la conexión y reanuda exactamente desde `receivedBytes`.
  - Si el servidor reporta `409 offset_incorrecto`, el cliente adopta el `receivedBytes` del servidor de inmediato.

---

## 🛠️ Estructura del Código

```
src/
├── api/
│   └── client.ts            # Cliente tipado de la API (fetch HTTP + enrutador mock)
├── upload/
│   ├── crc32.ts             # Implementación pura en JS de CRC32 (sin crypto.subtle)
│   └── engine.ts            # Motor de subida por bloques, reintentos y wake lock
├── mock/
│   ├── mockData.ts          # 4 juegos de ejemplo, PIN 123456 y espacio de 40 GB
│   ├── mockCovers.ts        # Generador de portadas SVG temáticas de alta fidelidad
│   └── mockService.ts       # Servidor en memoria con simulación de velocidad y fallos
├── components/
│   ├── Header.tsx           # Barra superior Apple TV con contrato de 3 zonas
│   ├── StorageBar.tsx       # Visualizador de espacio libre vs total en la PS4
│   ├── FileDropzone.tsx     # Zona de arrastre y selección táctil de juegos
│   ├── UploadQueue.tsx      # Cola de subida con progreso, MB/s y tiempo restante
│   ├── GameCard.tsx         # Tarjeta con portada, Title ID y formato
│   ├── GameDetailModal.tsx  # Ficha de detalle para renombrar o borrar con confirmación
│   ├── PinInput.tsx         # 6 casillas táctiles de PIN con avance y pegado automático
│   ├── MockControlsModal.tsx# Panel para simular cortes de red y errores de CRC en vivo
│   └── WakeLockNotice.tsx   # Notificación de pantalla activa
├── screens/
│   ├── PairScreen.tsx       # Pantalla de emparejamiento con TV
│   ├── UploadScreen.tsx     # Pantalla principal de subida y cola
│   ├── LibraryScreen.tsx    # Biblioteca con filtro PSP/PS1 y buscador
│   └── DisconnectedScreen.tsx # Pantalla de fallo de conexión y reintento
├── utils/
│   └── format.ts            # Formateo de bytes, velocidad (MB/s) y tiempo
└── tests/
    ├── uploadEngine.test.ts # Pruebas unitarias de CRC32, chunking, 409 y 422
    ├── integrity.test.ts    # El archivo llega idéntico aunque se pierdan respuestas; orden .bin -> .cue
    └── contract.server.test.ts # Contra el servidor real de SAMPSP4 (se omite sin SAMPSP4_SERVER)
```

---

## 🎨 Diseño Visual

- **Paleta de Colores**: Fondo negro `#000000`, tarjetas `#121214`, borde `#242428`, acento azul Apple TV `#0A84FF`.
- **Zonas Táctiles**: Diseñadas para uso ergonómico a una sola mano (mínimo 48 px de área táctil).
- **Metadatos Sin Píldoras**: Separadores tipográficos limpios (`·`), números tabulares monoespaciados para velocidades y tamaños de archivo.
- **Pie de página**: `SAMPSP4 · Sam Dev`.
