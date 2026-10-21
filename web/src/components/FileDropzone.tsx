import React, { useRef, useState } from 'react';
import { UploadCloud, FolderOpen, Gamepad2, Disc } from 'lucide-react';

interface FileDropzoneProps {
  onFilesSelected: (files: File[]) => void;
  disabled?: boolean;
}

export const FileDropzone: React.FC<FileDropzoneProps> = ({ onFilesSelected, disabled = false }) => {
  const [isDragOver, setIsDragOver] = useState(false);
  const fileInputRef = useRef<HTMLInputElement>(null);

  const handleDragEnter = (e: React.DragEvent) => {
    e.preventDefault();
    e.stopPropagation();
    if (!disabled) setIsDragOver(true);
  };

  const handleDragLeave = (e: React.DragEvent) => {
    e.preventDefault();
    e.stopPropagation();
    setIsDragOver(false);
  };

  const handleDragOver = (e: React.DragEvent) => {
    e.preventDefault();
    e.stopPropagation();
    if (!disabled) setIsDragOver(true);
  };

  const handleDrop = (e: React.DragEvent) => {
    e.preventDefault();
    e.stopPropagation();
    setIsDragOver(false);
    if (disabled) return;

    if (e.dataTransfer.files && e.dataTransfer.files.length > 0) {
      const droppedFiles = Array.from(e.dataTransfer.files);
      onFilesSelected(droppedFiles);
    }
  };

  const handleFileChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    if (e.target.files && e.target.files.length > 0) {
      const selected = Array.from(e.target.files);
      onFilesSelected(selected);
      // Reset input value so re-selecting same file triggers change
      e.target.value = '';
    }
  };

  const handleClick = () => {
    if (!disabled && fileInputRef.current) {
      fileInputRef.current.click();
    }
  };

  return (
    <div
      onClick={handleClick}
      onDragEnter={handleDragEnter}
      onDragOver={handleDragOver}
      onDragLeave={handleDragLeave}
      onDrop={handleDrop}
      className={`relative w-full rounded-3xl p-6 sm:p-10 border-2 border-dashed cursor-pointer transition-all duration-200 select-none group
        ${
          isDragOver
            ? 'border-[#0A84FF] bg-[#0A84FF]/10 scale-[1.008]'
            : 'border-[#27272a] hover:border-neutral-600 bg-[#121214]/80 hover:bg-[#151518]'
        }
        ${disabled ? 'opacity-50 cursor-not-allowed pointer-events-none' : ''}
      `}
    >
      <input
        ref={fileInputRef}
        type="file"
        multiple
        accept=".iso,.cso,.pbp,.chd,.cue,.bin"
        onChange={handleFileChange}
        className="hidden"
      />

      <div className="flex flex-col items-center text-center">
        <div
          className={`w-16 h-16 sm:w-20 sm:h-20 rounded-2xl flex items-center justify-center transition-transform duration-200 ${
            isDragOver ? 'scale-110 bg-[#0A84FF] text-white shadow-lg shadow-[#0A84FF]/30' : 'bg-[#18181c] text-neutral-300 group-hover:text-white border border-[#27272a]'
          }`}
        >
          <UploadCloud className="w-8 h-8 sm:w-10 sm:h-10" />
        </div>

        <h3 className="mt-5 text-base sm:text-lg font-semibold text-white tracking-tight">
          Toca para elegir juegos o arrastra archivos aquí
        </h3>

        <p className="mt-1.5 text-xs sm:text-sm text-neutral-400 max-w-md">
          Soporta imágenes de PSP y PS1. Para discos múltiples o pistas CD, selecciona el archivo{' '}
          <span className="font-mono text-neutral-300">.cue</span> y sus{' '}
          <span className="font-mono text-neutral-300">.bin</span> juntos.
        </p>

        {/* Supported formats row */}
        <div className="mt-5 flex flex-wrap items-center justify-center gap-2 pt-4 border-t border-neutral-800/80 w-full max-w-md">
          <div className="flex items-center gap-1.5 text-xs text-neutral-400 bg-neutral-900/90 px-3 py-1.5 rounded-xl border border-neutral-800">
            <Gamepad2 className="w-3.5 h-3.5 text-[#0A84FF]" />
            <span className="font-medium text-neutral-200">PSP:</span>
            <span className="font-mono text-neutral-400">.iso · .cso · .pbp</span>
          </div>

          <div className="flex items-center gap-1.5 text-xs text-neutral-400 bg-neutral-900/90 px-3 py-1.5 rounded-xl border border-neutral-800">
            <Disc className="w-3.5 h-3.5 text-neutral-300" />
            <span className="font-medium text-neutral-200">PS1:</span>
            <span className="font-mono text-neutral-400">.chd · .pbp · .cue + .bin</span>
          </div>
        </div>
      </div>
    </div>
  );
};
