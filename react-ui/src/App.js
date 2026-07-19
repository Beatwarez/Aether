import React, { Component } from "react";
import { View, Text, Slider, Button } from "react-juce";
import { useParameter, ParamIds } from "./ParameterValueContext";
import {
  setParameterValueNotifyingHost,
  beginParameterChangeGesture,
  endParameterChangeGesture
} from "./nativeMethods";

const ControlGroup = ({ label, children }) => (
  <View {...styles.controlGroup}>
    <Text {...styles.groupLabel}>{label}</Text>
    <View {...styles.groupContent}>
      {children}
    </View>
  </View>
);

const StepRow = ({ stepIndex }) => {
  const stepId = `step${stepIndex}`;
  const pitch = useParameter(`${stepId}_pitch`);
  const vel = useParameter(`${stepId}_vel`);
  const prob = useParameter(`${stepId}_prob`);
  const mute = useParameter(`${stepId}_mute`);

  const handleSliderChange = (pid, val) => {
    setParameterValueNotifyingHost(pid, val);
  };

  const handleMouseDown = (pid) => beginParameterChangeGesture(pid);
  const handleMouseUp = (pid) => endParameterChangeGesture(pid);

  return (
    <View {...styles.stepRow}>
      <Text {...styles.stepNumber}>{stepIndex + 1}</Text>

      {/* Pitch Slider - pitch.currentValue is already normalized 0-1 */}
      <View {...styles.cell}>
        <Slider
          value={pitch.currentValue}
          onChange={(v) => handleSliderChange(`${stepId}_pitch`, v)}
          onMouseDown={() => handleMouseDown(`${stepId}_pitch`)}
          onMouseUp={() => handleMouseUp(`${stepId}_pitch`)}
          {...styles.stepSlider}
        />
        <Text {...styles.cellText}>{pitch.stringValue}</Text>
      </View>

      {/* Vel Slider */}
      <View {...styles.cell}>
        <Slider
          value={vel.currentValue}
          onChange={(v) => handleSliderChange(`${stepId}_vel`, v)}
          onMouseDown={() => handleMouseDown(`${stepId}_vel`)}
          onMouseUp={() => handleMouseUp(`${stepId}_vel`)}
          {...styles.stepSlider}
        />
        <Text {...styles.cellText}>{vel.stringValue}</Text>
      </View>

      {/* Prob Slider */}
      <View {...styles.cell}>
        <Slider
          value={prob.currentValue}
          onChange={(v) => handleSliderChange(`${stepId}_prob`, v)}
          onMouseDown={() => handleMouseDown(`${stepId}_prob`)}
          onMouseUp={() => handleMouseUp(`${stepId}_prob`)}
          {...styles.stepSlider}
        />
        <Text {...styles.cellText}>{prob.stringValue}</Text>
      </View>

      {/* Mute Toggle */}
      <Button
        onClick={() => setParameterValueNotifyingHost(`${stepId}_mute`, mute.currentValue > 0.5 ? 0.0 : 1.0)}
        {...(mute.currentValue > 0.5 ? styles.muteButtonActive : styles.muteButton)}
      >
        <Text {...styles.muteButtonText}>M</Text>
      </Button>
    </View>
  );
};

const App = () => {
  const delayTime = useParameter(ParamIds.delayTimeMs);
  const enabled = useParameter(ParamIds.enabled);
  const loopEnabled = useParameter(ParamIds.loopEnabled);
  const stepCount = useParameter(ParamIds.stepCount);

  return (
    <View {...styles.container}>
      <View {...styles.header}>
        <Text {...styles.title}>AETHER</Text>
        <Button
          onClick={() => setParameterValueNotifyingHost(ParamIds.enabled, enabled.currentValue > 0.5 ? 0.0 : 1.0)}
          {...(enabled.currentValue > 0.5 ? styles.powerButtonActive : styles.powerButton)}
        >
          <Text {...styles.powerButtonText}>{enabled.currentValue > 0.5 ? "ON" : "OFF"}</Text>
        </Button>
      </View>

      <View {...styles.mainContent}>
        {/* Global Controls */}
        <View {...styles.sidebar}>
          <ControlGroup label="GLOBAL">
            <View {...styles.globalControl}>
              <Text {...styles.label}>TIME Ms</Text>
              <Slider
                value={delayTime.currentValue}
                onChange={(v) => setParameterValueNotifyingHost(ParamIds.delayTimeMs, v)}
                onMouseDown={() => beginParameterChangeGesture(ParamIds.delayTimeMs)}
                onMouseUp={() => endParameterChangeGesture(ParamIds.delayTimeMs)}
                {...styles.globalSlider}
              />
              <Text {...styles.valueText}>{delayTime.stringValue}</Text>
            </View>

            <View {...styles.globalControl}>
              <Text {...styles.label}>STEPS</Text>
              <Slider
                value={stepCount.currentValue}
                onChange={(v) => setParameterValueNotifyingHost(ParamIds.stepCount, v)}
                onMouseDown={() => beginParameterChangeGesture(ParamIds.stepCount)}
                onMouseUp={() => endParameterChangeGesture(ParamIds.stepCount)}
                {...styles.globalSlider}
              />
              <Text {...styles.valueText}>{stepCount.stringValue}</Text>
            </View>

            <View {...styles.globalControl}>
              <Text {...styles.label}>LOOP</Text>
              <Button
                onClick={() => setParameterValueNotifyingHost(ParamIds.loopEnabled, loopEnabled.currentValue > 0.5 ? 0.0 : 1.0)}
                {...(loopEnabled.currentValue > 0.5 ? styles.toggleActive : styles.toggle)}
              >
                <Text {...styles.toggleText}>{loopEnabled.currentValue > 0.5 ? "ENABLED" : "DISABLED"}</Text>
              </Button>
            </View>
          </ControlGroup>
        </View>

        {/* Steps Editor */}
        <View {...styles.stepsContainer}>
          <View {...styles.stepsHeader}>
            <Text {...styles.headerCellNum}>#</Text>
            <Text {...styles.headerCell}>PITCH</Text>
            <Text {...styles.headerCell}>VEL</Text>
            <Text {...styles.headerCell}>PROB</Text>
            <Text {...styles.headerCellMute}>M</Text>
          </View>
          <View {...styles.stepsList}>
            {[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14].map((i) => (
              <StepRow key={i} stepIndex={i} />
            ))}
          </View>
        </View>
      </View>
    </View>
  );
};

