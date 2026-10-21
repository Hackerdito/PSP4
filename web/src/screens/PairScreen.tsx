import React, { useState } from 'react';
import { PinInput } from '../components/PinInput';
import { apiClient, mockAllowed } from '../api/client';
import { ShieldCheck, ArrowRight, AlertCircle, Loader2, Sparkles } from 'lucide-react';

interface PairScreenProps {
  onPaired: () => void;
  onOpenMockControls: () => void;
}

export const PairScreen: React.FC<PairScreenProps> = ({ onPaired, onOpenMockControls }) => {
  const [pin, setPin] = useState('');
  const [isLoading, setIsLoading] = useState(false);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);

  const handlePair = async (pinToTry = pin) => {
    if (pinToTry.length < 6) {
      setErrorMessage('Ingresa los 6 dígitos del PIN mostrado en tu televisor');
      return;
    }

    setIsLoading(true);
    setErrorMessage(null);

    try {
      await apiClient.pair(pinToTry);
      onPaired();
    } catch (err: any) {
      if (err.data?.error === 'pin_invalido') {
        setErrorMessage('El PIN ingresado no coincide con el de tu PS4. Revisa la pantalla de la TV.');
      } else {
        setErrorMessage(err.message || 'No se pudo conectar con la consola. Verifica que esté en la misma red WiFi.');
      }
    } finally {
      setIsLoading(false);
    }
  };

  const handleUseDemoPin = () => {
    setPin('123456');
    handlePair('123456');
  };

  return (
    <div className="min-h-screen w-full flex flex-col items-center justify-between p-6 sm:p-10 bg-black text-neutral-100">
      {/* Top spacer or dev tools toggle */}
      <div className="w-full max-w-md flex justify-end">
        {mockAllowed && (
          <button
            onClick={onOpenMockControls}
            className="text-xs text-neutral-400 hover:text-white bg-neutral-900 border border-neutral-800 px-3 py-1.5 rounded-xl transition-colors"
          >
            {apiClient.isMockMode() ? 'Modo Simulado activo' : 'Configurar conexión'}
          </button>
        )}
      </div>

      {/* Main card */}
      <div className="w-full max-w-md flex flex-col items-center text-center my-auto">
        {/* Logo and PS4 Graphic */}
        <div className="relative mb-6">
          <img
            src="/logo.png"
            alt="SAMPSP4"
            width={144}
            height={144}
            className="w-28 h-28 sm:w-36 sm:h-36 drop-shadow-2xl select-none"
            draggable={false}
          />
          <div className="absolute -top-2 -right-2 w-8 h-8 rounded-full bg-[#0A84FF] border-2 border-black flex items-center justify-center text-white">
            <ShieldCheck className="w-4 h-4" />
          </div>
        </div>

        {/* Title & Instructions */}
        <h1 className="text-2xl sm:text-3xl font-extrabold tracking-tight text-white">
          SAMPSP4 Upload
        </h1>
        <p className="mt-2 text-sm sm:text-base text-neutral-400 max-w-sm">
          Escribe el PIN que aparece en tu TV para emparejar tu dispositivo con la PlayStation 4.
        </p>

        {/* PIN Input Box */}
        <div className="mt-8 w-full">
          <PinInput
            value={pin}
            onChange={(val) => {
              setPin(val);
              if (errorMessage) setErrorMessage(null);
              if (val.length === 6) {
                handlePair(val);
              }
            }}
            disabled={isLoading}
            isError={!!errorMessage}
          />
        </div>

        {/* Error message */}
        {errorMessage && (
          <div className="mt-4 p-3 w-full rounded-2xl bg-red-950/40 border border-red-900/60 text-xs text-red-300 flex items-center gap-2 text-left animate-shake">
            <AlertCircle className="w-4 h-4 shrink-0 text-red-400" />
            <span>{errorMessage}</span>
          </div>
        )}

        {/* Pair Action Button */}
        <button
          onClick={() => handlePair()}
          disabled={isLoading || pin.length < 6}
          className="mt-6 w-full min-h-[50px] rounded-2xl bg-[#0A84FF] hover:bg-[#0070E0] active:scale-[0.99] text-white font-semibold text-sm sm:text-base flex items-center justify-center gap-2 transition-all disabled:opacity-40 disabled:pointer-events-none shadow-lg shadow-[#0A84FF]/25"
        >
          {isLoading ? (
            <>
              <Loader2 className="w-4 h-4 animate-spin" />
              <span>Verificando PIN...</span>
            </>
          ) : (
            <>
              <span>Conectar con PS4</span>
              <ArrowRight className="w-4 h-4" />
            </>
          )}
        </button>

        {/* Demo PIN helper for easy testing */}
        {apiClient.isMockMode() && (
          <button
            onClick={handleUseDemoPin}
            className="mt-4 text-xs text-neutral-400 hover:text-[#0A84FF] transition-colors flex items-center gap-1.5 py-1 px-3 rounded-lg hover:bg-neutral-900"
          >
            <Sparkles className="w-3.5 h-3.5 text-[#0A84FF]" />
            <span>Usar PIN de prueba simulado (<strong className="text-white font-mono">123456</strong>)</span>
          </button>
        )}
      </div>

      {/* Footer */}
      <footer className="w-full text-center py-4 text-xs text-neutral-600 font-medium">
        SAMPSP4 · Sam Dev
      </footer>
    </div>
  );
};
