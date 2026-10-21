export type GameSystem = 'psp' | 'ps1';

export type GameFormat = 'iso' | 'cso' | 'pbp' | 'chd' | 'bincue';

export interface Game {
  id: string;
  title: string;
  titleId: string;
  system: GameSystem;
  format: GameFormat;
  sizeBytes: number;
  coverUrl: string;
  addedAt: string;
}

export interface SystemInfo {
  app: string;
  version: string;
  freeBytes: number;
  totalBytes: number;
}

export interface UploadSession {
  uploadId: string;
  chunkSize: number;
  receivedBytes: number;
}

export type UploadItemStatus =
  | 'waiting'     // En espera
  | 'uploading'   // Subiendo
  | 'resuming'    // Reanudando
  | 'verifying'   // Verificando
  | 'completed'   // Listo
  | 'error'       // Error
  | 'cancelled';  // Cancelado

export interface UploadQueueItem {
  id: string;
  file: File;
  associatedFiles?: File[]; // e.g. for .cue + .bin groupings
  displayName: string;
  system: GameSystem;
  format: GameFormat;
  sizeBytes: number;
  status: UploadItemStatus;
  progressPercent: number;
  receivedBytes: number;
  currentSpeedBytesPerSec: number;
  estimatedRemainingSec: number | null;
  errorMessage?: string;
  uploadId?: string;
  retryCount: number;
  startedAt?: number;
  completedAt?: number;
}

export interface ApiErrorResponse {
  error: 'pin_invalido' | 'ya_existe' | 'sin_espacio' | 'formato_no_soportado' | 'offset_incorrecto' | 'crc_incorrecto' | string;
  receivedBytes?: number;
  message?: string;
}
