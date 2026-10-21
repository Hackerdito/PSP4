import React from 'react';
import { UploadQueueItem, GameSystem } from '../types';
import { formatBytes, formatSpeed, formatDuration } from '../utils/format';
import {
  CheckCircle2,
  AlertTriangle,
  RotateCcw,
  X,
  Clock,
  ArrowUpCircle,
  FileCheck2,
  Trash2,
  Loader2,
} from 'lucide-react';

interface UploadQueueProps {
  queue: UploadQueueItem[];
  onCancelItem: (id: string) => void;
  onRetryItem: (id: string) => void;
  onRemoveItem: (id: string) => void;
  onClearCompleted: () => void;
  onUpdateSystem: (id: string, system: GameSystem) => void;
}

export const UploadQueue: React.FC<UploadQueueProps> = ({
  queue,
  onCancelItem,
  onRetryItem,
  onRemoveItem,
  onClearCompleted,
  onUpdateSystem,
}) => {
  if (queue.length === 0) return null;

  const hasCompletedOrCancelled = queue.some(
    (q) => q.status === 'completed' || q.status === 'cancelled'
  );

  return (
    <div className="w-full mt-6">
      <div className="flex items-center justify-between mb-3 px-1">
        <div className="flex items-center gap-2">
          <h3 className="text-base font-semibold text-white tracking-tight">Cola de subida</h3>
          <span className="font-mono text-xs text-neutral-400 bg-neutral-900 px-2 py-0.5 rounded-md border border-neutral-800">
            {queue.length}
          </span>
        </div>

        {hasCompletedOrCancelled && (
          <button
            onClick={onClearCompleted}
            className="text-xs text-neutral-400 hover:text-white transition-colors flex items-center gap-1.5 py-1 px-2.5 rounded-lg hover:bg-neutral-800/60"
          >
            <Trash2 className="w-3.5 h-3.5" />
            <span>Limpiar terminados</span>
          </button>
        )}
      </div>

      <div className="space-y-3">
        {queue.map((item) => {
          const isUploading = item.status === 'uploading';
          const isVerifying = item.status === 'verifying';
          const isResuming = item.status === 'resuming';
          const isCompleted = item.status === 'completed';
          const isError = item.status === 'error';
          const isWaiting = item.status === 'waiting';

          return (
            <div
              key={item.id}
              className={`relative bg-[#121214] border rounded-2xl p-4 sm:p-5 transition-all overflow-hidden ${
                isUploading || isResuming
                  ? 'border-[#0A84FF]/60 bg-[#14151a]'
                  : isCompleted
                  ? 'border-emerald-900/50 bg-[#111613]'
                  : isError
                  ? 'border-red-900/50 bg-[#161213]'
                  : 'border-[#242428]'
              }`}
            >
              {/* Header row: file name + actions */}
              <div className="flex items-start justify-between gap-3">
                <div className="min-w-0 flex-1">
                  <div className="flex items-center gap-2">
                    <span className="font-semibold text-sm sm:text-base text-white truncate">
                      {item.displayName}
                    </span>
                  </div>

                  {/* Clean unboxed metadata with bullet separators */}
                  <div className="flex flex-wrap items-center gap-1.5 text-xs text-neutral-400 mt-1">
                    {/* System Switcher if waiting */}
                    {isWaiting ? (
                      <span className="inline-flex items-center gap-1 bg-neutral-900 rounded-md border border-neutral-800 p-0.5">
                        <button
                          onClick={() => onUpdateSystem(item.id, 'psp')}
                          className={`px-1.5 py-0.5 rounded text-[11px] font-medium transition-colors ${
                            item.system === 'psp'
                              ? 'bg-[#0A84FF] text-white'
                              : 'text-neutral-400 hover:text-white'
                          }`}
                        >
                          PSP
                        </button>
                        <button
                          onClick={() => onUpdateSystem(item.id, 'ps1')}
                          className={`px-1.5 py-0.5 rounded text-[11px] font-medium transition-colors ${
                            item.system === 'ps1'
                              ? 'bg-neutral-700 text-white'
                              : 'text-neutral-400 hover:text-white'
                          }`}
                        >
                          PS1
                        </button>
                      </span>
                    ) : (
                      <span className="font-semibold text-neutral-300 uppercase">
                        {item.system}
                      </span>
                    )}

                    <span aria-hidden="true">·</span>
                    <span className="uppercase font-mono text-[11px]">{item.format}</span>
                    <span aria-hidden="true">·</span>
                    <span className="font-mono tabular-nums">{formatBytes(item.sizeBytes)}</span>
                  </div>
                </div>

                {/* Right action button */}
                <div className="flex items-center gap-1 shrink-0">
                  {isError && (
                    <button
                      onClick={() => onRetryItem(item.id)}
                      className="p-2 text-neutral-300 hover:text-white hover:bg-neutral-800 rounded-xl transition-colors"
                      title="Reintentar subida"
                      aria-label="Reintentar"
                    >
                      <RotateCcw className="w-4 h-4 text-amber-400" />
                    </button>
                  )}

                  {(isUploading || isResuming || isVerifying || isWaiting) && (
                    <button
                      onClick={() => onCancelItem(item.id)}
                      className="p-2 text-neutral-400 hover:text-red-400 hover:bg-neutral-800 rounded-xl transition-colors"
                      title="Cancelar"
                      aria-label="Cancelar"
                    >
                      <X className="w-4 h-4" />
                    </button>
                  )}

                  {(isCompleted || item.status === 'cancelled') && (
                    <button
                      onClick={() => onRemoveItem(item.id)}
                      className="p-2 text-neutral-500 hover:text-neutral-300 hover:bg-neutral-800 rounded-xl transition-colors"
                      title="Quitar"
                      aria-label="Quitar"
                    >
                      <X className="w-4 h-4" />
                    </button>
                  )}
                </div>
              </div>

              {/* Progress Bar */}
              <div className="mt-3.5">
                <div className="w-full h-2 bg-neutral-900 rounded-full overflow-hidden border border-neutral-800/80">
                  <div
                    className={`h-full transition-all duration-300 ease-out rounded-full ${
                      isCompleted
                        ? 'bg-emerald-500'
                        : isError
                        ? 'bg-red-500'
                        : isResuming
                        ? 'bg-amber-500 animate-pulse'
                        : isVerifying
                        ? 'bg-cyan-500'
                        : 'bg-[#0A84FF]'
                    }`}
                    style={{ width: `${item.progressPercent}%` }}
                  />
                </div>
              </div>

              {/* Status and Telemetry row */}
              <div className="flex flex-wrap items-center justify-between gap-2 mt-2.5 text-xs">
                {/* State explanation */}
                <div className="flex items-center gap-1.5 font-medium">
                  {isUploading && (
                    <>
                      <ArrowUpCircle className="w-3.5 h-3.5 text-[#0A84FF] animate-pulse" />
                      <span className="text-[#0A84FF]">Subiendo por bloques</span>
                    </>
                  )}
                  {isVerifying && (
                    <>
                      <FileCheck2 className="w-3.5 h-3.5 text-cyan-400" />
                      <span className="text-cyan-400">Verificando CRC32...</span>
                    </>
                  )}
                  {isResuming && (
                    <>
                      <Loader2 className="w-3.5 h-3.5 text-amber-400 animate-spin" />
                      <span className="text-amber-400">Reanudando conexión...</span>
                    </>
                  )}
                  {isWaiting && (
                    <>
                      <Clock className="w-3.5 h-3.5 text-neutral-400" />
                      <span className="text-neutral-400">En espera</span>
                    </>
                  )}
                  {isCompleted && (
                    <>
                      <CheckCircle2 className="w-3.5 h-3.5 text-emerald-400" />
                      <span className="text-emerald-400">Completado y registrado en PS4</span>
                    </>
                  )}
                  {isError && (
                    <>
                      <AlertTriangle className="w-3.5 h-3.5 text-red-400" />
                      <span className="text-red-400 truncate max-w-xs">{item.errorMessage || 'Error'}</span>
                    </>
                  )}
                  {item.status === 'cancelled' && (
                    <span className="text-neutral-500">Cancelado</span>
                  )}
                </div>

                {/* Real-time stats */}
                <div className="flex items-center gap-3 text-neutral-400 font-mono text-[11px] tabular-nums">
                  {(isUploading || isResuming) && (
                    <>
                      <span className="text-neutral-300 font-medium">
                        {formatSpeed(item.currentSpeedBytesPerSec)}
                      </span>
                      <span aria-hidden="true">·</span>
                      <span>
                        {formatBytes(item.receivedBytes)} / {formatBytes(item.sizeBytes)}
                      </span>
                      {item.estimatedRemainingSec !== null && (
                        <>
                          <span aria-hidden="true">·</span>
                          <span title="Tiempo restante estimado">
                            ETA: {formatDuration(item.estimatedRemainingSec)}
                          </span>
                        </>
                      )}
                    </>
                  )}

                  {!isUploading && !isResuming && !isCompleted && (
                    <span>{item.progressPercent}%</span>
                  )}

                  {isCompleted && (
                    <span className="text-emerald-400 font-medium">100%</span>
                  )}
                </div>
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
};
