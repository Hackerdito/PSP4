import { Game, GameFormat, GameSystem, SystemInfo, UploadSession } from '../types';
import { INITIAL_GAMES, MOCK_AUTH_TOKEN, MOCK_SYSTEM_INFO, MOCK_VALID_PIN } from './mockData';
import { getMockGameSvg } from './mockCovers';
import { calculateCRC32 } from '../upload/crc32';

export interface MockState {
  token: string | null;
  info: SystemInfo;
  games: Game[];
  uploads: Map<string, {
    id: string;
    fileName: string;
    sizeBytes: number;
    system: GameSystem;
    receivedBytes: number;
    chunkSize: number;
    startedAt: number;
  }>;
  // Simulation flags for testing
  simulateNetworkCutNextChunk: boolean;
  simulateCrcMismatchNextChunk: boolean;
  simulateSpeedLimit: boolean; // ~5 MB/s
  forceOffline: boolean;
}

class MockService {
  private state: MockState = {
    token: null,
    info: { ...MOCK_SYSTEM_INFO },
    games: [...INITIAL_GAMES],
    uploads: new Map(),
    simulateNetworkCutNextChunk: false,
    simulateCrcMismatchNextChunk: false,
    simulateSpeedLimit: true,
    forceOffline: false,
  };

  private listeners = new Set<() => void>();

  constructor() {
    // Check if token was saved previously in session
    try {
      const savedToken = sessionStorage.getItem('sampsp4_token');
      if (savedToken) {
        this.state.token = savedToken;
      }
    } catch {
      // ignore
    }
  }

