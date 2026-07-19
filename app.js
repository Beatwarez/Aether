// AETHER UI Logic & C++ Host Communication Bridge
const BUILD_VERSION = 'build 1.0.11';

// 18 Sync divisions strings
const SYNC_DIVISIONS = [
    '1/1', '1/2', '1/4', '1/8', '1/16', '1/32',
    '1/1d', '1/2d', '1/4d', '1/8d', '1/16d', '1/32d',
    '1/1t', '1/2t', '1/4t', '1/8t', '1/16t', '1/32t'
];

// UI Local State
let state = {
    isEnabled: true,
    delayMode: 'sync',
    delayTimeMs: 500,
    syncDivision: '1/4',
    stepCount: 15,
    killOnStop: true,
    activeSnapshot: 0,
    steps: Array.from({ length: 15 }, (_, i) => ({
        pitch: 0,
        velocity: Math.round(127 - (i * (126 / 14))),
        modwheel: 0,
        probability: 100,
        muted: false
    }))
};

// Drag tracking variables
let activeDraggingLane = null; // { property, element, min, max, isBipolar }
let isDraggingStepCount = false;
let isDraggingMs = false;
let msStartY = 0;
let msStartValue = 500;

// Bridge: Send call to C++
let juceBridgeReady = false;
const juceMessageQueue = [];

function sendParamToCpp(param, val) {
    if (juceBridgeReady) {
        const resultId = Math.floor(Math.random() * 1000000);
        window.__JUCE__.backend.emitEvent("__juce__invoke", {
            name: "sendParamToCpp",
            params: [param, val],
            resultId: resultId
        });
    } else if (window.__JUCE__ && window.__JUCE__.backend) {
        juceBridgeReady = true;
        const resultId = Math.floor(Math.random() * 1000000);
        window.__JUCE__.backend.emitEvent("__juce__invoke", {
            name: "sendParamToCpp",
            params: [param, val],
            resultId: resultId
        });
    } else {
        juceMessageQueue.push({ param, val });
        console.log("C++ Bridge Call Queued: " + param + " = " + val);
    }
}

function checkJuceBridge() {
    if (window.__JUCE__ && window.__JUCE__.backend) {
        juceBridgeReady = true;
        console.log("JUCE Bridge is ready. Flushing queue of length: " + juceMessageQueue.length);
        while (juceMessageQueue.length > 0) {
            const msg = juceMessageQueue.shift();
            sendParamToCpp(msg.param, msg.val);
        }
    } else {
        setTimeout(checkJuceBridge, 30);
    }
}
checkJuceBridge();

// --------------------------------------------------------------------------
// UI Layout Builders
// --------------------------------------------------------------------------

const diceIconSvg = `<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">
    <rect x="3" y="3" width="18" height="18" rx="2" ry="2" />
    <circle cx="8.5" cy="8.5" r="1.5" fill="currentColor" />
    <circle cx="15.5" cy="15.5" r="1.5" fill="currentColor" />
    <circle cx="12" cy="12" r="1.5" fill="currentColor" />
</svg>`;

const resetIconSvg = `<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round">
    <path d="M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8" />
    <path d="M3 3v5h5" />
</svg>`;

