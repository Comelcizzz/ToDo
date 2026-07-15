# Milestone 2A — Evidence Report

## A. Commits

| Field | Value |
|---|---|
| Branch | `cursor/mastering-audio-932f` |
| PR | https://github.com/Comelcizzz/ToDo/pull/1 |
| Implementation SHA | `e4d2d995f1046be56dcc51a2aa005d94c37377cb` |
| Tip SHA | `a5e253cdc096772766f8e8412a929f68b406d571` (packaging fix + docs) |
| CI (green) | https://github.com/Comelcizzz/ToDo/actions/runs/29388472018 |

## B. Mix Node architecture

| Component | File/class | Shared DSP | Latency | Status |
|---|---|---|---:|---|
| Mix Node chain | `MixNodeChain` | Input→Static EQ→DynEQ→Sat→Out | sat OS | IMPLEMENTED |
| Controller | `MixNodeController` | preview/commit/undo | — | IMPLEMENTED |
| Protocol | `MixNodeProtocol` | versioned envelopes | — | IMPLEMENTED |
| VST3 | `MasteringAudioMixNode` / `MixNodeProcessor` | uses chain | reported | IMPLEMENTED |
| Suite panel | `MixNodesPanel` | actions over IPC | — | IMPLEMENTED |
| Analyzer | unchanged | pass-through | 0 | KEPT |

## C. Audio bus layouts

| Main input | Output | Sidechain | Status |
|---|---|---|---|
| mono | mono | optional mono/stereo | IMPLEMENTED |
| stereo | stereo | optional mono/stereo | IMPLEMENTED |
| surround / 5.1 | — | — | OUT OF SCOPE |

## D. Action protocol

Messages: Hello, RegisterInstance, Heartbeat, StateSnapshot, PreviewAction, CommitAction, CancelPreview, RequestState, StateUpdated, Error, Disconnect, Undo.

- Validates project/session/instance IDs
- Duplicate Commit → idempotent
- Preview does not mutate committed; Cancel restores; disconnect cancels preview
- Conflict: local edit marks stale vs Suite commit

## E. State persistence

| Scenario | Expected | Actual | Result |
|---|---|---|---|
| Save while previewing | committed only | `hasPreview:false` | PASS (unit) |
| Restore | absolute chain | matches | PASS (unit) |
| Newer schema | reject safely | error code schema | PASS (unit) |
| FL reopen | host SoT | | NOT VERIFIED IN FL STUDIO |

## F. DSP integration

| Processor | Test | Audio changed | Result |
|---|---|---|---|
| Input Gain | +6 dB sine | yes | PASS |
| Static EQ | +9 dB @ 1 kHz | yes | PASS |
| Dynamic EQ | external SC | ducks | PASS |
| Saturation | shared OS processor | wired | PASS |

## G. Latency

| Configuration | Reported | Measured | Delta | Result |
|---|---:|---:|---:|---|
| Static EQ / DynEQ | 0 | 0 | 0 | PASS |
| Saturation 1× / 4× | OS latency | impulse | ≤1 | see validation JSON |

## H. Realtime safety

- Audio callback: no mutex, socket, JSON, file I/O
- State publish via `atomic<shared_ptr>` snapshot
- Oversized blocks chunked
- IPC on bridge/timer thread

## I. Suite integration

- Instance discovery via RegisterInstance / Heartbeat
- Host DSP is source of truth; Suite stores orchestration
- Preview / Apply / Cancel / Undo / Refresh host state panel

## J. Installer

| Item | Status |
|---|---|
| Technology | Inno Setup 6 (`scripts/windows-installer.iss`) |
| Portable ZIP | `MasteringAudioSuite-Portable-x64.zip` |
| Setup exe | built in Windows CI when ISCC available |
| Install paths | Suite → Program Files; VST3 → `%CommonProgramFiles%\VST3` |
| Uninstall | removes binaries; keeps user projects |
| Signing | **unsigned** |
| Manual verify | **NOT VERIFIED** — see `docs/FL_MIX_NODE_VALIDATION.md` |

## K. FL Studio validation

All host tests: **NOT VERIFIED IN FL STUDIO** (checklist prepared).

## L. Performance

See `artifacts/mix_node_validation/benchmark_smoke.md`. RTF = audio_s / wall_s. Hardware: CI x86_64.

## M. Artifacts

- VST3 Mix Node + Analyzer
- Portable ZIP + optional Setup.exe
- `artifacts/mix_node_validation/**`
- hashes in `validation.md`

## N. Limitations

- No Metalcore Mix Pass / automatic actions / section awareness
- No reference matching redesign / ML Lab
- No advanced routing / M/S processing
- No macOS
- Unsigned installer; FL manual tests outstanding
- Saturation OS factor changes deferred to prepare (structural)

## O. Gate decision

**`MILESTONE 2A REMAINS PARTIAL`**

Reason: automated Mix Node DSP/protocol/persistence/CI path is implemented, but FL Studio manual validation and signed/manual installer verification are outstanding. Do not start Metalcore Mix Pass / ML Lab without separate confirmation after a green Windows CI and explicit acceptance of PARTIAL or a follow-up FL verification pass.
