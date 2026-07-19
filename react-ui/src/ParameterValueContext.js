import React, {
  createContext,
  useContext,
  useState,
  useCallback,
  useEffect,
} from "react";
import { EventBridge } from "react-juce";

export const ParamIds = {
  delayTimeMs: "delayTimeMs",
  enabled: "enabled",
  stepCount: "stepCount",
  killOnStop: "killOnStop",
  syncDivision: "syncDivision",
  loopEnabled: "loopEnabled",
  loopMode: "loopMode",
  loopRestart: "loopRestart",
};

// Add step parameters programmatically
for (let i = 0; i < 15; i++) {
  const stepId = `step${i}`;
  ParamIds[`${stepId}_pitch`] = `${stepId}_pitch`;
  ParamIds[`${stepId}_vel`] = `${stepId}_vel`;
  ParamIds[`${stepId}_mod`] = `${stepId}_mod`;
  ParamIds[`${stepId}_prob`] = `${stepId}_prob`;
  ParamIds[`${stepId}_mute`] = `${stepId}_mute`;
}

const defaultValues = Object.keys(ParamIds).reduce((acc, id) => {
  acc[id] = {
    defaultValue: 0,
    currentValue: 0,
    stringValue: "...",
  };
  return acc;
}, {});

export const ParameterValueContext = createContext(defaultValues);

export const useParameter = (paramId) => {
  const ctx = useContext(ParameterValueContext);
  return ctx[paramId] || { defaultValue: 0, currentValue: 0, stringValue: "..." };
};

export const ParameterValueProvider = ({ children }) => {
  const [params, setParams] = useState(defaultValues);

  const onParameterValueChange = useCallback(
    (index, changedParamId, defaultValue, currentValue, stringValue) => {
      if (!ParamIds[changedParamId]) return;
      setParams((prevParams) => ({
        ...prevParams,
        [changedParamId]: {
          index,
          defaultValue,
          currentValue,
          stringValue,
        },
      }));
    },
    []
  );

  useEffect(() => {
    EventBridge.addListener("parameterValueChange", onParameterValueChange);
    return () => {
      EventBridge.removeListener("parameterValueChange", onParameterValueChange);
    };
  }, [onParameterValueChange]);

  return (
    <ParameterValueContext.Provider value={params}>
      {children}
    </ParameterValueContext.Provider>
  );
};
