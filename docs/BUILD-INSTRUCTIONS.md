# Build instructions: FALLEN, rhythm-system and FM modules for VCV Rack in the manner of Mark Fell

**Audience:** an Opus model implementing this plugin from scratch.
**Owner:** Hardy (also Haer). Fluent in VCV Rack, does not read C++. Build the whole plugin in one go, test it yourself, and hand over the finished result. No milestone check-ins.

**What it is:** a VCV Rack 2 plugin, brand and slug `Fallen`, with 10 modules for making music the way Mark Fell makes it: small deterministic pattern systems timed in milliseconds rather than beats, with one control that stretches a whole pattern; cycles of different lengths drifting in and out of phase; the length of each sound treated as a parameter; and dry 4-operator FM voices and drum-machine drums. Background research and the reason for each module are in `docs/RESEARCH-AND-PROPOSAL.md` (copy of the proposal file). Read it first.

**Repository:** https://github.com/hardyfinn/Fallen. This spec is committed on `main` as `docs/BUILD-INSTRUCTIONS.md`. Build on `main`.

**Hardy's decisions (2026-11-15), which override anything else:**

| Question | Answer |
|---|---|
| Plugin name | **Fallen** (slug, brand and name). Personal plugin, not for the VCV Library. |
| Module set | **All 10** modules in §2. |
| FM4 | **Must load SysEx**: 4-operator Yamaha voice data (`.syx`, 32-voice banks and single voices), as well as having its own factory bank. §8. |
| KIT | **Must load WAV files** per drum voice (and a whole kit from a folder), as well as having synthesised voices. §10. |
| Names | "Mark Fell", his album titles (Multistability, UL8, Sensate Focus, Intra...) and "Yamaha", "TX81Z", "DX", "Linn", "Roland", "808", "909" never appear in plugin, module, panel, preset or tag names. The manual may name them when describing what inspired a module or what file format is read. |
| Labels | Text labels on every control and jack, no symbols. |
| Search | Short module names findable in Rack's browser (§1). |
| Builds | **Apple silicon Mac only** (macOS arm64) in CI, plus host unit tests on Linux. No Intel Mac, Windows or Linux plugin builds unless Hardy asks. |
| Manual | `docs/MANUAL.md` and `docs/Fallen-Manual.pdf`, with patch recipes. |

---

## 0. Ground rules

1. **Deterministic, never random.** Fell: "Nothing from last night was random." Anything that varies (SPREAD, priority orders, factory pattern generators) uses a seeded hash so the same settings always give the same result. The only exception is KIT's noise sources, which are audio noise, not decisions.
2. **Time in seconds, sample-accurate.** All timing modules count time in `double` seconds and fire events on the exact sample where the scheduled time falls (not rounded to blocks). Behaviour must be identical at 44.1, 48, 96 and 192 kHz to within one sample.
3. **DSP and engines separate from Rack.** Everything in `src/dsp/`, `src/time/` and `src/io/` is plain C++17 with no Rack includes, so it can be unit tested on the host. `src/modules/*.cpp` are thin wrappers.
4. **No allocation, locks, file I/O or logging in `process()`.** File loading (SysEx, WAV) happens on the UI thread or a worker thread; the result is handed to the audio thread by an atomic pointer swap of a fully built object (the old one is freed on the UI thread later, never in `process()`).
5. **Every param has a tooltip and unit; every port has a name** (`configParam`, `configSwitch` with a label per position, `configInput`, `configOutput`). Tooltip names are the panel labels spelled out in full.
6. **Stability.** Every state variable is clamped; every `process()` ends with an `isfinite` guard that resets that module's state. Outputs within ±10 V; audio soft-clipped at ±10 V.
7. **Ambiguity:** choose what best serves the behaviour described in `RESEARCH-AND-PROPOSAL.md`, write it in `docs/DECISIONS.md`, list it in the hand-over.
8. **CPU at 48 kHz, one core:** FM4 with 8 voices under 3%, SPECTRA (4 voices × 32 partials) under 3%, KIT under 1.5%, all timing modules under 0.3%.
9. Licence GPL-3.0-or-later. Third-party code allowed only with a compatible licence, credited in `docs/CREDITS.md`. `dr_wav` (public domain / MIT-0) is the WAV reader. `osdialog` (ships with Rack) for file dialogs.
10. **Polyphony** only where a section says "poly".
11. **Everything the user makes is saved in the patch** (`dataToJson`/`dataFromJson`): DURA steps, LANES grids, CYCLE lanes, CHORD shapes, FM4's working bank of 32 voices (so a patch reopens with its sounds even if the `.syx` file is gone), KIT's sample paths (§10.6 for missing files).

---

## 1. Setup

