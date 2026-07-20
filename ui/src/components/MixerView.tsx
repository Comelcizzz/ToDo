import { useMemo, useState } from "react";
import { sendCommand } from "../nativeBridge";
import { trackRoleOptions, type MixPassAction, type SuiteState } from "../types";
import { MixNodesPanel } from "./MixNodesPanel";

function formatTime(seconds: number) {
  const safeSeconds = Number.isFinite(seconds) ? Math.max(0, seconds) : 0;
  const minutes = Math.floor(safeSeconds / 60);
  return `${minutes}:${Math.floor(safeSeconds % 60)
    .toString()
    .padStart(2, "0")}`;
}

function trackName(state: SuiteState, id: string) {
  return state.tracks.find((track) => track.id === id)?.name ?? id.slice(0, 8);
}

function evidenceText(action: MixPassAction) {
  if (action.evidenceLabel) return action.evidenceLabel;
  const score = action.evidenceScore ?? action.confidence;
  if (score >= 0.7) return "High evidence";
  if (score >= 0.4) return "Medium evidence";
  return "Low evidence";
}

type ActionFilter =
  | "all"
  | "critical"
  | "high-evidence"
  | "low-end"
  | "guitars"
  | "vocals"
  | "drums"
  | "sections"
  | "reference"
  | "technical";

function matchesFilter(action: MixPassAction, filter: ActionFilter): boolean {
  if (filter === "all") return true;
  const p = action.problemType.toLowerCase();
  const score = action.evidenceScore ?? action.confidence;
  switch (filter) {
    case "critical":
      return score >= 0.7 && action.state !== "rejected";
    case "high-evidence":
      return score >= 0.7 || (action.evidenceLabel ?? "").toLowerCase().includes("high");
    case "low-end":
      return p.includes("kick") || p.includes("bass") || p.includes("sub");
    case "guitars":
      return p.includes("guitar") || p.includes("fizz") || p.includes("mud") || p.includes("harsh");
    case "vocals":
      return p.includes("vocal") || p.includes("deess") || p.includes("resonance") || p.includes("ride");
    case "drums":
      return p.includes("snare") || p.includes("drum") || p.includes("parallel");
    case "sections":
      return !!action.sectionScope && action.sectionScope !== "full";
    case "reference":
      return p.includes("reference");
    case "technical":
      return p.includes("unavailable") || p.includes("qc") || p.includes("technical");
    default:
      return true;
  }
}

function ActionCard({
  action,
  state,
}: {
  action: MixPassAction;
  state: SuiteState;
}) {
  return (
    <article className="suggestion" key={action.actionId}>
      <div>
        <span>{action.problemType}</span>
        <strong>{evidenceText(action)}</strong>
      </div>
      <h3>
        {action.processorId}/{action.parameterId} → {trackName(state, action.targetTrackId)}
      </h3>
      <p>{action.explanation}</p>
      <p className="assistant__intro">
        {action.currentValue.toFixed(2)} → {action.proposedValue.toFixed(2)} (
        {action.allowedMin.toFixed(1)}…{action.allowedMax.toFixed(1)}) · {action.state}
        {action.processingLevel ? ` · ${action.processingLevel}` : ""}
        {action.sectionScope && action.sectionScope !== "full"
          ? ` · section ${action.sectionScope.slice(0, 6)}`
          : ""}
      </p>
      {action.decisionTrace && (
        <p className="assistant__intro">
          Trace: {action.decisionTrace.slice(0, 220)}
          {action.decisionTrace.length > 220 ? "…" : ""}
        </p>
      )}
      <label className="compact-control">
        <span>Edit proposed</span>
        <input
          type="range"
          min={action.allowedMin}
          max={action.allowedMax}
          step="0.1"
          value={action.proposedValue}
          onChange={(event) =>
            sendCommand({
              type: "mixpass-edit",
              actionId: action.actionId,
              proposedValue: Number(event.target.value),
            })
          }
        />
      </label>
      <div className="variant-row">
        <button
          className="button"
          disabled={action.state === "applied" || action.state === "rejected"}
          onClick={() => sendCommand({ type: "mixpass-preview", actionId: action.actionId })}
        >
          Preview
        </button>
        <button
          className="button button--primary"
          disabled={action.state === "rejected"}
          onClick={() => sendCommand({ type: "mixpass-apply", actionId: action.actionId })}
        >
          Apply
        </button>
        <button
          className="button"
          disabled={action.state === "applied" || action.state === "rejected"}
          onClick={() => sendCommand({ type: "mixpass-reject", actionId: action.actionId })}
        >
          Reject
        </button>
        <button
          className="button"
          disabled={action.state !== "previewing"}
          onClick={() =>
            sendCommand({ type: "mixpass-cancel-preview", actionId: action.actionId })
          }
        >
          Cancel
        </button>
      </div>
    </article>
  );
}

