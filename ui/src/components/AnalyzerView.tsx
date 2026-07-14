import { sendCommand } from "../nativeBridge";
import { emptyMetrics, type SuiteState, type TrackRole } from "../types";
import { Meter } from "./Meter";

const roles: TrackRole[] = [
  "kick",
  "snare",
  "drums",
  "bass",
  "rhythm-guitar",
  "lead-guitar",
  "clean-vocal",
  "scream-vocal",
  "backing-vocal",
  "synth",
  "orchestra",
  "effects",
  "custom",
];

export function AnalyzerView({ state }: { state: SuiteState }) {
  const metrics = state.analyzerMetrics ?? emptyMetrics;
  const estimatedLoudness = metrics.estimatedLoudnessIsValid
    ? (metrics.estimatedLoudnessDb ?? metrics.rmsDbfs)
    : metrics.rmsDbfs;

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
        <div className="metric-card">
          <span>Estimated Loudness</span>
          <strong>{estimatedLoudness.toFixed(1)}</strong>
          <small>dBFS RMS-derived</small>
        </div>
        <div className="metric-card">
          <span>Sample Peak</span>
          <strong>{metrics.samplePeakDbfs.toFixed(1)}</strong>
          <small>dBFS</small>
        </div>
        <div className="metric-card">
          <span>Crest</span>
          <strong>{metrics.crestFactorDb.toFixed(1)}</strong>
          <small>dB</small>
        </div>
        <div className="metric-card">
          <span>Stereo</span>
          <strong>{metrics.stereoCorrelation.toFixed(2)}</strong>
          <small>correlation</small>
        </div>
      </section>

      <section className="panel">
        <Meter label="Sample Peak" value={metrics.samplePeakDbfs} />
        <Meter label="Estimated Loudness" value={estimatedLoudness} />
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
