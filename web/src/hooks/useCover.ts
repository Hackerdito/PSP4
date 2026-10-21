import { useEffect, useState } from 'react';
import { apiClient } from '../api/client';

/**
 * Portada de un juego como URL que un <img> puede mostrar.
 * La API pide el token en el encabezado Authorization, y un <img src> no puede mandarlo:
 * por eso la portada se baja con fetch y se muestra como blob: URL.
 * Devuelve undefined mientras carga y null si el juego no tiene portada.
 */
export function useCover(id: string | null | undefined): string | null | undefined {
  const [url, setUrl] = useState<string | null | undefined>(undefined);
  useEffect(() => {
    let alive = true;
    if (!id) {
      setUrl(null);
      return;
    }
    setUrl(undefined);
    apiClient.loadCover(id).then((u) => {
      if (alive) setUrl(u);
    });
    return () => {
      alive = false;
    };
  }, [id]);
  return url;
}
