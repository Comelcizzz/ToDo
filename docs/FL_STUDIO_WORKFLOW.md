# FL Studio workflow

## 1. Analyze buses while arranging

1. Open **Mastering Audio Suite** first so the local bridge listens on `127.0.0.1:58432`.
2. In FL Studio Mixer, insert **Mastering Audio Analyzer** on each important bus:
   - Kick, Snare, Drums/OH, Bass, Rhythm Guitars, Lead Vocals, Screams, Synths/FX
3. In the plugin UI choose the matching **signal role**.
4. Play the full song once. The analyzer is pass-through and never changes the audio.
5. Click **Save analysis**. Metrics sync into the open Suite project when connected, or write a sidecar under Documents / `Mastering Audio Reports`.

## 2. Export stems

1. In FL Studio use **File → Export → Wave file**.
2. Enable **Split mixer tracks**.
3. Export at the session sample rate, 24-bit or 32-bit float WAV.
4. Keep all stems beginning at the same song start.

## 3. Mix and master in the Suite

1. Create or open a Suite project.
2. Import the exported stems. Roles are inferred from filenames and can be corrected.
3. Use gain, pan, polarity, mute, and solo while auditioning synchronized playback.
4. Click **Analyze mix**, then choose a bounded variant: balanced, punchy, or vocal-forward.
5. Optionally import a reference and toggle loudness-matched MIX/REF A/B before judging tone.
6. Apply the selected plan, then export a master as 24-bit PCM or 32-bit float WAV.

## Guardrails

- Auto-trim is capped at ±9 dB
- Master reference EQ moves are capped at ±1.5 dB
- Mono/true-peak issues are reported, not silently ignored
- Every applied DSP stage has bypass and amount control
