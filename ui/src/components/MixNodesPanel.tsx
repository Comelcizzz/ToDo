import { sendCommand } from "../nativeBridge";
import type { MixNodeInfo, SuiteState } from "../types";

export function MixNodesPanel({ state }: { state: SuiteState }) {
  const nodes = state.mixNodes ?? [];
  const selectedId = state.selectedMixNodeId ?? nodes[0]?.instanceId ?? "";
  const selected = nodes.find((n) => n.instanceId === selectedId) ?? nodes[0];

  return (
    <section className="panel mix-nodes-panel">
      <div className="panel__header">
        <div>
          <p className="eyebrow">Mix Node vertical slice</p>
          <h2>Connected Mix Nodes</h2>
        </div>
        <small>{nodes.length} instance(s)</small>
      </div>

      {nodes.length === 0 ? (
        <p className="muted">
          No Mix Node plugins connected. Insert Mastering Audio Mix Node on FL tracks and
          ensure Suite bridge is listening.
        </p>
      ) : (
        <div className="mix-nodes-grid">
          <ul className="mix-node-list">
            {nodes.map((node) => (
              <li key={node.instanceId}>
                <button
                  className={node.instanceId === selected?.instanceId ? "is-active" : ""}
                  onClick={() =>
                    sendCommand({ type: "select-mix-node", instanceId: node.instanceId })
                  }
                >
                  <strong>{node.trackName || "Mix Node"}</strong>
                  <span>
                    {node.role} · {node.channelPosition} ·{" "}
                    {node.connected ? "online" : "offline"}
                  </span>
                </button>
              </li>
            ))}
          </ul>

          {selected && <MixNodeActionControls node={selected} />}
        </div>
      )}
    </section>
  );
}

function MixNodeActionControls({ node }: { node: MixNodeInfo }) {
  const preview = (processorId: string, parameterId: string, proposedValue: number) =>
    sendCommand({
      type: "mix-node-preview",
      instanceId: node.instanceId,
      processorId,
      parameterId,
      previousValue: 0,
      proposedValue,
      explanation: "Suite preview",
    });

  const commit = (processorId: string, parameterId: string, proposedValue: number) =>
    sendCommand({
      type: "mix-node-commit",
      instanceId: node.instanceId,
      processorId,
      parameterId,
      previousValue: 0,
      proposedValue,
      explanation: "Suite apply",
    });

  return (
    <div className="mix-node-actions">
      <header>
        <strong>{node.trackName}</strong>
        <span>
          rev {node.stateRevision} · latency {node.latencySamples} · {node.status}
        </span>
      </header>

      <label>
        Input gain (dB)
        <input
          type="range"
          min={-12}
          max={12}
          step={0.1}
          defaultValue={node.inputGainDb ?? 0}
          onMouseUp={(e) =>
            commit("inputGain", "gainDb", Number((e.target as HTMLInputElement).value))
          }
          onTouchEnd={(e) =>
            commit("inputGain", "gainDb", Number((e.target as HTMLInputElement).value))
          }
        />
      </label>

      <label>
        Static EQ gain (dB)
        <input
          type="range"
          min={-12}
          max={12}
          step={0.1}
          defaultValue={node.eqGain ?? 0}
          onChange={(e) => preview("staticEq", "gainDb", Number(e.target.value))}
          onMouseUp={(e) =>
            commit("staticEq", "gainDb", Number((e.target as HTMLInputElement).value))
          }
        />
      </label>

      <label>
        DynEQ threshold (dBFS)
        <input
          type="range"
          min={-48}
          max={0}
          step={0.5}
          defaultValue={node.dynThreshold ?? -24}
          onChange={(e) => preview("dynamicEq", "thresholdDb", Number(e.target.value))}
          onMouseUp={(e) =>
            commit("dynamicEq", "thresholdDb", Number((e.target as HTMLInputElement).value))
          }
        />
      </label>

      <label>
        Saturation drive
        <input
          type="range"
          min={1}
          max={4}
          step={0.05}
          defaultValue={node.satDrive ?? 1}
          onMouseUp={(e) =>
            commit("saturation", "drive", Number((e.target as HTMLInputElement).value))
          }
        />
      </label>

      <div className="button-row">
        <button
          className="button"
          onClick={() => sendCommand({ type: "mix-node-cancel-preview", instanceId: node.instanceId })}
        >
          Cancel preview
        </button>
        <button
          className="button"
          onClick={() => sendCommand({ type: "mix-node-undo", instanceId: node.instanceId })}
        >
          Undo
        </button>
        <button
          className="button"
          onClick={() =>
            commit("bypass", "enabled", node.bypass ? 0 : 1)
          }
        >
          {node.bypass ? "Enable" : "Bypass"}
        </button>
        <button
          className="button button--primary"
          onClick={() =>
            sendCommand({ type: "mix-node-request-state", instanceId: node.instanceId })
          }
        >
          Refresh host state
        </button>
      </div>
    </div>
  );
}
