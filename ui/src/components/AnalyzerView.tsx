import { sendCommand } from "../nativeBridge";
import { emptyMetrics, type SuiteState, type TrackRole } from "../types";
import { Meter } from "./Meter";

const roles: TrackRole[] = [
  "kick",
  "snare",
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
  "custom",
];

type MetricState =
  | "unavailable"
  | "warmingUp"
  | "valid"
  | "provisional"
  | "unverified"
  | "stale"
  | "degraded"
  | "invalidInput";

function stateLabel(state: string | undefined, fallback: MetricState = "unavailable"): string {
  switch (state as MetricState) {
    case "warmingUp":
      return "Warming up";
    case "valid":
      return "Valid";
    case "provisional":
      return "Provisional";
    case "unverified":
      return "Unverified";
    case "stale":
      return "Stale";
    case "degraded":
      return "Degraded";
    case "invalidInput":
      return "Invalid input";
    case "unavailable":
    default:
      return fallback === "warmingUp" ? "Warming up" : "Unavailable";
  }
}

function formatWhenValid(
  valid: boolean | undefined,
  state: string | undefined,
  value: number | undefined,
): string {
  if (state === "warmingUp")
    return "Warming up";
  if (state === "stale" || state === "unavailable" || state === "invalidInput")
    return state === "invalidInput" ? "Invalid input" : "Unavailable";
  if (!valid && state !== "unverified" && state !== "provisional" && state !== "degraded")
    return "Unavailable";
  if (value === undefined || !Number.isFinite(value))
    return "Unavailable";
  if (state === "provisional")
    return `${value.toFixed(1)} (provisional)`;
  if (state === "degraded")
    return `${value.toFixed(1)} (degraded)`;
  if (state === "unverified")
    return `${value.toFixed(1)} (unverified)`;
  return value.toFixed(1);
}

export function AnalyzerView({ state }: { state: SuiteState }) {
  const metrics = state.analyzerMetrics ?? emptyMetrics;
  const dropped = metrics.droppedAnalysisFrames ?? 0;
  const truePeakLabel = metrics.truePeakIsEstimate ? "Estimated True Peak" : "True Peak";
  const lraState = metrics.loudnessRangeState ?? "unavailable";
  const showLraNumber =
    metrics.loudnessRangeIsValid === true && lraState !== "unavailable" && lraState !== "stale";

  return (
    <main className="plugin-layout">
      <header className="product-header">
        <div>
          <p className="eyebrow">FL Studio track analyzer</p>
          <h1>Mastering Audio</h1>
        </div>
        <span
          className={`status ${state.connected ? "status--online" : "status--offline"}`}
          data-state={state.connected ? "connected" : "offline"}
        >
          {state.connected ? "Suite connected" : "Offline report"}
        </span>
      </header>

      <section className="panel role-panel">
        <label htmlFor="track-role">Signal role</label>
        <select
          id="track-role"
          value={state.analyzerRole ?? "custom"}
          onChange={(event) =>
            sendCommand({ type: "set-role", role: event.target.value })
          }
        >
          {roles.map((role) => (
            <option value={role} key={role}>
              {role.replaceAll("-", " ")}
            </option>
          ))}
        </select>
        <p>
          Play the full song once. Analysis is accumulated without changing the
          audio passing through this insert.
        </p>
      </section>

      <section className="metric-grid">
        <div className="metric-card" data-state={metrics.momentaryState ?? "unavailable"}>
          <span>Momentary</span>
          <strong>
            {formatWhenValid(
              metrics.momentaryLufsIsValid,
              metrics.momentaryState,
              metrics.momentaryLufs,
            )}
          </strong>
          <small>{stateLabel(metrics.momentaryState)} · LUFS</small>
        </div>
        <div className="metric-card" data-state={metrics.shortTermState ?? "unavailable"}>
          <span>Short-term</span>
          <strong>
            {formatWhenValid(
              metrics.shortTermLufsIsValid,
              metrics.shortTermState,
              metrics.shortTermLufs,
            )}
          </strong>
          <small>{stateLabel(metrics.shortTermState)} · LUFS</small>
        </div>
        <div className="metric-card" data-state={metrics.integratedState ?? "unavailable"}>
          <span>Integrated</span>
          <strong>
            {formatWhenValid(
              metrics.integratedLufsIsValid,
              metrics.integratedState,
              metrics.integratedLufs,
            )}
          </strong>
          <small>{stateLabel(metrics.integratedState)} · LUFS</small>
        </div>
        <div className="metric-card" data-state={metrics.truePeakState ?? "unavailable"}>
          <span>{truePeakLabel}</span>
          <strong>
            {formatWhenValid(metrics.truePeakValid, metrics.truePeakState, metrics.truePeakDbtp)}
          </strong>
          <small>{stateLabel(metrics.truePeakState)} · dBTP</small>
        </div>
        <div className="metric-card">
          <span>Sample Peak</span>
          <strong>{metrics.samplePeakDbfs.toFixed(1)}</strong>
          <small>dBFS</small>
        </div>
        <div className="metric-card" data-state={lraState}>
          <span>LRA</span>
          <strong>
            {showLraNumber
              ? formatWhenValid(true, lraState, metrics.loudnessRangeLu)
              : lraState === "unverified"
                ? "LRA unverified"
                : "LRA unavailable"}
          </strong>
          <small>{stateLabel(lraState)} · LU</small>
        </div>
        <div className="metric-card" data-state={dropped > 0 ? "degraded" : "valid"}>
          <span>Analysis frames dropped</span>
          <strong>
            {dropped > 0 ? `Analysis degraded — ${dropped} frames dropped` : "0"}
          </strong>
          <small>{dropped > 0 ? "Degraded" : "Healthy"}</small>
        </div>
      </section>

      <section className="panel">
        <Meter label="Sample Peak" value={metrics.samplePeakDbfs} />
        <Meter
          label={truePeakLabel}
          value={metrics.truePeakValid ? (metrics.truePeakDbtp ?? -120) : -120}
        />
        <Meter
          label="Correlation"
          value={metrics.stereoCorrelation}
          suffix=""
          minimum={-1}
          maximum={1}
        />
      </section>

      <footer className="plugin-footer">
        <span>Project {state.projectId || "not selected"}</span>
        <button className="button button--primary" onClick={() => sendCommand({ type: "analyze" })}>
          Save analysis
        </button>
      </footer>
    </main>
  );
}
