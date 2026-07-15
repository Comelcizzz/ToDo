import { useEffect, useState } from "react";
import { AnalyzerView } from "./components/AnalyzerView";
import { MixNodeView } from "./components/MixNodeView";
import { MixerView } from "./components/MixerView";
import { sendCommand, subscribeToState } from "./nativeBridge";
import { emptyMetrics, type SuiteState } from "./types";

function detectProduct(): SuiteState["product"] {
  if (window.location.hash.includes("mix-node")) return "mix-node";
  if (window.location.hash.includes("plugin")) return "plugin";
  return "desktop";
}

function initialState(): SuiteState {
  return {
    product: detectProduct(),
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
    mixNodes: [],
  };
}

export function App() {
  const [state, setState] = useState<SuiteState>(initialState);

  useEffect(() => {
    const unsubscribe = subscribeToState(setState);
    sendCommand({ type: "ui-ready" });
    return unsubscribe;
  }, []);

  if (state.product === "mix-node") return <MixNodeView state={state} />;
  if (state.product === "plugin") return <AnalyzerView state={state} />;
  return <MixerView state={state} />;
}
