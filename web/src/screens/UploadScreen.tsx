import React from 'react';
import { StorageBar } from '../components/StorageBar';
import { FileDropzone } from '../components/FileDropzone';
import { UploadQueue } from '../components/UploadQueue';
import { WakeLockNotice } from '../components/WakeLockNotice';
import { SystemInfo, UploadQueueItem, GameSystem } from '../types';

interface UploadScreenProps {
  systemInfo: SystemInfo | null;
  queue: UploadQueueItem[];
  onFilesSelected: (files: File[]) => void;
  onCancelItem: (id: string) => void;
  onRetryItem: (id: string) => void;
  onRemoveItem: (id: string) => void;
  onClearCompleted: () => void;
  onUpdateSystem: (id: string, system: GameSystem) => void;
}

export const UploadScreen: React.FC<UploadScreenProps> = ({
  systemInfo,
  queue,
  onFilesSelected,
  onCancelItem,
  onRetryItem,
  onRemoveItem,
  onClearCompleted,
  onUpdateSystem,
}) => {
  const hasActiveUploads = queue.some(
    (q) => q.status === 'uploading' || q.status === 'resuming' || q.status === 'verifying'
  );

  return (
    <div className="w-full max-w-4xl mx-auto px-4 sm:px-6 py-6 sm:py-8 space-y-6 animate-fadeIn">
      {/* Wake Lock Tip */}
      <WakeLockNotice hasActiveUploads={hasActiveUploads} />

      {/* Storage Information */}
      {systemInfo && (
        <StorageBar freeBytes={systemInfo.freeBytes} totalBytes={systemInfo.totalBytes} />
      )}

      {/* Upload Zone */}
      <FileDropzone onFilesSelected={onFilesSelected} />

      {/* Upload Queue */}
      <UploadQueue
        queue={queue}
        onCancelItem={onCancelItem}
        onRetryItem={onRetryItem}
        onRemoveItem={onRemoveItem}
        onClearCompleted={onClearCompleted}
        onUpdateSystem={onUpdateSystem}
      />
    </div>
  );
};
