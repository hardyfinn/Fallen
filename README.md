# Fallen for VCV Rack

Ten VCV Rack 2 modules for millisecond-timed pattern systems, 4-operator FM (with SysEx loading) and drum machines, in the manner of Mark Fell.

| Group | Modules |
|---|---|
| Time | DURA (duration sequencer), GROUP (bursts), PRIME (nested prime loops), HOLD (sustain against the gap) |
| Patterns | LANES (drum grid with drifting loops), CYCLE (lanes in and out of phase), CHORD (chord memory) |
| Sound | FM4 (4-op FM voice, loads `.syx` banks), KIT (drum machine, loads WAVs), SPECTRA (additive spectrum) |

In Rack's browser, search for the short name (`DURA`, `FM4`, `KIT`...) or for words like *milliseconds*, *SysEx*, *WAV*, *prime*.

- Manual with patch recipes: [docs/MANUAL.md](docs/MANUAL.md), also as [docs/Fallen-Manual.pdf](docs/Fallen-Manual.pdf).
- Design choices and where the build departs from the spec: [docs/DECISIONS.md](docs/DECISIONS.md).
- Sources and third-party code: [docs/CREDITS.md](docs/CREDITS.md).
- The design: [docs/RESEARCH-AND-PROPOSAL.md](docs/RESEARCH-AND-PROPOSAL.md) and [docs/BUILD-INSTRUCTIONS.md](docs/BUILD-INSTRUCTIONS.md).

## Install

GitHub Actions builds the plugin for Apple silicon Macs (`mac-arm64`) on every push. Download the `Fallen-mac-arm64` artifact from the latest run under Actions, unzip it, and copy the `.vcvplugin` file into Rack's plugins folder (`~/Library/Application Support/Rack2/plugins-mac-arm64/`). Rack unpacks it on the next start. Tagged versions (`v*`) also appear under Releases.

## Build

```
export RACK_DIR=/path/to/Rack-SDK
make && make install     # or: make dist
make test                # host tests of the engines; no Rack SDK needed
make panels              # regenerate res/*.svg and src/generated/Layout.hpp
```

`make test` also renders `tests/out/fallen-demo.wav`, ten seconds of DURA, CYCLE, FM4, LANES, KIT and SPECTRA playing together.

## Layout of the code

- `src/time`, `src/dsp`, `src/io`: engines with no Rack dependency, tested by `tests/`.
- `src/modules`: the Rack modules, thin wrappers around the engines.
- `scripts/make_panels.py`: draws every panel and generates the control placement, so panel and code cannot disagree.

Fallen is an independent project. It is not affiliated with Mark Fell, Yamaha, Roland or any other maker; names belong to their owners.

GPL-3.0-or-later.
