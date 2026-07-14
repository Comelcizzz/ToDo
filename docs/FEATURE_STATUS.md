# Feature status

Statuses: `IMPLEMENTED` | `PARTIAL` | `STUB` | `MOCK` | `MISSING`

Updated after Milestone 0.

| Feature | Status | Notes |
|---|---|---|
| Analyzer float pass-through | IMPLEMENTED | Sanitizes NaN/Inf only |
| Analyzer double pass-through | IMPLEMENTED | Meter via float scratch; supportsDoublePrecisionProcessing=true |
| Realtime LUFS | MISSING | Not published; UI shows Estimated Loudness (RMS-derived) |
| Realtime true peak | MISSING | Not published; UI shows Sample Peak |
| Offline integrated LUFS | PARTIAL | K-weight + gating approximation |
| Offline estimated true peak | PARTIAL | Cubic estimate, flagged `truePeakIsEstimate` |
| Stem import replace | IMPLEMENTED | Non-empty replaces; empty/failed is no-op |
| Ghost tracks on re-import | FIXED | Engine clears before replace |
| MixAdvisor absolute Apply | IMPLEMENTED | `targetGainDb`; re-Apply idempotent |
| Per-suggestion Apply/Reject/Edit | PARTIAL | Plan-level Apply/Reject only |
| Undo/Redo | MISSING | |
| Project schema v2 | IMPLEMENTED | Actions + selectedVariant persisted |
| IPC schema validation | PARTIAL | Version + type/role/metrics + anti-fake-LUFS/TP |
| Sidecar write | PARTIAL | Written; Suite import still missing |
| True-peak limiter | PARTIAL | Cubic ISP estimate, no oversampling |
| Dynamic EQ | MISSING | |
| Mix Node | MISSING | PoC after M1 |
| Installer | MISSING | ZIP only; early Inno after M1 |
| ML Lab UI | MISSING | Research CLI only |
| Section-aware processing | MISSING | |
| UI design tokens | PARTIAL | Tokens + disabled/offline states; not polished UI |

Do not call the product production-ready while any critical metering/DSP row above is MOCK, PARTIAL, or MISSING for the claimed capability.
