# Architecture

Mastering Audio Suite is a personal local Windows metalcore mixing ecosystem.

## Components

| Component | Status after Milestone 0 | Path |
|---|---|---|
| Analyzer VST3 | Present; honest RT labels; float+double pass-through | `apps/analyzer-plugin/` |
| Mix Node VST3 | Missing (PoC planned after Milestone 1) | — |
| Standalone Suite | Present; stem mixer + rule MixAdvisor | `apps/mix-desktop/` |
| ML Lab | Research CLI only | `ml/` |
| Installer | Portable ZIP script only | `scripts/package-windows.ps1` |

## Shared core

- `modules/audio-analysis` — offline analysis + realtime meter
- `modules/dsp` — ProcessorChain + DynamicTools (limiter still approximate)
- `modules/assistant` — MixAdvisor with absolute Action targets
- `modules/project-bridge` — `.masuite` schema v2
- `modules/ipc` — bridge payload validation
- `modules/research-export` — privacy-safe ML examples

## Data flow (current)

```text
FL Analyzer --schemaVersioned JSON--> Bridge(58432) --> Suite
Suite --Apply absolute Actions--> StemEngine DSP
stems --> Suite import (replace semantics)
Suite --optional research JSON--> ML CLI
```

## Schema versions

- Project: `kCurrentSchemaVersion = 2` (accepts 1 with migration; rejects newer)
- IPC: `kCurrentSchemaVersion = 1` (rejects missing/newer)

## Milestone order

0 Truth cleanup (this milestone) → 1 Trustworthy DSP/metrics → early installer + Mix Node PoC → FL hierarchy → Mix Pass → full Mix Node → polished UI/installer → ML Lab.
