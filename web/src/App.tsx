/**
 * @license
 * SAMPSP4 Upload · Sam Dev
 */

import React, { useState, useEffect, useCallback } from 'react';
import { apiClient, mockAllowed } from './api/client';
import { uploadEngine } from './upload/engine';
import { mockService } from './mock/mockService';
import { Game, GameSystem, SystemInfo, UploadQueueItem } from './types';
import { Header } from './components/Header';
import { MockControlsModal } from './components/MockControlsModal';
import { PairScreen } from './screens/PairScreen';
import { UploadScreen } from './screens/UploadScreen';
import { LibraryScreen } from './screens/LibraryScreen';
import { DisconnectedScreen } from './screens/DisconnectedScreen';

export default function App() {
  const [token, setToken] = useState<string | null>(() => apiClient.getToken());
  const [currentTab, setCurrentTab] = useState<'upload' | 'library'>('upload');
  const [systemInfo, setSystemInfo] = useState<SystemInfo | null>(null);
  const [games, setGames] = useState<Game[]>([]);
  const [queue, setQueue] = useState<UploadQueueItem[]>([]);
  const [isConnected, setIsConnected] = useState(true);
  const [isRefreshing, setIsRefreshing] = useState(false);
  const [showMockControls, setShowMockControls] = useState(false);
  const [toastMessage, setToastMessage] = useState<string | null>(null);

  // Play subtle synthetic beep via Web Audio API when an upload completes
  const playSuccessChime = useCallback(() => {
    try {
      const AudioCtx = window.AudioContext || (window as any).webkitAudioContext;
      if (!AudioCtx) return;
      const ctx = new AudioCtx();
      const osc = ctx.createOscillator();
      const gain = ctx.createGain();
      osc.type = 'sine';
      osc.frequency.setValueAtTime(587.33, ctx.currentTime); // D5
      osc.frequency.exponentialRampToValueAtTime(880, ctx.currentTime + 0.15); // A5
      gain.gain.setValueAtTime(0.08, ctx.currentTime);
      gain.gain.exponentialRampToValueAtTime(0.001, ctx.currentTime + 0.35);
      osc.connect(gain);
      gain.connect(ctx.destination);
      osc.start();
      osc.stop(ctx.currentTime + 0.35);
    } catch {
      // Audio might be blocked by browser policy until user gesture
    }
  }, []);

  const showToast = useCallback((msg: string) => {
    setToastMessage(msg);
    setTimeout(() => {
      setToastMessage((current) => (current === msg ? null : current));
    }, 4000);
  }, []);

  // Fetch initial info and games
  const loadData = useCallback(async () => {
    if (!token) return;
    setIsRefreshing(true);
    try {
      const [info, gameList] = await Promise.all([
        apiClient.getInfo(),
        apiClient.getGames(),
      ]);
      setSystemInfo(info);
      setGames(gameList);
      setIsConnected(true);
    } catch (err: any) {
      if (err.status === 401) {
        setToken(null);
        apiClient.clearSession();
        showToast('Sesión expirada. Vuelve a ingresar el PIN.');
      } else {
        setIsConnected(false);
      }
    } finally {
      setIsRefreshing(false);
    }
  }, [token, showToast]);

  useEffect(() => {
    if (token) {
      loadData();
    }
  }, [token, loadData]);

  // Subscribe to upload engine events
  useEffect(() => {
    const unsubscribe = uploadEngine.subscribe({
      onQueueUpdated: (updatedQueue) => {
        setQueue(updatedQueue);
      },
      onItemCompleted: (_item, newGame) => {
        playSuccessChime();
        showToast(`¡"${newGame.title}" subido y listo en tu PS4!`);
        // Refresh games & storage
        loadData();
      },
      onItemFailed: (item, error) => {
        showToast(`Fallo al subir ${item.displayName}: ${error}`);
      },
    });

    return () => unsubscribe();
  }, [playSuccessChime, showToast, loadData]);

  // Subscribe to mock service changes (if in mock mode)
  useEffect(() => {
    const unsubMock = mockService.subscribe(() => {
      if (apiClient.isMockMode() && token) {
        loadData();
      }
    });
    return () => {
      unsubMock();
    };
  }, [token, loadData]);

  const handlePaired = () => {
    setToken(apiClient.getToken());
    loadData();
  };

  const handleDisconnect = () => {
    apiClient.clearSession();
    setToken(null);
    setSystemInfo(null);
    setGames([]);
  };

  const handleFilesSelected = (files: File[]) => {
    uploadEngine.addFiles(files);
    showToast(`${files.length} archivo(s) agregado(s) a la cola`);
  };

  const handleGameUpdated = (updatedGame: Game) => {
    setGames((prev) => prev.map((g) => (g.id === updatedGame.id ? updatedGame : g)));
    showToast(`Título actualizado a "${updatedGame.title}"`);
  };

  const handleGameDeleted = (deletedId: string) => {
    setGames((prev) => prev.filter((g) => g.id !== deletedId));
    loadData(); // Re-read updated free space
    showToast('Juego eliminado de la PS4');
  };

  // If not authenticated, render Pairing Screen
  if (!token) {
    return (
      <>
        <PairScreen
          onPaired={handlePaired}
          onOpenMockControls={() => setShowMockControls(true)}
        />
        {mockAllowed && (
          <MockControlsModal
            isOpen={showMockControls}
            onClose={() => setShowMockControls(false)}
            onStateChanged={loadData}
          />
        )}
      </>
    );
  }

  return (
    <div className="min-h-screen w-full flex flex-col bg-black text-neutral-100 selection:bg-[#0A84FF] selection:text-white">
      {/* Header Contract */}
      <Header
        currentTab={currentTab}
        onTabChange={setCurrentTab}
        onOpenMockControls={() => setShowMockControls(true)}
        onDisconnect={handleDisconnect}
        isConnected={isConnected}
      />

      {/* Floating Toast Notification */}
      {toastMessage && (
        <div className="fixed bottom-6 right-4 left-4 sm:left-auto sm:right-6 sm:max-w-md z-50 bg-[#16161a] border border-[#27272a] text-white text-xs sm:text-sm font-medium py-3 px-4 rounded-2xl shadow-2xl flex items-center justify-between gap-3 animate-slideUp">
          <span>{toastMessage}</span>
          <button
            onClick={() => setToastMessage(null)}
            className="text-neutral-400 hover:text-white p-1"
          >
            ✕
          </button>
        </div>
      )}

      {/* Main Body */}
      <main className="flex-1 w-full pb-16">
        {!isConnected ? (
          <DisconnectedScreen
            onRetry={loadData}
            onOpenMockControls={() => setShowMockControls(true)}
            isRetrying={isRefreshing}
          />
        ) : currentTab === 'upload' ? (
          <UploadScreen
            systemInfo={systemInfo}
            queue={queue}
            onFilesSelected={handleFilesSelected}
            onCancelItem={(id) => uploadEngine.cancelItem(id)}
            onRetryItem={(id) => uploadEngine.retryItem(id)}
            onRemoveItem={(id) => uploadEngine.removeItem(id)}
            onClearCompleted={() => uploadEngine.clearCompleted()}
            onUpdateSystem={(id, sys) => uploadEngine.updateItemSystem(id, sys)}
          />
        ) : (
          <LibraryScreen
            games={games}
            isLoading={isRefreshing}
            onRefresh={loadData}
            onGameUpdated={handleGameUpdated}
            onGameDeleted={handleGameDeleted}
          />
        )}
      </main>

      {/* Dev & Mock Simulation Controls Drawer */}
      {mockAllowed && (
        <MockControlsModal
          isOpen={showMockControls}
          onClose={() => setShowMockControls(false)}
          onStateChanged={loadData}
        />
      )}

      {/* Footer */}
      <footer className="w-full border-t border-[#1c1c20] py-5 px-6 text-center text-xs text-neutral-500 font-medium">
        SAMPSP4 · Sam Dev
      </footer>
    </div>
  );
}
