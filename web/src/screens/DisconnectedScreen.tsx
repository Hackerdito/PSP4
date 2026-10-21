import React from 'react';
import { WifiOff, RefreshCw, Sliders } from 'lucide-react';
import { mockAllowed } from '../api/client';

interface DisconnectedScreenProps {
  onRetry: () => void;
  onOpenMockControls: () => void;
  isRetrying: boolean;
}

export const DisconnectedScreen: React.FC<DisconnectedScreenProps> = ({
  onRetry,
  onOpenMockControls,
  isRetrying,
}) => {
  return (
    <div className="min-h-[70vh] w-full flex flex-col items-center justify-center p-6 text-center animate-fadeIn">
      <div className="w-20 h-20 rounded-3xl bg-red-950/20 border border-red-900/40 flex items-center justify-center text-red-400 mb-6 shadow-xl">
        <WifiOff className="w-10 h-10" />
      </div>

      <h2 className="text-xl sm:text-2xl font-bold text-white tracking-tight">
        Sin conexión con la PS4
      </h2>

      <p className="mt-2 text-sm text-neutral-400 max-w-md">
        Asegúrate de que SAMPSP4 esté abierto en tu PlayStation 4 y que este dispositivo esté conectado a la misma red WiFi local.
      </p>

      <div className="mt-8 flex flex-col sm:flex-row items-center gap-3 w-full max-w-xs">
        <button
          onClick={onRetry}
          disabled={isRetrying}
          className="w-full min-h-[48px] rounded-2xl bg-[#0A84FF] hover:bg-[#0070E0] text-white font-semibold text-sm flex items-center justify-center gap-2 transition-colors disabled:opacity-50 shadow-lg shadow-[#0A84FF]/20"
        >
          <RefreshCw className={`w-4 h-4 ${isRetrying ? 'animate-spin' : ''}`} />
          <span>{isRetrying ? 'Comprobando...' : 'Reintentar conexión'}</span>
        </button>

        {mockAllowed && (
          <button
            onClick={onOpenMockControls}
            className="w-full min-h-[48px] rounded-2xl bg-[#141416] hover:bg-[#1a1a1f] border border-[#27272a] text-neutral-300 hover:text-white font-medium text-sm flex items-center justify-center gap-2 transition-colors"
          >
            <Sliders className="w-4 h-4 text-[#0A84FF]" />
            <span>Ajustar conexión / Mock</span>
          </button>
        )}
      </div>
    </div>
  );
};
