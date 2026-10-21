/**
 * Pure JavaScript implementation of CRC32 (IEEE 802.3 standard)
 * Does NOT rely on crypto.subtle so it functions properly in plain HTTP contexts.
 */

// Precompute the CRC-32 table using polynomial 0xEDB88320
const crcTable: Uint32Array = new Uint32Array(256);

for (let i = 0; i < 256; i++) {
  let c = i;
  for (let j = 0; j < 8; j++) {
    if (c & 1) {
      c = 0xEDB88320 ^ (c >>> 1);
    } else {
      c = c >>> 1;
    }
  }
  crcTable[i] = c >>> 0;
}

/**
 * Calculates CRC32 for an ArrayBuffer or Uint8Array.
 * Returns an 8-character hexadecimal string (e.g. "cbf43926").
 */
export function calculateCRC32(data: ArrayBuffer | Uint8Array): string {
  const bytes = data instanceof Uint8Array ? data : new Uint8Array(data);
  let crc = 0xFFFFFFFF;

  for (let i = 0; i < bytes.length; i++) {
    const tableIndex = (crc ^ bytes[i]) & 0xFF;
    crc = (crc >>> 8) ^ crcTable[tableIndex];
  }

  const finalCrc = ((crc ^ 0xFFFFFFFF) >>> 0);
  return finalCrc.toString(16).padStart(8, '0').toLowerCase();
}

/**
 * Returns raw numeric 32-bit unsigned integer CRC for fast equality checks.
 */
export function calculateCRC32Number(data: ArrayBuffer | Uint8Array): number {
  const bytes = data instanceof Uint8Array ? data : new Uint8Array(data);
  let crc = 0xFFFFFFFF;

  for (let i = 0; i < bytes.length; i++) {
    const tableIndex = (crc ^ bytes[i]) & 0xFF;
    crc = (crc >>> 8) ^ crcTable[tableIndex];
  }

  return ((crc ^ 0xFFFFFFFF) >>> 0);
}
