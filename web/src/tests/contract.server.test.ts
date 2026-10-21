// Prueba de contrato: el cliente real de la web (apiClient + UploadEngine, sin mock) contra el
// servidor C de SAMPSP4 corriendo en la PC.
//
//   make upload-host
//   SAMPSP4_PIN=424242 ../build/host/upload-host &        (desde una carpeta con build/host/)
//   SAMPSP4_SERVER=http://127.0.0.1:8080 SAMPSP4_GAME=<ruta a un .iso> npx vitest run contract
//
// Sin SAMPSP4_SERVER se omite (no corre en `npm test` normal).
import { describe, it, expect } from 'vitest';
import { readFileSync } from 'node:fs';
import { basename } from 'node:path';
import { apiClient } from '../api/client';
import { UploadEngine } from '../upload/engine';

const SERVER = process.env.SAMPSP4_SERVER;
const GAME = process.env.SAMPSP4_GAME;
const PIN = process.env.SAMPSP4_PIN || '424242';

describe.skipIf(!SERVER || !GAME)('Contrato con el servidor de la PS4', () => {
  it('empareja, sube un juego completo con el motor real y lo ve en la biblioteca', async () => {
    apiClient.setMockMode(false);
    apiClient.setCustomBaseUrl(SERVER!);
    expect(apiClient.isMockMode()).toBe(false);

    await expect(apiClient.pair('000000')).rejects.toThrow('PIN inválido');
    const token = await apiClient.pair(PIN);
    expect(token).toHaveLength(32);

    const info = await apiClient.getInfo();
    expect(info.app).toBe('SAMPSP4');

    const bytes = readFileSync(GAME!);
    const file = new File([bytes], basename(GAME!));
    const engine = new UploadEngine();
    const result = await new Promise<{ title: string; titleId: string; status: string }>((resolve, reject) => {
      engine.subscribe({
        onQueueUpdated: (q) => {
          if (q[0]?.status === 'error') reject(new Error(q[0].errorMessage));
        },
        onItemCompleted: (item, game) => resolve({ title: game.title, titleId: game.titleId, status: item.status }),
        onItemFailed: (_item, err) => reject(new Error(err)),
      });
      engine.addFiles([file]);
    });
    expect(result.status).toBe('completed');
    expect(result.titleId).toMatch(/^[A-Z]{4}\d{5}$/);

    const games = await apiClient.getGames();
    const g = games.find((x) => x.titleId === result.titleId);
    expect(g).toBeDefined();
    expect(g!.sizeBytes).toBe(bytes.length);

    const renamed = await apiClient.updateGame(g!.id, 'Renombrado desde la web');
    expect(renamed.title).toBe('Renombrado desde la web');

    await apiClient.deleteGame(g!.id);
    expect((await apiClient.getGames()).find((x) => x.id === g!.id)).toBeUndefined();
  }, 60000);
});
