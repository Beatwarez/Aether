export type DelayMode = 'ms' | 'sync';

export type Step = {
  pitch: number;        // -24 to 24
  velocity: number;     // 1 to 127
  modwheel: number;     // 0 to 127
  probability: number;  // 0 to 100
  muted: boolean;
};

export type LoopMode = 'forward' | 'pendulum' | 'random';

export type PluginState = {
  isEnabled: boolean;
  delayMode: DelayMode;
  delayTimeMs: number;
  syncDivision: string;
  stepCount: number;    // 1 to 15
  steps: Step[];
  killOnStop: boolean;
  isLooping: boolean;
  loopMode: LoopMode;
  loopNoteRestart: boolean;
};

export const INITIAL_STATE: PluginState = {
  isEnabled: true,
  delayMode: 'sync',
  delayTimeMs: 500,
  syncDivision: '1/4',
  stepCount: 15,
  killOnStop: true,
  isLooping: false,
  loopMode: 'forward',
  loopNoteRestart: false,
  steps: Array.from({ length: 15 }, (_, i) => ({
    pitch: 0,
    velocity: Math.round(127 - (i * (126 / 14))),
    modwheel: 0,
    probability: 100,
    muted: false,
  })),
};

export const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
export const getNoteName = (midi: number) => {
  const name = NOTE_NAMES[midi % 12];
  const oct = Math.floor(midi / 12) - 1;
  return `${name}${oct}`;
};