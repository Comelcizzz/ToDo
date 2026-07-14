# Feature status

Statuses (only these values): `IMPLEMENTED` | `PARTIAL` | `STUB` | `MOCK` | `MISSING`

Updated after Milestone 0 verification gate.

| Feature | Status | Evidence | Limitation |
|---|---|---|---|
| Analyzer float pass-through | IMPLEMENTED | `PassThroughPolicy` + `[milestone0][bit-transparency][float]` | NaN/Inf sanitized to 0 |
| Analyzer double pass-through | IMPLEMENTED | Double `processBlock` + `[milestone0][bit-transparency][double]` | Meter via float scratch; finite samples unchanged |
| Honest RT labels | IMPLEMENTED | UI Estimated Loudness / Sample Peak; RT JSON omits LUFS/TP | Not true LUFS/TP |
| Realtime LUFS | MISSING | — | Not computed or published |
| Realtime true peak | MISSING | — | Not computed or published |
| Offline integrated LUFS | PARTIAL | Offline analyzer path | Approximation; M1A will align to BS.1770 vectors |
| Offline estimated true peak | PARTIAL | Flagged `truePeakIsEstimate` | Cubic estimate, not BS.1770 Annex 2 |
| Stem import replace | IMPLEMENTED | `StemEngine::importFiles` + Suite no-op on empty | No multi-slot append API |
| Ghost tracks on re-import | IMPLEMENTED | Engine clear-then-replace; Suite skips empty import | — |
| MixAdvisor absolute Apply | IMPLEMENTED | Absolute `targetGainDb` + `ProcessorSettings`; re-Apply idempotent | Plan-level UI, not per-row editor polish |
| Per-action Apply | PARTIAL | Absolute Action records applied from plan list | UI applies whole plan; no single-action UI control |
| Per-action Reject | PARTIAL | Reject-before-Apply only (`pending`→`rejected`) | Reject after Apply does not revert; Undo not shipped |
| Per-action Edit | PARTIAL | Edit target then re-Apply absolute values | No dedicated edit UI; values editable in plan JSON/state |
| Persistent Action state | IMPLEMENTED | `.masuite` actions[] with state + previous snapshot | Undo command not implemented yet |
| Project schema v2 | IMPLEMENTED | `kCurrentSchemaVersion=2`; reject newer; migrate v1 | — |
| Full project restore | PARTIAL | Tracks, gains, processing, actions, master round-trip | Relative asset paths / pairs / sections not in schema yet |
| IPC bridge | PARTIAL | TCP localhost + schema validation | No heartbeat/queue isolation hardening |
| Sidecar writer | PARTIAL | Analyzer writes when Suite offline | Format still role/metrics oriented |
| Sidecar Suite reader | MISSING | — | Suite does not import sidecar files yet |
| ZIP packaging | IMPLEMENTED | CI `mastering-audio-suite-windows` portable ZIP | Not an installer |
| Actual installer | MISSING | — | Inno Setup planned after M1 / early M2 |
| Mix Node | MISSING | — | PoC after trustworthy DSP (post-1B) |
| ML CLI | PARTIAL | `ml/` research ridge pipeline | Research-only; not product MixAdvisor |
| ML Lab GUI | MISSING | — | Separate milestone |
| True-peak limiter | PARTIAL | Cubic ISP estimate ceiling helper | No oversampling/look-ahead/host latency |
| Dynamic EQ | MISSING | — | Milestone 1C |
| UI design tokens | PARTIAL | CSS tokens + loading/error/disabled/offline | Not polished product UI |
| Undo | MISSING | Previous values captured on Apply | Command not exposed |

Do not call ZIP packaging an installer. Do not call the ML CLI an ML Lab. Do not call the Analyzer bit-transparent without the bit-transparency tests above.