// Generate dynamically rendered sequencer lanes
function buildLanes() {
    const container = document.getElementById("lanes-container");
    container.innerHTML = "";

    const lanesConfig = [
        { label: "Pitch Shift", prop: "pitch", min: -24, max: 24, isBipolar: true },
        { label: "Velocity", prop: "velocity", min: 1, max: 127 },
        { label: "Modwheel", prop: "modwheel", min: 0, max: 127 },
        { label: "Probability", prop: "probability", min: 0, max: 100 },
        { label: "Mute", prop: "muted", min: 0, max: 1 }
    ];

    lanesConfig.forEach(config => {
        const laneWrapper = document.createElement("div");
        laneWrapper.className = "lane-wrapper";
        laneWrapper.setAttribute("data-prop", config.prop);

        // Header
        const header = document.createElement("div");
        header.className = "lane-header";
        header.innerHTML = `
            <div class="lane-title-group">
                <div class="lane-actions">
                    <button class="action-btn r-btn" title="Randomize Lane">${diceIconSvg}</button>
                    <button class="action-btn rst-btn" title="Reset Lane">${resetIconSvg}</button>
                </div>
                <span class="lane-title">${config.label}</span>
            </div>
        `;

        // Register action buttons click
        header.querySelector(".r-btn").onclick = () => {
            randomizeLaneLocal(config.prop, config.min, config.max);
            sendParamToCpp("randomizeLane", config.prop);
        };
        header.querySelector(".rst-btn").onclick = () => {
            resetLaneLocal(config.prop);
            sendParamToCpp("resetLane", config.prop);
        };

        // Grid container
        const grid = document.createElement("div");
        grid.className = "steps-grid";
        grid.style.height = config.prop === 'muted' ? "24px" : "128px";

        if (config.isBipolar) {
            const line = document.createElement("div");
            line.className = "bipolar-center-line";
            line.style.top = "calc(50% - 14px)"; // half of the 28px value label
            grid.appendChild(line);
        }

        // Draw 15 steps
        for (let i = 0; i < 15; i++) {
            const col = document.createElement("div");
            col.className = `step-column${i >= state.stepCount ? ' inactive' : ''}`;
            col.setAttribute("data-step", i);

            const barContainer = document.createElement("div");
            barContainer.className = "step-bar-container";

            if (config.prop === 'muted') {
                const muteBox = document.createElement("div");
                muteBox.className = "step-mute-box muted";
                barContainer.appendChild(muteBox);
            } else if (config.isBipolar) {
                const fill = document.createElement("div");
                fill.className = "step-bar-bipolar";
                barContainer.appendChild(fill);
            } else {
                const fill = document.createElement("div");
                fill.className = "step-bar-fill";
                barContainer.appendChild(fill);
            }

            col.appendChild(barContainer);

            if (config.prop !== 'muted') {
                const label = document.createElement("div");
                label.className = "step-value-label";
                col.appendChild(label);
            }

            // Drag registration on down
            col.onmousedown = (e) => {
                if (!state.isEnabled) return;
                if (config.prop === 'muted') {
                    // Mute lane is click-only
                    state.steps[i].muted = !state.steps[i].muted;
                    updateStepUI(config.prop, i);
                    sendParamToCpp("stepUpdate", JSON.stringify({ index: i, property: "muted", value: state.steps[i].muted }));
                } else {
                    activeDraggingLane = {
                        property: config.prop,
                        element: grid,
                        min: config.min,
                        max: config.max,
                        isBipolar: config.isBipolar
                    };
                    updateStepFromMouse(e, grid, config.prop, config.min, config.max, config.isBipolar);
                }
            };

            grid.appendChild(col);
        }

        laneWrapper.appendChild(header);
        laneWrapper.appendChild(grid);
        container.appendChild(laneWrapper);
    });

    // Generate 15 Step Count segments
    const track = document.getElementById("step-count-track");
    track.innerHTML = "";
    for (let i = 0; i < 15; i++) {
        const seg = document.createElement("div");
        seg.className = `step-count-segment${i < state.stepCount ? ' active' : ''}`;
        track.appendChild(seg);
    }
}

// --------------------------------------------------------------------------
// UI State Updates
// --------------------------------------------------------------------------

// Update a single step visual bar
function updateStepUI(property, index) {
    const lane = document.querySelector(`.lane-wrapper[data-prop="${property}"]`);
    if (!lane) return;

    const col = lane.querySelectorAll(".step-column")[index];
    if (!col) return;

    const val = state.steps[index][property];

    if (property === 'muted') {
        const muteBox = col.querySelector(".step-mute-box");
        if (muteBox) {
            if (val) {
                muteBox.className = "step-mute-box muted";
            } else {
                muteBox.className = "step-mute-box unmuted";
            }
        }
    } else {
        const fill = col.querySelector(".step-bar-fill, .step-bar-bipolar");
        const label = col.querySelector(".step-value-label");

        // Format label text
        if (label) {
            let labelStr = val.toString();
            if (property === 'probability') labelStr = `${val}%`;
            else if (property === 'pitch' && val > 0) labelStr = `+${val}`;
            label.textContent = labelStr;
        }

        // Draw fill bar
        if (fill) {
            const min = property === 'pitch' ? -24 : (property === 'velocity' ? 1 : 0);
            const max = property === 'pitch' ? 24 : (property === 'velocity' ? 127 : 100);

            if (property === 'pitch') {
                const absVal = Math.abs(val);
                const heightPercent = (absVal / 24) * 50;
                fill.style.height = `${heightPercent}%`;
                if (val >= 0) {
                    fill.style.top = "auto";
                    fill.style.bottom = "50%";
                } else {
                    fill.style.top = "50%";
                    fill.style.bottom = "auto";
                }
            } else {
                const percent = ((val - min) / (max - min)) * 100;
                fill.style.height = `${percent}%`;
            }
        }
    }
}

