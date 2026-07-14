import { useEffect, useState } from "react";
import { AnalyzerView } from "./components/AnalyzerView";
import { MixerView } from "./components/MixerView";
import { sendCommand, subscribeToState } from "./nativeBridge";
import { emptyMetrics, type SuiteState } from "./types";

function initialState(): SuiteState {
  const product = window.location.hash.includes("plugin") ? "plugin" : "desktop";
  return {
    product,
    connected: false,
    projectId: "",
    projectName: "Untitled Mix",
    playing: false,
    positionSeconds: 0,
    durationSeconds: 0,
    tracks: [],
    suggestions: [],
    analyzerMetrics: emptyMetrics,
    analyzerRole: "custom",
  };
}

export function App() {
  const [state, setState] = useState<SuiteState>(initialState);

  useEffect(() => {
    const unsubscribe = subscribeToState(setState);
    sendCommand({ type: "ui-ready" });
    return unsubscribe;
  }, []);

  return state.product === "plugin" ? (
    <AnalyzerView state={state} />
  ) : (
    <MixerView state={state} />
  );
}
