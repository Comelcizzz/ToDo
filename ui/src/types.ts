export type TrackRole =
  | "custom"
  | "drums"
  | "kick"
  | "snare"
  | "toms"
  | "cymbals"
  | "drum-bus"
  | "bass"
  | "bass-bus"
  | "rhythm-guitar"
  | "rhythm-guitar-left"
  | "rhythm-guitar-right"
  | "lead-guitar"
  | "clean-guitar"
  | "guitar-bus"
  | "clean-vocal"
  | "scream-vocal"
  | "backing-vocal"
  | "vocal-bus"
  | "synth"
  | "orchestra"
  | "effects"
  | "music-bus"
  | "master";

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
  loudnessRangeState?: string;
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
  pairId?: string;
  parentBusId?: string;
  channelPosition?: string;
  dynamicEqEnabled?: boolean;
}

export interface PairRecord {
  id: string;
  name: string;
  leftTrackId: string;
  rightTrackId: string;
  parentBusId?: string;
  linkedProcessing?: boolean;
}

export interface BusRecord {
  id: string;
  name: string;
  role: string;
  childTrackIds?: string[];
  childPairIds?: string[];
  gainDb?: number;
}

export interface SectionMarker {
  id: string;
  kind: string;
  name: string;
  startSeconds: number;
  endSeconds: number;
}

export interface MixPassAction {
  actionId: string;
  actionVersion?: number;
  problemType: string;
  targetTrackId: string;
  targetPairId?: string;
  targetBusId?: string;
  processorId: string;
  parameterId: string;
  currentValue: number;
  proposedValue: number;
  allowedMin: number;
  allowedMax: number;
  confidence: number;
  evidenceScore?: number;
  evidenceLabel?: string;
  explanation: string;
  sourceMetrics?: string;
  evidence?: string;
  decisionTrace?: string;
  processingLevel?: string;
  sectionScope?: string;
  state: string;
  origin?: string;
}

export interface Suggestion {
  kind: string;
  trackId: string;
  title: string;
  explanation: string;
  confidence: number;
}

export interface MixNodeInfo {
  instanceId: string;
  trackName: string;
  role: string;
  channelPosition: string;
  pairId?: string;
  parentBusId?: string;
  connected: boolean;
  stateRevision: number;
  latencySamples: number;
  sampleRate: number;
  sidechainActive?: boolean;
  status: string;
  projectId?: string;
  inputGainDb?: number;
  outputGainDb?: number;
  eqFreq?: number;
  eqGain?: number;
  dynThreshold?: number;
  dynMaxCut?: number;
  satDrive?: number;
  bypass?: boolean;
}

export interface SuiteState {
  product: "plugin" | "desktop" | "mix-node";
  connected: boolean;
  projectId: string;
  projectName: string;
  playing: boolean;
  positionSeconds: number;
  durationSeconds: number;
  tracks: Track[];
  pairs?: PairRecord[];
  buses?: BusRecord[];
  sections?: SectionMarker[];
  mixPassActions?: MixPassAction[];
  suggestions: Suggestion[];
  analyzerMetrics?: AudioMetrics;
  analyzerRole?: TrackRole;
  monitorSource?: "mix" | "reference";
  compareMode?: "raw" | "auto" | "current" | "reference";
  hasReference?: boolean;
  referenceGainDb?: number;
  selectedVariant?: string;
  variants?: string[];
  exportBitDepth?: number;
  bpm?: number;
  analysisStatus?: string;
  canUndoMixPass?: boolean;
  canRedoMixPass?: boolean;
  mixNodes?: MixNodeInfo[];
  selectedMixNodeId?: string;
  suiteSessionId?: string;
  // Mix Node plugin surface
  trackName?: string;
  role?: string;
  channelPosition?: string;
  status?: string;
  instanceId?: string;
  sessionId?: string;
  stateRevision?: number;
  latencySamples?: number;
  sampleRate?: number;
  previewActive?: boolean;
  bypass?: boolean;
  inputGainDb?: number;
  outputGainDb?: number;
  eqFreq?: number;
  eqGain?: number;
  dynThreshold?: number;
  dynMaxCut?: number;
  satDrive?: number;
  saturationEnabled?: boolean;
  inputPeakDb?: number;
  outputPeakDb?: number;
  grDb?: number;
  sidechainPeakDb?: number;
  degraded?: boolean;
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
  loudnessRangeState: "unavailable",
  droppedAnalysisFrames: 0,
  crestFactorDb: 0,
  stereoCorrelation: 1,
  transientDensityHz: 0,
};

export const trackRoleOptions: TrackRole[] = [
  "custom",
  "kick",
  "snare",
  "toms",
  "cymbals",
  "drums",
  "drum-bus",
  "bass",
  "bass-bus",
  "rhythm-guitar-left",
  "rhythm-guitar-right",
  "rhythm-guitar",
  "lead-guitar",
  "clean-guitar",
  "guitar-bus",
  "clean-vocal",
  "scream-vocal",
  "backing-vocal",
  "vocal-bus",
  "synth",
  "orchestra",
  "effects",
  "music-bus",
  "master",
];