const styles = {
  container: {
    width: "100%",
    height: "100%",
    backgroundColor: "#121417",
    padding: 20,
    flexDirection: "column",
  },
  header: {
    flexDirection: "row",
    justifyContent: "space-between",
    alignItems: "center",
    marginBottom: 20,
    borderBottomWidth: 1.0,
    borderBottomColor: "#2a2e35",
    paddingBottom: 10,
    height: 60.0,
  },
  title: {
    fontSize: 32.0,
    color: "#ffffff",
    fontStyle: Text.FontStyleFlags.bold,
  },
  powerButton: {
    paddingHorizontal: 20.0,
    paddingVertical: 8.0,
    borderRadius: 4.0,
    backgroundColor: "#2a2e35",
    justifyContent: "center",
  },
  powerButtonActive: {
    paddingHorizontal: 20.0,
    paddingVertical: 8.0,
    borderRadius: 4.0,
    backgroundColor: "#34d399",
    justifyContent: "center",
  },
  powerButtonText: {
    color: "#ffffff",
    fontSize: 14.0,
    fontStyle: Text.FontStyleFlags.bold,
  },
  mainContent: {
    flexDirection: "row",
    flex: 1.0,
  },
  sidebar: {
    width: 200.0,
    marginRight: 20.0,
  },
  controlGroup: {
    backgroundColor: "#1a1d23",
    borderRadius: 8.0,
    padding: 15.0,
    marginBottom: 20.0,
  },
  groupLabel: {
    fontSize: 12.0,
    color: "#94a3b8",
    marginBottom: 15.0,
    fontStyle: Text.FontStyleFlags.bold,
  },
  groupContent: {
    flexDirection: "column",
  },
  globalControl: {
    marginBottom: 15.0,
  },
  label: {
    fontSize: 10.0,
    color: "#64748b",
    marginBottom: 5.0,
  },
  globalSlider: {
    height: 12.0,
    width: "100%",
  },
  valueText: {
    fontSize: 12.0,
    color: "#34d399",
    textAlign: "right",
  },
  toggle: {
    backgroundColor: "#2a2e35",
    padding: 8.0,
    borderRadius: 4.0,
    alignItems: "center",
    justifyContent: "center",
  },
  toggleActive: {
    backgroundColor: "#3b82f6",
    padding: 8.0,
    borderRadius: 4.0,
    alignItems: "center",
    justifyContent: "center",
  },
  toggleText: {
    color: "#ffffff",
    fontSize: 10.0,
    fontStyle: Text.FontStyleFlags.bold,
  },
  stepsContainer: {
    flex: 1.0,
    backgroundColor: "#1a1d23",
    borderRadius: 8.0,
  },
  stepsHeader: {
    flexDirection: "row",
    backgroundColor: "#242933",
    padding: 10,
    height: 40.0,
  },
  headerCell: {
    flex: 1.0,
    fontSize: 11.0,
    color: "#94a3b8",
    textAlign: "center",
  },
  headerCellNum: {
    width: 30.0,
    fontSize: 11.0,
    color: "#94a3b8",
    textAlign: "center",
  },
  headerCellMute: {
    width: 40.0,
    fontSize: 11.0,
    color: "#94a3b8",
    textAlign: "center",
  },
  stepsList: {
    flex: 1.0,
    flexDirection: "column",
  },
  stepRow: {
    flexDirection: "row",
    alignItems: "center",
    padding: 8,
    borderBottomWidth: 1.0,
    borderBottomColor: "#242933",
    height: 48.0,
  },
  stepNumber: {
    width: 30.0,
    fontSize: 12.0,
    color: "#475569",
    textAlign: "center",
  },
  cell: {
    flex: 1.0,
    alignItems: "center",
    justifyContent: "center",
  },
  stepSlider: {
    width: "80%",
    height: 12.0,
  },
  cellText: {
    fontSize: 10.0,
    color: "#cbd5e1",
    marginTop: 2.0,
  },
  muteButton: {
    width: 30.0,
    height: 30.0,
    backgroundColor: "#2a2e35",
    borderRadius: 4.0,
    justifyContent: "center",
    alignItems: "center",
  },
  muteButtonActive: {
    width: 30.0,
    height: 30.0,
    backgroundColor: "#ef4444",
    borderRadius: 4.0,
    justifyContent: "center",
    alignItems: "center",
  },
  muteButtonText: {
    color: "#ffffff",
    fontSize: 12.0,
    fontStyle: Text.FontStyleFlags.bold,
  },
};

export default App;
