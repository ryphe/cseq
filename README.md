<p align="center">
  <img src="cseq.png" alt="cseq icon" width="128">
</p>

<h2 align="center">cseq</h2>

<p align="center">
  multitrack sampler sequencer workstation (C99/Win32)
</p>

---

<p align="center">
  <img src="screenshot1.png">
</p>

---

### media

| Component | Description |
| :--- | :--- |
| **Audio Input** | `WAV`, `MP3`, `FLAC`, `OGG`, `M4A`, `AIFF`; SoundFont 2 (`.sf2`) in MIDI clips |
| **Audio Output** | `44.1 kHz` `Stereo` mixdown, `WAV` export (16‑bit & 24‑bit TPDF dithered, 32‑bit float) |

---

### controls

| Component | Description |
| :--- | :--- |
| **Keybinds Reference** | Press `K` or click the `[KEYBINDS]` button in the top toolbar to open a searchable reference window listing every keyboard shortcut |
| **Timeline Canvas** | Click a clip to select it; drag to reposition. Drag left/right edges to **trim** length; hold `Alt` while dragging a clip to **slip‑edit** its internal sample offset (audio content shifts under the clip). Drag the small fade‑in/out handles (top corners) to adjust fade length. Drag a marquee (click empty space and drag) to select multiple clips; `Ctrl+Drag` a selection to **duplicate** clips in place. `Shift+Drag` a sample clip's edge to both adjust playback rate and edge position. Right‑click any clip for context menus (rate, fades, granular, slicing) |
| **Track Headers** | Click the track number/name area to **mute** the track; `Shift+Drag` vertically to **reorder** tracks. Hover and use the **scroll wheel** to adjust track volume. Right‑click to open dedicated dialogs |
| **Piano Roll** | Click on the note grid to **add** a note; double‑click or right‑click to **delete**. Drag a note left/right to **move** it in time; drag its right edge to **resize** its length. Scroll the mouse wheel while hovering over a note to adjust its **velocity** (0–127). The four ADSR knobs (Attack, Decay, Sustain, Release) shape the amplitude envelope of every sample or soundfont note in the clip |
| **Media Explorer** | Browse your file system hierarchy. Click any audio file to **preview** it through the master bus (supports looped or one‑shot playback via the Repeat toggle). Use the **Import** button to add the selected file at the timeline cursor, or **drag** any file directly from the explorer onto the main canvas to place it at a precise track and beat position |
| **FX Rack** | Drag an effect from the **modules list** (left pane) onto the **chain** (right pane) to insert it. Drag modules vertically within the chain to reorder the processing order. Right‑click a module in the chain to remove it. Click a chain slot to select it and reveal its parameters in the bottom strip, adjust with direct knob/slider drags or mouse wheel |
| **Synth UI** | For **Quadrum** (drum synth): click the 8 voice pads (Kick, Snare, etc.) to select and audition a voice; 16 knobs below edit its parameters. For **Halo** (poly synth): select from the preset dropdown (8 factory presets) or tweak the 29 knobs across the rack. Both synth interfaces provide real‑time audition directly from the piano roll |

---

### features

| Component | Description |
| :--- | :--- |
| **Scale & Capacity** | 128 tracks, 1024 bars, 2048 clips, 256 unique PCM buffers (shared via zero‑copy referencing), 32‑step undo/redo |
| **Sample Editing** | Non‑destructive, zero‑copy slicing. Trim, split, and duplicate metadata only (audio never copies). Per‑clip volume, playback rate (0.01×–2.00×), slip‑edit offset, and snap‑to‑grid quantisation. Four fade curves (linear, exponential, logarithmic, smooth). Marquee selection across the main timeline and piano rolls |
| **MIDI & Sequencing** | Full piano roll with per‑note velocity, length, and pitch editing. Built‑in **generative note sequencer** for chord/arpeggio patterns (configurable root, scale, chord type, and pattern direction). **Humanize** tool for timing, duration, and velocity variation. Per‑track **trigger probability** (0‑100%) for probabilistic sample/note triggering during playback |
| **Granular Synthesis** | Dedicated per‑clip and per‑track granular engine. Independent density, grain size, pitch jitter, pan spread, and envelope (attack/release). Freeze and drone modes for infinite sustained textures |
| **Quadrum Drum Synth** | 8‑voice procedural virtual‑analog drum module. Voices: Kick, Snare, Clap, Closed/Open Hat, Tom, Cowbell, Cymbal. FM, noise shaping, biquad filtering, and multi‑tap flam/spread for realistic clap burst effects |
| **Halo Poly Synth** | 8‑voice hybrid additive/FM subtractive synth. Continuous waveform morphing (sine → triangle → square → saw), spectral tilt, unison detune with stereo spread, and analog‑style chorus ensemble. Zero‑delay TPT state‑variable filter (LP/HP/BP) with drive |
| **SoundFont 2 Player** | Load `.sf2` banks with full preset browser and instrument selector. Pre‑rendered note cache ensures zero‑latency, deterministic playback without real‑time synthesis lock‑in, just sample‑accurate streaming |
| **Track Tools** | Per‑track **3‑band parametric EQ** (sweepable frequency/Q, ±12 dB gain) with real‑time curve display. **Stackable filter plotter** (LP/HP/BP/Notch) with live drag‑adjustable cutoff and Q; active bands combine in series |
| **Modular FX Rack** | Drag‑and‑drop chain of up to 8 effects per track. Modules: Gain, Buffer, Delay, Reverb, Compressor, Phaser, Chorus, Resonator, and Lo‑Fi. Direct knob/slider manipulation with visual feedback |
| **Master Bus** | Switchable **limiter** (look‑ahead, 2 ms) or **soft‑clipper** for transparent peak control. Global **Lo‑Fi** degradation (bit depth 8–12, sample rate 8–32 kHz) over the entire mix |
| **Visualizer** | Real‑time spectrum + oscilloscope overlay (OSC/SPEC/COMBO modes). Adjustable FFT size (256–8192), channel routing (Stereo / L / R / Mid‑Side), zoom, and colour hue. Hover for frequency/note/dB tooltips. Driven by a lock‑free audio ring |
| **UI & Workflow** | 120 FPS GDI compositor with hash‑based dirty tracking for efficient partial redraws. Content‑addressed PCM disk cache deduplicates imported audio. **Media Explorer** with loopable preview. Full **Undo/Redo** with atomic whole‑struct snapshots. Built‑in **Keybinds** reference window |
| **Real‑Time Core** | Audio callback is strictly allocation‑free, I/O‑free, and GUI‑free. SIMD‑accelerated track summing (AVX2/SSE2). Denormal‑hardened (FTZ/DAZ) for stable CPU usage. Heavy lifting (decode, save, export, peak‑building, SF2 pre‑render) runs on background workers so playback never stalls |
| **Project & Export** | Versioned trailing sections ensure forward/backward compatibility across builds. Deterministic WAV export (16‑/24‑/32‑bit with TPDF dithering) using the exact same render loop as live playback. Export output is mathematically bit‑for‑bit identical to what you hear in‑session |
| **Wine Compatible** | Since the program is mostly Wine32, the Wine project allows running cseq on Linux/Mac (untested locally)

### libraries

- [miniaudio](https://github.com/mackron/miniaudio)
- [TinySoundFont](https://github.com/schellingb/TinySoundFont)
- [Airwindows](https://github.com/airwindows/airwindows) (`SmoothEQ3`)
