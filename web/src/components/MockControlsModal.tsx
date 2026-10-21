import React, { useState } from 'react';
import { mockService } from '../mock/mockService';
import { apiClient } from '../api/client';
import { X, Sliders, WifiOff, Zap, ShieldAlert, RotateCcw, Globe } from 'lucide-react';

interface MockControlsModalProps {
  isOpen: boolean;
  onClose: () => void;
  onStateChanged: () => void;
}

export const MockControlsModal: React.FC<MockControlsModalProps> = ({
  isOpen,
  onClose,
  onStateChanged,
}) => {
  const [mockState, setMockState] = useState(() => mockService.getState());
  const [isMockMode, setIsMockMode] = useState(() => apiClient.isMockMode());
  const [customIp, setCustomIp] = useState(() => apiClient.getCustomBaseUrl());
  const [feedback, setFeedback] = useState<string | null>(null);

  if (!isOpen) return null;

  const showFeedback = (msg: string) => {
    setFeedback(msg);
    setTimeout(() => setFeedback(null), 3000);
  };

  const handleToggleMock = (enabled: boolean) => {
    setIsMockMode(enabled);
    apiClient.setMockMode(enabled);
    showFeedback(enabled ? 'Modo simulado activado' : 'Conexión directa a PS4 activada');
    onStateChanged();
  };

  const handleSaveIp = () => {
    apiClient.setCustomBaseUrl(customIp);
    showFeedback(`URL base configurada: ${customIp || '(relativa)'}`);
    onStateChanged();
  };

  const handleSimulateNetworkCut = () => {
    mockService.setSimulateNetworkCut(true);
    setMockState({ ...mockService.getState() });
    showFeedback('Próximo bloque simulará corte de red abrupto.');
    onStateChanged();
  };

  const handleSimulateCrc = () => {
    mockService.setSimulateCrcMismatch(true);
    setMockState({ ...mockService.getState() });
    showFeedback('Próximo bloque simulará error de CRC (422).');
    onStateChanged();
  };

  const handleToggleSpeed = () => {
    const next = !mockState.simulateSpeedLimit;
    mockService.setSpeedLimit(next);
    setMockState({ ...mockService.getState() });
    showFeedback(next ? 'Velocidad limitada a ~5 MB/s' : 'Velocidad sin límite de simulación');
    onStateChanged();
  };

  const handleToggleOffline = () => {
    const next = !mockState.forceOffline;
    mockService.setForceOffline(next);
    setMockState({ ...mockService.getState() });
    showFeedback(next ? 'Simulando PS4 sin conexión' : 'Conexión restaurada');
    onStateChanged();
  };

  const handleResetData = () => {
    mockService.resetToDefault();
    setMockState({ ...mockService.getState() });
    showFeedback('Datos reseteados a 4 juegos y 40 GB libres');
    onStateChanged();
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/80 backdrop-blur-md animate-fadeIn">
      <div className="absolute inset-0" onClick={onClose} />

      <div className="relative w-full max-w-lg bg-[#141416] border border-[#27272a] rounded-3xl p-6 sm:p-7 shadow-2xl z-10 max-h-[90vh] overflow-y-auto">
        <div className="flex items-center justify-between pb-4 border-b border-neutral-800">
          <div className="flex items-center gap-2.5">
            <div className="w-8 h-8 rounded-xl bg-[#0A84FF]/20 text-[#0A84FF] flex items-center justify-center">
              <Sliders className="w-4 h-4" />
            </div>
            <div>
              <h3 className="text-base font-bold text-white">Panel de Desarrollo & Simulación</h3>
              <p className="text-xs text-neutral-400">Herramientas para probar la resiliencia de SAMPSP4</p>
            </div>
          </div>

          <button
            onClick={onClose}
            className="w-9 h-9 rounded-full bg-neutral-900 border border-neutral-800 flex items-center justify-center text-neutral-400 hover:text-white"
          >
            <X className="w-4 h-4" />
          </button>
        </div>

        {feedback && (
          <div className="mt-4 p-3 rounded-xl bg-[#0A84FF]/10 border border-[#0A84FF]/30 text-xs text-[#0A84FF] font-medium animate-fadeIn">
            {feedback}
          </div>
        )}

        <div className="mt-5 space-y-4">
          {/* Mock mode toggle */}
          <div className="p-4 bg-[#18181c] border border-neutral-800 rounded-2xl flex items-center justify-between">
            <div>
              <div className="text-sm font-semibold text-white">Modo Simulado en memoria</div>
              <div className="text-xs text-neutral-400">
                Responde las llamadas /api localmente para desarrollar sin consola
              </div>
            </div>
            <button
              onClick={() => handleToggleMock(!isMockMode)}
              className={`w-12 h-7 rounded-full p-1 transition-colors ${
                isMockMode ? 'bg-[#0A84FF]' : 'bg-neutral-800'
              }`}
            >
              <div
                className={`w-5 h-5 rounded-full bg-white transition-transform ${
                  isMockMode ? 'translate-x-5' : 'translate-x-0'
                }`}
              />
            </button>
          </div>

          {/* Test connection resilience */}
          <div className="p-4 bg-[#18181c] border border-neutral-800 rounded-2xl space-y-3">
            <div className="text-xs font-semibold text-neutral-300 uppercase tracking-wider">
              Pruebas de subida y fallos de red
            </div>

            <div className="grid grid-cols-1 sm:grid-cols-2 gap-2">
              <button
                onClick={handleSimulateNetworkCut}
                className={`p-3 rounded-xl text-left border transition-colors flex items-center gap-2.5 ${
                  mockState.simulateNetworkCutNextChunk
                    ? 'bg-amber-950/40 border-amber-500 text-amber-300'
                    : 'bg-neutral-900 border-neutral-800 text-neutral-200 hover:border-neutral-700'
                }`}
              >
                <WifiOff className="w-4 h-4 shrink-0 text-amber-400" />
                <div className="text-xs">
                  <div className="font-semibold">Cortar red al subir</div>
                  <div className="text-[10px] text-neutral-400">Prueba el auto-reanudar</div>
                </div>
              </button>

              <button
                onClick={handleSimulateCrc}
                className={`p-3 rounded-xl text-left border transition-colors flex items-center gap-2.5 ${
                  mockState.simulateCrcMismatchNextChunk
                    ? 'bg-red-950/40 border-red-500 text-red-300'
                    : 'bg-neutral-900 border-neutral-800 text-neutral-200 hover:border-neutral-700'
                }`}
              >
                <ShieldAlert className="w-4 h-4 shrink-0 text-red-400" />
                <div className="text-xs">
                  <div className="font-semibold">Simular error CRC 422</div>
                  <div className="text-[10px] text-neutral-400">Prueba reintento de bloque</div>
                </div>
              </button>

              <button
                onClick={handleToggleSpeed}
                className="p-3 rounded-xl text-left bg-neutral-900 border border-neutral-800 text-neutral-200 hover:border-neutral-700 transition-colors flex items-center gap-2.5"
              >
                <Zap className="w-4 h-4 shrink-0 text-[#0A84FF]" />
                <div className="text-xs">
                  <div className="font-semibold">Velocidad ~5 MB/s</div>
                  <div className="text-[10px] text-neutral-400">
                    {mockState.simulateSpeedLimit ? 'Activa (5 MB/s)' : 'Sin límite'}
                  </div>
                </div>
              </button>

              <button
                onClick={handleToggleOffline}
                className={`p-3 rounded-xl text-left border transition-colors flex items-center gap-2.5 ${
                  mockState.forceOffline
                    ? 'bg-red-950/40 border-red-500 text-red-300'
                    : 'bg-neutral-900 border-neutral-800 text-neutral-200 hover:border-neutral-700'
                }`}
              >
                <WifiOff className="w-4 h-4 shrink-0 text-neutral-400" />
                <div className="text-xs">
                  <div className="font-semibold">Forzar offline</div>
                  <div className="text-[10px] text-neutral-400">
                    {mockState.forceOffline ? 'PS4 desconectada' : 'Normal'}
                  </div>
                </div>
              </button>
            </div>
          </div>

          {/* Real PS4 Endpoint Config */}
          {!isMockMode && (
            <div className="p-4 bg-[#18181c] border border-neutral-800 rounded-2xl space-y-2">
              <div className="flex items-center gap-2 text-xs font-semibold text-neutral-300">
                <Globe className="w-4 h-4 text-[#0A84FF]" />
                <span>Dirección de la PS4 (opcional si sirves desde la PS4)</span>
              </div>
              <div className="flex gap-2">
                <input
                  type="text"
                  placeholder="http://192.168.1.50:8080"
                  value={customIp}
                  onChange={(e) => setCustomIp(e.target.value)}
                  className="flex-1 px-3 py-2 bg-neutral-900 border border-neutral-800 rounded-xl text-xs font-mono text-white outline-none focus:border-[#0A84FF]"
                />
                <button
                  onClick={handleSaveIp}
                  className="px-3 py-2 bg-neutral-800 hover:bg-neutral-700 text-white text-xs font-semibold rounded-xl"
                >
                  Guardar
                </button>
              </div>
            </div>
          )}

          {/* Reset button */}
          <button
            onClick={handleResetData}
            className="w-full py-3 rounded-2xl bg-neutral-900 border border-neutral-800 hover:bg-neutral-800 text-neutral-300 text-xs font-semibold flex items-center justify-center gap-2 transition-colors"
          >
            <RotateCcw className="w-3.5 h-3.5" />
            <span>Restablecer juegos y 40 GB de espacio</span>
          </button>
        </div>
      </div>
    </div>
  );
};
