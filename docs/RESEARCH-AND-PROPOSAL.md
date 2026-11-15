# Mark Fell-style modules for VCV Rack: research and proposed set

Draft 1, 2026-11-15. Nothing here is fixed until Hardy picks. See "Choices for Hardy" at the end.

Working plugin name: **Microtime**. Mark Fell is a living artist's personal name, and his album titles (Multistability, UL8, Sensate Focus) are his, so neither goes in the plugin, module or panel names. The manual can say "in the manner of Mark Fell" when describing what a module is for.

Sources are listed at the end. Where something is my inference rather than something Fell has said, it is marked (inf.).

---

## Part 1. How Fell's systems actually work

### 1.1 No timeline, no BPM: milliseconds and loops

- Almost none of his solo music is written on a timeline. He builds **small Max/MSP systems with three or four parameters**, runs them in real time, changes the parameters by hand, and records the output as MIDI. The MIDI then drives synths and samplers. He calls the systems "very crude" and "very, very simple".
- He works in **milliseconds, not beats per minute**. A bar's length can be anything and its divisions are not halves or quarters.
- His sequencers are lists of **steps, each with its own duration and its own repeat count**. He found it "much more interesting" with durations in ms than as fractions of a bar.
- Another form: **each step is a ratio to a fundamental tempo**, so one step can be a triplet and the next not ("sounds great on hi-hats").
- For a piece with Okkyung Lee: a five or six step rhythm where each step is an **integer multiple of a base value** (example 3, 3, 3, 4, 2, 1). **One slider changes the base value and so stretches the whole pattern.** This is the core "time-stretched pulse pattern" idea: the shape is fixed, the time scale moves.
- Choices are not random. "Nothing from last night was random." Where a system chooses, it chooses among a few fixed values.

### 1.2 Multistability (2010): the three published methods

Fell described three methods in his 2013 portfolio "Works in sound and pattern synthesis":

1. **On-phase control.** Trigger times are set by hand (for example a 1000 ms interval). The **sound's duration is a random 0 to 99% of the interval**. Changing the interval changes the speed.
2. **Tables.** Intervals are **integers 1 to 10 multiplied by a unit (40 ms or 50 ms)**, so the rhythm is irregular but always on a fine hidden grid: "somewhat regular". A second table sets a **speed and a repeat count**, so a very short interval repeats N times: the glitchy machine-gun loops.
3. **Dynamic durations.** A **group of N events** (five in his example) at set intervals, with groups fired at a longer interval. The group is **compressed or expanded** at a rate set by hand, so the inner spacing speeds up or slows down.

The result is what he calls microtemporal rhythm: no stable tempo or meter, very short time units, and a pattern that keeps flipping in how you hear it. That flip is what "multistability" means for him: "a rapid and ongoing switch in how we perceive", "no fixed state".

### 1.3 Loops that are a little too short or long (Sensate Focus)

- Sensate Focus tracks were drawn with a pencil tool, not generated, but the loops are not 4/4. A riff loops over **seven beats instead of eight**, hi-hats loop over **13 or 15 sixteenths instead of 16**. He talks about **"collapsing or expanding the loop point"** to make tension.
- Loops can have 16, 30 or 80 divisions.
- Live, this became a **grid in Max: rows of drum sounds plus a keyboard sound**.
- From the SND days: **layers scrolling in different directions**, and a single **"relaxed" to "intense" control** for density.

### 1.4 Intra (2018): nested prime loops

- Four percussionists, each hearing a different pattern built from **nested loops of prime-number lengths**. The parts drift out of step and back, but the whole is "totally stable". Fell ran it live, pressing start and stop and sending triggers at the change points.

### 1.5 Sound: FM presets, stepping voices, house stabs, drum machine samples

- Pitched sounds are mostly **4-operator Yamaha FM** (TX81Z, DX100), or FM8 loaded with those patches. The sounds are **"presets just tweaked a little bit"**. Named favourites are an organ preset (the one heard on early-90s house records such as "Show Me Love") and a bass preset.
- Why FM: it moves harmonics and inharmonics around. He got "really bored of just a low pass filter". He changes timbre through the synthesis, not through effects. Treble comes from the algorithm, not EQ.
- **The TX81Z trick**: in one of its performance modes each new note steps to the **next voice in a list**, so the sound sequence and the note sequence run **in and out of phase**. He still uses this idea. The same thing appears in his earliest rig: a drum machine loop of three events triggering a synth sequence of five.
- **Duration is a parameter in its own right**. He wanted to "isolate and expand" the sustain of early-90s house organ stabs; a piece can be just the variation of a sound's length against the gap to the next onset.
- Multistability is "two kinds of sounds: rhythmic sounds and pitched chorded sounds". At times the pitched part is reduced to **"long linear spectrum"** textures under the percussion.
- **Drums are drum-machine samples**, chosen to point at moments in club history: a sharp early-80s kick, then deeper 808/909-style kicks after about 1986. He spent a long time on kicks and claps. (The coordinator's note mentions Linn-style drums; the sources read don't name Linn, but early-80s sampled drum machines are exactly that family (inf.).)
- Solo work is dry. Reverb only shows up in Sensate Focus.

