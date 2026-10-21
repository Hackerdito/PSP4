import { Game } from '../types';

export const INITIAL_GAMES: Game[] = [
  {
    id: 'UCUS98653',
    title: 'God of War: Chains of Olympus',
    titleId: 'UCUS98653',
    system: 'psp',
    format: 'iso',
    sizeBytes: 1288490188,
    coverUrl: '/api/games/UCUS98653/cover',
    addedAt: '2026-10-15T14:20:00.000Z'
  },
  {
    id: 'ULUS10336',
    title: 'Crisis Core: Final Fantasy VII',
    titleId: 'ULUS10336',
    system: 'psp',
    format: 'cso',
    sizeBytes: 1027604480,
    coverUrl: '/api/games/ULUS10336/cover',
    addedAt: '2026-10-18T19:40:00.000Z'
  },
  {
    id: 'SLUS00067',
    title: 'Castlevania: Symphony of the Night',
    titleId: 'SLUS00067',
    system: 'ps1',
    format: 'chd',
    sizeBytes: 440401920,
    coverUrl: '/api/games/SLUS00067/cover',
    addedAt: '2026-10-10T11:15:00.000Z'
  },
  {
    id: 'SLUS00594',
    title: 'Metal Gear Solid',
    titleId: 'SLUS00594',
    system: 'ps1',
    format: 'bincue',
    sizeBytes: 713031680,
    coverUrl: '/api/games/SLUS00594/cover',
    addedAt: '2026-10-12T08:30:00.000Z'
  }
];

export const MOCK_SYSTEM_INFO = {
  app: 'SAMPSP4',
  version: '1.1',
  freeBytes: 42949672960, // 40 GB libre
  totalBytes: 1000000000000 // 1 TB total
};

export const MOCK_VALID_PIN = '123456';
export const MOCK_AUTH_TOKEN = 'sampsp4-mock-token-secret-789xyz';
