# FL Studio — Mix Node validation checklist

Manual host validation for Milestone 2A. Automated CI does **not** replace this list.

## Environment

| Field | Value |
|---|---|
| FL Studio version | |
| Windows version | |
| Audio driver | |
| Sample rate | |
| Buffer size | |
| Suite build / SHA | |
| Tester | |
| Date | |

Result values: `PASS` | `FAIL` | `NOT TESTED`

## Checklist

| Test | Result | Notes |
|---|---|---|
| Analyzer discovered after VST3 rescan | NOT TESTED | |
| Mix Node discovered after VST3 rescan | NOT TESTED | |
| Analyzer on mono channel | NOT TESTED | |
| Analyzer on stereo channel | NOT TESTED | |
| Mix Node on mono channel | NOT TESTED | |
| Mix Node on stereo channel | NOT TESTED | |
| Rhythm Guitar Left identity preset | NOT TESTED | |
| Rhythm Guitar Right identity preset | NOT TESTED | |
| Guitar Bus identity preset | NOT TESTED | |
| External sidechain into Mix Node | NOT TESTED | FL sidechain routing specifics |
| Suite connection / instance list | NOT TESTED | |
| Preview from Suite | NOT TESTED | |
| Apply / Commit from Suite | NOT TESTED | |
| Cancel Preview restores committed | NOT TESTED | |
| Undo restores previous absolute state | NOT TESTED | |
| FL project save | NOT TESTED | |
| FL project reopen restores Mix Node | NOT TESTED | |
| Preview not persisted as committed | NOT TESTED | |
| Host automation (gain/EQ/threshold) | NOT TESTED | |
| PDC / reported latency | NOT TESTED | |
| Two FL projects isolated in Suite | NOT TESTED | |
| Suite restart; Mix Node keeps audio | NOT TESTED | |
| Plugin UI DPI | NOT TESTED | |
| WebView focus | NOT TESTED | |
| Buffer size changes | NOT TESTED | |
| Sample-rate changes | NOT TESTED | |

## FL sidechain notes

Document any FL-specific limitations discovered during manual testing (bus layout, mono SC → stereo target, etc.).

## Sign-off

Installer / FL workflow must not be marked verified until this checklist is completed on a real Windows + FL Studio machine.
