import { describe, it, expect, vi, beforeEach } from 'vitest';
import { calculateCRC32, calculateCRC32Number } from '../upload/crc32';
import { UploadEngine } from '../upload/engine';
import { apiClient } from '../api/client';

describe('CRC32 Engine (IEEE 802.3)', () => {
  it('correctly calculates standard test vector "123456789"', () => {
    const encoder = new TextEncoder();
    const data = encoder.encode('123456789');
    const crcHex = calculateCRC32(data);
    expect(crcHex).toBe('cbf43926');
  });

  it('correctly handles empty buffer', () => {
    const empty = new Uint8Array(0);
    const crcHex = calculateCRC32(empty);
    expect(crcHex).toBe('00000000');
  });

  it('calculates numerical CRC32 accurately', () => {
    const encoder = new TextEncoder();
    const data = encoder.encode('123456789');
    const crcNum = calculateCRC32Number(data);
    expect(crcNum).toBe(0xcbf43926 >>> 0);
  });
});

describe('Upload Engine Chunking & System Detection', () => {
  it('detects PSP and PS1 systems and formats accurately', () => {
    expect(UploadEngine.detectSystemAndFormat('Tekken.iso')).toEqual({ system: 'psp', format: 'iso' });
    expect(UploadEngine.detectSystemAndFormat('crisis.cso')).toEqual({ system: 'psp', format: 'cso' });
    expect(UploadEngine.detectSystemAndFormat('crash.chd')).toEqual({ system: 'ps1', format: 'chd' });
    expect(UploadEngine.detectSystemAndFormat('spyro.cue')).toEqual({ system: 'ps1', format: 'bincue' });
    expect(UploadEngine.detectSystemAndFormat('spyro.bin')).toEqual({ system: 'ps1', format: 'bincue' });
    expect(UploadEngine.detectSystemAndFormat('game.pbp')).toEqual({ system: 'psp', format: 'pbp' });
  });

  it('groups .cue with matching .bin files as a single PS1 game entry', () => {
    const engine = new UploadEngine();
    const cueFile = new File(['FILE "game.bin" BINARY'], 'game.cue', { type: 'text/plain' });
    const binFile = new File([new Uint8Array(2048)], 'game.bin', { type: 'application/octet-stream' });
    const isoFile = new File([new Uint8Array(1024)], 'other.iso', { type: 'application/octet-stream' });

    engine.addFiles([cueFile, binFile, isoFile]);
    const queue = engine.getQueue();

    expect(queue.length).toBe(2);
    const grouped = queue.find((q) => q.format === 'bincue');
    expect(grouped).toBeDefined();
    expect(grouped?.associatedFiles?.length).toBe(1);
    expect(grouped?.system).toBe('ps1');
  });
});

