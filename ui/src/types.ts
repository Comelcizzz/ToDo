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
  truePeakDbtp?: number;
  truePeakValid?: boolean;
  estimatedTruePeakDbtp?: number;
  truePeakIsEstimate?: boolean;
  rmsDbfs: number;
  estimatedLoudnessDb?: number;
  estimatedLoudnessIsValid?: boolean;
  momentaryLufs?: number;
  momentaryLufsIsValid?: boolean;
  shortTermLufs?: number;
  shortTermLufsIsValid?: boolean;
  integratedLufs?: number;
  integratedLufsIsValid?: boolean;
  integratedLufsIsProvisional?: boolean;
  loudnessRangeLu?: number;
  loudnessRangeIsValid?: boolean;
  momentaryState?: string;
  shortTermState?: string;
  integratedState?: string;
  truePeakState?: string;
  droppedAnalysisFrames?: number;
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
  rmsDbfs: -120,
  estimatedLoudnessDb: -120,
  estimatedLoudnessIsValid: false,
  truePeakValid: false,
  truePeakIsEstimate: false,
  momentaryLufsIsValid: false,
  shortTermLufsIsValid: false,
  integratedLufsIsValid: false,
  integratedLufsIsProvisional: false,
  loudnessRangeIsValid: false,
  momentaryState: "unavailable",
  shortTermState: "unavailable",
  integratedState: "unavailable",
  truePeakState: "unavailable",
  droppedAnalysisFrames: 0,
  crestFactorDb: 0,
  stereoCorrelation: 1,
  transientDensityHz: 0,
};
