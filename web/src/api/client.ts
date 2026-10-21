import { Game, GameSystem, SystemInfo, UploadSession } from '../types';
import { mockService } from '../mock/mockService';
import { calculateCRC32 } from '../upload/crc32';

// El modo simulado existe solo en desarrollo (npm run dev / dev:mock / pruebas).
// En el build que va dentro del PKG la web habla siempre con la PS4 que la sirve.
export const mockAllowed: boolean = import.meta.env.DEV || import.meta.env.MODE === 'mock';

export class ApiClient {
  private baseUrl: string = '';
  private token: string | null = null;
  private isMock: boolean = false;

  constructor() {
    // Check if token exists in sessionStorage
    try {
      this.token = sessionStorage.getItem('sampsp4_token');
    } catch {
      // ignore
    }

    this.isMock = false;
    if (mockAllowed) {
      try {
        const savedMode = localStorage.getItem('sampsp4_mock_mode');
        this.isMock = savedMode !== null ? savedMode === 'true' : import.meta.env.MODE === 'mock';
      } catch {
        this.isMock = import.meta.env.MODE === 'mock';
      }
    }
  }

  public isMockMode(): boolean {
    return this.isMock;
  }

  public setMockMode(enabled: boolean) {
    if (!mockAllowed) return;
    this.isMock = enabled;
    try {
      localStorage.setItem('sampsp4_mock_mode', String(enabled));
    } catch {
      // ignore
    }
  }

  public setCustomBaseUrl(url: string) {
    this.baseUrl = url.replace(/\/+$/, '');
  }

  public getCustomBaseUrl(): string {
    return this.baseUrl;
  }

  public getToken(): string | null {
    return this.token;
  }

  public setToken(token: string | null) {
    this.token = token;
    try {
      if (token) {
        sessionStorage.setItem('sampsp4_token', token);
      } else {
        sessionStorage.removeItem('sampsp4_token');
      }
    } catch {
      // ignore
    }
  }

  public clearSession() {
    this.setToken(null);
  }

  private getHeaders(extraHeaders: Record<string, string> = {}): HeadersInit {
    const headers: Record<string, string> = {
      ...extraHeaders,
    };
    if (this.token) {
      headers['Authorization'] = `Bearer ${this.token}`;
    }
    return headers;
  }