// Update all UI elements according to the main `state`
function updateUIFromState() {
    // 1. Power Switch
    const powerBtn = document.getElementById("power-btn");
    const appContainer = document.getElementById("app-container");
    if (state.isEnabled) {
        powerBtn.classList.add("active");
        appContainer.classList.remove("disabled");
    } else {
        powerBtn.classList.remove("active");
        appContainer.classList.add("disabled");
    }



    // 3. Step Count Slider
    document.getElementById("step-count-label").textContent = `STEP COUNT: ${state.stepCount}`;
    document.querySelectorAll(".step-count-segment").forEach((seg, i) => {
        seg.classList.toggle("active", i < state.stepCount);
    });
    // Update active/inactive state of lane columns
    document.querySelectorAll(".step-column").forEach(col => {
        const stepIdx = parseInt(col.getAttribute("data-step"));
        col.classList.toggle("inactive", stepIdx >= state.stepCount);
    });

    // 4. Delay Time Matrix & Footer
    const isMs = state.syncDivision === 'ms';
    document.getElementById("manual-btn").classList.toggle("active", isMs);
    document.getElementById("ms-value-box").classList.toggle("active", isMs);
    document.getElementById("ms-value-text").textContent = Math.round(state.delayTimeMs);
    document.getElementById("kill-btn").classList.toggle("active", state.killOnStop);

    document.querySelectorAll(".matrix-btn").forEach(btn => {
        btn.classList.toggle("active", !isMs && btn.getAttribute("data-div") === state.syncDivision);
    });

    // 5. Steps & Lanes
    for (let i = 0; i < 15; i++) {
        updateStepUI("pitch", i);
        updateStepUI("velocity", i);
        updateStepUI("modwheel", i);
        updateStepUI("probability", i);
        updateStepUI("muted", i);
    }

    // 6. Snapshot Presets Buttons
    document.querySelectorAll(".snapshot-btn").forEach((btn, i) => {
        btn.classList.toggle("active", i === state.activeSnapshot);
    });
}

// Calculate and apply step value changes from drag/mouse movement
function updateStepFromMouse(e, gridElement, property, min, max, isBipolar) {
    const rect = gridElement.getBoundingClientRect();
    const relativeX = e.clientX - rect.left;
    const stepWidth = rect.width / 15;
    const stepIndex = Math.max(0, Math.min(14, Math.floor(relativeX / stepWidth)));

    if (stepIndex >= state.stepCount) return;

    // Calculate vertical value
    const barAreaHeight = rect.height - (isBipolar ? 0 : 28); // subtract label height for unipolar
    const relativeY = Math.max(0, Math.min(barAreaHeight, e.clientY - rect.top));
    const normalized = 1 - (relativeY / barAreaHeight);
    const value = Math.round(min + (normalized * (max - min)));

    if (state.steps[stepIndex][property] !== value) {
        state.steps[stepIndex][property] = value;
        updateStepUI(property, stepIndex);
        sendParamToCpp("stepUpdate", JSON.stringify({ index: stepIndex, property: property, value: value }));
    }
}

// --------------------------------------------------------------------------
// Local Modifications Helpers
// --------------------------------------------------------------------------

function randomizeLaneLocal(property, min, max) {
    for (let i = 0; i < 15; i++) {
        if (property === 'muted') {
            state.steps[i].muted = Math.random() > 0.5;
        } else {
            state.steps[i][property] = Math.round(min + Math.random() * (max - min));
        }
        updateStepUI(property, i);
    }
}

