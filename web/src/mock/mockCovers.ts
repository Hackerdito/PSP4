/**
 * Generates dark, high-contrast SVG cover images for mock games
 */
export function getMockGameSvg(id: string, title: string, system: 'psp' | 'ps1'): string {
  const isPSP = system === 'psp';
  const headerColor = isPSP ? '#003791' : '#101010';
  const badgeText = isPSP ? 'PSP™' : 'PlayStation®';
  const gradStart = isPSP ? '#0f2b48' : '#2b1b1b';
  const gradEnd = isPSP ? '#050d18' : '#0c0707';

  // Specific game motifs
  let motif = '';
  if (id === 'UCUS98653') {
    // God of War: Omega symbol & chains
    motif = `
      <circle cx="150" cy="180" r="60" fill="none" stroke="#D32F2F" stroke-width="12" opacity="0.8"/>
      <path d="M 120 220 L 90 240 M 180 220 L 210 240" stroke="#D32F2F" stroke-width="12" stroke-linecap="round"/>
      <text x="150" y="190" font-family="-apple-system, sans-serif" font-size="28" font-weight="900" fill="#FFFFFF" text-anchor="middle" letter-spacing="2">CHAINS</text>
    `;
  } else if (id === 'ULUS10336') {
    // Crisis Core: Buster Sword silhouette
    motif = `
      <rect x="142" y="90" width="16" height="150" rx="3" fill="#0A84FF" opacity="0.85"/>
      <polygon points="150,70 142,90 158,90" fill="#0A84FF"/>
      <rect x="130" y="240" width="40" height="10" rx="2" fill="#E5E5EA"/>
      <rect x="146" y="250" width="8" height="40" rx="2" fill="#8E8E93"/>
      <text x="150" y="320" font-family="-apple-system, sans-serif" font-size="14" font-weight="700" fill="#0A84FF" text-anchor="middle" letter-spacing="3">FINAL FANTASY VII</text>
    `;
  } else if (id === 'SLUS00067') {
    // Castlevania: Gothic Castle & Moon
    motif = `
      <circle cx="150" cy="170" r="55" fill="#C53030" opacity="0.3"/>
      <path d="M 120 240 L 120 180 L 135 160 L 150 180 L 165 150 L 180 180 L 180 240 Z" fill="#E5E5EA" opacity="0.9"/>
      <text x="150" y="270" font-family="-apple-system, sans-serif" font-size="16" font-weight="800" fill="#FCA5A5" text-anchor="middle" letter-spacing="2">SYMPHONY OF THE NIGHT</text>
    `;
  } else if (id === 'SLUS00594') {
    // Metal Gear Solid: Tactical crosshair & Foxhound vibe
    motif = `
      <circle cx="150" cy="170" r="50" fill="none" stroke="#22C55E" stroke-width="2" stroke-dasharray="8 6"/>
      <circle cx="150" cy="170" r="25" fill="none" stroke="#22C55E" stroke-width="2"/>
      <line x1="150" y1="105" x2="150" y2="235" stroke="#22C55E" stroke-width="2"/>
      <line x1="85" y1="170" x2="215" y2="170" stroke="#22C55E" stroke-width="2"/>
      <text x="150" y="260" font-family="-apple-system, sans-serif" font-size="18" font-weight="900" fill="#E5E5EA" text-anchor="middle" letter-spacing="3">TACTICAL ESPIONAGE</text>
    `;
  } else {
    // Generic
    motif = `
      <rect x="90" y="120" width="120" height="120" rx="16" fill="rgba(255,255,255,0.05)" stroke="rgba(255,255,255,0.1)"/>
      <text x="150" y="190" font-family="-apple-system, sans-serif" font-size="32" font-weight="800" fill="#8E8E93" text-anchor="middle">${system.toUpperCase()}</text>
    `;
  }

  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 300 400" width="300" height="400">
    <defs>
      <linearGradient id="bg-${id}" x1="0%" y1="0%" x2="0%" y2="100%">
        <stop offset="0%" stop-color="${gradStart}"/>
        <stop offset="100%" stop-color="${gradEnd}"/>
      </linearGradient>
    </defs>
    <rect width="300" height="400" fill="url(#bg-${id})"/>
    
    <!-- Top System Header Banner -->
    <rect width="300" height="44" fill="${headerColor}"/>
    <text x="16" y="28" font-family="-apple-system, sans-serif" font-size="14" font-weight="800" fill="#FFFFFF" letter-spacing="1">${badgeText}</text>
    <text x="284" y="28" font-family="monospace" font-size="11" font-weight="600" fill="rgba(255,255,255,0.7)" text-anchor="end">${id}</text>
    
    <!-- Game Motif -->
    ${motif}
    
    <!-- Bottom Title Panel -->
    <rect y="330" width="300" height="70" fill="rgba(0,0,0,0.85)"/>
    <text x="16" y="358" font-family="-apple-system, sans-serif" font-size="14" font-weight="700" fill="#FFFFFF">${escapeXml(truncate(title, 32))}</text>
    <text x="16" y="382" font-family="-apple-system, sans-serif" font-size="11" font-weight="500" fill="#8E8E93">SAMPSP4 HOMEBREW REPOSITORY</text>
  </svg>`;
}

function escapeXml(unsafe: string): string {
  return unsafe.replace(/[<>&'"]/g, (c) => {
    switch (c) {
      case '<': return '&lt;';
      case '>': return '&gt;';
      case '&': return '&amp;';
      case '\'': return '&apos;';
      case '"': return '&quot;';
      default: return c;
    }
  });
}

function truncate(text: string, max: number): string {
  return text.length > max ? text.slice(0, max - 1) + '…' : text;
}
