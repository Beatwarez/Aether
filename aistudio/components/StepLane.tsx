import React, { useRef, useCallback, useEffect } from 'react';
import { Step } from '../types';

interface StepLaneProps {
  label: string;
  property: keyof Step;
  steps: Step[];
  stepCount: number;
  min: number;
  max: number;
  isBipolar?: boolean;
  onUpdate: (index: number, value: number | boolean) => void;
  onRandomize: () => void;
  onReset: () => void;
}

const LABEL_HEIGHT = 28;

const DiceIcon = () => (
  <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
    <rect x="3" y="3" width="18" height="18" rx="2" ry="2" />
    <circle cx="8.5" cy="8.5" r="1.5" fill="currentColor" />
    <circle cx="15.5" cy="15.5" r="1.5" fill="currentColor" />
    <circle cx="12" cy="12" r="1.5" fill="currentColor" />
  </svg>
);

const ResetIcon = () => (
  <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round">
    <path d="M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8" />
    <path d="M3 3v5h5" />
  </svg>
);

export const StepLane: React.FC<StepLaneProps> = ({ 
  label, property, steps, stepCount, min, max, isBipolar, onUpdate, onRandomize, onReset 
}) => {
  const laneRef = useRef<HTMLDivElement>(null);
  const isDragging = useRef(false);

  const calculateValue = useCallback((clientY: number) => {
    if (!laneRef.current) return 0;
    const rect = laneRef.current.getBoundingClientRect();
    const barAreaHeight = rect.height - (property === 'muted' ? 0 : LABEL_HEIGHT);
    const relativeY = Math.max(0, Math.min(barAreaHeight, clientY - rect.top));
    const normalized = 1 - (relativeY / barAreaHeight);
    return Math.round(min + (normalized * (max - min)));
  }, [min, max, property]);

  const updateFromMouseEvent = useCallback((e: MouseEvent | React.MouseEvent) => {
    if (!laneRef.current) return;
    const rect = laneRef.current.getBoundingClientRect();
    const relativeX = e.clientX - rect.left;
    const stepWidth = rect.width / 15;
    const index = Math.max(0, Math.min(14, Math.floor(relativeX / stepWidth)));

    if (property === 'muted') return;

    const value = calculateValue(e.clientY);
    onUpdate(index, value);
  }, [calculateValue, onUpdate, property]);

  const onMouseDown = (e: React.MouseEvent, index: number) => {
    isDragging.current = true;
    if (property === 'muted') {
      onUpdate(index, !steps[index].muted);
    } else {
      updateFromMouseEvent(e);
    }
  };

  useEffect(() => {
    const handleGlobalMouseMove = (e: MouseEvent) => {
      if (isDragging.current) updateFromMouseEvent(e);
    };
    const handleGlobalMouseUp = () => {
      isDragging.current = false;
    };
    window.addEventListener('mousemove', handleGlobalMouseMove);
    window.addEventListener('mouseup', handleGlobalMouseUp);
    return () => {
      window.removeEventListener('mousemove', handleGlobalMouseMove);
      window.removeEventListener('mouseup', handleGlobalMouseUp);
    };
  }, [updateFromMouseEvent]);

  const formatValue = (val: number | boolean) => {
    if (typeof val === 'boolean') return '';
    if (property === 'probability') return `${val}%`;
    if (property === 'pitch' && val > 0) return `+${val}`;
    return val.toString();
  };

  return (
    <div className="flex flex-col gap-1">
      <div className="flex justify-between items-end px-1 mb-1">
        <div className="flex items-center gap-4">
          <div className="flex items-center gap-2">
            <button 
              onClick={(e) => { e.stopPropagation(); onRandomize(); }}
              className="text-[#00ff41]/60 hover:text-[#00ff41] active:bg-[#00ff41] active:text-black transition-colors p-1 border border-transparent hover:border-green-500/30 rounded"
              title="Randomize Lane"
            >
              <DiceIcon />
            </button>
            <button 
              onClick={(e) => { e.stopPropagation(); onReset(); }}
              className="text-[#00ff41]/60 hover:text-[#00ff41] active:bg-[#00ff41] active:text-black transition-colors p-1 border border-transparent hover:border-green-500/30 rounded"
              title="Reset Lane"
            >
              <ResetIcon />
            </button>
          </div>
          <span className="text-[16px] font-bold uppercase tracking-widest opacity-70">{label}</span>
        </div>
      </div>
      
      <div 
        ref={laneRef}
        className={`${property === 'muted' ? 'h-44' : 'h-32'} bg-transparent flex items-stretch gap-[4px] relative overflow-hidden px-1`}
      >
        {isBipolar && (
          <div 
            className="absolute left-0 right-0 h-[1px] bg-green-500/20 pointer-events-none z-10"
            style={{ top: `calc(50% - ${LABEL_HEIGHT / 2}px)` }}
          ></div>
        )}

        {steps.map((step, i) => {
          const val = step[property];
          const isInactive = i >= stepCount;
          
          return (
            <div
              key={i}
              onMouseDown={(e) => onMouseDown(e, i)}
              className={`flex-1 flex flex-col relative cursor-crosshair group transition-opacity ${isInactive ? 'opacity-20' : 'opacity-100'}`}
            >
              <div className="flex-1 relative border border-[#00ff41]/30 bg-[#121212]">
                {property === 'muted' ? (
                   <div className={`w-full h-full transition-colors flex items-center justify-center z-20 ${step.muted ? 'bg-green-900/10' : 'bg-[#00ff41]'}`}>
                   </div>
                ) : isBipolar ? (
                  <div 
                    className="absolute left-0 right-0 bg-[#00ff41] shadow-[0_0_8px_#00ff4155] pointer-events-none z-20"
                    style={{ 
                      top: (val as number) >= 0 ? '50%' : `${50 - (Math.abs(val as number)/max * 50)}%`,
                      bottom: (val as number) >= 0 ? `${50 - (Math.abs(val as number)/max * 50)}%` : '50%',
                      transform: (val as number) >= 0 ? 'translateY(-100%)' : 'translateY(100%)'
                    }}
                  ></div>
                ) : (
                  <div 
                    className="absolute bottom-0 left-0 right-0 bg-[#00ff41] shadow-[0_0_8px_#00ff4155] pointer-events-none z-20"
                    style={{ height: `${((val as number - min) / (max - min)) * 100}%` }}
                  ></div>
                )}
              </div>
              {property !== 'muted' && (
                <div 
                  className="flex items-center justify-center text-[16.5px] font-black text-[#00ff41] select-none pointer-events-none mt-[2px]"
                  style={{ height: `${LABEL_HEIGHT}px` }}
                >
                  {formatValue(val)}
                </div>
              )}
            </div>
          );
        })}
      </div>
    </div>
  );
};