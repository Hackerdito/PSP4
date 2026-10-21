import React from 'react';
import { Upload, Library, Sliders, LogOut, Tv } from 'lucide-react';
import { apiClient, mockAllowed } from '../api/client';

interface HeaderProps {
  currentTab: 'upload' | 'library';
  onTabChange: (tab: 'upload' | 'library') => void;
  onOpenMockControls: () => void;
  onDisconnect: () => void;
  isConnected: boolean;
}

export const Header: React.FC<HeaderProps> = ({
  currentTab,
  onTabChange,
  onOpenMockControls,
  onDisconnect,
  isConnected,
}) => {
  const isMock = apiClient.isMockMode();

  return (
    <header className="sticky top-0 z-30 w-full bg-black/90 backdrop-blur-md border-b border-[#242428] px-4 sm:px-8 py-3.5 flex items-center justify-between gap-4">
      {/* Zone 1: Brand wordmark */}
      <div className="flex items-center gap-3 shrink-0">
        <div className="w-9 h-9 rounded-xl bg-gradient-to-br from-neutral-800 to-neutral-950 border border-neutral-700/80 flex items-center justify-center text-white shadow-sm">
          <Tv className="w-5 h-5 text-[#0A84FF]" />
        </div>
        <div className="flex flex-col">
          <div className="flex items-center gap-2">
            <span className="text-base font-bold tracking-tight text-white whitespace-nowrap">
              SAMPSP4
            </span>
            <span className="text-[10px] font-mono font-medium text-neutral-400 bg-neutral-900 border border-neutral-800 px-1.5 py-0.5 rounded">
              v1.1
            </span>
          </div>
          <span className="text-[10px] text-neutral-500 whitespace-nowrap hidden sm:inline">
            PS4 Local Homebrew Uploader
          </span>
        </div>
      </div>

      {/* Zone 2: Navigation links */}
      <nav className="flex items-center gap-1 bg-[#121214] p-1 rounded-xl border border-[#242428]">
        <button
          onClick={() => onTabChange('upload')}
          className={`flex items-center gap-1.5 px-3.5 py-1.5 rounded-lg text-xs font-semibold whitespace-nowrap transition-colors ${
            currentTab === 'upload'
              ? 'bg-[#1e1e24] text-white shadow-sm'
              : 'text-neutral-400 hover:text-white'
          }`}
        >
          <Upload className="w-3.5 h-3.5" />
          <span>Subir</span>
        </button>
        <button
          onClick={() => onTabChange('library')}
          className={`flex items-center gap-1.5 px-3.5 py-1.5 rounded-lg text-xs font-semibold whitespace-nowrap transition-colors ${
            currentTab === 'library'
              ? 'bg-[#1e1e24] text-white shadow-sm'
              : 'text-neutral-400 hover:text-white'
          }`}
        >
          <Library className="w-3.5 h-3.5" />
          <span>Biblioteca</span>
        </button>
      </nav>

      {/* Zone 3: Connection & Dev Tools */}
      <div className="flex items-center gap-2 shrink-0">
        {mockAllowed && (
          <button
            onClick={onOpenMockControls}
            className="flex items-center gap-1.5 px-2.5 py-1.5 bg-[#121214] hover:bg-[#1a1a1f] border border-[#242428] rounded-xl text-xs font-medium text-neutral-300 hover:text-white transition-colors"
            title="Panel de simulación y pruebas"
          >
            <Sliders className="w-3.5 h-3.5 text-[#0A84FF]" />
            <span className="hidden md:inline font-mono text-[11px]">
              {isMock ? 'Mock' : 'PS4 Directa'}
            </span>
          </button>
        )}

        <button
          onClick={onDisconnect}
          className="p-2 text-neutral-400 hover:text-red-400 hover:bg-[#18181c] rounded-xl border border-transparent hover:border-neutral-800 transition-colors"
          title="Desconectar consola"
          aria-label="Cerrar sesión"
        >
          <LogOut className="w-4 h-4" />
        </button>
      </div>
    </header>
  );
};
