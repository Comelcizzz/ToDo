import { sendCommand } from "../nativeBridge";
import type { SuiteState } from "../types";

function formatTime(seconds: number) {
  const safeSeconds = Number.isFinite(seconds) ? Math.max(0, seconds) : 0;
  const minutes = Math.floor(safeSeconds / 60);
  return `${minutes}:${Math.floor(safeSeconds % 60)
    .toString()
    .padStart(2, "0")}`;
}

export function MixerView({ state }: { state: SuiteState }) {
  const variants = state.variants?.length
    ? state.variants
    : ["balanced", "punchy", "vocal-forward"];
  const monitoringReference = state.monitorSource === "reference";

  return (
    <main className="desktop-layout">
      <aside className="sidebar">
        <div className="brand">
          <span className="brand__mark">MA</span>
          <div>
            <strong>Mastering Audio</strong>
            <small>Stem Mix Suite</small>
          </div>
        </div>

        <nav>
          <button onClick={() => sendCommand({ type: "create-project" })}>New project</button>
          <button onClick={() => sendCommand({ type: "open-project" })}>Open project</button>
          <button onClick={() => sendCommand({ type: "import-stems" })}>Import stems</button>
          <button onClick={() => sendCommand({ type: "import-reference" })}>
            Add reference
          </button>
        </nav>

        <div className="sidebar__status">
          <span className={`status-dot ${state.connected ? "status-dot--online" : ""}`} />
          <div>
            <strong>{state.connected ? "Bridge listening" : "Bridge starting"}</strong>
            <small>FL Studio analyzers sync locally</small>
          </div>
        </div>
      </aside>

      <section className="workspace">
        <header className="workspace-header">
          <div>
            <p className="eyebrow">Current mix</p>
            <h1>{state.projectName || "Untitled Mix"}</h1>
          </div>
          <div className="header-actions">
            <button className="button" onClick={() => sendCommand({ type: "save-project" })}>
              Save
            </button>
            <button
              className="button"
              onClick={() => sendCommand({ type: "export-master", bitsPerSample: 24 })}
            >
              Export 24-bit
            </button>
            <button
              className="button button--primary"
              onClick={() => sendCommand({ type: "export-master", bitsPerSample: 32 })}
            >
              Export 32f
            </button>
          </div>
        </header>

        <div className="transport panel">
          <button
            className="transport__play"
            aria-label={state.playing ? "Pause" : "Play"}
            onClick={() => sendCommand({ type: "toggle-playback" })}
          >
            {state.playing ? "Ⅱ" : "▶"}
          </button>
          <span>{formatTime(state.positionSeconds)}</span>
          <div className="transport__timeline">
            <span
              style={{
                width: `${
                  state.durationSeconds > 0
                    ? (state.positionSeconds / state.durationSeconds) * 100
                    : 0
                }%`,
              }}
            />
          </div>
          <span>{formatTime(state.durationSeconds)}</span>
          <button
            className={monitoringReference ? "button button--primary" : "button"}
            disabled={!state.hasReference}
            onClick={() => sendCommand({ type: "toggle-ab" })}
            title={
              state.hasReference
                ? `Loudness-matched A/B (${(state.referenceGainDb ?? 0).toFixed(1)} dB)`
                : "Import a reference first"
            }
          >
            {monitoringReference ? "REF" : "MIX"}
          </button>
        </div>

        <div className="content-grid">
          <section className="track-section panel">
            <div className="section-heading">
              <div>
                <p className="eyebrow">Split mixer tracks</p>
                <h2>{state.tracks.length} stems</h2>
              </div>
              <button className="button" onClick={() => sendCommand({ type: "import-stems" })}>
                + Import
              </button>
            </div>

            <div className="track-list">
              {state.tracks.length === 0 ? (
                <button
                  className="drop-zone"
                  onClick={() => sendCommand({ type: "import-stems" })}
                >
                  <strong>Drop exported FL Studio stems here</strong>
                  <span>WAV or AIFF, all beginning at the same timestamp</span>
                </button>
              ) : (
                state.tracks.map((track) => (
                  <article className="track-row track-row--extended" key={track.id}>
                    <span className={`role role--${track.role}`}>{track.role.slice(0, 2)}</span>
                    <div className="track-row__identity">
                      <strong>{track.name}</strong>
                      <select
                        value={track.role}
                        onChange={(event) =>
                          sendCommand({
                            type: "set-role",
                            trackId: track.id,
                            role: event.target.value,
                          })
                        }
                      >
                        <option value={track.role}>{track.role.replaceAll("-", " ")}</option>
                        <option value="custom">custom</option>
                        <option value="kick">kick</option>
                        <option value="snare">snare</option>
                        <option value="bass">bass</option>
                        <option value="rhythm-guitar">rhythm guitar</option>
                        <option value="clean-vocal">clean vocal</option>
                        <option value="scream-vocal">scream vocal</option>
                        <option value="synth">synth</option>
                        <option value="effects">effects</option>
                      </select>
                    </div>
                    <label className="compact-control">
                      <span>Gain {track.gainDb.toFixed(1)} dB</span>
                      <input
                        type="range"
                        min="-18"
                        max="12"
                        step="0.1"
                        value={track.gainDb}
                        onChange={(event) =>
                          sendCommand({
                            type: "set-track-gain",
                            trackId: track.id,
                            value: Number(event.target.value),
                          })
                        }
                      />
                    </label>
                    <label className="compact-control">
                      <span>Pan {track.pan.toFixed(2)}</span>
                      <input
                        type="range"
                        min="-1"
                        max="1"
                        step="0.01"
                        value={track.pan}
                        onChange={(event) =>
                          sendCommand({
                            type: "set-track-pan",
                            trackId: track.id,
                            value: Number(event.target.value),
                          })
                        }
                      />
                    </label>
                    <button
                      className={track.polarityInverted ? "toggle toggle--active" : "toggle"}
                      onClick={() =>
                        sendCommand({
                          type: "toggle-track",
                          trackId: track.id,
                          field: "polarityInverted",
                        })
                      }
                    >
                      Ø
                    </button>
                    <button
                      className={track.muted ? "toggle toggle--active" : "toggle"}
                      onClick={() =>
                        sendCommand({ type: "toggle-track", trackId: track.id, field: "muted" })
                      }
                    >
                      M
                    </button>
                    <button
                      className={track.soloed ? "toggle toggle--solo" : "toggle"}
                      onClick={() =>
                        sendCommand({ type: "toggle-track", trackId: track.id, field: "soloed" })
                      }
                    >
                      S
                    </button>
                  </article>
                ))
              )}
            </div>
          </section>

          <aside className="assistant panel">
            <p className="eyebrow">Mix assistant</p>
            <h2>Explain every move</h2>
            <p className="assistant__intro">
              Role-aware analysis finds measurable conflicts. Changes remain bounded and
              reversible.
            </p>

            <button
              className="button button--primary button--wide"
              onClick={() => sendCommand({ type: "generate-mix-plan" })}
            >
              Analyze mix
            </button>

            <div className="variant-row">
              {variants.map((variant) => (
                <button
                  key={variant}
                  className={
                    state.selectedVariant === variant
                      ? "button button--primary"
                      : "button"
                  }
                  onClick={() => sendCommand({ type: "select-variant", variant })}
                >
                  {variant}
                </button>
              ))}
            </div>

            <div className="suggestion-list">
              {state.suggestions.map((suggestion) => (
                <article className="suggestion" key={`${suggestion.trackId}-${suggestion.title}`}>
                  <div>
                    <span>{suggestion.kind}</span>
                    <strong>{Math.round(suggestion.confidence * 100)}%</strong>
                  </div>
                  <h3>{suggestion.title}</h3>
                  <p>{suggestion.explanation}</p>
                </article>
              ))}
            </div>

            {state.suggestions.length > 0 && (
              <button
                className="button button--wide"
                onClick={() => sendCommand({ type: "apply-mix-plan" })}
              >
                Apply selected variant
              </button>
            )}
          </aside>
        </div>
      </section>
    </main>
  );
}
