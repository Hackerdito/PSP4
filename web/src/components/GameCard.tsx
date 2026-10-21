import React, { useState } from 'react';
import { Game } from '../types';
import { formatBytes } from '../utils/format';
import { apiClient } from '../api/client';
import { Gamepad2, Disc } from 'lucide-react';

interface GameCardProps {
  game: Game;
  onClick: () => void;
}

export const GameCard: React.FC<GameCardProps> = ({ game, onClick }) => {
  const [imageError, setImageError] = useState(false);
  const coverUrl = apiClient.getCoverUrl(game.id);

  const isPSP = game.system === 'psp';

  return (
    <div
      onClick={onClick}
      role="button"
      tabIndex={0}
      onKeyDown={(e) => {
        if (e.key === 'Enter' || e.key === ' ') {
          e.preventDefault();
          onClick();
        }
      }}
      className="group relative flex flex-col bg-[#121214] border border-[#242428] hover:border-[#383840] rounded-2xl overflow-hidden cursor-pointer transition-all duration-200 hover:-translate-y-1 hover:shadow-xl hover:shadow-black/60 focus:outline-none focus:ring-2 focus:ring-[#0A84FF]/50 select-none text-left"
    >
      {/* Cover Aspect Ratio container (3:4 standard game cover) */}
      <div className="relative aspect-[3/4] w-full bg-neutral-900 overflow-hidden">
        {!imageError ? (
          <img
            src={coverUrl}
            alt={game.title}
            referrerPolicy="no-referrer"
            loading="lazy"
            onError={() => setImageError(true)}
            className="w-full h-full object-cover transition-transform duration-300 group-hover:scale-105"
          />
        ) : (
          <div className="w-full h-full flex flex-col items-center justify-center p-4 bg-gradient-to-b from-neutral-800 to-neutral-950 text-center">
            {isPSP ? (
              <Gamepad2 className="w-10 h-10 text-neutral-500 mb-2" />
            ) : (
              <Disc className="w-10 h-10 text-neutral-500 mb-2" />
            )}
            <span className="font-semibold text-xs text-neutral-300 line-clamp-2">
              {game.title}
            </span>
            <span className="font-mono text-[10px] text-neutral-500 mt-1">
              {game.titleId}
            </span>
          </div>
        )}

        {/* Subtle hover gradient scrim */}
        <div className="absolute inset-0 bg-gradient-to-t from-black/80 via-transparent to-transparent opacity-60 group-hover:opacity-40 transition-opacity" />

        {/* System indicator watermark/tag */}
        <div className="absolute top-2.5 left-2.5">
          <span className="text-[10px] font-bold tracking-wider uppercase px-2 py-0.5 rounded bg-black/60 backdrop-blur-md text-white border border-white/10 font-mono">
            {game.system}
          </span>
        </div>
      </div>

      {/* Info Card Content */}
      <div className="p-3.5 flex flex-col justify-between flex-1">
        <div>
          <h4 className="text-sm font-semibold text-white tracking-tight line-clamp-1 group-hover:text-[#0A84FF] transition-colors">
            {game.title}
          </h4>

          {/* Clean unboxed metadata */}
          <div className="flex items-center gap-1.5 text-xs text-neutral-400 mt-1">
            <span className="font-mono text-[11px] text-neutral-300">{game.titleId}</span>
            <span aria-hidden="true">·</span>
            <span className="font-mono text-[11px] uppercase">{game.format}</span>
          </div>
        </div>

        <div className="mt-2 text-[11px] font-mono text-neutral-500 tabular-nums">
          {formatBytes(game.sizeBytes)}
        </div>
      </div>
    </div>
  );
};
