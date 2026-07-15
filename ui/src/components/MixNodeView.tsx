import { sendCommand } from "../nativeBridge";
import type { SuiteState } from "../types";

export function MixNodeView({ state }: { state: SuiteState }) {
  return (
    <main className="plugin-layout mix-node-layout">
      <header className="plugin-header">
        <div>
          <p className="eyebrow">Mix Node</p>
          <h1>{state.trackName || "Mix Node"}</h1>
          <small>
            {state.role} · {state.channelPosition} · {state.status}
          </small>
          <small>{state.productVersion ?? "0.4.0-alpha.m4a"}</small>
        </div>
        <div className="header-meta">
          <span className={`status-dot ${state.connected ? "status-dot--online" : ""}`} />
          <div>
            <strong>{state.connected ? "Suite linked" : "Standalone"}</strong>
            <small>
              latency {state.latencySamples ?? 0} · rev {state.stateRevision ?? 0}
            </small>
          </div>
        </div>
      </header>

      <section className="chain panel">
        <h2>Processing chain</h2>
        <div className="chain-blocks">
          <details open>
            <summary>Input</summary>
            <label>
              Gain {Number(state.inputGainDb ?? 0).toFixed(1)} dB
              <input
                type="range"
                min={-24}
                max={24}
                step={0.1}
                value={Number(state.inputGainDb ?? 0)}
                onChange={(e) =>
                  sendCommand({
                    type: "mix-node-set-param",
                    parameterId: "inputGainDb",
                    value: Number(e.target.value),
                  })
                }
              />
            </label>
          </details>
          <details open>
            <summary>EQ</summary>
            <label>
              Frequency {Number(state.eqFreq ?? 1000).toFixed(0)} Hz
              <input
                type="range"
                min={40}
                max={12000}
                step={1}
                value={Number(state.eqFreq ?? 1000)}
                onChange={(e) =>
                  sendCommand({
                    type: "mix-node-set-param",
                    parameterId: "eqFreq",
                    value: Number(e.target.value),
                  })
                }
              />
            </label>
            <label>
              Gain {Number(state.eqGain ?? 0).toFixed(1)} dB
              <input
                type="range"
                min={-18}
                max={18}
                step={0.1}
                value={Number(state.eqGain ?? 0)}
                onChange={(e) =>
                  sendCommand({
                    type: "mix-node-set-param",
                    parameterId: "eqGain",
                    value: Number(e.target.value),
                  })
                }
              />
            </label>
          </details>
          <details open>
            <summary>Dynamic EQ</summary>
            <label>
              Threshold {Number(state.dynThreshold ?? -24).toFixed(1)} dBFS
              <input
                type="range"
                min={-60}
                max={0}
                step={0.5}
                value={Number(state.dynThreshold ?? -24)}
                onChange={(e) =>
                  sendCommand({
                    type: "mix-node-set-param",
                    parameterId: "dynThreshold",
                    value: Number(e.target.value),
                  })
                }
              />
            </label>
            <label>
              Max cut {Number(state.dynMaxCut ?? 8).toFixed(1)} dB
              <input
                type="range"
                min={0}
                max={24}
                step={0.1}
                value={Number(state.dynMaxCut ?? 8)}
                onChange={(e) =>
                  sendCommand({
                    type: "mix-node-set-param",
                    parameterId: "dynMaxCut",
                    value: Number(e.target.value),
                  })
                }
              />
            </label>
          </details>
          <details>
            <summary>Saturation</summary>
            <label>
              Drive {Number(state.satDrive ?? 1).toFixed(2)}
              <input
                type="range"
                min={1}
                max={8}
                step={0.05}
                value={Number(state.satDrive ?? 1)}
                onChange={(e) =>
                  sendCommand({
                    type: "mix-node-set-param",
                    parameterId: "satDrive",
                    value: Number(e.target.value),
                  })
                }
              />
            </label>
          </details>
          <details open>
            <summary>Output</summary>
            <label>
              Gain {Number(state.outputGainDb ?? 0).toFixed(1)} dB
              <input
                type="range"
                min={-24}
                max={24}
                step={0.1}
                value={Number(state.outputGainDb ?? 0)}
                onChange={(e) =>
                  sendCommand({
                    type: "mix-node-set-param",
                    parameterId: "outputGainDb",
                    value: Number(e.target.value),
                  })
                }
              />
            </label>
          </details>
        </div>
      </section>

      <section className="meters panel">
        <div>
          <span>In</span>
          <strong>{Number(state.inputPeakDb ?? -120).toFixed(1)} dB</strong>
        </div>
        <div>
          <span>GR</span>
          <strong>{Number(state.grDb ?? 0).toFixed(2)} dB</strong>
        </div>
        <div>
          <span>Out</span>
          <strong>{Number(state.outputPeakDb ?? -120).toFixed(1)} dB</strong>
        </div>
        <div>
          <span>SC</span>
          <strong>{Number(state.sidechainPeakDb ?? -120).toFixed(1)} dB</strong>
        </div>
      </section>

      <div className="button-row">
        <button
          className="button"
          onClick={() => sendCommand({ type: "mix-node-cancel-preview" })}
        >
          Cancel preview
        </button>
        <button className="button" onClick={() => sendCommand({ type: "mix-node-undo" })}>
          Undo
        </button>
        <button
          className="button button--primary"
          onClick={() =>
            sendCommand({
              type: "mix-node-set-param",
              parameterId: "bypass",
              value: state.bypass ? 0 : 1,
            })
          }
        >
          {state.bypass ? "Enable" : "Bypass"}
        </button>
      </div>
    </main>
  );
}