### 1.6 Threads to carry into modules

1. Time is a list of durations (ms, unit multiples, or ratios), and one knob rescales the whole list.
2. Each step can repeat N times at its own speed.
3. Duration of the sound relative to the gap is its own control.
4. Separate cycles of different lengths (notes, voices, timbres, loop points) drift in and out of phase. Never random; always deterministic and repeatable.
5. 4-op FM with "tweak a preset" workflow, and a voice that steps through presets.
6. Dry, sharp drum-machine kit with era character.

---

## Part 2. Proposed set: 10 modules

Short names are what you type in Rack's browser. All panels use text labels.

### Time: making the patterns

**1. DURA — duration sequencer** (the heart of the set)
- Up to 16 steps. Each step has a **length**, a **repeat count** (1 to 16) and a **repeat speed** (the stutter table from method 2).
- Step length mode, one switch: **MS** (raw ms), **UNITS** (integer 1 to 16 × a UNIT knob, method 2 and the Lee piece) or **RATIO** (1, 3/4, 2/3, 1/2, 1/3 ... of a BASE time, the triplet-on-hats idea).
- **STRETCH** knob and CV scale every step at once. This is the one-slider "time-stretch".
- **LENGTH** (gate as % of the interval, 1 to 99%) with a **SPREAD** that varies it per step from a fixed seed (method 1, repeatable not random).
- **LOOP END** with a fine **NUDGE** to collapse or expand the loop point by one step or by milliseconds.
- Outputs: TRIG, GATE, STEP (CV), INTERVAL (CV of the current gap, for HOLD and the voices), END-OF-CYCLE.
- Optional clock input: when patched, UNIT/BASE follow the clock so it can sit in a tempo-based patch.

**2. GROUP — compressing and expanding bursts** (method 3)
- Fires a group of N events (1 to 16), inner spacing SPACE ms, groups every PERIOD ms.
- **CURVE** compresses or expands the spacing across the group (accelerando to ritardando).
- **DRIFT** changes SPACE a little each group, so the burst slowly tightens or loosens over minutes; it reverses at the limits.
- Outputs: TRIG, FIRST (first of group), LAST, INDEX CV.

**3. PRIME — nested prime loops** (the Intra method)
- Four outputs. Each is a loop over a common pulse; its length is picked from primes (2, 3, 5, 7, 11, 13, 17, 19, 23...).
- **NEST**: each loop can be gated by the one above it, so a 7-loop only plays inside the 3-loop's "on" span (inf. on the exact nesting; Fell doesn't publish the rule).
- Shows the cycle count until all four realign. RESET and a REALIGN output when they meet.

**4. LANES — grid with drifting loop points** (the Sensate Focus live grid)
- 6 rows × 32 steps of gates, one row per drum sound plus one pitch row.
- Each row has its **own loop length** (so hats can loop on 13 or 15 while the kick loops on 16) and its own **SCROLL** (rotate left or right by k steps each time the row wraps: the SND scrolling layers).
- **INTENSITY** knob: thins the pattern from "relaxed" to "intense" using a fixed priority per step, so the same knob position always gives the same pattern.
- Accepts a DURA TRIG as its clock, so the grid can run on irregular time.

**5. CYCLE — parameter lanes in and out of phase** (the TX81Z stepping idea, generalised)
- Four lanes, each a short list of values (2 to 16) with its **own length**. Every trigger advances all lanes.
- Lane outputs: CV (pitch, timbre, decay, anything) and a VOICE output for FM4's voice slot select.
- With note lane length 5 and voice lane length 3 you get the three-against-five phasing from his first rig. A CLOCK DIV per lane lets a lane advance every 2nd, 3rd... trigger.

**6. HOLD — sustain against the gap**
- Takes a trigger, measures the time since the previous one, and outputs a gate and an envelope whose length is **a percentage of that gap** (or of the INTERVAL CV from DURA, which knows the *next* gap).
- Has a simple VCA built in so it can shape a stab directly. This turns "isolate and expand the sustain" into one knob.

### Sound: voices

