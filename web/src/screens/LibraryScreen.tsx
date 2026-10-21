import React, { useState, useMemo } from 'react';
import { Game, GameSystem } from '../types';
import { GameCard } from '../components/GameCard';
import { GameDetailModal } from '../components/GameDetailModal';
import { Search, Filter, Gamepad2, Disc, FolderSearch, RefreshCw } from 'lucide-react';

interface LibraryScreenProps {
  games: Game[];
  isLoading: boolean;
  onRefresh: () => void;
  onGameUpdated: (game: Game) => void;
  onGameDeleted: (id: string) => void;
}

export const LibraryScreen: React.FC<LibraryScreenProps> = ({
  games,
  isLoading,
  onRefresh,
  onGameUpdated,
  onGameDeleted,
}) => {
  const [searchQuery, setSearchQuery] = useState('');
  const [selectedFilter, setSelectedFilter] = useState<'all' | GameSystem>('all');
  const [activeGame, setActiveGame] = useState<Game | null>(null);

  // Filtered games
  const filteredGames = useMemo(() => {
    return games.filter((g) => {
      const matchesFilter = selectedFilter === 'all' || g.system === selectedFilter;
      const query = searchQuery.toLowerCase().trim();
      const matchesQuery =
        !query ||
        g.title.toLowerCase().includes(query) ||
        g.titleId.toLowerCase().includes(query);
      return matchesFilter && matchesQuery;
    });
  }, [games, selectedFilter, searchQuery]);

  const pspCount = games.filter((g) => g.system === 'psp').length;
  const ps1Count = games.filter((g) => g.system === 'ps1').length;

  return (
    <div className="w-full max-w-6xl mx-auto px-4 sm:px-6 py-6 sm:py-8 space-y-6 animate-fadeIn">
      {/* Top Filter and Search Bar */}
      <div className="flex flex-col sm:flex-row items-stretch sm:items-center justify-between gap-4">
        {/* Filter Segmented Control */}
        <div className="flex items-center gap-1 bg-[#121214] border border-[#242428] p-1 rounded-2xl self-start sm:self-auto">
          <button
            onClick={() => setSelectedFilter('all')}
            className={`px-3.5 py-1.5 rounded-xl text-xs font-semibold whitespace-nowrap transition-colors ${
              selectedFilter === 'all'
                ? 'bg-[#1e1e24] text-white shadow-sm'
                : 'text-neutral-400 hover:text-white'
            }`}
          >
            Todos ({games.length})
          </button>
          <button
            onClick={() => setSelectedFilter('psp')}
            className={`flex items-center gap-1.5 px-3.5 py-1.5 rounded-xl text-xs font-semibold whitespace-nowrap transition-colors ${
              selectedFilter === 'psp'
                ? 'bg-[#0A84FF] text-white shadow-sm'
                : 'text-neutral-400 hover:text-white'
            }`}
          >
            <Gamepad2 className="w-3.5 h-3.5" />
            <span>PSP ({pspCount})</span>
          </button>
          <button
            onClick={() => setSelectedFilter('ps1')}
            className={`flex items-center gap-1.5 px-3.5 py-1.5 rounded-xl text-xs font-semibold whitespace-nowrap transition-colors ${
              selectedFilter === 'ps1'
                ? 'bg-neutral-700 text-white shadow-sm'
                : 'text-neutral-400 hover:text-white'
            }`}
          >
            <Disc className="w-3.5 h-3.5" />
            <span>PS1 ({ps1Count})</span>
          </button>
        </div>

        {/* Search input + Refresh button */}
        <div className="flex items-center gap-2">
          <div className="relative flex-1 sm:w-64">
            <Search className="absolute left-3.5 top-1/2 -translate-y-1/2 w-4 h-4 text-neutral-500" />
            <input
              type="text"
              placeholder="Buscar por título o Title ID..."
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
              className="w-full pl-9 pr-4 py-2 bg-[#121214] border border-[#242428] focus:border-[#0A84FF] rounded-xl text-xs text-white placeholder-neutral-500 outline-none transition-colors"
            />
          </div>

          <button
            onClick={onRefresh}
            disabled={isLoading}
            className="p-2 rounded-xl bg-[#121214] border border-[#242428] text-neutral-400 hover:text-white hover:bg-neutral-800 transition-colors disabled:opacity-50"
            title="Refrescar lista"
            aria-label="Refrescar lista de juegos"
          >
            <RefreshCw className={`w-4 h-4 ${isLoading ? 'animate-spin text-[#0A84FF]' : ''}`} />
          </button>
        </div>
      </div>

      {/* Grid of game covers */}
      {filteredGames.length > 0 ? (
        <div className="grid grid-cols-2 sm:grid-cols-3 md:grid-cols-4 lg:grid-cols-5 xl:grid-cols-6 gap-3 sm:gap-5">
          {filteredGames.map((game) => (
            <GameCard
              key={game.id}
              game={game}
              onClick={() => setActiveGame(game)}
            />
          ))}
        </div>
      ) : (
        <div className="w-full py-16 flex flex-col items-center justify-center text-center bg-[#121214]/40 border border-[#242428] rounded-3xl p-8">
          <div className="w-14 h-14 rounded-2xl bg-neutral-900 border border-neutral-800 flex items-center justify-center text-neutral-500 mb-3">
            <FolderSearch className="w-7 h-7" />
          </div>
          <h4 className="text-base font-semibold text-white">
            {searchQuery ? 'No se encontraron juegos' : 'Tu biblioteca está vacía'}
          </h4>
          <p className="mt-1 text-xs text-neutral-400 max-w-sm">
            {searchQuery
              ? `No hay coincidencias para "${searchQuery}". Prueba con otro nombre o Title ID.`
              : 'Sube tus copias de seguridad de PSP o PS1 desde la pestaña "Subir" para verlas aquí.'}
          </p>
        </div>
      )}

      {/* Game Details Modal */}
      <GameDetailModal
        game={activeGame}
        onClose={() => setActiveGame(null)}
        onGameUpdated={(updated) => {
          onGameUpdated(updated);
          setActiveGame(updated);
        }}
        onGameDeleted={(id) => {
          onGameDeleted(id);
          setActiveGame(null);
        }}
      />
    </div>
  );
};
