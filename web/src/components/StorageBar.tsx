import React from 'react';
import { formatBytes } from '../utils/format';
import { HardDrive } from 'lucide-react';

interface StorageBarProps {
  freeBytes: number;
  totalBytes: number;
}

export const StorageBar: React.FC<StorageBarProps> = ({ freeBytes, totalBytes }) => {
  // totalBytes 0 = la PS4 no pudo medir el disco: no se muestra un 0 GB engañoso.
  if (!totalBytes) {
    return (
      <div className="w-full bg-[#121214] border border-[#242428] rounded-2xl p-4 sm:p-5 flex items-center gap-2.5">
        <div className="w-8 h-8 rounded-xl bg-neutral-900 border border-neutral-800 flex items-center justify-center text-neutral-300">
          <HardDrive className="w-4 h-4" />
        </div>
        <div>
          <div className="text-sm font-semibold text-white tracking-tight">Almacenamiento PS4</div>
          <div className="text-xs text-neutral-400">Espacio libre no disponible en esta consola</div>
        </div>
      </div>
    );
  }
  const usedBytes = Math.max(0, totalBytes - freeBytes);
  const usedPercent = totalBytes > 0 ? Math.min(100, (usedBytes / totalBytes) * 100) : 0;
  const isLowSpace = freeBytes < 5 * 1024 * 1024 * 1024; // Less than 5 GB

  return (
    <div className="w-full bg-[#121214] border border-[#242428] rounded-2xl p-4 sm:p-5">
      <div className="flex items-center justify-between mb-3">
        <div className="flex items-center gap-2.5">
          <div className="w-8 h-8 rounded-xl bg-neutral-900 border border-neutral-800 flex items-center justify-center text-neutral-300">
            <HardDrive className="w-4 h-4" />
          </div>
          <div>
            <div className="text-sm font-semibold text-white tracking-tight">Almacenamiento PS4</div>
            <div className="text-xs text-neutral-400">
              <span className="font-mono text-neutral-300 font-medium">{formatBytes(freeBytes)}</span> libres de{' '}
              <span className="font-mono">{formatBytes(totalBytes)}</span>
            </div>
          </div>
        </div>

        <div className="text-right">
          <span className="font-mono text-xs font-semibold text-neutral-300 tabular-nums">
            {usedPercent.toFixed(1)}% usado
          </span>
          {isLowSpace && (
            <div className="text-[11px] text-amber-400 font-medium">Espacio limitado</div>
          )}
        </div>
      </div>

      {/* Storage progress bar */}
      <div className="w-full h-2.5 bg-neutral-900 rounded-full overflow-hidden p-0.5 border border-neutral-800/80">
        <div
          className={`h-full rounded-full transition-all duration-500 ease-out ${
            isLowSpace ? 'bg-amber-500' : 'bg-[#0A84FF]'
          }`}
          style={{ width: `${usedPercent}%` }}
        />
      </div>
    </div>
  );
};