export function MixerView({ state }: { state: SuiteState }) {
  const variants = state.variants?.length
    ? state.variants
    : ["balanced", "punchy", "vocal-forward"];
  const compareMode = state.compareMode ?? "current";
  const actions = state.mixPassActions ?? [];
  const pairs = state.pairs ?? [];
  const buses = state.buses ?? [];
  const sections = state.sections ?? [];
  const [actionFilter, setActionFilter] = useState<ActionFilter>("all");
  const filteredActions = useMemo(
    () => actions.filter((action) => matchesFilter(action, actionFilter)),
    [actions, actionFilter],
  );

  return (
    <main className="desktop-layout">
      <aside className="sidebar">
        <div className="brand">
          <span className="brand__mark">MA</span>
          <div>
            <strong>Mastering Audio</strong>
            <small>Metalcore Mix Pass</small>
            <small>{state.productVersion ?? "0.4.0-alpha.m4b"}</small>
          </div>
        </div>

        <nav>
          <button onClick={() => sendCommand({ type: "create-project" })}>New project</button>
          <button onClick={() => sendCommand({ type: "open-project" })}>Open project</button>
          <button onClick={() => sendCommand({ type: "save-project" })}>
            {state.projectDirty ? "Save *" : "Save"}
          </button>
          <button onClick={() => sendCommand({ type: "save-project-as" })}>Save as</button>
          <button onClick={() => sendCommand({ type: "import-stems" })}>Import stems</button>
          <button onClick={() => sendCommand({ type: "import-reference" })}>
            Add reference
          </button>
          <button onClick={() => sendCommand({ type: "ensure-hierarchy" })}>
            Build hierarchy
          </button>
          <button
            disabled={state.tracks.length === 0}
            title="Exports metrics and approved settings only—never audio, names, or file paths"
            onClick={() => sendCommand({ type: "export-research-example" })}
          >
            Export ML example
          </button>
          <button
            title="Validate local personal benchmark session layout (no auto guitar align)"
            onClick={() => sendCommand({ type: "validate-benchmark-import" })}
          >
            Import wizard check
          </button>
          <button onClick={() => sendCommand({ type: "run-profile-experiment" })}>
            Run profile A/B
          </button>
          <button onClick={() => sendCommand({ type: "cancel-experiment" })}>
            Cancel experiment
          </button>
          <button onClick={() => sendCommand({ type: "open-local-data-folder" })}>
            Open local data
          </button>
          <button onClick={() => sendCommand({ type: "clear-benchmark-cache" })}>
            Clear cache
          </button>
          <button onClick={() => sendCommand({ type: "clear-benchmark-renders" })}>
            Clear renders
          </button>
          <button onClick={() => sendCommand({ type: "export-diagnostics" })}>
            Export diagnostics
          </button>
        </nav>

        <div className="sidebar__status">
          <span className={`status-dot ${state.connected ? "status-dot--online" : ""}`} />
          <div>
            <strong>{state.connected ? "Bridge listening" : "Bridge starting"}</strong>
            <small>Status: {state.analysisStatus ?? "idle"}</small>
            <small>
              {state.localOnly === false ? "cloud" : "local-only"} ·{" "}
              {state.metalcoreProfileId ?? "modern-metalcore-balanced"}
            </small>
            <small title={state.localDataPath}>
              Data: {state.localDataPath ? "personal benchmarks" : "unset"}
            </small>
            <small>
              Autosave: {state.autosaveStatus ?? "idle"}
              {state.lastSavedIso ? ` · ${state.lastSavedIso}` : ""}
            </small>
            <small>Schema {state.projectSchemaVersion ?? 7}</small>
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
          {(["raw", "auto", "current", "reference"] as const).map((mode) => (
            <button
              key={mode}
              className={compareMode === mode ? "button button--primary" : "button"}
              disabled={mode === "reference" && !state.hasReference}
              onClick={() => sendCommand({ type: "set-compare-mode", mode })}
              title={
                mode === "reference"
                  ? `Loudness-matched REF (${(state.referenceGainDb ?? 0).toFixed(1)} dB)`
                  : mode.toUpperCase()
              }
            >
              {mode === "reference" ? "REF" : mode.toUpperCase()}
            </button>
          ))}
        </div>

        <div className="content-grid">
          <section className="track-section panel">
            <div className="section-heading">
              <div>
                <p className="eyebrow">Tracks · pairs · buses</p>
                <h2>
                  {state.tracks.length} stems · {pairs.length} pairs · {buses.length} buses
                </h2>
              </div>
              <button className="button" onClick={() => sendCommand({ type: "import-stems" })}>
                + Import
              </button>
            </div>

            <label className="compact-control" style={{ marginBottom: "0.75rem" }}>
              <span>BPM {state.bpm?.toFixed(0) ?? 140}</span>
              <input
                type="range"
                min="60"
                max="220"
                step="1"
                value={state.bpm ?? 140}
                onChange={(event) =>
                  sendCommand({ type: "set-bpm", bpm: Number(event.target.value) })
                }
              />
            </label>

            {buses.length > 0 && (
              <div className="suggestion-list" style={{ marginBottom: "1rem" }}>
                {buses.map((bus) => (
                  <article className="suggestion" key={bus.id}>
                    <div>
                      <span>{bus.role}</span>
                      <strong>{bus.childTrackIds?.length ?? 0} tracks</strong>
                    </div>
                    <h3>{bus.name}</h3>
                  </article>
                ))}
              </div>
            )}

            {pairs.length > 0 && (
              <div className="suggestion-list" style={{ marginBottom: "1rem" }}>
                {pairs.map((pair) => (
                  <article className="suggestion" key={pair.id}>
                    <div>
                      <span>pair</span>
                      <strong>{pair.linkedProcessing ? "linked" : "split"}</strong>
                    </div>
                    <h3>{pair.name}</h3>
                    <p>
                      L: {trackName(state, pair.leftTrackId)} · R:{" "}
                      {trackName(state, pair.rightTrackId)}
                    </p>
                  </article>
                ))}
              </div>
            )}

            <div className="track-list">
              {state.tracks.length === 0 ? (
                <button
                  className="drop-zone"
                  onClick={() => sendCommand({ type: "import-stems" })}
                >
                  <strong>Drop metalcore stems here</strong>
                  <span>WAV or AIFF · assign L/R guitar roles · set BPM & sections</span>
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
                        {trackRoleOptions.map((role) => (
                          <option key={role} value={role}>
                            {role.replaceAll("-", " ")}
                          </option>
                        ))}
                      </select>
                      <small>
                        {track.channelPosition ?? "Stereo"}
                        {track.pairId ? ` · pair ${track.pairId.slice(0, 6)}` : ""}
                        {track.parentBusId ? ` · bus ${track.parentBusId.slice(0, 6)}` : ""}
                      </small>
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

            <div className="section-heading" style={{ marginTop: "1.25rem" }}>
              <div>
                <p className="eyebrow">Manual sections</p>
                <h2>{sections.length} markers</h2>
              </div>
              <button
                className="button"
                onClick={() =>
                  sendCommand({
                    type: "add-section",
                    kind: "chorus",
                    name: "Chorus",
                    startSeconds: Math.max(0, state.positionSeconds),
                    endSeconds: Math.max(1, state.positionSeconds + 8),
                  })
                }
              >
                + Chorus @ playhead
              </button>
            </div>
            <div className="variant-row" style={{ marginBottom: "0.75rem" }}>
              {(
                [
                  "intro",
                  "verse",
                  "pre-chorus",
                  "chorus",
                  "breakdown",
                  "bridge",
                  "outro",
                ] as const
              ).map((kind) => (
                <button
                  key={kind}
                  className="button"
                  onClick={() =>
                    sendCommand({
                      type: "add-section",
                      kind,
                      name: kind,
                      startSeconds: Math.max(0, state.positionSeconds),
                      endSeconds: Math.max(1, state.positionSeconds + 8),
                    })
                  }
                >
                  {kind}
                </button>
              ))}
            </div>
            <div className="suggestion-list">
              {sections.map((section) => (
                <article className="suggestion" key={section.id}>
                  <div>
                    <span>{section.kind}</span>
                    <button
                      className="button"
                      onClick={() =>
                        sendCommand({ type: "remove-section", sectionId: section.id })
                      }
                    >
                      Remove
                    </button>
                  </div>
                  <h3>{section.name}</h3>
                  <p>
                    {formatTime(section.startSeconds)} – {formatTime(section.endSeconds)}
                  </p>
                </article>
              ))}
            </div>
          </section>

          <aside className="assistant panel">
            <p className="eyebrow">Metalcore Mix Pass</p>
            <h2>Actionable DSP</h2>
            <p className="assistant__intro">
              Each suggestion is a typed Action with absolute processor state, Preview / Apply /
              Reject / Edit, and Undo. Re-Apply is idempotent.
            </p>

            <button
              className="button button--primary button--wide"
              disabled={state.tracks.length === 0}
              onClick={() => sendCommand({ type: "generate-metalcore-mix-pass" })}
            >
              Run Metalcore Mix Pass
            </button>
            <div className="variant-row">
              {(
                [
                  "modern-metalcore-balanced",
                  "modern-metalcore-aggressive",
                  "custom",
                ] as const
              ).map((profileId) => (
                <button
                  key={profileId}
                  className={
                    (state.metalcoreProfileId ?? "modern-metalcore-balanced") === profileId
                      ? "button button--primary"
                      : "button"
                  }
                  onClick={() => sendCommand({ type: "set-metalcore-profile", profileId })}
                >
                  {profileId.replace("modern-metalcore-", "")}
                </button>
              ))}
            </div>
            <p className="assistant__intro">
              Analysis: {state.analysisStatus ?? "idle"}
              {typeof state.referenceGainDb === "number"
                ? ` · REF match ${state.referenceGainDb.toFixed(1)} dB`
                : ""}
            </p>
            <p className="assistant__intro">
              Benchmark: {state.lastExperimentSummary ?? "none"} · edits{" "}
              {state.userEditEventCount ?? 0}
            </p>
            {state.recoverySummary ? (
              <p className="assistant__intro">Recovery: {state.recoverySummary.slice(0, 180)}</p>
            ) : null}
            {state.lastErrorJson ? (
              <p className="assistant__intro">Error: {state.lastErrorJson.slice(0, 160)}</p>
            ) : null}
            {state.jobQueueJson && state.jobQueueJson !== "[]" ? (
              <p className="assistant__intro">Jobs: {state.jobQueueJson.slice(0, 160)}</p>
            ) : null}
            {state.lastImportValidation && state.lastImportValidation !== "idle" ? (
              <p className="assistant__intro">Import: {state.lastImportValidation.slice(0, 160)}</p>
            ) : null}

            <div className="variant-row">
              <button
                className="button"
                disabled={!state.canUndoMixPass}
                onClick={() => sendCommand({ type: "mixpass-undo" })}
              >
                Undo
              </button>
              <button
                className="button"
                disabled={!state.canRedoMixPass}
                onClick={() => sendCommand({ type: "mixpass-redo" })}
              >
                Redo
              </button>
            </div>

            <div className="variant-row">
              {(
                [
                  "all",
                  "critical",
                  "high-evidence",
                  "low-end",
                  "guitars",
                  "vocals",
                  "drums",
                  "sections",
                  "reference",
                  "technical",
                ] as const
              ).map((filter) => (
                <button
                  key={filter}
                  className={actionFilter === filter ? "button button--primary" : "button"}
                  onClick={() => setActionFilter(filter)}
                >
                  {filter}
                </button>
              ))}
            </div>

            <div className="suggestion-list">
              {filteredActions.length === 0 ? (
                <p className="assistant__intro">No Mix Pass actions for this filter.</p>
              ) : (
                filteredActions.map((action) => (
                  <ActionCard key={action.actionId} action={action} state={state} />
                ))
              )}
            </div>

            <hr style={{ border: 0, borderTop: "1px solid #333", margin: "1rem 0" }} />
            <p className="eyebrow">Legacy advisor</p>
            <button
              className="button button--wide"
              onClick={() => sendCommand({ type: "generate-mix-plan" })}
            >
              Analyze (variants)
            </button>
            <div className="variant-row">
              {variants.map((variant) => (
                <button
                  key={variant}
                  className={
                    state.selectedVariant === variant ? "button button--primary" : "button"
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
              <>
                <button
                  className="button button--wide"
                  onClick={() => sendCommand({ type: "apply-mix-plan" })}
                >
                  Apply selected variant
                </button>
                <button
                  className="button button--wide"
                  onClick={() => sendCommand({ type: "reject-mix-plan" })}
                >
                  Reject plan
                </button>
              </>
            )}
          </aside>
        </div>

        <MixNodesPanel state={state} />
      </section>
    </main>
  );
}
