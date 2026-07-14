export type TrackRole =
  | "custom"
  | "drums"
  | "kick"
  | "snare"
  | "toms"
  | "cymbals"
  | "bass"
  | "rhythm-guitar"
  | "lead-guitar"
  | "clean-vocal"
  | "scream-vocal"
  | "backing-vocal"
  | "synth"
  | "orchestra"
  | "effects";

export interface AudioMetrics {
  samplePeakDbfs: number;
  estimatedTruePeakDbtp: number;
  rmsDbfs: number;
  integratedLufs: number;
  crestFactorDb: number;
  stereoCorrelation: number;
  transientDensityHz: number;
}

export interface Track {
  id: string;
  name: string;
  role: TrackRole;
  gainDb: number;
  pan: number;
  muted: boolean;
  soloed: boolean;
  polarityInverted?: boolean;
  metrics: AudioMetrics;
}

export interface Suggestion {
  kind: string;
  trackId: string;
  title: string;
  explanation: string;
  confidence: number;
}

export interface SuiteState {
  product: "plugin" | "desktop";
  connected: boolean;
  projectId: string;
  projectName: string;
  playing: boolean;
  positionSeconds: number;
  durationSeconds: number;
  tracks: Track[];
  suggestions: Suggestion[];
  analyzerMetrics?: AudioMetrics;
  analyzerRole?: TrackRole;
  monitorSource?: "mix" | "reference";
  hasReference?: boolean;
  referenceGainDb?: number;
  selectedVariant?: string;
  variants?: string[];
  exportBitDepth?: number;
}

export const emptyMetrics: AudioMetrics = {
  samplePeakDbfs: -120,
  estimatedTruePeakDbtp: -120,
  rmsDbfs: -120,
  integratedLufs: -120,
  crestFactorDb: 0,
  stereoCorrelation: 1,
  transientDensityHz: 0,
};