  public subscribe(listener: () => void) {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  private notify() {
    this.listeners.forEach((fn) => fn());
  }

  public getState() {
    return this.state;
  }

  public setSimulateNetworkCut(value: boolean) {
    this.state.simulateNetworkCutNextChunk = value;
    this.notify();
  }

  public setSimulateCrcMismatch(value: boolean) {
    this.state.simulateCrcMismatchNextChunk = value;
    this.notify();
  }

  public setForceOffline(value: boolean) {
    this.state.forceOffline = value;
    this.notify();
  }

  public setSpeedLimit(enabled: boolean) {
    this.state.simulateSpeedLimit = enabled;
    this.notify();
  }

  public resetToDefault() {
    this.state.info = { ...MOCK_SYSTEM_INFO };
    this.state.games = [...INITIAL_GAMES];
    this.state.uploads.clear();
    this.state.simulateNetworkCutNextChunk = false;
    this.state.simulateCrcMismatchNextChunk = false;
    this.state.forceOffline = false;
    this.notify();
  }

  // --- API Handlers ---

  public async pair(pin: string): Promise<{ token: string }> {
    if (this.state.forceOffline) {
      throw new Error('Sin conexión con PS4');
    }
    // Artificial slight network latency
    await delay(180);

    if (pin.trim() === MOCK_VALID_PIN) {
      this.state.token = MOCK_AUTH_TOKEN;
      try {
        sessionStorage.setItem('sampsp4_token', MOCK_AUTH_TOKEN);
      } catch {
        // ignore
      }
      this.notify();
      return { token: MOCK_AUTH_TOKEN };
    }

    const err = new Error('PIN inválido');
    (err as any).status = 401;
    (err as any).data = { error: 'pin_invalido' };
    throw err;
  }

  public async getInfo(): Promise<SystemInfo> {
    if (this.state.forceOffline) {
      throw new Error('Sin conexión con PS4');
    }
    await delay(100);
    return { ...this.state.info };
  }

  public async getGames(): Promise<Game[]> {
    if (this.state.forceOffline) {
      throw new Error('Sin conexión con PS4');
    }
    await delay(120);
    return [...this.state.games];
  }

  public getCoverSvg(id: string): string {
    const game = this.state.games.find((g) => g.id === id);
    if (!game) {
      return getMockGameSvg(id, 'Desconocido', 'psp');
    }
    return getMockGameSvg(game.id, game.title, game.system);
  }

  public async updateGame(id: string, title: string): Promise<Game> {
    if (this.state.forceOffline) {
      throw new Error('Sin conexión con PS4');
    }
    await delay(150);
    const index = this.state.games.findIndex((g) => g.id === id);
    if (index === -1) {
      const err = new Error('Juego no encontrado');
      (err as any).status = 404;
      throw err;
    }
    const updated: Game = {
      ...this.state.games[index],
      title: title.trim() || this.state.games[index].title
    };
    this.state.games[index] = updated;
    this.notify();
    return updated;
  }

  public async deleteGame(id: string): Promise<void> {
    if (this.state.forceOffline) {
      throw new Error('Sin conexión con PS4');
    }
    await delay(150);
    const game = this.state.games.find((g) => g.id === id);
    if (game) {
      this.state.info.freeBytes += game.sizeBytes;
      this.state.games = this.state.games.filter((g) => g.id !== id);
      this.notify();
    }
  }

  public async createUpload(
    fileName: string,
    sizeBytes: number,
    system: GameSystem
  ): Promise<UploadSession> {
    if (this.state.forceOffline) {
      throw new Error('Sin conexión con PS4');
    }
    await delay(150);

    const ext = fileName.toLowerCase().split('.').pop() || '';
    const allowedPsp = ['iso', 'cso', 'pbp'];
    const allowedPs1 = ['chd', 'pbp', 'cue', 'bin'];

    const isAllowed =
      (system === 'psp' && allowedPsp.includes(ext)) ||
      (system === 'ps1' && allowedPs1.includes(ext));

    if (!isAllowed) {
      const err = new Error('Formato no soportado');
      (err as any).status = 415;
      (err as any).data = { error: 'formato_no_soportado' };
      throw err;
    }

    // Check space
    if (this.state.info.freeBytes < sizeBytes) {
      const err = new Error('Sin espacio suficiente en PS4');
      (err as any).status = 507;
      (err as any).data = { error: 'sin_espacio' };
      throw err;
    }

    // Check already exists
    const cleanBaseName = fileName.replace(/\.[^/.]+$/, '').toLowerCase();
    const existing = this.state.games.find(
      (g) => g.title.toLowerCase() === cleanBaseName || g.titleId.toLowerCase() === cleanBaseName
    );
    if (existing) {
      const err = new Error('El juego ya existe en la PS4');
      (err as any).status = 409;
      (err as any).data = { error: 'ya_existe' };
      throw err;
    }

    const uploadId = 'up_' + Math.random().toString(36).substring(2, 9);
    // 8 MB chunks
    const chunkSize = 8 * 1024 * 1024;

    this.state.uploads.set(uploadId, {
      id: uploadId,
      fileName,
      sizeBytes,
      system,
      receivedBytes: 0,
      chunkSize,
      startedAt: Date.now()
    });

    return {
      uploadId,
      chunkSize,
      receivedBytes: 0
    };
  }

  public async uploadChunk(
    uploadId: string,
    offset: number,
    chunk: ArrayBuffer,
    clientCrc32Hex: string
  ): Promise<{ receivedBytes: number }> {
    if (this.state.forceOffline) {
      throw new Error('Sin conexión con PS4');
    }

    const upload = this.state.uploads.get(uploadId);
    if (!upload) {
      const err = new Error('Sesión de subida no encontrada');
      (err as any).status = 404;
      throw err;
    }

    // Check simulate network cut trigger
    if (this.state.simulateNetworkCutNextChunk) {
      this.state.simulateNetworkCutNextChunk = false;
      this.notify();
      const err = new Error('Error de red: La conexión se interrumpió abruptamente.');
      (err as any).isNetworkDrop = true;
      throw err;
    }

    // Check simulate CRC mismatch trigger
    if (this.state.simulateCrcMismatchNextChunk) {
      this.state.simulateCrcMismatchNextChunk = false;
      this.notify();
      const err = new Error('CRC incorrecto');
      (err as any).status = 422;
      (err as any).data = { error: 'crc_incorrecto' };
      throw err;
    }

    // Validate offset
    if (offset !== upload.receivedBytes) {
      const err = new Error('Offset incorrecto');
      (err as any).status = 409;
      (err as any).data = {
        error: 'offset_incorrecto',
        receivedBytes: upload.receivedBytes
      };
      throw err;
    }

    // Validate CRC32
    const actualCrc = calculateCRC32(chunk);
    if (clientCrc32Hex && clientCrc32Hex.toLowerCase() !== actualCrc.toLowerCase()) {
      const err = new Error('CRC incorrecto');
      (err as any).status = 422;
      (err as any).data = { error: 'crc_incorrecto' };
      throw err;
    }

    // Simulate transfer speed: ~5 MB/s
    // For 8 MB chunk at 5 MB/s = ~1600ms, or scaled for responsiveness
    if (this.state.simulateSpeedLimit) {
      const simulatedTimeMs = Math.max(120, Math.min(1500, Math.round((chunk.byteLength / (5 * 1024 * 1024)) * 1000)));
      await delay(simulatedTimeMs);
    } else {
      await delay(80);
    }

    upload.receivedBytes += chunk.byteLength;
    return { receivedBytes: upload.receivedBytes };
  }

  public async getUploadStatus(uploadId: string): Promise<{ receivedBytes: number; sizeBytes: number }> {
    if (this.state.forceOffline) {
      throw new Error('Sin conexión con PS4');
    }
    const upload = this.state.uploads.get(uploadId);
    if (!upload) {
      const err = new Error('Subida no encontrada');
      (err as any).status = 404;
      throw err;
    }
    return {
      receivedBytes: upload.receivedBytes,
      sizeBytes: upload.sizeBytes
    };
  }

  public async completeUpload(uploadId: string): Promise<{ game: Game }> {
    if (this.state.forceOffline) {
      throw new Error('Sin conexión con PS4');
    }
    await delay(200);

    const upload = this.state.uploads.get(uploadId);
    if (!upload) {
      const err = new Error('Subida no encontrada');
      (err as any).status = 404;
      throw err;
    }

    // Generate title ID and game title from file name
    const ext = upload.fileName.toLowerCase().split('.').pop() || '';
    const rawTitle = upload.fileName.replace(/\.[^/.]+$/, '').replace(/[_.-]/g, ' ');
    const title = capitalizeWords(rawTitle);

    let format: GameFormat = 'iso';
    if (ext === 'cso') format = 'cso';
    else if (ext === 'pbp') format = 'pbp';
    else if (ext === 'chd') format = 'chd';
    else if (ext === 'cue' || ext === 'bin') format = 'bincue';

    const prefix = upload.system === 'psp' ? 'ULES' : 'SLES';
    const randomDigits = Math.floor(10000 + Math.random() * 90000);
    const titleId = `${prefix}${randomDigits}`;

    const newGame: Game = {
      id: titleId,
      title,
      titleId,
      system: upload.system,
      format,
      sizeBytes: upload.sizeBytes,
      coverUrl: `/api/games/${titleId}/cover`,
      addedAt: new Date().toISOString()
    };

    // Deduct free space
    this.state.info.freeBytes = Math.max(0, this.state.info.freeBytes - upload.sizeBytes);
    this.state.games.unshift(newGame);
    this.state.uploads.delete(uploadId);

    this.notify();
    return { game: newGame };
  }

  public async cancelUpload(uploadId: string): Promise<void> {
    if (this.state.forceOffline) {
      throw new Error('Sin conexión con PS4');
    }
    await delay(100);
    this.state.uploads.delete(uploadId);
    this.notify();
  }
}

function delay(ms: number) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function capitalizeWords(str: string): string {
  return str
    .split(' ')
    .filter(Boolean)
    .map((w) => w.charAt(0).toUpperCase() + w.slice(1))
    .join(' ');
}

export const mockService = new MockService();
