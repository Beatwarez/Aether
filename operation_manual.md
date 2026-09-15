# Aether Operation Manual

Welcome to **Aether**, a powerful step-sequenced MIDI repeater plugin designed to bring dynamic, rhythmic modulation to your audio tracks. Aether combines a high-quality delay engine with an intuitive 15-step sequencer that modulates Pitch, Velocity, Modwheel, and Probability per step, wrapped in a beautiful CRT-styled interface.

---

## 1. Global Controls

The top section of the plugin contains the global parameters that affect the overall behavior of the delay and sequencer.

- **Power Button**: Toggles the effect on and off (bypass).
- **Delay Time**: Sets the base delay time. 
  - When **Sync** is disabled, this is set in milliseconds (ms).
  - When **Sync** is enabled, this is set in musical divisions (e.g., 1/4, 1/8d, 1/16t).
- **Steps**: Controls the number of active steps in the sequence (from 1 to 15). Changing this dynamically resizes the sequencer grid.
- **Sync Toggle**: Switches the delay time between free-running milliseconds and host-tempo synchronized beat divisions.

---

## 2. Global Toggles

Found in the bottom left, these switches control how Aether reacts to transport and preset changes.

- **Kill on Stop**: When enabled, the delay buffer and sequencer will immediately silence when the host transport stops.
- **Kill on Switch**: When enabled, the delay buffer will be cleared whenever you change presets or snapshots, preventing audio artifacts from carrying over.
- **Kill on Note**: When enabled, receiving a new MIDI note will reset the delay buffer and restart the sequencer from step 1.
- **End Switch**: Determines the behavior when the sequence reaches the final step. (Specific behaviors depend on your routing setup).
- **Mod Slew**: Controls the smoothing time (slew rate) applied to the Modwheel modulation lane, preventing sudden zipper noise on parameter jumps.

---

## 3. The Sequencer

The heart of Aether is its 15-step sequencer, featuring 5 distinct lanes. You can click and drag across steps to easily draw modulation curves.

- **Pitch Lane**: Modulates the pitch of the delayed signal. Values range from -12 to +12 semitones. Double-click to reset a step to 0.
- **Velocity Lane**: Modulates the amplitude/volume of the delayed signal. 
- **Modwheel Lane**: Outputs a continuous modulation signal that can be mapped to external parameters or internal macro controls. The transition between steps is smoothed by the **Mod Slew** parameter.
- **Probability Lane**: Sets the chance (0% to 100%) that a step will trigger. If the probability fails, the step is effectively muted for that cycle.
- **Mute Lane**: A row of red toggle buttons at the bottom. Clicking a button explicitly mutes the delay output for that specific step.

### Quick Actions
Each lane has a header on the left side with quick actions:
- **Randomize (Dice icon)**: Randomizes all steps in that lane.
- **Reset (X icon)**: Resets all steps in that lane to their default neutral values.

---

## 4. Snapshot System

Aether features a powerful Snapshot system, allowing you to store and recall 8 completely different sequence patterns and global settings within a single preset.

- **Snapshot Buttons (1-8)**: Click to instantly switch the active snapshot.
- **Copy / Paste**: Use the **C** (Copy) and **P** (Paste) buttons next to the snapshot numbers to easily duplicate a sequence to another snapshot slot.

---

## 5. Preset Manager

The Preset Manager allows you to save, load, and organize your patches.

- **Top Bar**: Displays the currently loaded Bank and Preset name.
- **Navigation (< >)**: Use the left and right arrows to quickly cycle through presets in alphabetical order.
- **Save Button**: Opens the save dialog to name your preset and choose a bank.
- **Preset Browser**: Clicking the preset name opens the full browser where you can:
  - Create new Banks.
  - Favorite presets (Star icon).
  - Delete user presets and banks (Factory Presets cannot be deleted).

---

## 6. UI Scaling & Resizing

Aether's interface is completely scalable.
- **Window Resizing**: Click and drag the bottom-right corner of the plugin window (marked by the 3-line diagonal indicator) to resize the interface. The UI will automatically scale proportionally to fit your screen.
