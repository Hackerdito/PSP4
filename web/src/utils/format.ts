/**
 * Utility formatting functions
 */

export function formatBytes(bytes: number, decimals: number = 1): string {
  if (bytes <= 0) return '0 B';
  const k = 1024;
  const dm = decimals < 0 ? 0 : decimals;
  const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  const val = parseFloat((bytes / Math.pow(k, i)).toFixed(dm));
  return `${val} ${sizes[i]}`;
}

export function formatSpeed(bytesPerSec: number): string {
  if (bytesPerSec <= 0) return '0 MB/s';
  const mbPerSec = bytesPerSec / (1024 * 1024);
  if (mbPerSec < 0.1) {
    const kbPerSec = bytesPerSec / 1024;
    return `${kbPerSec.toFixed(0)} KB/s`;
  }
  return `${mbPerSec.toFixed(1)} MB/s`;
}

export function formatDuration(seconds: number | null): string {
  if (seconds === null || seconds < 0 || !isFinite(seconds)) return '--';
  if (seconds < 60) {
    return `${Math.round(seconds)} s`;
  }
  const mins = Math.floor(seconds / 60);
  const remainingSec = Math.round(seconds % 60);
  if (mins < 60) {
    return `${mins} min ${remainingSec > 0 ? `${remainingSec} s` : ''}`.trim();
  }
  const hours = Math.floor(mins / 60);
  const remainingMins = mins % 60;
  return `${hours} h ${remainingMins} min`;
}

export function formatDate(isoString: string): string {
  try {
    const date = new Date(isoString);
    return date.toLocaleDateString('es-ES', {
      day: 'numeric',
      month: 'short',
      year: 'numeric'
    });
  } catch {
    return isoString;
  }
}
