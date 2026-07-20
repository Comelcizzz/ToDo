# M4B Reliability Audit

| Area | Current behavior | Failure risk | Decision |
|---|---|---|---|
| create project | Resets in-memory model; clears MixPass stacks | Unsaved work discarded silently | Keep create; add dirty warning before discard |
| open project | Deserialize + loadProject; no undo clear on open | Stale undo IDs; missing stems silent | Clear stacks; report missing assets; migrate via pipeline |
| save | Direct `replaceWithText` | Crash/disk-full corrupts `.masuite` | Atomic temp→validate→flush→rename + backup |
| save as | Only when path empty | Cannot save copy after first save | Dedicated save-as command |
| autosave | None | Crash loses work | Interval + event triggers; debounce; never commit Preview |
| crash recovery | None | Lost edits | Scan autosave/incomplete; recovery UI choices |
| schema migration | Soft bump 1–6 → 6 | Silent defaults; no unsupported reject | Central pipeline; backup; refuse newer unsupported |
| missing assets | Silent empty playback | Wrong/missing audio | Relink by fingerprint; mark unavailable |
| moved assets | Breaks absolute paths | Same | Fingerprint search + manual relink |
| external references | Path string only | Same | Relink + invalidate analysis cache |
| analysis cache | In-memory; stub version field | Stale after asset change | Invalidate on fingerprint change |
| render cache | None formal | Partial WAVs look complete | Temp output + integrity before rename |
| temporary previews | MixPass preview in memory | Recovery could commit preview | Exclude preview from autosave committed state |
| experiment state | Ad-hoc JSON under AppData | Incomplete experiments | Job system + interrupted status |
| benchmark sessions | Local folder | Unrelated to project crash | Atomic manifest writes |
| user edits | LocalUserEditLog in memory | Lost on crash | Persist via atomic log append |
| action history | Session-only undo stacks | Lost on reopen | Document session-only; clear on open; exact absolute undo |
| export jobs | Sync StemEngine::renderMaster | Partial WAV after delete dest | Snapshot queue + temp + QC integrity |

Implementation follows this table in `modules/reliability/` + Suite wiring.
