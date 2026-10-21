import { apiClient } from '../api/client';
import { Game, GameFormat, GameSystem, UploadItemStatus, UploadQueueItem } from '../types';
import { calculateCRC32 } from './crc32';

export interface UploadEngineListener {
  onQueueUpdated: (queue: UploadQueueItem[]) => void;
  onItemCompleted: (item: UploadQueueItem, game: Game) => void;
  onItemFailed: (item: UploadQueueItem, error: string) => void;
}

export class UploadEngine {
  private queue: UploadQueueItem[] = [];
  private isProcessing = false;
  private currentAbortController: AbortController | null = null;
  private listeners = new Set<UploadEngineListener>();
  private wakeLockSentinel: any = null;

  // Smoothing speed metrics: stores { time: number, bytes: number }
  private speedSamples: { time: number; bytes: number }[] = [];

  constructor() {
    this.handleVisibilityChange = this.handleVisibilityChange.bind(this);
    if (typeof document !== 'undefined') {
      document.addEventListener('visibilitychange', this.handleVisibilityChange);
    }
  }

  public subscribe(listener: UploadEngineListener): () => void {
    this.listeners.add(listener);
    listener.onQueueUpdated([...this.queue]);
    return () => this.listeners.delete(listener);
  }

  private notifyQueue() {
    const copy = [...this.queue];
    this.listeners.forEach((l) => l.onQueueUpdated(copy));
  }

  public getQueue(): UploadQueueItem[] {
    return [...this.queue];
  }

  /**
   * Helper to identify system and format based on file extension
   */
  public static detectSystemAndFormat(fileName: string): { system: GameSystem; format: GameFormat } | null {
    const ext = fileName.toLowerCase().split('.').pop() || '';
    if (ext === 'iso') return { system: 'psp', format: 'iso' };
    if (ext === 'cso') return { system: 'psp', format: 'cso' };
    if (ext === 'chd') return { system: 'ps1', format: 'chd' };
    if (ext === 'cue' || ext === 'bin') return { system: 'ps1', format: 'bincue' };
    if (ext === 'pbp') return { system: 'psp', format: 'pbp' }; // PBP: PSP por defecto (puede ser EBOOT de PS1, el usuario lo cambia)
    return null;
  }

  /**
   * Adds files to the queue, grouping .cue and .bin together if detected
   */
  public addFiles(files: File[]) {
    // Check for cue + bin grouping
    const cueFiles = files.filter((f) => f.name.toLowerCase().endsWith('.cue'));
    const binFiles = files.filter((f) => f.name.toLowerCase().endsWith('.bin'));
    const otherFiles = files.filter(
      (f) => !f.name.toLowerCase().endsWith('.cue') && !f.name.toLowerCase().endsWith('.bin')
    );

    // Group matching .cue with its .bin files
    const newItems: UploadQueueItem[] = [];

    if (cueFiles.length > 0 && binFiles.length > 0) {
      cueFiles.forEach((cue) => {
        const cueBase = cue.name.replace(/\.cue$/i, '').toLowerCase();
        // Find matching bin files (e.g. game.bin or game (Track 1).bin)
        const matchingBins = binFiles.filter((bin) =>
          bin.name.toLowerCase().startsWith(cueBase)
        );

        if (matchingBins.length > 0) {
          const totalSize = cue.size + matchingBins.reduce((acc, b) => acc + b.size, 0);
          newItems.push({
            id: 'item_' + Math.random().toString(36).substring(2, 9),
            file: cue,
            associatedFiles: matchingBins,
            displayName: `${cue.name.replace(/\.cue$/i, '')} (CUE + ${matchingBins.length} BIN)`,
            system: 'ps1',
            format: 'bincue',
            sizeBytes: totalSize,
            status: 'waiting',
            progressPercent: 0,
            receivedBytes: 0,
            currentSpeedBytesPerSec: 0,
            estimatedRemainingSec: null,
            retryCount: 0,
          });

          // Remove grouped bins so they aren't added individually
          matchingBins.forEach((b) => {
            const idx = binFiles.indexOf(b);
            if (idx > -1) binFiles.splice(idx, 1);
          });
        } else {
          // Individual CUE
          newItems.push(this.createQueueItem(cue));
        }
      });

      // Remaining orphaned bin files
      binFiles.forEach((b) => newItems.push(this.createQueueItem(b)));
    } else {
      cueFiles.forEach((f) => newItems.push(this.createQueueItem(f)));
      binFiles.forEach((f) => newItems.push(this.createQueueItem(f)));
    }

    otherFiles.forEach((f) => newItems.push(this.createQueueItem(f)));

    this.queue.push(...newItems);
    this.notifyQueue();
    this.processNextInQueue();
  }