describe('Upload Engine Resilience & Recovery', () => {
  beforeEach(() => {
    vi.restoreAllMocks();
  });

  it('retries block when server responds with 422 crc_incorrecto', async () => {
    const engine = new UploadEngine();
    const testFile = new File([new Uint8Array(1024)], 'test-game.iso', { type: 'application/octet-stream' });

    vi.spyOn(apiClient, 'createUpload').mockResolvedValue({
      uploadId: 'test-session-crc',
      chunkSize: 512,
      receivedBytes: 0,
    });

    let chunkAttempts = 0;
    vi.spyOn(apiClient, 'uploadChunk').mockImplementation(async (_id, offset, chunk) => {
      chunkAttempts++;
      if (chunkAttempts === 1) {
        const err = new Error('CRC incorrecto');
        (err as any).status = 422;
        (err as any).data = { error: 'crc_incorrecto' };
        throw err;
      }
      return { receivedBytes: offset + chunk.byteLength };
    });

    vi.spyOn(apiClient, 'completeUpload').mockResolvedValue({
      game: {
        id: 'ULES00001',
        title: 'Test Game',
        titleId: 'ULES00001',
        system: 'psp',
        format: 'iso',
        sizeBytes: 1024,
        coverUrl: '/api/games/ULES00001/cover',
        addedAt: new Date().toISOString(),
      },
    });

    // Mock fast backoff so test finishes in milliseconds
    vi.spyOn(engine as any, 'exponentialBackoff').mockResolvedValue(undefined);

    await (engine as any).uploadSingleItem({
      id: 'item_test_crc',
      file: testFile,
      displayName: 'test-game.iso',
      system: 'psp',
      format: 'iso',
      sizeBytes: 1024,
      status: 'waiting',
      progressPercent: 0,
      receivedBytes: 0,
      currentSpeedBytesPerSec: 0,
      estimatedRemainingSec: null,
      retryCount: 0,
    });

    // Chunk 1 was retried after 422, then succeeded, plus chunk 2 succeeded
    expect(chunkAttempts).toBeGreaterThanOrEqual(2);
  });

  it('resumes from new offset when server returns 409 offset_incorrecto', async () => {
    const engine = new UploadEngine();
    const testFile = new File([new Uint8Array(2048)], 'test-offset.iso', { type: 'application/octet-stream' });

    vi.spyOn(apiClient, 'createUpload').mockResolvedValue({
      uploadId: 'test-session-offset',
      chunkSize: 512,
      receivedBytes: 0,
    });

    let calledOffsets: number[] = [];
    let conflictSimulated = false;

    vi.spyOn(apiClient, 'uploadChunk').mockImplementation(async (_id, offset, chunk) => {
      calledOffsets.push(offset);
      // Simulate client thought offset was 0, but server already had 1024
      if (!conflictSimulated) {
        conflictSimulated = true;
        const err = new Error('Offset incorrecto');
        (err as any).status = 409;
        (err as any).data = { error: 'offset_incorrecto', receivedBytes: 1024 };
        throw err;
      }
      return { receivedBytes: offset + chunk.byteLength };
    });

    vi.spyOn(apiClient, 'completeUpload').mockResolvedValue({
      game: {
        id: 'ULES00002',
        title: 'Test Offset Game',
        titleId: 'ULES00002',
        system: 'psp',
        format: 'iso',
        sizeBytes: 2048,
        coverUrl: '/api/games/ULES00002/cover',
        addedAt: new Date().toISOString(),
      },
    });

    vi.spyOn(engine as any, 'exponentialBackoff').mockResolvedValue(undefined);

    await (engine as any).uploadSingleItem({
      id: 'item_test_offset',
      file: testFile,
      displayName: 'test-offset.iso',
      system: 'psp',
      format: 'iso',
      sizeBytes: 2048,
      status: 'waiting',
      progressPercent: 0,
      receivedBytes: 0,
      currentSpeedBytesPerSec: 0,
      estimatedRemainingSec: null,
      retryCount: 0,
    });

    // Verify engine adjusted to 1024 after 409
    expect(calledOffsets).toContain(1024);
  });

  it('recovers after network disconnection by probing status and resuming', async () => {
    const engine = new UploadEngine();
    const testFile = new File([new Uint8Array(1024)], 'test-network.iso', { type: 'application/octet-stream' });

    vi.spyOn(apiClient, 'createUpload').mockResolvedValue({
      uploadId: 'test-session-net',
      chunkSize: 512,
      receivedBytes: 0,
    });

    let networkDropped = false;
    vi.spyOn(apiClient, 'uploadChunk').mockImplementation(async (_id, offset, chunk) => {
      if (!networkDropped) {
        networkDropped = true;
        throw new Error('Failed to fetch (Network disconnected)');
      }
      return { receivedBytes: offset + chunk.byteLength };
    });

    vi.spyOn(apiClient, 'getUploadStatus').mockResolvedValue({
      receivedBytes: 0,
      sizeBytes: 1024,
    });

    vi.spyOn(apiClient, 'completeUpload').mockResolvedValue({
      game: {
        id: 'ULES00003',
        title: 'Network Recovered Game',
        titleId: 'ULES00003',
        system: 'psp',
        format: 'iso',
        sizeBytes: 1024,
        coverUrl: '/api/games/ULES00003/cover',
        addedAt: new Date().toISOString(),
      },
    });

    vi.spyOn(engine as any, 'exponentialBackoff').mockResolvedValue(undefined);

    const testItem = {
      id: 'item_test_net',
      file: testFile,
      displayName: 'test-network.iso',
      system: 'psp' as const,
      format: 'iso' as const,
      sizeBytes: 1024,
      status: 'waiting' as const,
      progressPercent: 0,
      receivedBytes: 0,
      currentSpeedBytesPerSec: 0,
      estimatedRemainingSec: null,
      retryCount: 0,
    };

    await (engine as any).uploadSingleItem(testItem);
    expect(testItem.status).toBe('completed');
  });
});
