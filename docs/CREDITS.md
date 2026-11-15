# Credits and sources

## Third-party code

- **dr_wav** by David Reid (`src/io/third_party/dr_wav.h`), public domain or MIT No Attribution, at the user's choice. Used to read WAV files in KIT.
- **Inter** typeface (SIL Open Font License 1.1), converted to paths in the panel SVGs by `scripts/make_panels.py`. No font file ships with the plugin.
- Rack's own **ShareTechMono** font is used for the displays.
- The build workflow, test harness and manual scripts come from the author's own `Tudor` plugin.

## Formats

- 4-operator Yamaha SysEx (VMEM 32-voice bulk, VCED single voice, ACED additional voice data): laid out from the DX21/DX27/DX100 and TX81Z owner's manuals' MIDI data format appendices as summarised in the build spec. No third-party parser code was copied. The byte layout is locked by round-trip tests; it has not yet been checked against a real bank with a published parameter listing (see DECISIONS.md).

## Research

The ideas behind each module, and the sources (interviews, papers, the Max patches Mark Fell has described), are in [RESEARCH-AND-PROPOSAL.md](RESEARCH-AND-PROPOSAL.md).
