import React, { useState, useCallback, useRef, useEffect } from 'react';
import { INITIAL_STATE, PluginState, Step } from './types';
import { StepLane } from './components/StepLane';

const SYNC_STRAIGHT = ['1/1', '1/2', '1/4', '1/8', '1/16', '1/32'];
const SYNC_DOTTED = ['1/1d', '1/2d', '1/4d', '1/8d', '1/16d', '1/32d'];
const SYNC_TRIPLET = ['1/1t', '1/2t', '1/4t', '1/8t', '1/16t', '1/32t'];
const SYNC_DIVISIONS = [...SYNC_STRAIGHT, ...SYNC_DOTTED, ...SYNC_TRIPLET];

// Helper to communicate with the C++ host
const sendParamToCpp = (param: string, val: any) => {
  if ((window as any).__JUCE__ && (window as any).__JUCE__.backend) {
    const resultId = Math.floor(Math.random() * 1000000);
    (window as any).__JUCE__.backend.emitEvent("__juce__invoke", {
      name: "sendParamToCpp",
      params: [param, val],
      resultId: resultId
    });
  } else {
    console.log(`sendParamToCpp fallback: ${param} = ${val}`);
  }
};

export const App: React.FC = () => {
  const [state, setState] = useState<PluginState>(INITIAL_STATE);
  const [dimensions, setDimensions] = useState({ width: window.innerWidth, height: window.innerHeight });

  useEffect(() => {
    const handleResize = () => {
      setDimensions({ width: window.innerWidth, height: window.innerHeight });
    };
    window.addEventListener('resize', handleResize);
    handleResize(); // run initially
    return () => window.removeEventListener('resize', handleResize);
  }, []);

  const scaleX = dimensions.width / 1040;
  const scaleY = dimensions.height / 1200;
  const scale = Math.min(scaleX, scaleY);

  const stepCountRef = useRef<HTMLDivElement>(null);
  const isDraggingStepCount = useRef(false);
  
  // Value Box dragging state
  const isDraggingMs = useRef(false);
  const startY = useRef(0);
  const startMsVal = useRef(0);

  // Sync state back to C++ on user actions
  const updateStep = useCallback((index: number, value: number | boolean, property: keyof Step) => {
    setState(prev => {
      const newSteps = [...prev.steps];
      newSteps[index] = { ...newSteps[index], [property]: value };
      return { ...prev, steps: newSteps };
    });
    sendParamToCpp("stepUpdate", JSON.stringify({ index, property, value }));
  }, []);

  const randomizeLane = useCallback((property: keyof Step, min: number, max: number) => {
    setState(prev => {
      const newSteps = prev.steps.map(step => {
        let newVal: any;
        if (property === 'muted') {
          newVal = Math.random() > 0.5;
        } else {
          newVal = Math.round(min + Math.random() * (max - min));
        }
        return { ...step, [property]: newVal };
      });
      return { ...prev, steps: newSteps };
    });
    sendParamToCpp("randomizeLane", property);
  }, []);

  const resetLane = useCallback((property: keyof Step) => {
    setState(prev => {
      const newSteps = prev.steps.map((step, i) => {
        let defaultVal: any;
        switch(property) {
          case 'pitch': defaultVal = 0; break;
          case 'velocity': defaultVal = Math.round(127 - (i * (126 / 14))); break;
          case 'modwheel': defaultVal = 0; break;
          case 'probability': defaultVal = 100; break;
          case 'muted': defaultVal = false; break;
          default: defaultVal = 0;
        }
        return { ...step, [property]: defaultVal };
      });
      return { ...prev, steps: newSteps };
    });
    sendParamToCpp("resetLane", property);
  }, []);

  const handleStepCountInteraction = (clientX: number) => {
    if (!stepCountRef.current) return;
    const rect = stepCountRef.current.getBoundingClientRect();
    const width = rect.width;
    const relativeX = Math.max(0, Math.min(width, clientX - rect.left));
    const normalized = relativeX / width;
    const newCount = Math.max(1, Math.min(15, Math.ceil(normalized * 15)));
    setState(prev => ({ ...prev, stepCount: newCount }));
    sendParamToCpp("stepCount", newCount);
  };

  const onStepCountMouseDown = (e: React.MouseEvent) => {
    isDraggingStepCount.current = true;
    handleStepCountInteraction(e.clientX);
  };

  const handleMsDrag = useCallback((clientY: number) => {
    if (!isDraggingMs.current) return;
    const deltaY = startY.current - clientY;
    const sensitivity = 0.5; 
    const newVal = Math.max(1, Math.min(2000, Math.round(startMsVal.current + (deltaY * sensitivity))));
    setState(prev => ({ ...prev, delayTimeMs: newVal }));
    sendParamToCpp("delayTimeMs", newVal);
  }, []);

  const onMsMouseDown = (e: React.MouseEvent) => {
    if (state.syncDivision !== 'ms') return;
    isDraggingMs.current = true;
    startY.current = e.clientY;
    startMsVal.current = state.delayTimeMs;
    document.body.style.cursor = 'ns-resize';
  };

  // Register bridge functions and query initial state on mount
  useEffect(() => {
    const aetherUI = {
      initializeState: (jsonStateStr: string) => {
        try {
          const parsed = JSON.parse(jsonStateStr);
          setState(parsed);
        } catch (e) {
          console.error("Error parsing initial state:", e);
        }
      },
      updateParamFromCpp: (param: string, value: any) => {
        setState(prev => {
          if (param === 'syncDivision') {
            const divStr = value === 0 ? 'ms' : SYNC_DIVISIONS[value - 1];
            if (prev.syncDivision === divStr) return prev;
            return { ...prev, syncDivision: divStr };
          }
          if (param === 'loopMode') {
            const modeStr = value === 0 ? 'forward' : (value === 1 ? 'pendulum' : 'random');
            if (prev.loopMode === modeStr) return prev;
            return { ...prev, loopMode: modeStr };
          }
          const keyMap: Record<string, keyof PluginState> = {
            'enabled': 'isEnabled',
            'delayTimeMs': 'delayTimeMs',
            'stepCount': 'stepCount',
            'killOnStop': 'killOnStop',
            'loopEnabled': 'isLooping',
            'loopRestart': 'loopNoteRestart'
          };
          const mappedKey = keyMap[param];
          if (mappedKey) {
            if (prev[mappedKey] === value) return prev;
            return { ...prev, [mappedKey]: value };
          }
          return prev;
        });
      },
      updateStepsFromCpp: (jsonStepsStr: string) => {
        try {
          const parsedSteps = JSON.parse(jsonStepsStr);
          setState(prev => ({ ...prev, steps: parsedSteps }));
        } catch (e) {
          console.error("Error parsing steps:", e);
        }
      }
    };
    (window as any).aetherUI = aetherUI;
    
    // Request current state from C++
    sendParamToCpp("queryall", 0);

    return () => {
      delete (window as any).aetherUI;
    };
  }, []);

  useEffect(() => {
    const handleGlobalMouseMove = (e: MouseEvent) => {
      if (isDraggingStepCount.current) handleStepCountInteraction(e.clientX);
      if (isDraggingMs.current) handleMsDrag(e.clientY);
    };
    const handleGlobalMouseUp = () => {
      isDraggingStepCount.current = false;
      isDraggingMs.current = false;
      document.body.style.cursor = 'default';
    };
    
    window.addEventListener('mousemove', handleGlobalMouseMove);
    window.addEventListener('mouseup', handleGlobalMouseUp);
    return () => {
      window.removeEventListener('mousemove', handleGlobalMouseMove);
      window.removeEventListener('mouseup', handleGlobalMouseUp);
    };
  }, [handleMsDrag]);

  const isMsMode = state.syncDivision === 'ms';

  const DelayRadioButton: React.FC<{ division: string; label?: string; isFirstInRow?: boolean; isFirstRow?: boolean }> = ({ division, label, isFirstInRow, isFirstRow }) => {
    const isActive = state.syncDivision === division;
    return (
      <button
        onClick={() => {
          if (state.isEnabled) {
            setState(p => ({ ...p, syncDivision: division }));
            const index = SYNC_DIVISIONS.indexOf(division) + 1; // 1-18 for sync divisions
            sendParamToCpp("syncDivision", index);
          }
        }}
        className={`flex-1 h-9 border border-[#00ff41]/40 text-[11px] font-bold transition-all flex items-center justify-center cursor-pointer bg-[#121212] ${isFirstInRow ? '' : '-ml-[1px]'} ${isFirstRow ? '' : '-mt-[1px]'} ${
          isActive 
            ? 'bg-[#00ff41] text-black shadow-[0_0_15px_#00ff41aa] border-[#00ff41] z-10' 
            : 'text-[#00ff41] hover:bg-[#00ff41]/10 hover:z-10'
        }`}
      >
        {label || division}
      </button>
    );
  };

  return (
    <div className="w-screen h-screen bg-[#050505] overflow-hidden relative">
      <div 
        style={{
          width: '1040px',
          height: '1200px',
          position: 'absolute',
          top: '50%',
          left: '50%',
          transform: `translate(-50%, -50%) scale(${scale})`,
          transformOrigin: 'center center'
        }}
        className={`bg-[#121212] crt-border p-8 flex flex-col justify-between overflow-hidden relative transition-opacity duration-500 ${!state.isEnabled ? 'opacity-40 grayscale-[0.5]' : 'opacity-100'}`}>
        
        <div className="flex justify-between items-center border-b border-green-500/20 pb-6">
          <div className="flex items-center gap-8">
            <div className="flex flex-col items-center gap-2">
               <span className="text-[9px] uppercase font-bold tracking-widest opacity-60">Power</span>
               <button 
                onClick={() => {
                  const newVal = !state.isEnabled;
                  setState(p => ({ ...p, isEnabled: newVal }));
                  sendParamToCpp("enabled", newVal ? 1.0 : 0.0);
                }}
                className={`w-12 h-12 rounded-full border-2 flex items-center justify-center transition-all cursor-pointer ${state.isEnabled ? 'bg-[#00ff41] border-[#00ff41] text-black shadow-[0_0_15px_#00ff41aa]' : 'bg-transparent border-[#00ff41]/20 text-[#00ff41]/20'}`}
               >
                 <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round">
                    <path d="M18.36 6.64a9 9 0 1 1-12.73 0" />
                    <line x1="12" y1="2" x2="12" y2="12" />
                 </svg>
               </button>
            </div>

            <div className="flex flex-col justify-center">
              <h1 className="text-[4.125rem] font-black tracking-tighter crt-glow leading-[0.85]">AETHER</h1>
              <div className="mt-2 flex flex-col">
                <p className="text-[13px] uppercase tracking-widest text-[#00ff41] opacity-60">Polyphonic MIDI Delay</p>
                <p className="text-[12px] uppercase tracking-widest text-[#00ff41] opacity-60 mt-0.5">by darkInteger</p>
              </div>
            </div>

            <div className="flex flex-col gap-2 border-l border-green-500/20 pl-8 ml-2">
              <div className="flex items-center gap-4">
                <div className="flex flex-col items-center gap-1">
                  <span className="text-[8px] uppercase font-bold tracking-widest opacity-60">Loop</span>
                  <button 
                    onClick={() => {
                      const newVal = !state.isLooping;
                      setState(p => ({ ...p, isLooping: newVal }));
                      sendParamToCpp("loopEnabled", newVal ? 1.0 : 0.0);
                    }}
                    className={`w-8 h-8 rounded-sm border flex items-center justify-center transition-all cursor-pointer ${state.isLooping ? 'bg-[#00ff41] border-[#00ff41] text-black shadow-[0_0_10px_#00ff41aa]' : 'bg-transparent border-[#00ff41]/40 text-[#00ff41]/40'}`}
                  >
                    <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round">
                      <polyline points="17 1 21 5 17 9"></polyline>
                      <path d="M3 11V9a4 4 0 0 1 4-4h14"></path>
                      <polyline points="7 23 3 19 7 15"></polyline>
                      <path d="M21 13v2a4 4 0 0 1-4 4H3"></path>
                    </svg>
                  </button>
                </div>

                <div className="flex flex-col gap-1">
                  <span className="text-[8px] uppercase font-bold tracking-widest opacity-60">Mode</span>
                  <div className="flex border border-[#00ff41]/40">
                    {(['forward', 'pendulum', 'random'] as const).map((m) => (
                      <button
                        key={m}
                        onClick={() => {
                          setState(p => ({ ...p, loopMode: m }));
                          const modeIdx = m === 'forward' ? 0 : (m === 'pendulum' ? 1 : 2);
                          sendParamToCpp("loopMode", modeIdx);
                        }}
                        className={`px-2 py-1 text-[9px] font-bold uppercase transition-all cursor-pointer ${state.loopMode === m ? 'bg-[#00ff41] text-black' : 'text-[#00ff41] hover:bg-[#00ff41]/10'}`}
                      >
                        {m}
                      </button>
                    ))}
                  </div>
                </div>

                <div className="flex flex-col items-center gap-1">
                  <span className="text-[8px] uppercase font-bold tracking-widest opacity-60">Note Restart</span>
                  <button 
                    onClick={() => {
                      const newVal = !state.loopNoteRestart;
                      setState(p => ({ ...p, loopNoteRestart: newVal }));
                      sendParamToCpp("loopRestart", newVal ? 1.0 : 0.0);
                    }}
                    className={`w-8 h-8 rounded-sm border flex items-center justify-center transition-all cursor-pointer ${state.loopNoteRestart ? 'bg-[#00ff41] border-[#00ff41] text-black shadow-[0_0_10px_#00ff41aa]' : 'bg-transparent border-[#00ff41]/40 text-[#00ff41]/40'}`}
                  >
                    <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round">
                      <path d="M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8"></path>
                      <path d="M3 3v5h5"></path>
                    </svg>
                  </button>
                </div>
              </div>
            </div>
          </div>

          <div className="flex gap-10 items-start h-full pt-4">
            <div className="flex flex-col gap-0 min-w-[380px]">
              <div className="flex justify-between items-center mb-2">
                <span className="text-[10px] uppercase font-bold opacity-60 tracking-wider">DELAY TIME MATRIX</span>
              </div>
              
              <div className="flex flex-col gap-0">
                <div className="flex gap-0">
                  {SYNC_STRAIGHT.map((div, i) => <DelayRadioButton key={div} division={div} isFirstInRow={i === 0} isFirstRow />)}
                </div>
                <div className="flex gap-0">
                  {SYNC_DOTTED.map((div, i) => <DelayRadioButton key={div} division={div} isFirstInRow={i === 0} />)}
                </div>
                <div className="flex gap-0">
                  {SYNC_TRIPLET.map((div, i) => <DelayRadioButton key={div} division={div} isFirstInRow={i === 0} />)}
                </div>
                
                <div className="flex items-stretch -mt-[1px] h-9 gap-0">
                  <button 
                    onClick={() => {
                      const newVal = !state.killOnStop;
                      setState(p => ({ ...p, killOnStop: newVal }));
                      sendParamToCpp("killOnStop", newVal ? 1.0 : 0.0);
                    }}
                    className={`flex-[2] border border-[#00ff41]/40 text-[9px] font-bold transition-all flex items-center justify-center tracking-tighter cursor-pointer ${state.killOnStop ? 'bg-[#00ff41] text-black border-[#00ff41] z-10' : 'bg-[#121212] text-[#00ff41] hover:bg-[#00ff41]/10 hover:z-10'}`}
                  >
                    KILL ON STOP
                  </button>

                  <button
                    onClick={() => {
                      if (state.isEnabled) {
                        setState(p => ({ ...p, syncDivision: 'ms' }));
                        sendParamToCpp("syncDivision", 0);
                      }
                    }}
                    className={`flex-[3] border border-[#00ff41]/40 -ml-[1px] text-[10px] font-black uppercase tracking-widest transition-all cursor-pointer ${
                      isMsMode 
                        ? 'bg-[#00ff41] text-black shadow-[0_0_15px_#00ff41aa] border-[#00ff41] z-10' 
                        : 'bg-[#121212] text-[#00ff41] hover:bg-[#00ff41]/10 hover:z-10'
                    }`}
                  >
                    MANUAL (MS)
                  </button>

                  <div className={`flex-[1] flex flex-col items-stretch transition-opacity duration-300 ${!isMsMode ? 'opacity-20' : 'opacity-100'}`}>
                    <div 
                      onMouseDown={onMsMouseDown}
                      className={`h-full border border-[#00ff41]/40 -ml-[1px] flex items-center justify-center transition-all group select-none relative overflow-hidden ${
                        isMsMode 
                          ? 'bg-[#00ff41] text-black cursor-ns-resize shadow-[0_0_15px_#00ff41aa] border-[#00ff41]' 
                          : 'bg-[#1a1a1a] text-[#00ff41]/40 cursor-default'
                      }`}
                    >
                      <span className="text-[14px] font-black">{Math.round(state.delayTimeMs)}</span>
                    </div>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </div>

        <div className={`flex flex-col gap-4 transition-all duration-300 ${!state.isEnabled ? 'pointer-events-none' : ''}`}>
          <StepLane 
            label="Pitch Shift" 
            property="pitch" 
            steps={state.steps} 
            stepCount={state.stepCount}
            min={-24} max={24} 
            isBipolar 
            onUpdate={(i, v) => updateStep(i, v, 'pitch')}
            onRandomize={() => randomizeLane('pitch', -24, 24)}
            onReset={() => resetLane('pitch')}
          />
          <StepLane 
            label="Velocity" 
            property="velocity" 
            steps={state.steps} 
            stepCount={state.stepCount}
            min={1} max={127} 
            onUpdate={(i, v) => updateStep(i, v, 'velocity')}
            onRandomize={() => randomizeLane('velocity', 1, 127)}
            onReset={() => resetLane('velocity')}
          />
          <StepLane 
            label="Modwheel" 
            property="modwheel" 
            steps={state.steps} 
            stepCount={state.stepCount}
            min={0} max={127} 
            onUpdate={(i, v) => updateStep(i, v, 'modwheel')}
            onRandomize={() => randomizeLane('modwheel', 0, 127)}
            onReset={() => resetLane('modwheel')}
          />
          <StepLane 
            label="Probability" 
            property="probability" 
            steps={state.steps} 
            stepCount={state.stepCount}
            min={0} max={100} 
            onUpdate={(i, v) => updateStep(i, v, 'probability')}
            onRandomize={() => randomizeLane('probability', 0, 100)}
            onReset={() => resetLane('probability')}
          />
          <StepLane 
            label="Mute" 
            property="muted" 
            steps={state.steps} 
            stepCount={state.stepCount}
            min={0} max={1} 
            onUpdate={(i, v) => updateStep(i, v, 'muted')}
            onRandomize={() => randomizeLane('muted', 0, 1)}
            onReset={() => resetLane('muted')}
          />

          <div className="flex flex-col gap-1 mt-4 pb-2">
            <div className="flex justify-between items-end px-1">
              <span className="text-[12px] font-bold text-[#00ff41] crt-glow uppercase tracking-widest opacity-90">STEP COUNT: {state.stepCount}</span>
            </div>
            <div 
              ref={stepCountRef}
              onMouseDown={onStepCountMouseDown}
              className="h-6 bg-[#1a1a1a] border border-[#333] flex items-stretch gap-[2px] p-[2px] cursor-pointer relative"
            >
              {Array.from({ length: 15 }).map((_, i) => (
                <div 
                  key={i} 
                  className={`flex-1 transition-colors ${i < state.stepCount ? 'bg-[#00ff41]' : 'bg-green-900/10'}`}
                >
                  <div className={`h-full w-full ${i < state.stepCount ? 'shadow-[0_0_5px_#00ff4155]' : ''}`}></div>
                </div>
              ))}
            </div>
          </div>
        </div>
      </div>
    </div>
  );
};

export default App;
