import React, { useRef, useEffect } from 'react';

interface PinInputProps {
  value: string;
  onChange: (val: string) => void;
  disabled?: boolean;
  isError?: boolean;
}

export const PinInput: React.FC<PinInputProps> = ({
  value,
  onChange,
  disabled = false,
  isError = false,
}) => {
  const inputRefs = useRef<(HTMLInputElement | null)[]>([]);

  useEffect(() => {
    // Focus first input on mount
    if (!disabled && inputRefs.current[0]) {
      inputRefs.current[0].focus();
    }
  }, [disabled]);

  const handleCharChange = (index: number, char: string) => {
    if (disabled) return;
    const sanitized = char.replace(/[^0-9]/g, '');
    if (!sanitized) {
      // Clear current digit
      const chars = value.split('');
      chars[index] = '';
      onChange(chars.join(''));
      return;
    }

    const digit = sanitized[sanitized.length - 1];
    const chars = (value.padEnd(6, ' ')).split('');
    chars[index] = digit;
    const newVal = chars.join('').trimEnd();
    onChange(newVal);

    // Auto advance to next box
    if (index < 5 && digit) {
      inputRefs.current[index + 1]?.focus();
    }
  };

  const handleKeyDown = (index: number, e: React.KeyboardEvent<HTMLInputElement>) => {
    if (e.key === 'Backspace') {
      if (!value[index] && index > 0) {
        inputRefs.current[index - 1]?.focus();
      }
    } else if (e.key === 'ArrowLeft' && index > 0) {
      inputRefs.current[index - 1]?.focus();
    } else if (e.key === 'ArrowRight' && index < 5) {
      inputRefs.current[index + 1]?.focus();
    }
  };

  const handlePaste = (e: React.ClipboardEvent<HTMLInputElement>) => {
    e.preventDefault();
    const pasted = e.clipboardData.getData('text').replace(/[^0-9]/g, '').slice(0, 6);
    if (pasted) {
      onChange(pasted);
      const targetIndex = Math.min(pasted.length, 5);
      inputRefs.current[targetIndex]?.focus();
    }
  };

  return (
    <div className="flex items-center justify-center gap-2 sm:gap-3">
      {[0, 1, 2, 3, 4, 5].map((index) => {
        const char = value[index] || '';
        return (
          <input
            key={index}
            ref={(el) => {
              inputRefs.current[index] = el;
            }}
            type="text"
            inputMode="numeric"
            pattern="[0-9]*"
            maxLength={1}
            value={char}
            disabled={disabled}
            onChange={(e) => handleCharChange(index, e.target.value)}
            onKeyDown={(e) => handleKeyDown(index, e)}
            onPaste={handlePaste}
            className={`w-12 h-16 sm:w-14 sm:h-20 text-center text-2xl sm:text-3xl font-mono font-bold rounded-2xl bg-[#141416] border transition-all outline-none select-none
              ${
                isError
                  ? 'border-red-500/80 text-red-400 bg-red-950/20 shadow-sm shadow-red-950/30'
                  : char
                  ? 'border-[#0A84FF] text-white bg-[#18181c] shadow-sm shadow-[#0A84FF]/20'
                  : 'border-[#27272a] text-neutral-200 hover:border-neutral-700 focus:border-[#0A84FF]'
              }
              focus:ring-2 focus:ring-[#0A84FF]/30 disabled:opacity-50`}
            aria-label={`Dígito ${index + 1} del PIN`}
          />
        );
      })}
    </div>
  );
};
