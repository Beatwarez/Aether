import React, { useState, useEffect, useRef } from 'react'
import Draggable from 'react-draggable'
import { Resizable } from 'react-resizable'
import 'react-resizable/css/styles.css'

const SYNC_NAMES = [
  "1/1", "1/2", "1/4", "1/8", "1/16", "1/32",
  "1/1d", "1/2d", "1/4d", "1/8d", "1/16d", "1/32d",
  "1/1t", "1/2t", "1/4t", "1/8t", "1/16t", "1/32t"
];

const LANES = [
  { label: "Pitch Shift", min: -24, max: 24, property: "pitch", isBipolar: true },
  { label: "Velocity", min: 1, max: 127, property: "velocity", isBipolar: false },
  { label: "Modwheel", min: 0, max: 127, property: "modwheel", isBipolar: false },
  { label: "Probability", min: 0, max: 100, property: "probability", isBipolar: false },
  { label: "Mute", min: 0, max: 1, property: "muted", isBipolar: false }
];

function App() {
  const [enabled, setEnabled] = useState(true);
  const [syncDivision, setSyncDivision] = useState(0);
  const [delayTimeMs, setDelayTimeMs] = useState(500);
  const [killOnStop, setKillOnStop] = useState(true);
  const [stepCount, setStepCount] = useState(15);

  // New Looping State
  const [loopEnabled, setLoopEnabled] = useState(false);
  const [loopMode, setLoopMode] = useState("Forward");
  const [loopNoteRestart, setLoopNoteRestart] = useState(false);
  const [loopStopPressed, setLoopStopPressed] = useState(false);

  // Layout Editor State
  const [layoutMode, setLayoutMode] = useState(false);
  const [positions, setPositions] = useState(() => {
    const defaults = {
      logo: { x: 0, y: 0, w: 400, h: 100 },
      looping: { x: 0, y: 0, w: 250, h: 200 },
      matrix: { x: 0, y: 0, w: 350, h: 200 },
      lanes: { x: 0, y: 0, w: 1100, h: 650 }
    };
    const saved = localStorage.getItem('aether-layout');
    if (!saved) return defaults;

    const parsed = JSON.parse(saved);
    // Merge defaults with parsed to ensure w/h are present
    const merged = {};
    Object.keys(defaults).forEach(key => {
      merged[key] = { ...defaults[key], ...parsed[key] };
    });
    // Ensure 'lanes' encompasses both lanes and footer now
    return merged;
  });

  // Refs for Draggable (Strict Mode compatibility)
  const logoRef = useRef(null);
  const loopingRef = useRef(null);
  const matrixRef = useRef(null);
  const lanesRef = useRef(null);

  useEffect(() => {
    localStorage.setItem('aether-layout', JSON.stringify(positions));
    if (layoutMode) {
      console.log("CURRENT LAYOUT:", JSON.stringify(positions, null, 2));
    }
  }, [positions, layoutMode]);

  const handleStop = (name, e, data) => {
    setPositions(prev => ({
      ...prev,
      [name]: { ...prev[name], x: data.x, y: data.y }
    }));
  };

  const handleResize = (name, e, { size }) => {
    setPositions(prev => ({
      ...prev,
      [name]: { ...prev[name], w: size.width, h: size.height }
    }));
  };

  // Initialize steps data
  const [steps, setSteps] = useState(() =>
    Array.from({ length: 15 }, (_, i) => ({
      pitch: 0,
      velocity: Math.floor(127 - (i * (126 / 14))),
      modwheel: 0,
      probability: 100,
      muted: false
    }))
  );

  const getStepValue = (step, lane) => {
    const val = step[lane.property];
    if (lane.property === "muted") return val ? "M" : "";
    if (lane.property === "probability") return val + "%";
    if (lane.property === "pitch") return (val > 0 ? "+" : "") + val;
    return val;
  };

  return (
    <div className={`plugin-container ${!enabled ? 'disabled' : ''}`}>
      {/* CRT Overlays */}
      <div className="crt-overlay scanlines"></div>
      <div className="crt-overlay vignette"></div>
      <div className="crt-overlay bloom"></div>
      <div className="noise"></div>

      {/* Layout Editor Controls */}
      <div className="layout-editor-bar">
        <button
          className={`layout-toggle-btn ${layoutMode ? 'active' : ''}`}
          onClick={() => setLayoutMode(!layoutMode)}
        >
          {layoutMode ? '✕ EXIT' : '⊹ LAYOUT'}
        </button>
      </div>

      <div className="header">
        <Draggable
          nodeRef={logoRef}
          disabled={!layoutMode}
          position={positions.logo}
          onStop={(e, data) => handleStop('logo', e, data)}
          cancel=".react-resizable-handle"
        >
          <div ref={logoRef} className={`draggable-wrapper ${layoutMode ? 'editing' : ''}`} style={{ width: positions.logo.w, height: positions.logo.h }}>
            <Resizable
              width={positions.logo.w}
              height={positions.logo.h}
              onResize={(e, data) => handleResize('logo', e, data)}
              disabled={!layoutMode}
              handle={<span className="react-resizable-handle" />}
            >
              <div className="inner-padding" style={{ width: positions.logo.w, height: positions.logo.h }}>
                <div className="title-section" style={{ display: 'flex', alignItems: 'flex-start', height: '100%' }}>
                  <div
                    className={`power-button ${enabled ? 'on' : ''}`}
                    onClick={() => setEnabled(!enabled)}
                  >
                    {enabled ? 'I' : 'O'}
                  </div>
                  <div className="title-container">
                    <h1>AETHER</h1>
                    <div className="subtitle">Polyphonic MIDI Delay</div>
                  </div>
                </div>
              </div>
            </Resizable>
          </div>
        </Draggable>

        <Draggable
          nodeRef={loopingRef}
          disabled={!layoutMode}
          position={positions.looping}
          onStop={(e, data) => handleStop('looping', e, data)}
          cancel=".react-resizable-handle"
        >
          <div ref={loopingRef} className={`draggable-wrapper ${layoutMode ? 'editing' : ''}`} style={{ width: positions.looping.w, height: positions.looping.h }}>
            <Resizable
              width={positions.looping.w}
              height={positions.looping.h}
              onResize={(e, data) => handleResize('looping', e, data)}
              disabled={!layoutMode}
              handle={<span className="react-resizable-handle" />}
            >
              <div className="inner-padding" style={{ width: positions.looping.w, height: positions.looping.h }}>
                <div className="loop-controls" style={{ height: '100%', display: 'flex', flexDirection: 'column' }}>
                  <div className="loop-header">LOOPING</div>
                  <div className="loop-column" style={{ flex: 1, display: 'flex', flexDirection: 'column', justifyContent: 'space-between' }}>
                    <button
                      className={`loop-toggle ${loopEnabled ? 'active' : ''}`}
                      onClick={() => setLoopEnabled(!loopEnabled)}
                    >
                      ACTIVE: {loopEnabled ? "ON" : "OFF"}
                    </button>

                    <div className="loop-mode-stack" style={{ flex: 1, display: 'flex', flexDirection: 'column', gap: '2px' }}>
                      <div className="mini-label">MODE</div>
                      {["Forward", "Pendulum", "Random"].map(mode => (
                        <button
                          key={mode}
                          className={`mode-btn ${loopMode === mode ? 'active' : ''}`}
                          onClick={() => setLoopMode(mode)}
                          style={{ flex: 1 }}
                        >
                          {mode.toUpperCase()}
                        </button>
                      ))}
                    </div>

                    <button
                      className={`restart-toggle ${loopNoteRestart ? 'active' : ''}`}
                      onClick={() => setLoopNoteRestart(!loopNoteRestart)}
                    >
                      NOTE RESTART
                    </button>

                    <button
                      className={`loop-stop-btn ${loopStopPressed ? 'pressed' : ''}`}
                      onMouseDown={() => {
                        setLoopStopPressed(true);
                      }}
                      onMouseUp={() => setLoopStopPressed(false)}
                      onMouseLeave={() => setLoopStopPressed(false)}
                    >
                      ■ STOP
                    </button>
                  </div>
                </div>
              </div>
            </Resizable>
          </div>
        </Draggable>

        <Draggable
          nodeRef={matrixRef}
          disabled={!layoutMode}
          position={positions.matrix}
          onStop={(e, data) => handleStop('matrix', e, data)}
          cancel=".react-resizable-handle"
        >
          <div ref={matrixRef} className={`draggable-wrapper ${layoutMode ? 'editing' : ''}`} style={{ width: positions.matrix.w, height: positions.matrix.h }}>
            <Resizable
              width={positions.matrix.w}
              height={positions.matrix.h}
              onResize={(e, data) => handleResize('matrix', e, data)}
              disabled={!layoutMode}
              handle={<span className="react-resizable-handle" />}
            >
              <div className="inner-padding" style={{ width: positions.matrix.w, height: positions.matrix.h }}>
                <div className="delay-time-matrix" style={{ height: '100%', display: 'flex', flexDirection: 'column' }}>
                  <div className="matrix-label">DELAY TIME MATRIX</div>
                  <div className="matrix-grid" style={{ flex: 1 }}>
                    {SYNC_NAMES.map((name, i) => (
                      <button
                        key={name}
                        className={`matrix-btn ${syncDivision === i + 1 ? 'active' : ''}`}
                        onClick={() => setSyncDivision(i + 1)}
                      >
                        {name}
                      </button>
                    ))}
                  </div>
                  <div className="matrix-row-4">
                    <button
                      className={`kill-btn ${killOnStop ? 'active' : ''}`}
                      onClick={() => setKillOnStop(!killOnStop)}
                    >
                      KILL ON STOP
                    </button>
                    <button
                      className={`manual-btn ${syncDivision === 0 ? 'active' : ''}`}
                      onClick={() => setSyncDivision(syncDivision === 0 ? 1 : 0)}
                    >
                      MANUAL
                    </button>
                    <div className={`ms-box ${syncDivision === 0 ? 'active' : ''}`}>
                      {Math.floor(delayTimeMs)} ms
                    </div>
                  </div>
                </div>
              </div>
            </Resizable>
          </div>
        </Draggable>
      </div>

      <Draggable
        nodeRef={lanesRef}
        disabled={!layoutMode}
        position={positions.lanes}
        onStop={(e, data) => handleStop('lanes', e, data)}
        cancel=".react-resizable-handle"
      >
        <div ref={lanesRef} className={`draggable-wrapper ${layoutMode ? 'editing' : ''}`} style={{ width: positions.lanes.w, height: positions.lanes.h }}>
          <Resizable
            width={positions.lanes.w}
            height={positions.lanes.h}
            onResize={(e, data) => handleResize('lanes', e, data)}
            disabled={!layoutMode}
            handle={<span className="react-resizable-handle" />}
          >
            <div className="inner-padding" style={{ width: positions.lanes.w, height: positions.lanes.h }}>
              <div className="lanes-container" style={{ display: 'flex', flexDirection: 'column' }}>
                {LANES.map((lane, laneIdx) => (
                  <div key={laneIdx} className="lane">
                    <div className="lane-header">
                      <div className="dice-btn">D</div>
                      <div className="reset-btn">R</div>
                      <div className="lane-title">{lane.label}</div>
                    </div>
                    <div className="steps-grid" style={{ flex: 1 }}>
                      {steps.map((step, i) => (
                        <div key={i} className="step-col" style={{ opacity: i >= stepCount ? 0.2 : 1 }}>
                          {lane.property !== "muted" && (
                            <div
                              className="step-fill"
                              style={{
                                height: lane.isBipolar
                                  ? `${(Math.abs(step[lane.property]) / lane.max) * 45}%`
                                  : `${((step[lane.property] - lane.min) / (lane.max - lane.min)) * 85}%`,
                                bottom: lane.isBipolar && step[lane.property] < 0 ? 'auto' : 0,
                                top: lane.isBipolar && step[lane.property] < 0 ? '50%' : 'auto',
                                transform: lane.isBipolar && step[lane.property] >= 0 ? 'translateY(0)' : 'none'
                              }}
                            ></div>
                          )}
                          {lane.property === "muted" && !step.muted && (
                            <div className="step-fill" style={{ height: '80%', bottom: '10%', left: '10%', width: '80%' }}></div>
                          )}
                          {lane.property !== "muted" && (
                            <div className="step-value" style={{ bottom: '2px', top: 'auto' }}>{getStepValue(step, lane)}</div>
                          )}
                        </div>
                      ))}
                    </div>
                  </div>
                ))}
                <div className="footer" style={{ marginTop: '20px' }}>
                  <div className="step-count-label">STEP COUNT: {stepCount}</div>
                  <div className="step-segments">
                    {Array.from({ length: 15 }).map((_, i) => (
                      <div
                        key={i}
                        className={`segment ${i < stepCount ? 'active' : ''}`}
                        onClick={() => setStepCount(i + 1)}
                      ></div>
                    ))}
                  </div>
                </div>
              </div>
            </div>
          </Resizable>
        </div>
      </Draggable>
    </div>
  )
}

export default App