function resetLaneLocal(property) {
    for (let i = 0; i < 15; i++) {
        let val;
        switch(property) {
            case 'pitch': val = 0; break;
            case 'velocity': val = Math.round(127 - (i * (126 / 14))); break;
            case 'modwheel': val = 0; break;
            case 'probability': val = 100; break;
            case 'muted': val = false; break;
        }
        state.steps[i][property] = val;
        updateStepUI(property, i);
    }
}

// Update Step Count from pointer position
function updateStepCountFromMouse(clientX) {
    const track = document.getElementById("step-count-track");
    const rect = track.getBoundingClientRect();
    const width = rect.width;
    const relativeX = Math.max(0, Math.min(width, clientX - rect.left));
    const normalized = relativeX / width;
    const newCount = Math.max(1, Math.min(15, Math.ceil(normalized * 15)));

    if (state.stepCount !== newCount) {
        state.stepCount = newCount;
        document.getElementById("step-count-label").textContent = `STEP COUNT: ${state.stepCount}`;
        document.querySelectorAll(".step-count-segment").forEach((seg, i) => {
            seg.classList.toggle("active", i < newCount);
        });
        document.querySelectorAll(".step-column").forEach(col => {
            const stepIdx = parseInt(col.getAttribute("data-step"));
            col.classList.toggle("inactive", stepIdx >= newCount);
        });
        sendParamToCpp("stepCount", newCount);
    }
}

// Drag delay MS value box
function updateMsFromMouse(clientY) {
    const deltaY = msStartY - clientY;
    const sensitivity = 0.5;
    const newVal = Math.max(1, Math.min(2000, Math.round(msStartValue + (deltaY * sensitivity))));
    if (state.delayTimeMs !== newVal) {
        state.delayTimeMs = newVal;
        document.getElementById("ms-value-text").textContent = newVal;
        sendParamToCpp("delayTimeMs", newVal);
    }
}

// --------------------------------------------------------------------------
// Window Resize & Scaling Handler
// --------------------------------------------------------------------------
function handleWindowResize() {
    const w = window.innerWidth;
    const h = window.innerHeight;
    const scaleX = w / 1040;
    const scaleY = h / 1200;
    const scale = Math.min(scaleX, scaleY);
    const container = document.getElementById("app-container");
    container.style.transform = `translate(-50%, -50%) scale(${scale})`;
}

// --------------------------------------------------------------------------
// Global Event Listeners Registration
// --------------------------------------------------------------------------

// Click events for basic controls
document.getElementById("power-btn").onclick = () => {
    state.isEnabled = !state.isEnabled;
    updateUIFromState();
    sendParamToCpp("enabled", state.isEnabled ? 1.0 : 0.0);
};



document.querySelectorAll(".matrix-btn").forEach(btn => {
    btn.onclick = () => {
        if (!state.isEnabled) return;
        const div = btn.getAttribute("data-div");
        state.syncDivision = div;
        
        // Update styling
        document.getElementById("manual-btn").classList.remove("active");
        document.getElementById("ms-value-box").classList.remove("active");
        document.querySelectorAll(".matrix-btn").forEach(b => b.classList.toggle("active", b === btn));
        
        const index = SYNC_DIVISIONS.indexOf(div) + 1; // 1 to 18
        sendParamToCpp("syncDivision", index);
    };
});

document.getElementById("manual-btn").onclick = () => {
    if (!state.isEnabled) return;
    state.syncDivision = 'ms';
    
    // Update styling
    document.getElementById("manual-btn").classList.add("active");
    document.getElementById("ms-value-box").classList.add("active");
    document.querySelectorAll(".matrix-btn").forEach(b => b.classList.remove("active"));
    
    sendParamToCpp("syncDivision", 0);
};

document.getElementById("kill-btn").onclick = () => {
    state.killOnStop = !state.killOnStop;
    document.getElementById("kill-btn").classList.toggle("active", state.killOnStop);
    sendParamToCpp("killOnStop", state.killOnStop ? 1.0 : 0.0);
};

document.querySelectorAll(".snapshot-btn").forEach(btn => {
    btn.onclick = () => {
        if (!state.isEnabled) return;
        const index = parseInt(btn.getAttribute("data-index"));
        state.activeSnapshot = index;
        updateUIFromState();
        sendParamToCpp("activeSnapshot", index + 1);
    };
});