  private createQueueItem(file: File): UploadQueueItem {
    const detected = UploadEngine.detectSystemAndFormat(file.name);
    const { system, format } = detected ?? { system: 'psp' as GameSystem, format: 'iso' as GameFormat };
    return {
      ...(detected ? {} : { errorMessage: 'Formato no soportado. Usa ISO, CSO, PBP, CHD o CUE + BIN.' }),
      id: 'item_' + Math.random().toString(36).substring(2, 9),
      file,
      displayName: file.name,
      system,
      format,
      sizeBytes: file.size,
      status: detected ? 'waiting' : 'error',
      progressPercent: 0,
      receivedBytes: 0,
      currentSpeedBytesPerSec: 0,
      estimatedRemainingSec: null,
      retryCount: 0,
    };
  }

  public updateItemSystem(id: string, system: GameSystem) {
    const item = this.queue.find((q) => q.id === id);
    if (item && item.status === 'waiting') {
      item.system = system;
      this.notifyQueue();
    }
  }

  public cancelItem(id: string) {
    const index = this.queue.findIndex((q) => q.id === id);
    if (index === -1) return;

    const item = this.queue[index];

    if (item.status === 'uploading' || item.status === 'resuming' || item.status === 'verifying') {
      if (this.currentAbortController) {
        this.currentAbortController.abort();
      }
      if (item.uploadId) {
        apiClient.cancelUpload(item.uploadId).catch(() => {});
      }
      item.status = 'cancelled';
      item.errorMessage = 'Subida cancelada por el usuario';
      this.notifyQueue();
      this.isProcessing = false;
      this.processNextInQueue();
    } else if (item.status === 'waiting') {
      item.status = 'cancelled';
      this.notifyQueue();
    }
  }

  public removeItem(id: string) {
    this.cancelItem(id);
    this.queue = this.queue.filter((q) => q.id !== id);
    this.notifyQueue();
  }

  public retryItem(id: string) {
    const item = this.queue.find((q) => q.id === id);
    if (item && (item.status === 'error' || item.status === 'cancelled')) {
      item.status = 'waiting';
      item.retryCount = 0;
      item.errorMessage = undefined;
      item.progressPercent = 0;
      item.receivedBytes = 0;
      this.notifyQueue();
      this.processNextInQueue();
    }
  }

  public clearCompleted() {
    this.queue = this.queue.filter((q) => q.status !== 'completed' && q.status !== 'cancelled');
    this.notifyQueue();
  }

  /**
   * Main Queue processing loop
   */
  private async processNextInQueue() {
    if (this.isProcessing) return;

    const nextItem = this.queue.find((q) => q.status === 'waiting');
    if (!nextItem) {
      this.releaseWakeLock();
      return;
    }

    this.isProcessing = true;
    await this.acquireWakeLock();

    try {
      await this.uploadSingleItem(nextItem);
    } catch (err: any) {
      nextItem.status = 'error';
      nextItem.errorMessage = err.message || 'Error en la subida';
      this.notifyQueue();
      this.listeners.forEach((l) => l.onItemFailed(nextItem, nextItem.errorMessage!));
    } finally {
      this.isProcessing = false;
      this.processNextInQueue();
    }
  }