- **VCV Rack 2.x**, newest 2.x Rack SDK. Read https://vcvrack.com/manual/PluginDevelopmentTutorial, https://vcvrack.com/manual/Building, https://vcvrack.com/manual/Panel, https://vcvrack.com/manual/DSP and https://vcvrack.com/manual/Polyphony first.
- `export RACK_DIR=/path/to/Rack-SDK`, then `make`, `make dist`, `make install`.
- Unit tests with **doctest** in `tests/`. `make test` builds `tests/*.cpp` with `src/dsp`, `src/time` and `src/io` as a host executable and runs it.
- `.github/workflows/build.yml`: **macOS arm64 only** with the official `rack-plugin-toolchain` (or the community `build-plugin` action based on it), plus a Linux job that runs `make test`. Upload the `.vcvplugin` as an artifact on every push and as a release asset on tags `v*`. Reuse the working workflow from `hardyfinn/Tudor` (mac-arm64 + Linux host tests) if it is reachable. In earlier projects the Rack SDK zip was blocked from the cloud container but Rack headers could be fetched from `raw.githubusercontent.com/VCVRack/Rack/v2.6.4/include` plus apt `glew/glfw/jansson/speexdsp`, which is enough for `g++ -fsyntax-only` checks before pushing.
- `README.md`: what the plugin is, install steps (drop the `.vcvplugin` into Rack's `plugins-mac-arm64` user folder).

```
plugin.json   Makefile   README.md   LICENSE
docs/   BUILD-INSTRUCTIONS.md  RESEARCH-AND-PROPOSAL.md  DECISIONS.md  CREDITS.md  MANUAL.md  Fallen-Manual.pdf
res/    <SLUG>.svg ×10,  components/,  fonts/ (OFL font, for displays only)
scripts/  make_panels.py  make_manual_pdf.sh
src/
  plugin.hpp  plugin.cpp
  time/     Clock.hpp  Hash.hpp  Dura.hpp  Group.hpp  Prime.hpp  Lanes.hpp  Cycle.hpp  Hold.hpp
  dsp/      Common.hpp  Filters.hpp  Env.hpp  Fm4Voice.hpp  Fm4Tables.hpp  FactoryBank.hpp
            KitVoices.hpp  SamplePlayer.hpp  Crunch.hpp  Additive.hpp  Chords.hpp
  io/       Syx4op.hpp/.cpp  WavLoad.hpp/.cpp  third_party/dr_wav.h
  modules/  one .cpp per module
  widgets/  FallenComponents.hpp  StepGrid.hpp  BarEditor.hpp  MiniKeys.hpp  TextDisplay.hpp
tests/
  syx/  (test banks generated by the tests themselves, see §8.9)
```

`plugin.json`: slug `Fallen`, name `Fallen`, brand `Fallen`, version `2.0.0`, licence `GPL-3.0-or-later`, author Hardy. Module names are the short name followed by a plain description, so Rack's search finds both.

| Slug | Name in browser | HP | Tags (only Rack's allowed tags) |
|---|---|---|---|
| `DURA` | `DURA Duration Sequencer` | 36 | Sequencer, Clock generator |
| `GROUP` | `GROUP Burst Generator` | 12 | Clock generator, Sequencer |
| `PRIME` | `PRIME Nested Prime Loops` | 14 | Clock generator, Sequencer |
| `LANES` | `LANES Drum Grid` | 32 | Sequencer, Drum |
| `CYCLE` | `CYCLE Phase Lanes` | 16 | Sequencer, Polyphonic |
| `HOLD` | `HOLD Sustain Against Gap` | 6 | Envelope generator, VCA |
| `FM4` | `FM4 Four-Operator FM Voice` | 28 | Synth voice, Polyphonic |
| `CHORD` | `CHORD Chord Memory` | 10 | Polyphonic, Quantizer |
| `KIT` | `KIT Drum Machine` | 30 | Drum, Sampler |
| `SPECTRA` | `SPECTRA Additive Spectrum` | 14 | Oscillator, Polyphonic |

Descriptions should contain words people will search for: "milliseconds", "ratio", "stutter", "polyrhythm", "prime", "FM", "SysEx", "4-op", "drum", "WAV", "sample", "additive", "chord". Widths are a guide; widen in 2 HP steps rather than crowd labels and note it in `DECISIONS.md`.

---

## 2. Signal conventions

| Signal | Level |
|---|---|
| Pitch | 1 V/oct, 0 V = C4 (261.63 Hz), Rack standard |
| Gates out | 0 or +10 V. Inputs: Schmitt trigger, low 0.1 V, high 1.0 V |
| Triggers out | +10 V, 1 ms |
| Velocity / level in | 0 to +10 V = 0 to full. A gate or trigger at +10 V = full |
| Time CV (STRETCH, SPACE...) | **1 V per doubling of time**: +1 V = twice as long, −1 V = half as long |
| INTERVAL out (DURA) | `log2(interval_ms / 100)` volts: 0 V = 100 ms, +1 V = 200 ms, −1 V = 50 ms. Clamped ±10 V |
| Index / step CV out | 0 V at the first step, rising by `10 V / count` per step, so it spans 0 to <10 V |
| Selector CV in (voice, chord, pattern) | **0.1 V per item** for FM4 VOICE (32 items, 0 to 3.1 V); **1 V per item** for CHORD and LANES PATTERN (8 items, 0 to 7 V). Rounded to the nearest item, clamped |
| Audio | about ±5 V, soft limit ±10 V |
| Bipolar CV in | ±5 V covers the knob's full range, added to the knob |

---

## 3. Panels

### 3.1 Look

Fell's record sleeves and Raster-Noton/Editions Mego graphics are plain: white or pale grey, small black sans type, lots of space, one thin rule. The panels follow that.

- **Ground** `#f1f1ee` (warm off-white). 0.3 mm border `#111` inset 1 mm.
- **Ink** `#111111`. Section rules 0.3 mm. **One accent colour**, `#e04a1f` (orange-red), used only for: the module title's short name, LEDs, and the output jack plates' outline.
- **Title** at the top: the short name in capitals (`DURA`), 4 mm cap height, accent colour, left-aligned, with the plain description under it in 1.8 mm black (`duration sequencer`). Bottom strip: `FALLEN` in 1.8 mm letter-spaced capitals.
- **Knobs**: flat black discs with a single white line, 9 mm (main), 6 mm (small, used in DURA's step columns and per-row controls). No skirts or numbers.
- **Jacks**: standard Rack jack with a black nut. **Outputs** sit on a small black rounded plate with the label inverted (off-white text) so inputs and outputs are told apart at a glance.
- **Buttons**: small square black buttons with an LED in the accent colour when on.
- **Displays** (DURA, LANES, CYCLE, CHORD, FM4, KIT): flat black rectangles with off-white text and graphics, one monospaced OFL font (e.g. JetBrains Mono or IBM Plex Mono, embedded under `res/fonts/`).

### 3.2 Labels

- **Every knob, switch position, jack, button and LED has a printed text label.** No pictograms, waveform icons or arrows.
- Labels are the names in the module tables below, verbatim, in capitals. Switch positions are printed beside the switch (`MS / UNITS / RATIO`).
- Knob label centred above the knob; jack label centred below the jack; button label below. Never rotated, never overlapping.
- Font: one OFL sans (e.g. Inter or IBM Plex Sans), 1.8 mm cap height (1.6 mm minimum for DURA's step columns), **converted to paths**.
- **The check**: `scripts/make_panels.py` generates all SVGs and a C++ header of positions from one table per module, and fails the build if any control has no label or if the label set differs from this document's tables.

### 3.3 Layout rules

- Minimum centre spacing: jack to jack 9 mm, main knob to main knob 13 mm, small knob to small knob 7.5 mm, knob to jack 11 mm.
- Inputs in the lower area, left side; outputs lower area, right side, on their plates.
- Render every panel to PNG (Playwright or Inkscape) and look at each one before hand-over: no overlaps, no clipped text.

---

## 4. Shared code

### 4.1 `Hash.hpp`
`float hash01(uint32_t seed, uint32_t a, uint32_t b = 0)`: a fixed integer hash (e.g. PCG or SplitMix32 finaliser over `seed ^ a·0x9E3779B9 ^ b·0x85EBCA6B`) mapped to [0, 1). Used everywhere something must look varied but be repeatable. Tests: same inputs give the same output on every platform.

### 4.2 `Clock.hpp`: event scheduler
A tiny helper each timing engine uses:
- `double now` (seconds since reset), `double nextAt` (time of next event).
- `step(dt)` advances `now`; returns true on the sample where `now >= nextAt`, and reports the fractional overshoot so the following event is scheduled from `nextAt`, not from `now` (no drift: a 40 ms pattern run for 10 minutes stays exactly aligned).
- Minimum interval 1 ms; anything shorter is clamped (log it in DECISIONS.md).

### 4.3 Gate and trigger outputs
`TrigOut` (1 ms pulse, retriggerable: a new trigger during a pulse restarts it after one low sample so downstream modules see two edges). `GateOut` with a length in seconds; when a new event arrives while the gate is high, the gate drops low for one sample first (so envelopes retrigger).

### 4.4 External clock measurement
Used by DURA and PRIME: the period between the last two rising edges at the CLOCK input, smoothed by a median of the last 3 periods. Before two edges have arrived, use the knob value.

### 4.5 Filters, envelopes
Standard: TPT state-variable filter, one-pole LP/HP, DC blocker, linear-in-dB exponential envelopes. Put them in `Filters.hpp`, `Env.hpp`.

---

## 5. DURA, duration sequencer (36 HP)

The heart of the set. A loop of up to 16 steps where **each step is a time interval with a repeat count**, timed in milliseconds, unit multiples or ratios, with one STRETCH control that rescales everything. Covers Fell's step-duration sequencer, the 40/50 ms tables of Multistability (method 2), the stutter repeats, the "one slider stretches the pattern" Lee piece, the triplet-on-hats ratio steps, and the random-length gates of method 1 (here deterministic).

### 5.1 How it runs
- Steps 1 to `STEPS` play in order and loop. Each step plays **REP events, each `interval` apart**, so the step lasts `interval × REP`. An event fires TRIG and starts GATE.
- A step whose **ON** button is off plays no events but still takes its time (a rest).
- `interval` for step *i*:
  - **MS**: `LEN_i` sets 1 ms to 2000 ms, exponential taper (knob centre ≈ 45 ms).
  - **UNITS**: `LEN_i` snaps to integer 1 to 16, times **UNIT** (5 ms to 500 ms, default 40 ms). The display shows "×n".
  - **RATIO**: `LEN_i` snaps to the list `4, 3, 2, 3/2, 4/3, 1, 3/4, 2/3, 1/2, 3/8, 1/3, 1/4, 1/6, 1/8`, times **BASE** (20 ms to 4000 ms, default 500 ms).
- Then `interval *= STRETCH`, where STRETCH is ×0.25 to ×4 (exponential, centre ×1) plus STRETCH CV at 1 V per doubling (total clamped ×1/16 to ×16).
- **NUDGE** (−50% to +50%, default 0) lengthens or shortens **only the last step's last interval**, to collapse or expand the loop point by a fraction of a step. Combined with STEPS this gives the "13 or 15 instead of 16" loops at any fine resolution.
- **Parameter changes take effect at the next event**, never mid-interval (so turning STRETCH never makes a glitch faster than the new interval). Exception: RESET.
- **CLOCK input**: when patched, the measured clock period replaces UNIT (in UNITS mode), BASE (in RATIO mode), or acts as a reference of 100 ms in MS mode (intervals are scaled by `period / 100 ms`). Lets DURA sit inside a tempo-based patch. Document this in the manual.

### 5.2 Gate length (method 1, deterministic)
For each event: `pct = clamp(LENGTH + SPREAD × (hash01(SEED, globalStep, rep) − 0.5) × 2 × 98, 1, 99)` percent, and gate length = `pct% × interval`. LENGTH default 50%, SPREAD default 0. LENGTH 50, SPREAD 1 reproduces Fell's "random 0 to 99% of the interval", but repeatably. SEED 0 to 99 picks a different repeatable set. `globalStep` = step index (not a running counter) so the pattern repeats every loop. A **VARY** switch (`LOOP / EVOLVE`): EVOLVE uses the loop count in the hash too, so gate lengths differ each loop but the whole sequence is still repeatable from RESET.

### 5.3 Controls

Per step *i* = 1..16 (a column each, small knobs, 7.5 mm pitch):

| Type | Label | Range / default |
|---|---|---|
| Small knob | `LEN` | per mode above. Default: 125 ms in MS mode, ×1 in UNITS, 1 in RATIO (store one value per mode so switching mode does not scramble the pattern) |
| Small knob | `REP` | 1 to 16, default 1 |
| Button + LED | `ON` | default on. LED also flashes on events |

Global:

| Type | Label | Range / default |
|---|---|---|
| Switch 3-way | `MS / UNITS / RATIO` | default UNITS |
| Knob | `UNIT` | 5 to 500 ms, default 40 ms |
| Knob | `BASE` | 20 to 4000 ms, default 500 ms |
| Knob | `STRETCH` | ×0.25 to ×4, default ×1 |
| Knob | `STEPS` | 1 to 16, default 8 |
| Knob | `NUDGE` | −50% to +50%, default 0 |
| Knob | `LENGTH` | 1 to 99%, default 50% |
| Knob | `SPREAD` | 0 to 1, default 0 |
| Knob | `SEED` | 0 to 99, default 0 |
| Switch | `LOOP / EVOLVE` | default LOOP |
| Button | `RUN` | latching, default on |
| Button | `RESET` | momentary |

Inputs: `CLOCK`, `RUN` (gate: high = run; when patched it overrides the button), `RESET`, `STRETCH CV`, `LENGTH CV`, `SPREAD CV`, `STEPS CV` (0 to 10 V over 1 to 16).

Outputs: `TRIG`, `GATE`, `STEP` (index CV, §2), `INTERVAL` (CV of the interval **from this event to the next one**, known in advance, §2; updated at each event), `REP` (CV of the repeat index within the step, 0 V at the first repeat), `EOC` (trigger when the loop wraps).

### 5.4 Display
A strip above the step columns: one bar per step whose width is proportional to its duration (`interval × REP`), with REP tick marks inside it, the current step highlighted and a playhead moving through it. Under it, the text: total loop length in ms and the current interval (e.g. `LOOP 1240 ms · 40 ms ×3`). The proportional bars make the "time-stretched pattern" visible.

### 5.5 Context menu
- `Fill from table`: sets LEN in UNITS mode from the hash (values 1 to 10, the Multistability table) for the current SEED.
- `Copy pattern`, `Paste pattern`, `Reverse steps`, `Rotate left/right`.
- `Reset on first clock edge` (default on).

### 5.6 Tests (DURA)
- UNITS mode, UNIT 40 ms, LEN 1,2,3 REP 1: events at 0, 40, 120 ms, loop 240 ms, exact to one sample at all four rates over 10 minutes.
- REP 4 at 10 ms: four events 10 ms apart, step takes 40 ms.
- STRETCH ×2 doubles every interval; STRETCH CV +1 V likewise.
- RATIO mode BASE 300 ms, LEN 2/3: interval 200 ms.
- NUDGE −50% on an 8-step UNITS loop of ×1: loop is 7.5 units long.
- SPREAD 1, LENGTH 50: gate percents span 1 to 99, identical on two runs; EVOLVE differs per loop but is identical between two runs from RESET.
- INTERVAL output at each event equals `log2(next_interval/100)` V.
- Changing STRETCH mid-interval does not change the current interval.

---

## 6. GROUP, compressing and expanding bursts (12 HP)

Multistability method 3: a group of N events, groups fired at a longer period, with the inner spacing compressed or expanded.

### 6.1 Behaviour
- A group has `COUNT` events (1 to 16). Event *k* (k = 0..COUNT−1) follows event k−1 after `space_k = SPACE × 2^(CURVE × 3 × k/(COUNT−1))` (for COUNT 1, just one event). CURVE −1 to +1: negative compresses (each gap shorter, accelerating), positive expands (ritardando), 0 even. Clamp every `space_k` to ≥ 1 ms.
- Groups start every `PERIOD`, measured from group start to group start. If a group's span is longer than PERIOD, the next group starts one `SPACE` after the last event instead (never overlap).
- **DRIFT**: a slow deterministic change of SPACE from group to group. A triangle phase advances by `RATE` per group (0 to 0.25 of a cycle per group); effective `SPACE × 2^(DEPTH × tri(phase))`, DEPTH 0 to 2 octaves. The burst tightens and loosens over many groups and reverses at the ends.
- **TRIG IN** patched: each rising edge fires one group now (interrupting a running one) and the internal PERIOD is ignored.
- Gate length per event: `LENGTH` % of the gap to the next event (last event: of SPACE).

### 6.2 Controls

| Type | Label | Range / default |
|---|---|---|
| Knob | `COUNT` | 1 to 16, default 5 |
| Knob | `SPACE` | 1 to 1000 ms, exp, default 60 ms |
| Knob | `CURVE` | −1 to +1, default −0.5 |
| Knob | `PERIOD` | 20 ms to 10 s, exp, default 1000 ms |
| Knob | `LENGTH` | 1 to 99%, default 50% |
| Knob | `RATE` | 0 to 0.25, default 0 |
| Knob | `DEPTH` | 0 to 2 oct, default 0 |
| Button | `FIRE` | momentary, same as TRIG IN |

Inputs: `TRIG IN`, `RESET`, `COUNT CV` (0–10 V over 1–16), `SPACE CV` (1 V/doubling), `CURVE CV`, `PERIOD CV` (1 V/doubling).
Outputs: `TRIG`, `GATE`, `FIRST` (trigger on event 0), `LAST` (trigger on the final event), `INDEX` (CV, §2), `BUSY` (gate high while a group runs).

### 6.3 Tests
COUNT 5, SPACE 100, CURVE 0: events at 0,100,200,300,400 ms. CURVE −1: gaps fall from 100 ms to 12.5 ms geometrically. PERIOD shorter than span: no overlap. DRIFT with RATE 0.1, DEPTH 1: SPACE follows a triangle over 10 groups, identical on two runs.

---

## 7. PRIME, nested prime loops (14 HP)

After *Intra*: parts built from nested loops of prime lengths that drift apart and realign. Fell does not publish his exact rule; this is our reading (write it in DECISIONS.md as an interpretation).

### 7.1 Behaviour
- A **pulse** comes from the internal `RATE` (5 ms to 2 s per pulse, exp, default 120 ms) or from `CLOCK` (each rising edge is one pulse; DURA's TRIG is a good source).
- Four rows A to D. Each row is a loop of `LEN` steps, LEN snapping to primes `2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31` (default A 3, B 5, C 7, D 11).
- Each row places `HITS` events (1 to LEN, default 2) evenly in its loop with the Bjorklund (Euclidean) algorithm, rotated by `ROT` (0 to LEN−1).
- **SOURCE** switch per row, `PULSE / NEST`:
  - PULSE: the row advances one step on each pulse.
  - NEST: the row advances one step on each **event of the row above** (B nests in A, C in B, D in C; A has no NEST position). This is the "loop of loops": a 7-loop nested in a 3-loop of hits.
- **REALIGN**: trigger when every row is at its step 0 on the same pulse. The display counts pulses until the next realignment (the LCM for PULSE rows; computed by simulation for NEST rows, up to 100 000 pulses, else shows `>100k`).

### 7.2 Controls

Per row (A to D): `LEN` (knob, snaps to primes), `HITS` (knob), `ROT` (knob), `SOURCE` switch (B to D only), LED.
Global: `RATE` knob, `RUN` button, `RESET` button.
Inputs: `CLOCK`, `RESET`, `RUN`, `RATE CV` (1 V/doubling).
Outputs per row: `TRIG A..D`, `GATE A..D` (gate length 50% of the pulse); plus `ANY` (trigger on any row event), `REALIGN`.
Display: four rows of dots showing each loop with its playhead, and `REALIGN IN 105`.

### 7.3 Tests
A=3/1, B=5/1, PULSE both: REALIGN every 15 pulses. B NEST under A (3 hits on 3): B advances every pulse. A=3/1, B NEST with LEN 5 HITS 1: B fires every 15 pulses. Bjorklund(5,2) = x.x.. (rotation-normalised). Identical sequences on two runs.

---

## 8. FM4, four-operator FM voice with SysEx loading (28 HP)

A 4-operator FM voice in the style of mid-80s 4-op Yamaha synths, which **reads their voice data from `.syx` files** so Hardy can load banks from his own synths or from the internet, then "tweak a preset a little" (Fell's own method). Up to 8-voice polyphonic. Voice slots can **step on each new note**, the in-and-out-of-phase trick.

### 8.1 Engine (per voice, `Fm4Voice.hpp`)
- 4 operators, each a phase accumulator, a waveform, and an envelope. Operator numbering follows the Yamaha convention: **operator 4 has the feedback loop, operator 1 is always a carrier.**
- Run at 2× oversampling (decimate with a half-band filter) to keep high-ratio modulation clean. Phase modulation (not true FM): `out_op = wave(phase + Σ mod_inputs) × amp`.
- **Algorithms** (verify against the owner's manual algorithm chart; this matches the YM2151/OPM family the 4-op Yamahas used, with operator numbers reversed):

| ALG | Routing (→ = modulates; + = mixed at output) |
|---|---|
| 1 | 4 → 3 → 2 → 1 |
| 2 | (4 + 3) → 2 → 1 |
| 3 | 4 → 1, 3 → 2 → 1 (1 gets both) |
| 4 | 4 → 3 → 1, 2 → 1 |
| 5 | 4 → 3, 2 → 1; output 3 + 1 |
| 6 | 4 → 3, 4 → 2, 4 → 1; output 3 + 2 + 1 |
| 7 | 4 → 3; output 3 + 2 + 1 |
| 8 | output 4 + 3 + 2 + 1 |

- **Feedback** FB 0–7 on op 4: modulation input = `fbScale[FB] × (y[n−1] + y[n−2]) / 2`, with `fbScale = {0, π/16, π/8, π/4, π/2, π, 2π, 4π}` (est.; tune so FB 7 is a bright saw-like buzz, not noise).
- **Frequency**: ratio mode `f = f_note × ratioTable[CRS] × (1 + FINE/16 × ...)`. `ratioTable` (64 entries, the 4-op table; verify against the manual's frequency chart):
  `0.50, 0.71, 0.78, 0.87, 1.00, 1.41, 1.57, 1.73, 2.00, 2.82, 3.00, 3.14, 3.46, 4.00, 4.24, 4.71, 5.00, 5.19, 5.65, 6.00, 6.28, 6.92, 7.00, 7.07, 7.85, 8.00, 8.48, 8.65, 9.00, 9.42, 9.89, 10.00, 10.38, 10.99, 11.00, 11.30, 12.00, 12.11, 12.56, 12.72, 13.00, 13.84, 14.00, 14.10, 14.13, 15.00, 15.55, 15.57, 15.70, 16.96, 17.27, 17.30, 18.37, 18.84, 19.03, 19.78, 20.41, 20.76, 21.20, 21.98, 22.49, 23.55, 24.22, 25.95`.
  FINE (0–15, extended voices only) adds a fraction to the ratio as documented in the manual's fine-ratio table; if unverifiable, use `ratio × (1 + FINE/16 × step)` where step is the gap to the next table entry, and log it.
  **Fixed mode** (extended voices): `f = fixedRange(FIXRG) × (CRS-derived coarse) + fine` per the manual's fixed-frequency table (8 Hz to ~32 kHz). Verify; log.
- **Detune** DET 0–6 (3 = centre): ±3 steps of about ±2.6 cents each (est.).
- **Waveforms** (extended voices; W1 otherwise). Verify against the manual's pictures; starting model with θ in [0, 2π):
  W1 `sin θ`; W2 `sign(sin θ)·sin²θ`-like (est.); W3 `max(sin θ, 0)`; W4 `max(W2, 0)`; W5 `θ<π ? sin 2θ : 0`; W6 `θ<π ? W2(2θ) : 0`; W7 `θ<π ? |sin 2θ| : 0`; W8 `θ<π ? |W2(2θ)| : 0`. Use 4096-point tables with linear interpolation.
- **Envelope** (Yamaha 4-op): attack at rate AR (0–31) to full, decay at D1R (0–31) to level D1L (0–15, 15 = full), then decay at D2R (0–31, 0 = hold) towards silence while gated; on gate off, release at RR (1–15, mapped to rate `RR×2+1`). Model in dB (log domain), decay stages linear in dB, attack exponential-approach in amplitude. Rate → time (est., all in `Fm4Tables.hpp` so they can be tuned): full-scale decay (0 to −96 dB) time `T(r) = 38 s × 2^(−(r−1)/2.35)` for r ≥ 1 (r 31 ≈ 5 ms); attack 0 → full `A(r) = 9 s × 2^(−(r−1)/2.1)` (r 31 ≈ 1 ms); rate 0 = no movement. **Rate scaling** RS 0–3: effective rate `r + RS_amount × keyCode/…` per the manual (est.: `+ (note−C1)/12 × RS`... log the final rule).
- **Output level** OUT 0–99 → attenuation `(99 − OUT) × 0.75 dB` (est.; 0 = silent). For a modulator, full level (99) gives a peak phase deviation of about `4π` rad (est.; tune so classic electric piano and bass voices sound right).
- **Keyboard level scaling** LS 0–99: attenuation rising above C3 by up to about 24 dB/oct × LS/99 (est.).
- **Velocity**: KVS 0–7 per operator: attenuation up to `KVS/7 × 24 dB` at velocity 0 (est.). Velocity comes from the VEL input (0–10 V), default full when unpatched.
- **LFO**: wave (saw up, square, triangle, sample-and-hold), speed LFS 0–99 (≈ 0.06 Hz to 50 Hz, exp, est.), delay LFD 0–99 (0 to ~5 s, est.), PMD 0–99 and AMD 0–99 depths, PMS 0–7 pitch sensitivity (max ±1 octave at 7, est.), AMS 0–3 amp sensitivity, AME per operator on/off, SY (key sync) on/off. S&H uses `hash01`, not a live random.
- **Transpose** TRPS (±2 oct) applies. **Pitch bend range**, **portamento**, **mono mode**, **foot/breath controller** settings are stored and preserved when saving the bank but **ignored by the engine** (Rack patches do this with cables). Log it.
- **Pitch EG** (DX21-style voices): stored, applied if present (3 rates, 3 levels; levels 50 = no shift, ±4 octaves est.). Optional: if time is short, store only and log.

### 8.2 Voice data model (`Fm4Voice.hpp`, `Syx4op.hpp`)
An internal struct `Voice4op` holding every parameter above in the original integer ranges, plus `name[10]`, plus `extended` (bool: wave/fixed/fine data present). This struct is what the bank, the panel edit section, the JSON save and the SysEx import/export all use. The engine converts it to floats in `prepare()` when a voice is selected or edited (on the audio thread, but it is just arithmetic).

### 8.3 SysEx import (`Syx4op.cpp`) — Hardy's requirement
Parse on the UI thread when the user picks a file (osdialog, filter `*.syx;*.SYX;*.mid`). A file may contain several SysEx messages; scan for every `F0 … F7` and handle the ones recognised. The format specs: the DX21/DX27/DX100 and TX81Z owner's manuals' "MIDI data format" appendices. Cross-check with an open-source parser (e.g. the TX81Z/DX21 editors on GitHub, or MAME's YM2414 notes) and credit any you read in CREDITS.md.

| Message | Header | Payload | Use |
|---|---|---|---|
| **32-voice bulk (VMEM)** | `F0 43 0n 04 20 00` | 4096 bytes = 32 × 128-byte packed voices, then checksum, `F7` | Replaces the working bank (all 32) |
| **Single voice (VCED)** | `F0 43 0n 03 00 5D` | 93 bytes, checksum, `F7` | Replaces the **currently selected** slot |
| **Additional voice (ACED, TX81Z)** | `F0 43 0n 7E 00 21` + ASCII `LM  8976AE` | 23 bytes, checksum, `F7` | Adds wave/fixed/fine data to the single voice loaded with it (ACED usually comes just before VCED) |
| Performance / system / microtune dumps (`LM  8976PE`, `LM  8023…`, etc.) | | | Ignored, listed in the load report |
| **6-op 32-voice bulk** (`F0 43 0n 09 20 00`) | | | Not supported: show "This is a 6-operator bank; FM4 reads 4-operator banks" |

**VMEM packed voice layout** (128 bytes; verify every byte against the manual, the test in §8.9 locks the final layout):
- Bytes 0–39: four 10-byte operator blocks in the order **op4, op2, op3, op1**. Each block: `0 AR`, `1 D1R`, `2 D2R`, `3 RR`, `4 D1L`, `5 LS`, `6 bits: AME(6) EBS(5–3) KVS(2–0)`, `7 OUT`, `8 CRS`, `9 bits: RS(4–3) DET(2–0)`.
- `40 bits: SY(6) FB(5–3) ALG(2–0)`, `41 LFS`, `42 LFD`, `43 PMD`, `44 AMD`, `45 bits: PMS(6–4) AMS(3–2) LFW(1–0)`, `46 TRPS`, `47 PBR`, `48 bits: CH(4) MO(3) SU(2) PO(1) PM(0)`, `49 PORT`, `50 FC VOL`, `51 MW PITCH`, `52 MW AMP`, `53 BC PITCH`, `54 BC AMP`, `55 BC PBIAS`, `56 BC EBIAS`, `57–66 name (10 ASCII)`, `67–72 pitch EG (DX21)`.
- **TX81Z extension bytes 73–83** (est.; the TX81Z stores its ACED data here in 32-voice dumps): per operator in order op4, op2, op3, op1, two bytes: `bits FIX(3) FIXRG(2–0)` then `bits OSW(6–4) FINE(3–0)`; then `81 REV`, `82 FC PITCH`, `83 FC AMP`. If these bytes are all zero, treat the voice as non-extended (W1, ratio mode, FINE 0), which is correct for DX21/27/100 banks.
- **Checksum**: two's complement of the 7-bit sum of the payload bytes. On mismatch, **load anyway** and show a warning in the report (many files on the internet have bad checksums).
- Clamp every value to its legal range on import; count clamped values in the report.
- **Raw 4096-byte files** with no SysEx header: offer to load as VMEM (menu item `Load raw 4096-byte bank`).
- **Device channel** `n` is ignored.

**Load report**: after loading, the FM4 display shows `LOADED 32 VOICES · <filename>` (or the warning) for 4 s, and the full report (messages found, recognised, ignored, checksum status, clamped values) is in the context menu under `Last load report`.

### 8.4 SysEx export
Context menu `Save bank as .syx…` writes a VMEM 32-voice bulk (with TX81Z extension bytes for extended voices) with a correct checksum. `Save voice as .syx…` writes ACED + VCED for the current slot. Round-trip test in §8.9.

### 8.5 Bank, slots and stepping
- FM4 holds a **working bank of 32 voices**, saved in the patch JSON (as a base64 of the VMEM image plus a JSON copy of any fields VMEM cannot hold). It starts as the **factory bank** (§8.7).
- It also keeps an **original copy** of each voice as loaded, for `Revert voice`.
- **VOICE** knob 1–32 plus **VOICE CV** (poly, 0.1 V per voice) selects the voice for new notes. The display shows `12 ORGAN STAB`.
- **STEP** switch `OFF / ON`: when ON, each new gate (rising edge on any channel) moves to the next voice in the range `FIRST` to `FIRST + COUNT − 1` (wrapping), starting from FIRST after RESET. VOICE knob and CV are ignored while STEP is on. With COUNT 3 and a 5-note sequence you get Fell's sounds-against-notes phasing.
- `STEP RESET` input.
- A note keeps the voice it started with until it ends (switching voice never changes a sounding note).

### 8.6 Panel edit section: "tweak the preset"
Two layers. Both edit the **displayed voice** (`EDIT` = the VOICE knob's voice, even while STEP plays other voices), and changes are stored in the working bank immediately.

**Macros** (relative offsets applied on top of the stored voice at note-on, not written into the voice; at centre they change nothing):

| Label | Effect | Range |
|---|---|---|
| `BRIGHT` | adds to all **modulator** output levels | ±20 level steps |
| `RATIO` | shifts all **modulator** CRS indexes | ±8 table steps |
| `DECAY` | scales D1R and D2R of all operators | ±10 rate steps (higher = shorter) |
| `ATTACK` | scales AR of all operators | ±10 rate steps |
| `FDBK` | adds to FB | ±7 |
| `SPREAD` | detunes poly voices from each other | 0 to 20 cents |

Each macro has a CV input (±5 V = full range). Macros are also polyphonic: a poly CV applies per channel.

**Operator edit** (absolute values written into the voice):
- Buttons `OP 1`, `OP 2`, `OP 3`, `OP 4` choose the operator (radio, LED on the chosen one).
- Knobs: `CRS` (0–63, shows the ratio), `FINE` (0–15), `LEVEL` (0–99), `AR`, `D1R`, `D1L`, `D2R`, `RR`, `WAVE` (W1–W8), `DET`.
- Voice-wide knobs: `ALG` (1–8), `FB` (0–7).
- When the chosen operator or the edited voice changes, the module sets these knob values from the voice data on the UI thread, so the knobs always show the truth; it must not then write them back as an edit. Use a "syncing" flag.
- Editing a non-extended voice's WAVE, FINE or fixed mode makes it extended.
- The display shows the algorithm as a small text diagram (`4>3>2>1`) and the chosen operator's values.

### 8.7 Factory bank (32 voices, our own names)
Design 32 voices with the engine, aimed at Fell's palette. Names are ours, never copied from Yamaha presets. Required characters (names are suggestions; keep them 10 characters or fewer):

1 `HOUSE ORG` (early-90s house organ stab: ALG 8 or 7, carriers at ratios 0.5/1/2/3 with short-decay percussive click on one op), 2 `ORG HOLLOW`, 3 `ORG CLICK`, 4 `RUBBER BS` (rubbery FM bass: ALG 5 or 1, op2 ratio 1 modulating op1 ratio 0.5, fast D1R on modulator), 5 `PICK BASS`, 6 `SUB BASS`, 7 `SYNC BASS`, 8 `E.PIANO`, 9 `GLASS EP`, 10 `BELL 1`, 11 `BELL 2`, 12 `TUBULAR`, 13 `MARIMBA`, 14 `METAL PERC`, 15 `WOOD PERC`, 16 `KALIMBA`, 17 `PLUCK`, 18 `HARP`, 19 `BRASS STB`, 20 `CHORD STB`, 21 `VOX PAD`, 22 `HOLLOW PAD`, 23 `WARM PAD`, 24 `SWEEP PAD`, 25 `FLUTE`, 26 `REED`, 27 `NOISE HIT`, 28 `ZAP`, 29 `CLANG`, 30 `INHARM 1`, 31 `INHARM 2`, 32 `INIT` (single sine carrier, everything neutral).

Use extended waveforms on at least 8 voices so the TX81Z-style waves are heard. Store the bank as a VMEM image in `FactoryBank.hpp` (so it also exports cleanly). Record final parameters in DECISIONS.md. Context menu: `Load factory bank`.

### 8.8 Ports and other controls

| Type | Label | Notes |
|---|---|---|
| Knob | `VOICE` | 1–32 |
| Switch | `STEP OFF / ON` | |
| Knob | `FIRST` | 1–32 |
| Knob | `COUNT` | 1–32, default 3 |
| Knob | `LEVEL` | output level, default 0 dB |
| Input poly | `V/OCT` | channels = voices, up to 8 |
| Input poly | `GATE` | |
| Input poly | `VEL` | 0–10 V |
| Input poly | `VOICE CV` | 0.1 V per voice |
| Input | `STEP RESET` | |
| Inputs | `BRIGHT CV`, `RATIO CV`, `DECAY CV`, `ATTACK CV`, `FDBK CV` | poly |
| Output | `OUT` | mono sum, ±5 V nominal |
| Output poly | `POLY OUT` | one channel per voice |

Context menu: `Load .syx…`, `Load raw 4096-byte bank…`, `Save bank as .syx…`, `Save voice as .syx…`, `Revert voice`, `Load factory bank`, `Last load report`, `Voices: 1–8` (max polyphony, default 8), `Voice allocation: by channel` (fixed; documented).

Polyphony: the number of output channels follows the V/OCT input's channel count (max 8). Mono patching works as a monosynth.

### 8.9 Tests (FM4)
- **SysEx parser**: tests build a VMEM image in code from known `Voice4op` values, wrap it in SysEx with a correct checksum, parse it, and compare every field. Same for VCED and ACED. A bad checksum still loads and reports. A 6-op header is refused with the right message. Truncated files do not crash.
- **Round trip**: export the factory bank, re-import, byte-identical voices.
- If any real 4-op `.syx` bank with a published parameter listing can be found and its licence allows it to be used in tests, add it to `tests/syx/` and check a few named voices' parameters against the listing; otherwise document that the layout was checked against the manuals only.
- Engine: ALG 8 with one carrier at ratio 1, LEVEL 99: a sine at the note frequency within 0.5 cent. ALG 1 with mods at 0: same. Envelope times within 10% of the formulas. FB 0 vs 7 changes harmonic content. Each W1–W8 table matches its formula. 8 voices at once under CPU budget.
- STEP: FIRST 5 COUNT 3, five gates → voices 5,6,7,5,6.

---

## 9. CHORD, chord memory for stabs (10 HP)

Feeds FM4 (or SPECTRA) with chorded stabs.

- **8 chord slots**, each holding 1 to 6 notes as semitone offsets from the root (−12 to +24). Factory slots: `m7 (0,3,7,10)`, `m9 (0,3,7,10,14)`, `m11 (0,3,7,10,14,17)`, `maj7 (0,4,7,11)`, `maj9 (0,4,7,11,14)`, `sus4 (0,5,7,10)`, `quartal (0,5,10,15)`, `7 (0,4,7,10)`. Hardy can edit any slot on a two-octave mini keyboard widget (click to toggle a note; the slot shown is the selected one).
- **CHORD** knob 1–8 + CV (1 V per slot).
- **INVERT** 0–5 + CV (1 V per step): rotates the lowest note up an octave, n times.
- **VOICING** switch `CLOSE / DROP2 / SPREAD`: DROP2 drops the second-highest note an octave; SPREAD alternates notes up an octave.
- **ROOT** V/oct input (mono) plus `TRANSPOSE` knob ±12 semitones.
- **GATE IN** (mono) is copied to each output channel.
- Outputs: `V/OCT` (poly, one channel per note), `GATE` (poly, same channel count).
- Chord changes take effect at the next rising GATE (so a stab never changes pitch mid-note), unless `GATE IN` is unpatched, then immediately.
- Tests: m7 on C4 root = 0, 3/12, 7/12, 10/12 V; INVERT 1 moves 0 V to 1 V; DROP2 output; channel counts.

---

## 10. KIT, drum machine voices with WAV loading (30 HP)

Six drum voices. Each is either **synthesised** (two era characters) or plays **a WAV file Hardy loads** (Hardy's requirement). Dry: no reverb.

### 10.1 Voices and per-voice controls

Voices: `KICK`, `SNARE`, `CLAP`, `C.HAT` (closed hat), `O.HAT` (open hat), `PERC` (rim in ANALOG era, a short tuned "tok" in EARLY era).

Per voice:

| Type | Label | Range / default |
|---|---|---|
| Knob | `TUNE` | ±12 semitones (synth) / ±24 semitones (sample), default 0 |
| Knob | `DECAY` | 0.1 to 2× the voice's natural decay; for samples, a fade-out envelope from 20 ms to full length, default full |
| Knob | `LEVEL` | −inf to +6 dB, default 0 dB |
| Switch | `EARLY / ANALOG / SAMPLE` | the source. Default: KICK ANALOG, others EARLY |
| Input | `TRIG` | rising edge triggers; the voltage at the edge sets velocity, linear in amplitude (10 V or more = full, 5 V = half) |
| Input | `TUNE CV` | 1 V/oct |
| Output | `OUT` | the voice alone. The voice also stays in MIX unless the menu item `Individual outs remove from mix` is ticked (default off) |

Global: `CRUNCH` knob (0–1, default 0.5), `MIX` output, `ACCENT` input (gate: when high, all triggers +4 dB), and a small display per voice showing the loaded sample's file name (or `EARLY`/`ANALOG`).

**Choke**: C.HAT stops a ringing O.HAT (20 ms fade), as on drum machines. Menu option to turn off.

### 10.2 EARLY (sharp early-80s sampled machine character)
Early-80s sample-based drum machines stored short 8-bit companded samples at roughly 25–35 kHz with fixed decay. We **synthesise** the sound, then pass it through `Crunch`: µ-law-style 8-bit companding plus sample-rate reduction to `28 kHz` (est.) with a simple anti-alias LP at 11 kHz before and a gentle LP at 12 kHz after. CRUNCH 0 bypasses, 0.5 = that machine character, 1 = 6-bit and 18 kHz.
- KICK: sine with a fast pitch drop 220 → 58 Hz in 20 ms, amplitude decay 180 ms, plus a 2 ms filtered click. Hard, short, tight.
- SNARE: two sines 185 Hz and 330 Hz (decay 90 ms) + band-passed noise (centre 3.5 kHz, Q 0.7, decay 160 ms), noise 60% of level.
- CLAP: noise through a band-pass at 1.1 kHz (Q 1.2); three 8 ms bursts 9 ms apart then a 120 ms tail.
- C.HAT: noise + six squares (below) through a high-pass at 7 kHz, decay 45 ms. O.HAT: same, decay 450 ms.
- PERC: a pitched "tok", sine 900 Hz with a short pitch drop and 40 ms decay plus a 2 kHz noise click.

### 10.3 ANALOG (808/909-style analogue synthesis)
- KICK: resonant bridged-T style: a sine ringing at 52 Hz (TUNE moves it), decay 0.3 to 1.5 s (DECAY), small pitch envelope (+6 semitones, 8 ms) and a click; soft saturation option in the menu (`Kick drive`).
- SNARE: two damped oscillators 180 Hz and 330 Hz + noise through a high-pass at 1.5 kHz, SNAPPY balance fixed at 0.6 (menu: 0.3/0.6/0.9).
- CLAP: noise, band-pass 1 kHz, four bursts 11 ms apart, reverb-like tail made by a second, longer filtered noise envelope (not a reverb).
- HATS: six square oscillators at 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 Hz (scaled by TUNE), summed, through two band-passes at 7.1 and 10 kHz and a high-pass at 6 kHz; C.HAT decay 50 ms, O.HAT 400 ms.
- PERC: rim shot: two resonant band-passes at 1.7 kHz and 480 Hz pinged by a 1 ms pulse, 30 ms decay.
CRUNCH does not apply to ANALOG.

### 10.4 SAMPLE (WAV loading) — Hardy's requirement
- **Load**: right-click a voice → `Load sample…` (osdialog, filter `*.wav;*.WAV`). Also `Clear sample`. Loading a file switches that voice's source to SAMPLE.
- **Load kit folder**: KIT context menu → `Load kit from folder…` picks a folder and assigns the first matching WAV per voice by file name keywords (case-insensitive): KICK `kick|kik|bd|bassdrum`; SNARE `snare|snr|sd`; CLAP `clap|clp|cp`; C.HAT `chh|closed|hihat|hh|ch` (excluding names that match open); O.HAT `ohh|open|oh`; PERC `rim|rs|perc|cow|tom|tamb|shaker`. The report lists what went where and what had no match.
- **Drag and drop**: dropping a WAV onto a voice's display area loads it into that voice (Rack 2 `onPathDrop`).
- **Reading** (`WavLoad.cpp`, `dr_wav`): 8/16/24/32-bit integer and 32/64-bit float PCM, any sample rate, mono or stereo (stereo is mixed to mono, −3 dB). Maximum 20 s (longer files are truncated, with a note). Convert to float at load, and **resample to Rack's current sample rate at load** with a windowed-sinc or high-quality polyphase resampler (speexdsp is available in Rack's SDK; use it). On sample-rate change, re-resample from the kept original on a worker thread and swap.
- **Playback** (`SamplePlayer.hpp`): one-shot, retriggered from the start on each trigger (a 2 ms fade-out of the previous hit to avoid clicks). TUNE repitches by reading at `2^(semitones/12)` with cubic (Hermite) interpolation. DECAY applies a fade envelope (exponential, reaching −60 dB at the DECAY time; full position = no fade). Velocity scales amplitude. CRUNCH applies to samples only if the menu item `Crunch samples` is ticked (default off).
- **Threading**: decoding and resampling run on a detached worker thread (or the UI thread for files under 1 MB); the finished buffer is published with an atomic pointer swap; the old buffer is freed on the UI thread one frame later. Never allocate in `process()`.
- **Saving**: the patch stores each voice's **absolute file path** and source mode. Context menu option `Store samples in patch` (default off): when ticked, the decoded original (16-bit, ≤ 20 s per voice) is embedded in the patch JSON as base64 so the patch is self-contained. If a stored path is missing on load and nothing is embedded, the voice falls back to EARLY and its display shows `MISSING: <name>`; the path is kept so relinking is easy.

### 10.5 Tests (KIT)
- WAV reader: generate test WAVs in the test itself (8/16/24/32-bit int, 32-bit float, mono, stereo, 22.05/44.1/96 kHz), load them, compare samples within quantisation error. Corrupt and truncated files fail cleanly.
- Resampling a 1 kHz sine from 44.1 to 48 kHz keeps 1 kHz within 0.1% and images below −80 dB.
- TUNE +12 on a sample halves its length.
- Choke works. Kit-folder keyword matching on a list of file names.
- All six voices in each mode: output finite, peak under 10 V, silent after decay (no DC).

---

## 11. SPECTRA, long linear spectrum (14 HP)

The "long linear spectrum" textures under the percussion in Multistability: an additive voice whose partials can be pushed from harmonic to inharmonic.

### 11.1 Engine
- Up to **32 partials** per voice, **poly up to 4 voices** (V/OCT channels). Each partial a sine (quadrature oscillator or wavetable; amplitude-smoothed).
- Partial *n* (1..N) frequency: `f_n = f0 × n^(1 + STRETCH × 0.5)`. STRETCH −1 to +1, 0 = harmonic. Partials above 0.45·fs are muted.
- Amplitude: `a_n = n^(TILT/6.02)` style, with TILT −12 to +6 dB/oct (default −6), times `ODD/EVEN` balance (−1 = odd only, +1 = even only, 0 = all), times the MOVE window.
- **MOVE**: a smooth bump (raised cosine, width 6 partials) that travels up and down the partial list at RATE (0.01 to 2 Hz, triangle, deterministic phase); DEPTH 0–1 blends from flat to only-the-bump. Long slow spectral movement without a filter, as Fell prefers.
- **DETUNE**: spreads each partial by `±DETUNE × hash01(n)` cents (0–20 cents) for slow beating.
- **Envelope**: GATE input drives a simple AR (`ATTACK` 1 ms–10 s, `RELEASE` 1 ms–20 s). Unpatched GATE = drone (always on).
- Stereo: partials alternate left and right by `WIDTH` (0 = mono, 1 = odd left, even right).

### 11.2 Controls and ports
Knobs: `PARTIALS` (1–32, default 16), `STRETCH`, `TILT`, `ODD/EVEN`, `RATE`, `DEPTH`, `DETUNE`, `WIDTH`, `ATTACK`, `RELEASE`, `LEVEL`.
Inputs: `V/OCT` (poly ≤ 4), `GATE` (poly), `STRETCH CV`, `TILT CV`, `DEPTH CV`, `PARTIALS CV` (0–10 V over 1–32).
Outputs: `L`, `R` (mono sum of voices).
Tests: STRETCH 0 partial 3 = 3·f0 exactly; STRETCH +1 partial 4 = 4^1.5 f0; partials above Nyquist muted; CPU budget.

---

## 12. LANES, drum grid with drifting loop points (32 HP)

After the Sensate Focus live set (a grid of drum sounds and a keyboard sound) with SND's layers scrolling in different directions and the relaxed-to-intense density control.

### 12.1 Behaviour
- **6 rows × 32 steps** of on/off cells. Rows 1–5 are drum rows (labelled `R1` to `R5` on the panel, nameable in the menu, e.g. "KICK"); row 6 is the **pitch row** with a note value per step.
- Each **CLOCK** rising edge advances every row by one step (DURA TRIG or PRIME ANY make good irregular clocks).
- Each row has its own **LEN** (1–32, default 16): the loop point. Rows with different LEN drift against each other (hats on 13 or 15 against a kick on 16).
- Each row has its own **SCROLL** (−4 to +4, default 0): each time the row wraps, its pattern rotates by SCROLL steps (positive = right). Rows scrolling in different directions is the SND idea. `Reset` restores the unrotated pattern.
- **INTENSITY** (0 to 1, plus CV): thins the pattern from "relaxed" to "intense". Each active cell has a fixed rank: `rank = bitReverse5(step)` + a small per-row offset from `hash01(row)`, so downbeats survive longest. At intensity *x*, a cell plays if it is among the top `ceil(x × activeCount)` ranked cells of its row. 1 = everything drawn plays. Same knob position always gives the same pattern.
- Pitch row: each step has a value −24 to +24 semitones (edited by dragging vertically in the grid), output as V/OCT relative to 0 V, held between steps.
- **8 pattern memories** (the whole grid, LENs and SCROLLs): `PATTERN` knob 1–8 + CV (1 V per pattern). Pattern changes take effect when row 1 wraps (menu: or immediately).

### 12.2 Controls
- Grid widget (`StepGrid.hpp`): click toggles a cell, drag paints; on row 6, vertical drag sets pitch. Playheads drawn per row, cells beyond LEN greyed, cells thinned by INTENSITY shown dimmer.
- Per row: `LEN` (small knob), `SCROLL` (small knob), `MUTE` button.
- Global: `INTENSITY`, `PATTERN`, `RESET` button.
Inputs: `CLOCK`, `RESET`, `INTENSITY CV`, `PATTERN CV`.
Outputs: per drum row `TRIG 1..5` (1 ms) and one poly `GATES` output (5 channels, gate = clock period × 50% measured from the clock); row 6 `PITCH` (V/oct) and `PITCH GATE`.
Context menu: clear row/pattern, copy/paste pattern, `Fill from seed` (seeded generator making a club-style pattern, using SEED from a menu slider), rename rows.

### 12.3 Tests
Row LEN 13 vs 16: realign after 208 clocks. SCROLL +1: after each wrap the pattern is rotated by one. INTENSITY 0.5 on 8 active cells plays exactly 4, the same 4 every time, including the step-1 cell. Pattern switch on wrap.

---

## 13. CYCLE, parameter lanes in and out of phase (16 HP)

The TX81Z "each note steps to the next sound" idea, generalised: lanes of values with their own lengths, all advanced by the same trigger, so they drift in and out of phase.

### 13.1 Behaviour
- **4 lanes**, each a list of up to 16 values (drag bars in the `BarEditor` widget).
- Every rising **TRIG** advances each lane by one step, divided by the lane's `DIV` (1–8: lane moves every DIV-th trigger).
- Each lane's `LEN` (1–16) sets its loop. Lanes of length 3 and 5 realign every 15 triggers.
- Lane **MODE** (switch, 4 positions, labelled): `UNI` (0 to 10 V), `BI` (±5 V), `PITCH` (values quantised to semitones, −24 to +24 st, output V/oct), `VOICE` (integer 1–32, output 0.1 V per voice for FM4 VOICE CV; the bars show integers).
- `OFFSET` knob per lane adds to the output (in the lane's own units).
- **GLIDE** per lane (menu, 0–500 ms; default 0).
- RESET input returns all lanes to step 1.

### 13.2 Controls and ports
Per lane: `LEN`, `DIV`, `OFFSET` knobs; `MODE` switch; output `OUT A..D`.
Global: `RESET` button. Inputs: `TRIG`, `RESET`. Output `POLY` (4 channels, A–D).
Display: each lane's bars with its playhead; above, `REALIGN IN n` (LCM of LEN×DIV, counted in triggers).
Tests: LEN 3/5 realign at 15; DIV 2 halves the speed; VOICE mode value 12 outputs 1.1 V (voice 12 = (12−1) × 0.1 V).

---

## 14. HOLD, sustain against the gap (6 HP)

Fell's "isolate and expand the sustain": the length of a sound as a percentage of the time to the next sound.

### 14.1 Behaviour
- On each rising **TRIG**, HOLD decides a gap `g`:
  - If **INTERVAL** is patched (from DURA), `g = 100 ms × 2^V` (the time to the *next* event, known in advance).
  - Otherwise `g` = the time since the previous trigger (the last gap), clamped 1 ms to 10 s; the first trigger after reset uses 250 ms.
- Gate length `= LENGTH% × g`, LENGTH 1% to 200% (above 100% the sound overlaps the next onset; on the next trigger the envelope restarts from its current level, no click).
- Envelope: `ATTACK` (0 to 200 ms, default 1 ms), then held at full until the gate length, then release; `SHAPE` (0 to 1) morphs from a hard gate (2 ms release) to an exponential decay that reaches −60 dB at the gate length. `ENV` output 0–10 V.
- Built-in VCA: `IN` (poly) × envelope → `OUT` (poly). Linear VCA with a 0.5 ms smoothing.
- `LENGTH CV` (±5 V = ±100%).

Ports: inputs `TRIG`, `INTERVAL`, `LENGTH CV`, `IN`; outputs `GATE`, `ENV`, `OUT`. Knobs `LENGTH` (default 50%), `ATTACK`, `SHAPE`.
Tests: triggers 200 ms apart, LENGTH 50%: gate 100 ms. With DURA INTERVAL patched the gate follows the next interval even when it changes.

---

## 15. Leaning on existing modules (document in the manual)

- **Mixing**: VCV's Mix or a Bogaudio mixer. No Fallen mixer.
- **Reverb**: only for Sensate Focus-type pieces (Fell's solo work is dry). Valley Plateau.
- **Recording the output and snapshots**: VCV Recorder for audio. Stoermelder 8FACE or TRANSIT to store and recall module settings, which fits "run it, change parameters, record". Check at build time that these are still in the library; name the ones used in DECISIONS.md.
- **Tempo clock** when DURA should follow a BPM: any clock module into DURA CLOCK (e.g. Impromptu Clocked).
- **Alternative FM**: Valley Dexter exists, but has no SysEx loading of 4-op banks or voice stepping, which is why FM4 is built.

---

## 16. General tests

For every module: the behaviour tests in its section, plus
- **Stability**: 10 s of ±10 V noise into every input with random knob settings at 44.1, 48, 96, 192 kHz. All outputs finite and within ±10 V; gates exactly 0 or 10 V.
- **Sample-rate independence**: event times identical to one sample; pitches within 1 cent.
- **Determinism**: two runs of any timing module from RESET with the same settings produce identical event lists.
- **Save/reload**: every module's JSON round-trips (FM4 bank, KIT paths and embedded samples, LANES patterns, CYCLE lanes, CHORD slots, DURA steps).
- **End to end**: a host test that runs the DURA → CYCLE → FM4 and DURA → LANES → KIT engines together for 10 s and writes `tests/out/fallen-demo.wav` (not committed) so the build can be listened to. Copy one render to `/mnt/project-files/fell-vcv/fallen-demo.wav` if that folder is reachable.

In Rack before hand-over (if a Rack install is available; otherwise say so in the hand-over): load every module, load a `.syx` bank and a kit folder, patch every manual recipe, change sample rate while running, save and reload.

---

## 17. Manual (`docs/MANUAL.md` and `docs/Fallen-Manual.pdf`)

Same structure as the Serge, Tudor and Silverbox manuals: what each module is for (with the Fell technique it comes from), a control table, then patch recipes as cable lists with knob settings. Build the PDF from the Markdown with a script (pandoc plus an HTML-to-PDF engine), with rendered panel images. Copy the PDF to `/mnt/project-files/fell-vcv/` if reachable.

Patch recipes (at least):
1. **Microtime from a table**: DURA in UNITS, UNIT 40 ms, `Fill from table`, SPREAD 1 → KIT C.HAT and FM4 METAL PERC. Then the same at UNIT 50 ms.
2. **Stutters**: steps with REP 8 at short intervals for the machine-gun loops.
3. **One slider**: a 6-step UNITS pattern (3,3,3,4,2,1); perform only with STRETCH (and a slow LFO into STRETCH CV).
4. **Bursts**: GROUP COUNT 5, CURVE −0.6, DRIFT on, into KIT and HOLD → FM4.
5. **Loops a little too short**: LANES kick on 16, hats on 13 and 15, a clap row scrolling left, INTENSITY performed from relaxed to intense.
6. **Nested primes**: PRIME A 3, B 5 NEST, C 7, D 11 into four pitched FM4 voices (KALIMBA, MARIMBA, BELL, METAL PERC) through CYCLE for pitches. A nod to Intra's metallophones.
7. **Sounds against notes**: CYCLE pitch lane length 5, FM4 STEP on with COUNT 3. The in-and-out-of-phase trick.
8. **Expanding the stab**: CHORD m9 → FM4 HOUSE ORG; DURA INTERVAL → HOLD; sweep HOLD LENGTH from 5% to 150%.
9. **Long linear spectrum**: SPECTRA drone with slow MOVE under recipe 1.
10. **Your own sounds**: loading a 4-op `.syx` bank into FM4 (where to find banks, what the load report means, tweaking with the macros and saving the bank back out), and loading a kit folder into KIT.
11. **Generate, record, perform**: run a system, change parameters by hand, record with VCV Recorder, store settings with 8FACE.

Also: a "Where the ideas come from" page (short summary of RESEARCH-AND-PROPOSAL.md with sources), the SysEx formats FM4 reads, and a note that the plugin is independent; names belong to their owners.

---

## 18. Order of work

1. Repo skeleton, Makefile, CI (mac arm64 + Linux tests), `plugin.json`, shared components, panel script, ten empty modules that load.
2. `Hash`, `Clock`, gate/trigger helpers with tests.
3. DURA engine and module (most important; get it right first), then GROUP, PRIME, HOLD.
4. FM4 engine, tables, envelope, LFO with tests.
5. SysEx import/export with tests; factory bank; FM4 module with stepping and the edit section.
6. CHORD.
7. KIT synth voices, then WAV loading, resampling, sample playback, kit folder, persistence.
8. SPECTRA.
9. LANES and CYCLE (grid and bar widgets).
10. Panels drawn, rendered, inspected; label check passes.
11. Manual and PDF; demo WAV; full test pass; CI green.
12. Hand-over note: what was built, DECISIONS.md contents, the final values of every estimated constant, what in §19 could or could not be verified, install steps.

---

## 19. Accuracy ledger

**From the research (see RESEARCH-AND-PROPOSAL.md for sources):** millisecond step durations with repeat counts; integer × 40/50 ms interval tables; repeat tables for stutters; groups of events compressed or expanded; random 0–99% gate lengths (made deterministic here); one parameter stretching a whole integer-multiple pattern; step ratios to a fundamental (triplet steps); loops one beat or one sixteenth short; relaxed/intense density control; layers scrolling in different directions; nested prime-number loops; 4-op FM presets tweaked a little; TX81Z-style stepping through voices per note; house organ stabs with variable sustain; dry drum-machine samples (sharp early-80s kick, deeper 808/909 style later); long linear-spectrum textures.

**Interpretations (ours, log in DECISIONS.md):** PRIME's Bjorklund hits and NEST rule; GROUP's geometric CURVE and DRIFT; LANES's bit-reversal priority for INTENSITY; HOLD using DURA's next interval; SPECTRA's MOVE window.

**Estimates to verify against the 4-op owner's manuals (all in `Fm4Tables.hpp`):** algorithm chart; ratio table; fine and fixed-frequency tables; waveform shapes W2, W4, W6, W8; envelope rate-to-time curves; output-level dB per step and modulation depth at 99; feedback scale; detune cents; level scaling and rate scaling rules; LFO speed and delay curves; PMS/AMS depths; VMEM byte layout, in particular the TX81Z extension bytes 73–83.

**KIT estimates:** all synthesis frequencies, decays and the 28 kHz / 8-bit companding of EARLY.

**Sources for the build**
- Yamaha DX21, DX27/DX100 and TX81Z owner's manuals (MIDI data format and voice parameter appendices). Widely archived online.
- YM2151 (OPM) datasheet for the algorithm set and envelope behaviour.
- dr_wav: https://github.com/mackron/dr_libs
- VCV Rack manual pages listed in §1.
