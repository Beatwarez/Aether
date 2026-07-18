
import React, { useRef, useState, useEffect, useCallback } from 'react';

interface KnobProps {
  label: string;
  value: number;
  min: number;
  max: number;
  disabled?: boolean;
  onChange: (val: number) => void;
  suffix?: string;
}

export const Knob: React.FC<KnobProps> = ({ label, value, min, max, disabled, onChange, suffix }) => {
  const knobRef = useRef<HTMLDivElement>(null);
  const [isDragging, setIsDragging] = useState(false);
  const startY = useRef(0);
  const startVal = useRef(0);

  const onMouseDown = (e: React.MouseEvent) => {
    if (disabled) return;
    setIsDragging(true);
    startY.current = e.clientY;
    startVal.current = value;
    document.body.style.cursor = 'ns-resize';
  };

  const onMouseMove = useCallback((e: MouseEvent) => {
    if (!isDragging) return;
    const deltaY = startY.current - e.clientY;
    const range = max - min;
    const sensitivity = 200; // Pixels for full range
    const deltaVal = (deltaY / sensitivity) * range;
    const newVal = Math.max(min, Math.min(max, Math.round(startVal.current + deltaVal)));
    onChange(newVal);
  }, [isDragging, max, min, onChange]);

  const onMouseUp = useCallback(() => {
    setIsDragging(false);
    document.body.style.cursor = 'default';
  }, []);

  useEffect(() => {
    if (isDragging) {
      window.addEventListener('mousemove', onMouseMove);
      window.addEventListener('mouseup', onMouseUp);
    } else {
      window.removeEventListener('mousemove', onMouseMove);
      window.removeEventListener('mouseup', onMouseUp);
    }
    return () => {
      window.removeEventListener('mousemove', onMouseMove);
      window.removeEventListener('mouseup', onMouseUp);
    };
  }, [isDragging, onMouseMove, onMouseUp]);

  const rotation = ((value - min) / (max - min)) * 270 - 135;

  return (
    <div className={`flex flex-col items-center gap-1 transition-opacity ${disabled ? 'opacity-20' : 'opacity-100'}`}>
      <span className="text-[9px] uppercase font-bold tracking-widest">{label}</span>
      <div 
        ref={knobRef}
        onMouseDown={onMouseDown}
        className="relative w-12 h-12 rounded-full bg-[#1a1a1a] border border-green-500/40 cursor-ns-resize shadow-[inset_0_0_5px_rgba(0,0,0,0.5)]"
      >
        <div 
          className="absolute top-1/2 left-1/2 w-1 h-4 bg-[#00ff41] origin-bottom shadow-[0_0_8px_#00ff41]"
          style={{ 
            transform: `translate(-50%, -100%) rotate(${rotation}deg)`,
            marginTop: '0px'
          }}
        ></div>
        {/* Center cap */}
        <div className="absolute top-1/2 left-1/2 w-6 h-6 -translate-x-1/2 -translate-y-1/2 rounded-full bg-[#121212] border border-green-500/10"></div>
      </div>
      <span className="text-[10px] font-bold text-[#00ff41] crt-glow">{value}{suffix}</span>
    </div>
  );
};
