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
  | { type: "export-research-example" }
  | { type: "analyze" }
  | { type: "generate-mix-plan" }
  | { type: "generate-metalcore-mix-pass" }
  | { type: "ensure-hierarchy" }
  | { type: "set-bpm"; bpm: number }
  | {
      type: "add-section";
      kind: string;
      name?: string;
      startSeconds: number;
      endSeconds: number;
    }
  | { type: "remove-section"; sectionId: string }
  | { type: "mixpass-preview"; actionId: string }
  | { type: "mixpass-apply"; actionId: string }
  | { type: "mixpass-reject"; actionId: string }
  | { type: "mixpass-edit"; actionId: string; proposedValue: number }
  | { type: "mixpass-cancel-preview"; actionId: string }
  | { type: "mixpass-undo" }
  | { type: "mixpass-redo" }
  | { type: "set-compare-mode"; mode: "raw" | "auto" | "current" | "reference" }
  | { type: "select-variant"; variant: string }
  | { type: "apply-mix-plan" }
  | { type: "reject-mix-plan" }
  | { type: "toggle-playback" }
  | { type: "toggle-ab" }
  | { type: "set-monitor"; source: "mix" | "reference" }
  | { type: "export-master"; bitsPerSample?: number }
  | { type: "set-role"; trackId?: string; role: string }
  | { type: "set-track-gain"; trackId: string; value: number }
  | { type: "set-track-pan"; trackId: string; value: number }
  | {
      type: "toggle-track";
      trackId: string;
      field: "muted" | "soloed" | "polarityInverted";
    }
  | { type: "select-mix-node"; instanceId: string }
  | {
      type: "mix-node-preview";
      instanceId: string;
      processorId: string;
      parameterId: string;
      previousValue: number;
      proposedValue: number;
      explanation?: string;
      actionId?: string;
    }
  | {
      type: "mix-node-commit";
      instanceId: string;
      processorId: string;
      parameterId: string;
      previousValue: number;
      proposedValue: number;
      explanation?: string;
      actionId?: string;
    }
  | { type: "mix-node-cancel-preview"; instanceId?: string }
  | { type: "mix-node-undo"; instanceId?: string }
  | { type: "mix-node-request-state"; instanceId: string }
  | { type: "mix-node-set-param"; parameterId: string; value: number }
  | { type: "set-track-name"; name: string }
  | { type: "open-local-data-folder" }
  | { type: "clear-benchmark-cache" }
  | { type: "clear-benchmark-renders" }
  | { type: "run-profile-experiment" }
  | { type: "cancel-experiment" }
  | { type: "validate-benchmark-import" }
  | { type: "set-metalcore-profile"; profileId: string };

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
