import { describe, it, expect, vi, beforeEach } from 'vitest';
import { UploadEngine } from '../upload/engine';
import { apiClient } from '../api/client';
import { calculateCRC32 } from '../upload/crc32';

// Servidor falso que guarda los bytes de verdad, para comprobar que el archivo llega idéntico.
function fakeServer(opts: { dropResponseAt?: number[] } = {}) {
  const files = new Map<string, { name: string; size: number; data: Uint8Array; received: number }>();
  const completed: string[] = [];
  let n = 0;
  const drop = new Set(opts.dropResponseAt ?? []);
  let chunkCalls = 0;

  vi.spyOn(apiClient, 'createUpload').mockImplementation(async (fileName, sizeBytes) => {
    const id = `u${++n}`;
    files.set(id, { name: fileName, size: sizeBytes, data: new Uint8Array(sizeBytes), received: 0 });
    return { uploadId: id, chunkSize: 1000, receivedBytes: 0 };
  });
  vi.spyOn(apiClient, 'uploadChunk').mockImplementation(async (id, offset, chunk, crc) => {
    const f = files.get(id)!;
    chunkCalls++;
    if (offset !== f.received) {
      const e: any = new Error('offset');
      e.status = 409;
      e.data = { error: 'offset_incorrecto', receivedBytes: f.received };
      throw e;
    }
    if (calculateCRC32(chunk) !== crc) {
      const e: any = new Error('crc');
      e.status = 422;
      throw e;
    }
    f.data.set(new Uint8Array(chunk), offset);
    f.received = offset + chunk.byteLength;
    if (drop.has(chunkCalls)) {
      // La PS4 guardó el bloque pero la respuesta se perdió en el WiFi.
      throw new TypeError('Failed to fetch');
    }
    return { receivedBytes: f.received };
  });
  vi.spyOn(apiClient, 'getUploadStatus').mockImplementation(async (id) => {
    const f = files.get(id)!;
    return { receivedBytes: f.received, sizeBytes: f.size };
  });
  vi.spyOn(apiClient, 'completeUpload').mockImplementation(async (id) => {
    const f = files.get(id)!;
    completed.push(f.name);
    return { game: { id: f.name, title: f.name, titleId: 'X', system: 'ps1', format: 'bincue', sizeBytes: f.size, coverUrl: '', addedAt: '' } };
  });
  return { files, completed };
}

function bytes(len: number, seed: number) {
  const a = new Uint8Array(len);
  for (let i = 0; i < len; i++) a[i] = (i * 31 + seed) & 0xff;
  return a;
}

function waitFor(engine: UploadEngine, status: string) {
  return new Promise<void>((resolve) => {
    const unsub = engine.subscribe({
      onQueueUpdated: (q) => {
        if (q.length && q.every((i) => i.status === status || i.status === 'error')) {
          unsub();
          resolve();
        }
      },
      onItemCompleted: () => {},
      onItemFailed: () => {},
    });
  });
}

describe('Integridad del archivo', () => {
  beforeEach(() => {
    vi.restoreAllMocks();
    vi.spyOn(globalThis, 'setTimeout').mockImplementation(((fn: () => void) => { fn(); return 0 as any; }) as any);
  });

  it('no corrompe el archivo si la PS4 guardó el bloque y la respuesta se perdió', async () => {
    const srv = fakeServer({ dropResponseAt: [2, 5] });
    const src = bytes(4500, 7);
    const engine = new UploadEngine();
    const done = waitFor(engine, 'completed');
    engine.addFiles([new File([src], 'juego.iso')]);
    await done;

    const f = [...srv.files.values()][0];
    expect(f.received).toBe(src.length);
    expect(Array.from(f.data)).toEqual(Array.from(src));
    expect(engine.getQueue()[0].status).toBe('completed');
  });

  it('sube los .bin primero y el .cue al final, y completa cada archivo', async () => {
    const srv = fakeServer();
    const engine = new UploadEngine();
    const done = waitFor(engine, 'completed');
    engine.addFiles([
      new File(['FILE "spyro (Track 1).bin" BINARY'], 'spyro.cue'),
      new File([bytes(2500, 1)], 'spyro (Track 1).bin'),
      new File([bytes(1200, 2)], 'spyro (Track 2).bin'),
    ]);
    await done;
    expect(srv.completed).toEqual(['spyro (Track 1).bin', 'spyro (Track 2).bin', 'spyro.cue']);
  });

  it('rechaza extensiones que SAMPSP4 no puede abrir', () => {
    const engine = new UploadEngine();
    engine.addFiles([new File(['x'], 'foto.jpg')]);
    const q = engine.getQueue();
    expect(q[0].status).toBe('error');
    expect(q[0].errorMessage).toMatch(/Formato no soportado/);
  });
});
