import React, { useState, useEffect } from 'react';
import { Game } from '../types';
import { formatBytes, formatDate } from '../utils/format';
import { apiClient } from '../api/client';
import { X, Edit2, Trash2, Check, AlertTriangle, Loader2 } from 'lucide-react';

interface GameDetailModalProps {
  game: Game | null;
  onClose: () => void;
  onGameUpdated: (game: Game) => void;
  onGameDeleted: (id: string) => void;
}

export const GameDetailModal: React.FC<GameDetailModalProps> = ({
  game,
  onClose,
  onGameUpdated,
  onGameDeleted,
}) => {
  const [isEditingTitle, setIsEditingTitle] = useState(false);
  const [titleInput, setTitleInput] = useState('');
  const [isSaving, setIsSaving] = useState(false);
  const [isDeleting, setIsDeleting] = useState(false);
  const [showDeleteConfirm, setShowDeleteConfirm] = useState(false);
  const [errorMsg, setErrorMsg] = useState<string | null>(null);

  useEffect(() => {
    if (game) {
      setTitleInput(game.title);
      setIsEditingTitle(false);
      setShowDeleteConfirm(false);
      setErrorMsg(null);
    }
  }, [game]);

  if (!game) return null;

  const handleSaveTitle = async () => {
    if (!titleInput.trim() || titleInput === game.title) {
      setIsEditingTitle(false);
      return;
    }

    setIsSaving(true);
    setErrorMsg(null);
    try {
      const updated = await apiClient.updateGame(game.id, titleInput.trim());
      onGameUpdated(updated);
      setIsEditingTitle(false);
    } catch (err: any) {
      setErrorMsg(err.message || 'Error al guardar título');
    } finally {
      setIsSaving(false);
    }
  };

  const handleDelete = async () => {
    setIsDeleting(true);
    setErrorMsg(null);
    try {
      await apiClient.deleteGame(game.id);
      onGameDeleted(game.id);
      onClose();
    } catch (err: any) {
      setErrorMsg(err.message || 'Error al borrar el juego');
      setIsDeleting(false);
    }
  };

  return (
    <div className="fixed inset-0 z-50 flex items-end sm:items-center justify-center p-0 sm:p-4 bg-black/80 backdrop-blur-md animate-fadeIn">
      {/* Backdrop tap to close */}
      <div className="absolute inset-0" onClick={onClose} />

      {/* Modal / Bottom Sheet */}
      <div className="relative w-full sm:max-w-md bg-[#141416] border-t sm:border border-[#27272a] rounded-t-3xl sm:rounded-3xl p-6 sm:p-7 shadow-2xl z-10 max-h-[90vh] overflow-y-auto">
        {/* Mobile handle indicator */}
        <div className="sm:hidden w-12 h-1.5 bg-neutral-700 rounded-full mx-auto mb-4" />

        {/* Close Button */}
        <button
          onClick={onClose}
          className="absolute top-5 right-5 w-10 h-10 rounded-full bg-neutral-900 border border-neutral-800 flex items-center justify-center text-neutral-400 hover:text-white transition-colors"
          aria-label="Cerrar detalles"
        >
          <X className="w-5 h-5" />
        </button>

        {/* Header Cover + Basic Info */}
        <div className="flex gap-4 items-start">
          <div className="w-24 h-32 shrink-0 rounded-xl overflow-hidden bg-neutral-900 border border-neutral-800 shadow-md">
            <img
              src={apiClient.getCoverUrl(game.id)}
              alt={game.title}
              className="w-full h-full object-cover"
              referrerPolicy="no-referrer"
            />
          </div>

          <div className="flex-1 min-w-0 pr-8">
            <div className="text-[11px] font-mono uppercase text-[#0A84FF] font-semibold tracking-wider">
              {game.system} · {game.format}
            </div>

            {!isEditingTitle ? (
              <div className="mt-1">
                <h3 className="text-base sm:text-lg font-bold text-white tracking-tight leading-snug">
                  {game.title}
                </h3>
                <button
                  onClick={() => setIsEditingTitle(true)}
                  className="mt-1.5 inline-flex items-center gap-1.5 text-xs text-neutral-400 hover:text-[#0A84FF] transition-colors"
                >
                  <Edit2 className="w-3.5 h-3.5" />
                  <span>Renombrar</span>
                </button>
              </div>
            ) : (
              <div className="mt-1 space-y-2">
                <input
                  type="text"
                  value={titleInput}
                  onChange={(e) => setTitleInput(e.target.value)}
                  className="w-full px-3 py-2 text-sm bg-neutral-900 border border-[#0A84FF] rounded-xl text-white outline-none focus:ring-2 focus:ring-[#0A84FF]/40"
                  autoFocus
                />
                <div className="flex items-center gap-2">
                  <button
                    onClick={handleSaveTitle}
                    disabled={isSaving}
                    className="px-3 py-1.5 bg-[#0A84FF] text-white text-xs font-semibold rounded-lg hover:bg-[#0070E0] disabled:opacity-50 flex items-center gap-1"
                  >
                    {isSaving ? <Loader2 className="w-3 h-3 animate-spin" /> : <Check className="w-3 h-3" />}
                    <span>Guardar</span>
                  </button>
                  <button
                    onClick={() => {
                      setTitleInput(game.title);
                      setIsEditingTitle(false);
                    }}
                    className="px-3 py-1.5 text-neutral-400 hover:text-white text-xs rounded-lg"
                  >
                    Cancelar
                  </button>
                </div>
              </div>
            )}
          </div>
        </div>

        {errorMsg && (
          <div className="mt-4 p-3 rounded-xl bg-red-950/40 border border-red-900/60 text-xs text-red-300 flex items-center gap-2">
            <AlertTriangle className="w-4 h-4 shrink-0 text-red-400" />
            <span>{errorMsg}</span>
          </div>
        )}

        {/* Technical Details Grid */}
        <div className="mt-6 space-y-2.5 bg-[#18181c] border border-neutral-800/80 rounded-2xl p-4">
          <div className="flex items-center justify-between text-xs py-1 border-b border-neutral-800/50">
            <span className="text-neutral-400">Title ID</span>
            <span className="font-mono font-medium text-white">{game.titleId}</span>
          </div>

          <div className="flex items-center justify-between text-xs py-1 border-b border-neutral-800/50">
            <span className="text-neutral-400">Tamaño en disco</span>
            <span className="font-mono font-medium text-white tabular-nums">{formatBytes(game.sizeBytes)}</span>
          </div>

          <div className="flex items-center justify-between text-xs py-1 border-b border-neutral-800/50">
            <span className="text-neutral-400">Formato</span>
            <span className="font-mono uppercase font-medium text-white">{game.format}</span>
          </div>

          <div className="flex items-center justify-between text-xs py-1">
            <span className="text-neutral-400">Fecha de instalación</span>
            <span className="font-medium text-white">{formatDate(game.addedAt)}</span>
          </div>
        </div>

        {/* Delete confirmation section */}
        <div className="mt-6 pt-4 border-t border-neutral-800">
          {!showDeleteConfirm ? (
            <button
              onClick={() => setShowDeleteConfirm(true)}
              className="w-full min-h-[48px] rounded-2xl border border-red-900/40 hover:bg-red-950/30 text-red-400 hover:text-red-300 text-sm font-medium flex items-center justify-center gap-2 transition-colors"
            >
              <Trash2 className="w-4 h-4" />
              <span>Eliminar de la PS4</span>
            </button>
          ) : (
            <div className="space-y-3 bg-red-950/20 border border-red-900/40 rounded-2xl p-4 text-center">
              <p className="text-xs text-red-200">
                ¿Seguro que deseas eliminar <span className="font-semibold text-white">"{game.title}"</span>? Se liberarán{' '}
                <span className="font-mono font-semibold">{formatBytes(game.sizeBytes)}</span> en la consola.
              </p>
              <div className="flex items-center gap-2">
                <button
                  onClick={handleDelete}
                  disabled={isDeleting}
                  className="flex-1 min-h-[44px] bg-red-600 hover:bg-red-500 text-white font-semibold text-xs rounded-xl flex items-center justify-center gap-1.5 transition-colors disabled:opacity-50"
                >
                  {isDeleting ? <Loader2 className="w-4 h-4 animate-spin" /> : <Trash2 className="w-4 h-4" />}
                  <span>Sí, eliminar</span>
                </button>
                <button
                  onClick={() => setShowDeleteConfirm(false)}
                  disabled={isDeleting}
                  className="flex-1 min-h-[44px] bg-neutral-800 hover:bg-neutral-700 text-neutral-300 font-medium text-xs rounded-xl transition-colors"
                >
                  Cancelar
                </button>
              </div>
            </div>
          )}
        </div>
      </div>
    </div>
  );
};
