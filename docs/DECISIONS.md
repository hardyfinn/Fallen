# Decisions and departures from the spec

Where the build had to choose, or departs from `BUILD-INSTRUCTIONS.md`, it is written down here. Values marked *est.* are estimates that have not been checked against hardware or manuals; they all live in one place in the code so they can be tuned.

## General

- **Fonts.** Panels use Inter, converted to paths by `scripts/make_panels.py`. Displays use Rack's built-in ShareTechMono, so no font ships with the plugin.
- **Panel widths.** As specified, except PRIME is 18 HP instead of 14, so its four rows of knobs, switches and jacks keep their labels readable.
- **Panels and code are generated together.** `make_panels.py` writes both the SVGs and `src/generated/Layout.hpp` (ids and positions), and fails on overlapping controls or missing labels.
- **Determinism.** Everything that varies (gate spread, S&H, detune spread, noise seeds) uses `hash01`, never a live random source. Two runs from RESET are identical.
- **Resampler.** KIT resamples WAVs at load time with our own Blackman-windowed sinc resampler (`WavLoad.cpp`) instead of speexdsp. It is tested to keep a 1 kHz tone within 0.1% and to reject images below −80 dB, and it keeps the build free of an extra library.

## DURA, GROUP, PRIME, HOLD

- **DURA** keeps a separate set of the 16 LEN knob values per MODE (MS, UNITS, RATIO), so switching mode and back does not lose a pattern.
- **DURA** with an external clock waits for the first clock edge before it starts.
- **GROUP curve.** The gap before event *k* is `SPACE × 2^(CURVE × 3 × (k−1)/(COUNT−2))`. The next group starts at `span + SPACE` if the group is longer than PERIOD, otherwise at PERIOD. DRIFT is a slow triangle. This is our reading of "compressed or expanded groups".
- **PRIME nest rule.** A nested row advances only when the row above it fires. Row A cannot nest. Our interpretation of nested prime loops.

## LANES, CYCLE, CHORD

- **LANES** patterns store the cells and pitch row only. LEN and SCROLL are live knobs, not stored per pattern, so they can be performed.
- **LANES INTENSITY** ranks steps by bit-reversed position (5-bit), so lowering intensity removes the off-beat steps first, deterministically.
- **CYCLE MODE** is a snap knob (UNI, BI, PITCH, VOICE) with the mode shown in the display, instead of a switch. VOICE mode outputs 0.1 V per voice for FM4's VOICE CV.
- **CYCLE** plays step 1 on the first trigger after reset (it does not advance on that trigger).
- **CHORD keyboard** covers offsets 0 to +24 semitones (the spec allows −12 to +24 in data; the widget shows the two octaves above the root).

## FM4

- **Engine.** Phase modulation at 2× oversampling, decimated by a 31-tap half-band filter. Phases reset at note-on. Each voice's parameters are re-prepared every 128 samples, so edits and macro moves reach sounding notes within about 3 ms.
- **Macros** apply continuously, not only at note-on. **ATTACK** and **DECAY** macros: + means longer (slower rates), which reads more naturally on the panel than the spec's "higher = shorter".
- **SPREAD** detunes each poly channel by a fixed, deterministic amount within ±SPREAD cents.
- **Pitch EG** is stored and saved, but not applied. **Pitch bend range, portamento, mono mode, foot and breath controller** settings are stored and saved but ignored, as the spec allows (Rack patches do this with cables).
- **Factory bank** is defined in code (`FactoryBank.hpp`) as `Voice4op` values rather than a VMEM byte image. It exports to `.syx` and re-imports identically (tested).
- **Patch storage.** The working bank and the originals (for Revert voice) are saved in the patch as base64 VMEM images. Every field round-trips (tested). A voice whose TX81Z extension bytes are all zero reloads as non-extended, which sounds the same.
- **Bank loading** is parsed on the UI thread and handed to the audio thread through an atomic pointer, so the audio thread never allocates.
- **Edit knobs** follow the VOICE knob's voice and the chosen operator. When either changes, or a bank loads, the knobs are set from the voice and not written back. Randomise is turned off for them, so "Randomize" on the module does not scramble a voice.
- **Estimated constants** (`Fm4Tables.hpp`), all *est.*, not checked against the manuals:
  - decay time for a full 96 dB: `38 s × 2^(−(r−1)/2.35)`; attack `9 s × 2^(−(r−1)/2.1)`
  - output level: `(99 − OUT) × 0.75 dB`; D1L: `(15 − D1L) × 3 dB`
  - feedback, in cycles of phase: `0, 1/32, 1/16, 1/8, 1/4, 1/2, 1, 2`; modulator depth at OUT 99: 2 cycles
  - LFO: 0.06 to 50 Hz (exponential), delay 0 to 5 s; PMS cents `0, 5, 10, 20, 50, 100, 400, 1200`; AMS dB `0, 6, 12, 24`
  - detune: ±2.6 cents per DET step; rate scaling adds `RS × (note − C1)/24` to every rate; level scaling up to 24 dB per octave above C3 × LS/99; velocity sensitivity up to 24 dB at KVS 7
  - fixed frequency: `(16 × (CRS >> 2) + FINE) × 2^FIXRG` Hz
  - FINE in ratio mode multiplies the ratio by `1 + FINE/16` (the TX81Z fine-ratio table was not available to check)
  - waveforms W1–W8 as the spec's starting model
  - TX81Z extension bytes 73–83 in the 32-voice dump as the spec's layout
- **No real `.syx` bank** with a published parameter listing could be used in the tests from the cloud container, so the byte layout is checked against the spec (from the manuals) and by round trips only. Loading one of Hardy's own banks and comparing a voice with the synth's display is the first thing to try in Rack.

## KIT

- **TUNE** is ±24 semitones for every source (the spec has ±12 for synth voices), so one knob range covers both.
- **DECAY** is one control: 0.1× to 2× the natural decay for synth voices. For samples, below 1× it fades the sample out over that fraction of its length; at 1× and above the sample plays in full.
- **A SAMPLE voice with no sample** (not loaded yet, missing or failed) plays its EARLY synth voice, and the name strip says `NO SAMPLE`, `LOADING`, `MISSING: name` or `ERROR`.
- **Clear sample** returns the voice to its default source (KICK ANALOG, others EARLY).
- **Velocity** is the trigger voltage at the rising edge, linear, 10 V = full. ACCENT adds 4 dB.
- **Snare snappy** (ANALOG) is a menu choice of 0.3, 0.6 (default) or 0.9.
- **Kick drive** (ANALOG) is a tanh saturation, off by default.
- **Hat levels**: the hats are scaled ×1.4 (EARLY) and ×2.2 (ANALOG) so all six voices sit at similar peak levels.
- **Choke** (C.HAT stops O.HAT over 20 ms) is on by default and can be turned off in the menu.
- **Store samples in patch** embeds the original (not resampled) audio as 16-bit, so the patch works at any sample rate.

## SPECTRA

- Partial amplitudes are normalised so the sum never exceeds 1, then scaled to ±5 V by LEVEL; more partials do not get louder.
- The MOVE window phase is shared by all voices and saved in the patch, so a reloaded patch continues where it was.
- Partial phases start from fixed, spread values (from `hash01`) to avoid a click of 32 aligned sines.

## Not done in Rack

The plugin was built and tested in a cloud container without a Rack install. The engines are tested on the host (`make test`), the modules compile against Rack 2.6.4's headers, and CI builds the Apple silicon plugin. Nobody has yet loaded the modules in Rack, loaded a real `.syx` bank or kit folder, patched the manual's recipes, or changed sample rate while running.