document.getElementById("copy-snap-btn").onclick = () => {
    if (!state.isEnabled) return;
    sendParamToCpp("copyActiveSnapshot", 0);
};

document.getElementById("paste-snap-btn").onclick = () => {
    if (!state.isEnabled) return;
    sendParamToCpp("pasteActiveSnapshot", 0);
};

// Drag MS mouse registration
document.getElementById("ms-value-box").onmousedown = (e) => {
    if (state.syncDivision !== 'ms') return;
    isDraggingMs = true;
    msStartY = e.clientY;
    msStartValue = state.delayTimeMs;
    document.body.style.cursor = 'ns-resize';
};

// Drag Step Count mouse registration
document.getElementById("step-count-track").onmousedown = (e) => {
    if (!state.isEnabled) return;
    isDraggingStepCount = true;
    updateStepCountFromMouse(e.clientX);
};

// Global mouse tracking
window.addEventListener("mousemove", (e) => {
    if (activeDraggingLane) {
        updateStepFromMouse(e, activeDraggingLane.element, activeDraggingLane.property, activeDraggingLane.min, activeDraggingLane.max, activeDraggingLane.isBipolar);
    } else if (isDraggingStepCount) {
        updateStepCountFromMouse(e.clientX);
    } else if (isDraggingMs) {
        updateMsFromMouse(e.clientY);
    }
});

window.addEventListener("mouseup", () => {
    activeDraggingLane = null;
    isDraggingStepCount = false;
    if (isDraggingMs) {
        isDraggingMs = false;
        document.body.style.cursor = 'default';
    }
});

window.addEventListener("resize", handleWindowResize);

// --------------------------------------------------------------------------
// 3. APVTS C++ Callback Interface
// --------------------------------------------------------------------------

const aetherUI = {
    initializeState: (jsonStateStr) => {
        try {
            const parsed = JSON.parse(jsonStateStr);
            state = parsed;
            updateUIFromState();
        } catch (e) {
            console.error("Error parsing initial state:", e);
        }
    },
    updateParamFromCpp: (param, value) => {
        if (param === 'syncDivision') {
            const divStr = value === 0 ? 'ms' : SYNC_DIVISIONS[value - 1];
            if (state.syncDivision !== divStr) {
                state.syncDivision = divStr;
                updateUIFromState();
            }
            return;
        }
        if (param === 'activeSnapshot') {
            const index = Math.round(value) - 1; // 1-indexed to 0-indexed
            if (state.activeSnapshot !== index) {
                state.activeSnapshot = index;
                updateUIFromState();
            }
            return;
        }
        
        const keyMap = {
            'enabled': 'isEnabled',
            'delayTimeMs': 'delayTimeMs',
            'stepCount': 'stepCount',
            'killOnStop': 'killOnStop'
        };
        const mappedKey = keyMap[param];
        if (mappedKey) {
            if (state[mappedKey] !== value) {
                state[mappedKey] = value;
                updateUIFromState();
            }
        }
    },
    updateStepsFromCpp: (jsonStepsStr) => {
        try {
            const parsedSteps = JSON.parse(jsonStepsStr);
            state.steps = parsedSteps;
            // Update individual step columns
            for (let i = 0; i < 15; i++) {
                updateStepUI("pitch", i);
                updateStepUI("velocity", i);
                updateStepUI("modwheel", i);
                updateStepUI("probability", i);
                updateStepUI("muted", i);
            }
        } catch (e) {
            console.error("Error parsing steps:", e);
        }
    }
};

// Register bridge functions on mount
window.aetherUI = aetherUI;

// Show build version in the bottom-right corner
(function() {
    const el = document.getElementById('build-version-label');
    if (el) el.textContent = BUILD_VERSION;
})();

// Initialize app layout
buildLanes();
handleWindowResize();
// Request a full state sync from C++. This is queued via checkJuceBridge and fires
// only after window.__JUCE__ is available. C++ resets its caches; the timer then
// pushes all loaded parameter values to JS on its next tick (no reentrancy issues).
sendParamToCpp("queryall", 0);