  // POST /api/pair { pin }
  public async pair(pin: string): Promise<string> {
    if (this.isMock) {
      const res = await mockService.pair(pin);
      this.setToken(res.token);
      return res.token;
    }

    const res = await fetch(`${this.baseUrl}/api/pair`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ pin }),
    });

    if (!res.ok) {
      const data = await res.json().catch(() => ({}));
      const err = new Error(data.error === 'pin_invalido' ? 'PIN inválido' : 'Error al emparejar');
      (err as any).status = res.status;
      (err as any).data = data;
      throw err;
    }

    const data = await res.json();
    this.setToken(data.token);
    return data.token;
  }

  // GET /api/info
  public async getInfo(): Promise<SystemInfo> {
    if (this.isMock) {
      return await mockService.getInfo();
    }

    const res = await fetch(`${this.baseUrl}/api/info`, {
      headers: this.getHeaders(),
    });

    if (!res.ok) {
      const err = new Error('No se pudo obtener información de la consola');
      (err as any).status = res.status;
      throw err;
    }

    return await res.json();
  }

  // GET /api/games
  public async getGames(): Promise<Game[]> {
    if (this.isMock) {
      return await mockService.getGames();
    }

    const res = await fetch(`${this.baseUrl}/api/games`, {
      headers: this.getHeaders(),
    });

    if (res.status === 401) {
      this.clearSession();
      const err = new Error('Sesión expirada');
      (err as any).status = 401;
      throw err;
    }

    if (!res.ok) {
      const err = new Error('Error al listar juegos');
      (err as any).status = res.status;
      throw err;
    }

    return await res.json();
  }

  // GET /api/games/:id/cover
  public getCoverUrl(id: string): string {
    if (this.isMock) {
      // In mock mode, we create an inline SVG data URI
      const svg = mockService.getCoverSvg(id);
      return `data:image/svg+xml;utf8,${encodeURIComponent(svg)}`;
    }
    return `${this.baseUrl}/api/games/${encodeURIComponent(id)}/cover`;
  }

  // Portadas: fetch con el token (un <img> no puede mandarlo), guardadas como blob: URL.
  // Hasta 4 a la vez, para no saturar a la PS4 con una biblioteca grande.
  private coverCache = new Map<string, Promise<string | null>>();
  private coverActive = 0;
  private coverWaiting: Array<() => void> = [];

  public loadCover(id: string): Promise<string | null> {
    if (this.isMock) return Promise.resolve(this.getCoverUrl(id));
    let p = this.coverCache.get(id);
    if (!p) {
      p = this.withCoverSlot(async () => {
        try {
          const res = await fetch(this.getCoverUrl(id), { headers: this.getHeaders() });
          if (!res.ok) return null;
          return URL.createObjectURL(await res.blob());
        } catch {
          return null;
        }
      });
      this.coverCache.set(id, p);
      // Un fallo de red no se queda guardado: se reintenta la próxima vez.
      p.then((u) => {
        if (u === null) this.coverCache.delete(id);
      });
    }
    return p;
  }

  private async withCoverSlot<T>(job: () => Promise<T>): Promise<T> {
    if (this.coverActive >= 4) await new Promise<void>((go) => this.coverWaiting.push(go));
    this.coverActive++;
    try {
      return await job();
    } finally {
      this.coverActive--;
      this.coverWaiting.shift()?.();
    }
  }

  // PATCH /api/games/:id { title }
  public async updateGame(id: string, title: string): Promise<Game> {
    if (this.isMock) {
      return await mockService.updateGame(id, title);
    }

    const res = await fetch(`${this.baseUrl}/api/games/${encodeURIComponent(id)}`, {
      method: 'PATCH',
      headers: this.getHeaders({ 'Content-Type': 'application/json' }),
      body: JSON.stringify({ title }),
    });

    if (!res.ok) {
      const err = new Error('Error al actualizar el juego');
      (err as any).status = res.status;
      throw err;
    }

    return await res.json();
  }

  // DELETE /api/games/:id
  public async deleteGame(id: string): Promise<void> {
    if (this.isMock) {
      return await mockService.deleteGame(id);
    }

    const res = await fetch(`${this.baseUrl}/api/games/${encodeURIComponent(id)}`, {
      method: 'DELETE',
      headers: this.getHeaders(),
    });

    if (!res.ok && res.status !== 204) {
      const err = new Error('Error al eliminar juego');
      (err as any).status = res.status;
      throw err;
    }
  }

  // POST /api/uploads { fileName, sizeBytes, system }
  public async createUpload(
    fileName: string,
    sizeBytes: number,
    system: GameSystem
  ): Promise<UploadSession> {
    if (this.isMock) {
      return await mockService.createUpload(fileName, sizeBytes, system);
    }

    const res = await fetch(`${this.baseUrl}/api/uploads`, {
      method: 'POST',
      headers: this.getHeaders({ 'Content-Type': 'application/json' }),
      body: JSON.stringify({ fileName, sizeBytes, system }),
    });

    if (!res.ok) {
      const data = await res.json().catch(() => ({}));
      const msg =
        data.error === 'sin_espacio'
          ? 'Espacio insuficiente en PS4'
          : data.error === 'ya_existe'
          ? 'El juego ya existe en la PS4'
          : data.error === 'formato_no_soportado'
          ? 'Formato de archivo no soportado'
          : 'Error al iniciar subida';
      const err = new Error(msg);
      (err as any).status = res.status;
      (err as any).data = data;
      throw err;
    }

    return await res.json();
  }

  // PUT /api/uploads/:id/chunk?offset=N
  public async uploadChunk(
    uploadId: string,
    offset: number,
    chunk: ArrayBuffer,
    crc32Hex: string
  ): Promise<{ receivedBytes: number }> {
    if (this.isMock) {
      return await mockService.uploadChunk(uploadId, offset, chunk, crc32Hex);
    }

    const url = `${this.baseUrl}/api/uploads/${encodeURIComponent(uploadId)}/chunk?offset=${offset}`;
    const res = await fetch(url, {
      method: 'PUT',
      headers: this.getHeaders({
        'Content-Type': 'application/octet-stream',
        'X-Chunk-CRC32': crc32Hex,
      }),
      body: chunk,
    });

    if (!res.ok) {
      const data = await res.json().catch(() => ({}));
      const err = new Error(
        data.error === 'crc_incorrecto'
          ? 'Error de CRC32 en el bloque'
          : data.error === 'offset_incorrecto'
          ? 'Offset desfasado'
          : `Error al subir bloque (${res.status})`
      );
      (err as any).status = res.status;
      (err as any).data = data;
      throw err;
    }

    return await res.json();
  }

  // GET /api/uploads/:id
  public async getUploadStatus(uploadId: string): Promise<{ receivedBytes: number; sizeBytes: number }> {
    if (this.isMock) {
      return await mockService.getUploadStatus(uploadId);
    }

    const res = await fetch(`${this.baseUrl}/api/uploads/${encodeURIComponent(uploadId)}`, {
      headers: this.getHeaders(),
    });

    if (!res.ok) {
      const err = new Error('No se pudo verificar el estado de la subida');
      (err as any).status = res.status;
      throw err;
    }

    return await res.json();
  }

  // POST /api/uploads/:id/complete
  public async completeUpload(uploadId: string): Promise<{ game: Game }> {
    if (this.isMock) {
      return await mockService.completeUpload(uploadId);
    }

    const res = await fetch(`${this.baseUrl}/api/uploads/${encodeURIComponent(uploadId)}/complete`, {
      method: 'POST',
      headers: this.getHeaders(),
    });

    if (!res.ok) {
      const err = new Error('Error al finalizar el registro del juego');
      (err as any).status = res.status;
      throw err;
    }

    return await res.json();
  }

  // DELETE /api/uploads/:id
  public async cancelUpload(uploadId: string): Promise<void> {
    if (this.isMock) {
      return await mockService.cancelUpload(uploadId);
    }

    const res = await fetch(`${this.baseUrl}/api/uploads/${encodeURIComponent(uploadId)}`, {
      method: 'DELETE',
      headers: this.getHeaders(),
    });

    if (!res.ok && res.status !== 204) {
      const err = new Error('Error al cancelar subida');
      (err as any).status = res.status;
      throw err;
    }
  }
}

export const apiClient = new ApiClient();
