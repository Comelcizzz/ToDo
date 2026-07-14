# Feature status

Statuses (only these values): `IMPLEMENTED` | `PARTIAL` | `STUB` | `MOCK` | `MISSING`

Updated after Milestone 0 verification + Milestone 1A metering start.

| Feature | Status | Evidence | Limitation |
|---|---|---|---|
| Analyzer float pass-through | IMPLEMENTED | `PassThroughPolicy` + bit-transparency tests | NaN/Inf → 0 |
| Analyzer double pass-through | IMPLEMENTED | Double `processBlock` + tests | Meter via float scratch |
| Shared LoudnessMeter core | IMPLEMENTED | `LoudnessMeter` used by offline + RT | Official EBU WAVs not bundled |
| Sample peak | IMPLEMENTED | Analyzer + tests | — |
| True-peak meter (4×) | PARTIAL | `truePeakValid` + ISP relative test | Not yet validated on official Annex 2 vectors |
| Momentary / short-term / integrated LUFS | PARTIAL | Synthetic −23 tone ±0.5 LU; invariance tests | Official Tech 3341 vectors pending local fetch |
| LRA | PARTIAL | Computed in `finalize()` | Needs Tech 3342 official vectors |
| RT / offline shared metering | PARTIAL | Same `LoudnessMeter`; ST/M match test | Integrated finalize differs on RT stream |
| Honest RT labels | IMPLEMENTED | Validity-gated LUFS/TP fields | Empty UI shows — until window valid |
| Stem import replace | IMPLEMENTED | Engine clear-then-replace | — |
| MixAdvisor absolute Apply | IMPLEMENTED | Absolute gain + ProcessorSettings | Plan-level UI |
| Per-action Apply | PARTIAL | Absolute Action list | No single-action UI control |
| Per-action Reject | PARTIAL | Reject-before-Apply only | No Undo yet |
| Per-action Edit | PARTIAL | Edit targets then re-Apply | No dedicated edit UI |
| Persistent Action state | IMPLEMENTED | `.masuite` actions + previous snapshot | Undo command missing |
| Project schema v2 | IMPLEMENTED | Reject newer; migrate v1 | — |
| Full project restore | PARTIAL | Tracks/gains/processing/actions | No pairs/sections yet |
| IPC bridge | PARTIAL | Schema validation | No heartbeat/queue hardening |
| Sidecar writer | PARTIAL | Analyzer offline write | — |
| Sidecar Suite reader | MISSING | — | — |
| ZIP packaging | IMPLEMENTED | CI `mastering-audio-suite-windows` | Not an installer; WebView2 Runtime not bundled |
| Actual installer | MISSING | — | After M1 / early M2 |
| Mix Node | MISSING | — | After 1B |
| ML CLI | PARTIAL | `ml/` research | Not MixAdvisor |
| ML Lab GUI | MISSING | — | — |
| True-peak limiter | PARTIAL | Legacy cubic helper | Milestone 1B replacement |
| Dynamic EQ | MISSING | — | Milestone 1C |
| Undo | MISSING | Previous values captured | Command not exposed |

ZIP packaging ≠ installer. ML CLI ≠ ML Lab.