**7. FM4 — 4-operator FM voice**
- 8 algorithms and 8 waveforms in the spirit of late-80s 4-op FM, per-operator ratio or fixed frequency, level, rate-based envelopes, feedback on op 4.
- **8 voice slots**, selected by a knob or the VOICE CV, with **STEP**: each new gate moves to the next slot (the stepping trick built in).
- **Polyphonic up to 8 voices** for chorded stabs.
- Macro knobs for the "tweak a preset" workflow: BRIGHT (modulator levels), RATIO SHIFT, DECAY, DETUNE.
- Factory presets with our own names covering: house organ, bell, electric piano, lately-style rubbery bass, metallic perc, hollow pad, pluck, noise hit.
- Optional: load a 4-op `.syx` bank (32 voices) from your own synth or the internet. See Choices.

**8. CHORD — chord memory for stabs**
- Holds up to 8 chord shapes (house minor 7, minor 9, 11th, sus, quartal...), selected by a knob or CV, transposed by V/oct. Outputs poly V/oct to FM4.
- **VOICING** knob spreads or closes the shape; INVERT steps through inversions.

**9. KIT — drum machine voices with era**
- Six voices: kick, snare, clap, closed hat, open hat, rim/perc. Each has TUNE, DECAY, LEVEL and its own trigger and out, plus a mix out.
- **ERA** switch per voice: **EARLY** (sharp, short, early-80s sampled machine character: band-limited, slightly gritty 8-bit-style samples made by our own synthesis) and **ANALOG** (deeper 808/909-style synthesis).
- Synthesised, no sample files, so it ships as one plugin with nothing to install.

**10. SPECTRA — long linear spectrum**
- Additive voice of up to 32 partials. **STRETCH** moves partials from harmonic to inharmonic, **TILT** sets brightness, **MOVE** slowly shifts which partials are loud.
- For the Multistability-style textures under the percussion. Also takes V/oct so it can play chords via CHORD.

### How they fit

DURA (or PRIME, or GROUP) makes the time. LANES and CYCLE decide what plays on each event. HOLD sets how long it sounds. FM4, KIT and SPECTRA make the sound. A typical patch: DURA TRIG → CYCLE → FM4 voice slot and CHORD select; DURA INTERVAL → HOLD → FM4 gate; DURA TRIG → LANES clock → KIT.

---

## Part 3. Where existing VCV modules already do the job

- **Mixing**: VCV Mix or Bogaudio mixers. No need for our own.
- **Reverb** (only for Sensate Focus-type pieces): Valley Plateau.
- **Recording MIDI-style output**: VCV Recorder for audio, or Stoermelder's TRANSIT / 8FACE to snapshot module settings. This matches his "run it, record it, edit later" workflow without us building a recorder.
- **General FM**: Valley Dexter is a fine 4-op FM voice, but it has no voice-slot stepping, no presets in this style and no .syx import, which are the point here. FM4 is still worth building.
- **Clock**: Impromptu Clocked if Hardy wants to drive DURA from a BPM clock.

---

## Choices for Hardy (defaults in bold)

1. **Plugin name**: **Microtime**, or suggest your own.
2. **Module count**: **all 10**, or a core 6 (DURA, CYCLE, HOLD, LANES, FM4, KIT) first.
3. **FM4 sounds**: **built-in presets plus optional loading of 4-op `.syx` banks**, or built-in presets only.
4. **KIT**: **synthesised only (no sample files)**, or also allow loading your own drum WAVs.
5. **Repo**: create an empty **hardyfinn/Microtime** (or your chosen name) and add it to the project, as with Tudor and Silverbox.

Standing preferences already applied: text labels, short searchable names, PDF manual with patch recipes, build on main, Apple silicon Mac build only.

---

## Sources

- peeq, "On the Rhythmic Structures of Mark Fell" (2021), summarising Fell's "Works in sound and pattern synthesis" (2013): https://note.com/n_peeq_t/n/n27a24987d4e6
- FACT, "Mark Fell on his love of FM synthesis and algorithmic composition" (2018): https://www.factmag.com/2018/10/06/mark-fell-signal-path/
- Stray Landings, "Draw The Notes" interview (2016): https://straylandings.co.uk/_content/interviews/2016-09-07-mark-fell-interview/
- Red Bull Music Academy Daily interview (2012): https://daily.redbullmusicacademy.com/2012/05/mark-fell-interview/
- BOMB, "Intra-actions: Mark Fell Interviewed" (2018): https://bombmagazine.org/articles/intra-action-mark-fell-interviewed
- Arie Altena, interview with Mark Fell: https://ariealt.net/ariealt_txt/interview_mark_fell.html
- Wikipedia, Mark Fell (artist): https://en.wikipedia.org/wiki/Mark_Fell_(artist)