  /**
   * Sube un elemento de la cola: bloques con CRC32, reintentos con backoff y reanudación.
   *
   * Reglas que evitan corromper el archivo en la PS4:
   * - Cada intento envía SIEMPRE los bytes leídos en el offset que se manda. Si el servidor
   *   informa otro offset (409 o sondeo tras un corte), se vuelve a leer el bloque desde ahí.
   * - Cada archivo de un juego (.bin y .cue) tiene su propia sesión y se completa al terminar.
   *   Los .bin van primero y el .cue al final: el servidor registra el juego al completar el .cue.
   */
  private async uploadSingleItem(item: UploadQueueItem) {
    item.status = 'uploading';
    item.startedAt = Date.now();
    item.errorMessage = undefined;
    this.speedSamples = [{ time: Date.now(), bytes: 0 }];
    this.notifyQueue();

    const files = [...(item.associatedFiles || []), item.file];
    const maxAttempts = 5;
    let doneBytes = 0; // bytes de archivos ya completados
    let lastGame: Game | null = null;

    for (const file of files) {
      const session = await apiClient.createUpload(file.name, file.size, item.system);
      item.uploadId = session.uploadId;
      const chunkSize = session.chunkSize || 8 * 1024 * 1024;
      let offset = session.receivedBytes || 0;

      while (offset < file.size) {
        if ((item.status as UploadItemStatus) === 'cancelled') return;

        const end = Math.min(offset + chunkSize, file.size);
        const buffer = await file.slice(offset, end).arrayBuffer();
        const crc = calculateCRC32(buffer);
        const sentAt = offset;

        for (let attempt = 1; ; attempt++) {
          try {
            const res = await apiClient.uploadChunk(session.uploadId, sentAt, buffer, crc);
            offset = res.receivedBytes;
            break;
          } catch (err: any) {
            if ((item.status as UploadItemStatus) === 'cancelled') return;
            item.retryCount = attempt;

            if (err.status === 409 && typeof err.data?.receivedBytes === 'number') {
              // El servidor tiene otro offset: volver a leer desde ahí.
              offset = err.data.receivedBytes;
              item.status = 'resuming';
              this.notifyQueue();
              break;
            }
            if (attempt >= maxAttempts) {
              throw new Error(
                err.status === 422
                  ? 'El bloque llegó dañado varias veces. Revisa la conexión WiFi.'
                  : `Error de conexión tras ${maxAttempts} reintentos: ${err.message || 'desconectado'}`
              );
            }

            item.status = 'resuming';
            item.errorMessage =
              err.status === 422
                ? `Bloque dañado, reintentando (${attempt}/${maxAttempts})`
                : `Conexión inestable. Reintentando en ${Math.pow(2, attempt - 1)} s...`;
            this.notifyQueue();
            await this.exponentialBackoff(attempt);

            if (err.status !== 422) {
              // Tras un corte, preguntar cuánto guardó realmente la PS4.
              try {
                const st = await apiClient.getUploadStatus(session.uploadId);
                if (st.receivedBytes !== sentAt) {
                  offset = st.receivedBytes;
                  break; // releer el bloque correcto
                }
              } catch {
                // sin respuesta: reintentar el mismo bloque
              }
            }
          }
        }

        item.status = 'uploading';
        item.errorMessage = undefined;
        item.receivedBytes = doneBytes + offset;
        item.progressPercent = Math.min(99, Math.floor((item.receivedBytes / Math.max(1, item.sizeBytes)) * 100));
        this.updateSpeedAndEta(item, item.receivedBytes);
        this.notifyQueue();
      }

      item.status = 'verifying';
      this.notifyQueue();
      const done = await apiClient.completeUpload(session.uploadId);
      if (done?.game) lastGame = done.game;
      doneBytes += file.size;
    }

    item.status = 'completed';
    item.progressPercent = 100;
    item.receivedBytes = item.sizeBytes;
    item.completedAt = Date.now();
    item.currentSpeedBytesPerSec = 0;
    item.estimatedRemainingSec = 0;
    item.errorMessage = undefined;
    this.notifyQueue();
    if (lastGame) {
      const game = lastGame;
      this.listeners.forEach((l) => l.onItemCompleted(item, game));
    }
  }

  private updateSpeedAndEta(item: UploadQueueItem, currentBytes: number) {
    const now = Date.now();
    this.speedSamples.push({ time: now, bytes: currentBytes });

    // Keep only samples from last 4 seconds for a smooth rolling average
    const cutoff = now - 4000;
    while (this.speedSamples.length > 2 && this.speedSamples[0].time < cutoff) {
      this.speedSamples.shift();
    }

    if (this.speedSamples.length >= 2) {
      const oldest = this.speedSamples[0];
      const timeDiffSec = (now - oldest.time) / 1000;
      const bytesDiff = currentBytes - oldest.bytes;

      if (timeDiffSec > 0.3 && bytesDiff >= 0) {
        const speed = bytesDiff / timeDiffSec;
        item.currentSpeedBytesPerSec = Math.round(speed);

        const remainingBytes = Math.max(0, item.sizeBytes - currentBytes);
        if (speed > 1000) {
          item.estimatedRemainingSec = Math.round(remainingBytes / speed);
        } else {
          item.estimatedRemainingSec = null;
        }
      }
    }
  }

  private exponentialBackoff(attempt: number): Promise<void> {
    // 1s, 2s, 4s, 8s, up to 10s
    const ms = Math.min(10000, Math.pow(2, attempt - 1) * 1000);
    return new Promise((resolve) => setTimeout(resolve, ms));
  }

  /**
   * Screen Wake Lock API
   * Prevents phone screen from turning off during long uploads
   */
  private async acquireWakeLock() {
    try {
      if (typeof navigator !== 'undefined' && 'wakeLock' in navigator) {
        this.wakeLockSentinel = await (navigator as any).wakeLock.request('screen');
        this.wakeLockSentinel.addEventListener('release', () => {
          this.wakeLockSentinel = null;
        });
      }
    } catch {
      // Wake lock might fail if tab is not active or battery saver is on
      this.wakeLockSentinel = null;
    }
  }

  private releaseWakeLock() {
    try {
      if (this.wakeLockSentinel) {
        this.wakeLockSentinel.release().catch(() => {});
        this.wakeLockSentinel = null;
      }
    } catch {
      // ignore
    }
  }

  private async handleVisibilityChange() {
    if (this.wakeLockSentinel !== null && document.visibilityState === 'visible') {
      await this.acquireWakeLock();
    }
  }
}

export const uploadEngine = new UploadEngine();
