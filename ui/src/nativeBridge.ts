import type { SuiteState } from "./types";

interface JuceBackend {
  emitEvent(eventId: string, payload: unknown): void;
  addEventListener(eventId: string, listener: (payload: unknown) => void): number;
  removeEventListener?(eventId: string, listenerId: number): void;
}

declare global {
  interface Window {
    __JUCE__?: {
      backend: JuceBackend;
    };
  }
}

export type NativeCommand =
  | { type: "ui-ready" }
  | { type: "create-project" }
  | { type: "open-project" }
  | { type: "save-project" }
  | { type: "import-stems" }
  | { type: "import-reference" }
  | { type: "analyze" }
  | { type: "generate-mix-plan" }
  | { type: "apply-mix-plan" }
  | { type: "toggle-playback" }
  | { type: "export-master" }
  | { type: "set-role"; trackId?: string; role: string }
  | { type: "set-track-gain"; trackId: string; value: number }
  | { type: "set-track-pan"; trackId: string; value: number }
  | { type: "toggle-track"; trackId: string; field: "muted" | "soloed" };

export function sendCommand(command: NativeCommand): void {
  window.__JUCE__?.backend.emitEvent("masteringAudioCommand", command);
}

export function subscribeToState(listener: (state: SuiteState) => void): () => void {
  const backend = window.__JUCE__?.backend;
  if (!backend) {
    return () => undefined;
  }

  const listenerId = backend.addEventListener("masteringAudioState", (payload) => {
    listener(payload as SuiteState);
  });
  return () => backend.removeEventListener?.("masteringAudioState", listenerId);
}
