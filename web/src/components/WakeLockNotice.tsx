import React from 'react';
import { Sun, Smartphone } from 'lucide-react';

interface WakeLockNoticeProps {
  hasActiveUploads: boolean;
}

export const WakeLockNotice: React.FC<WakeLockNoticeProps> = ({ hasActiveUploads }) => {
  const supportsWakeLock = typeof navigator !== 'undefined' && 'wakeLock' in navigator;

  if (!hasActiveUploads) return null;

  return (
    <div className="w-full bg-[#14151a] border border-[#0A84FF]/30 rounded-2xl p-3.5 mb-4 flex items-center gap-3 animate-fadeIn">
      <div className="w-8 h-8 rounded-xl bg-[#0A84FF]/10 text-[#0A84FF] flex items-center justify-center shrink-0">
        {supportsWakeLock ? <Sun className="w-4 h-4 animate-spin-slow" /> : <Smartphone className="w-4 h-4" />}
      </div>
      <div className="text-xs text-neutral-300">
        <span className="font-semibold text-white">Subida en progreso: </span>
        {supportsWakeLock
          ? 'Bloqueo de pantalla activado para evitar interrupciones en la red local.'
          : 'Mantén esta pestaña abierta y la pantalla encendida para que la subida no se detenga.'}
      </div>
    </div>
  );
};
