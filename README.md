# Padomizer

Padomizer is a Windows x64 VST3 audio effect for a microphone pointed at a drum practice pad. Each detected strike either plays a WAV at a velocity-sensitive level or generates a pitched MIDI note for a downstream instrument.

See [Padomizer Design and Implementation](DESIGN.md) for the DSP, MIDI and modulation behavior, host routing, state format, and reviewer test plan.

## Set up REAPER

1. In **Options → Preferences → Plug-ins → VST**, add the directory containing the new `Pad Trigger.vst3` bundle and re-scan. The Ninja build in this workspace is under `build/ninja/PadTrigger_artefacts/Release/VST3`. You can also copy the entire bundle folder into your usual VST3 folder. The bundle still uses the filename `Pad Trigger.vst3` and the original VST3 ID so saved projects and templates can find it. REAPER lists newly scanned instances as **Padomizer**.
2. Under **Options → Preferences → Audio → Device**, select **ASIO** and your M-Audio driver. Enable the input that carries the Q2U.
3. Start with the [one-track MIDI template](#one-track-midi-recording). Set the actual mono microphone input, arm the track, and monitor through the DAW. Your interface's direct monitor path carries the raw mic.

| Desired take | FX placement | Record tap |
| --- | --- | --- |
| Editable MIDI | Padomizer in **Input FX**. Instrument in regular **FX**. | **Output MIDI, pre-FX** |
| Stereo instrument audio | Padomizer followed by the instrument in **Input FX**. | **Stereo output, latency compensated, post-FX/pre-fader** |
| Built-in one-shot audio | Padomizer in **Input FX** in **WAV Sample** mode. Browse, paste a path, or drag in a WAV. | **Record input** for processed mono from a mono mic. Use a stereo output tap to retain the sample's stereo image. |
| Dry mic for later processing | Record the dry input. Put Padomizer and, if needed, an instrument in regular **FX** when reprocessing. | Print to a separate stereo destination or apply track FX to a new take. |

If a take is quiet with Padomizer bypassed, check whether it contains the mic signal. A regular track FX can make monitoring sound right even while **Record: input** captures the dry mic. To recover a dry take, keep Padomizer in the normal track FX chain and run **Apply track/take FX to items as new take**. Set any gain added to the dry item back to unity before applying, so Padomizer sees the calibrated mic level. Bypass the normal Pad Trigger FX after printing. To enlarge the waveform visually, use **Shift+Up** in the arrange view. This changes the display height without changing the audio. See [REAPER User Guide, sections 7.37 and 8.9](https://dlx.reaper.fm/userguide/html/).

## Set the physical gain and calibrate

1. Put the Q2U close to the pad and start with your usual low interface gain. Open **Input setup** and use **MIC IN** to watch the raw peak in dBFS as you play your softest and hardest strokes. Increase the waveform magnification with the **FS** menu if the trace looks flat. The peak number stays on the same scale at every zoom setting. Turn the interface gain down if hard strikes light **CLIP**.
2. Leave **Sensitivity**, **Dynamics**, and **Output** where they are. Press **Calibrate** and leave the room quiet for two seconds. Keep the interface gain steady as you capture five soft taps and five hard accents, about half a second apart. The panel shows the noise floor (**N**) and the median strike peaks for soft (**S**) and hard (**H**) hits. Each calibration example measures 80 ms from the threshold crossing. Each stage has up to 15 seconds. Output is muted during capture. If the result is weak, **Retry** starts again, **Use anyway** saves it, and **Cancel** returns to your previous calibration.
3. The dashed teal lines on **MIC IN** show the positive and negative sides of the current threshold. The waveform turns teal where the input reaches either line. Vertical amber markers show detected hits, which also require a sharp onset. Check that those markers match your taps and that **SAMPLE OUT** shows corresponding hits. Turn **Sensitivity** up for missed taps or down for false triggers. Higher **Dynamics** values make ghost notes quieter relative to accents. If overlapping sample tails clip, turn **Output** down.
   **Retrigger** is the minimum spacing between accepted hits. For fast rolls, try **12–20 ms**. The signal strip reports note-ons sent in the last second.
   The detector confirms level and attack on the same input channel. A brief peak window of up to **1.5 ms** preserves velocity detail. Strikes that reach the maximum mapped level can fire as soon as that level is reached. The plugin adds no host-reported lookahead. The interface buffer and instrument also contribute to the latency you hear.
4. Recalibrate after moving the mic or adjusting its gain. If soft taps are close to the noise floor, reposition the mic nearer to the pad or reduce background noise before trying again.

Short markers along the top of **MIC IN** explain some missed hits. **LOW** marks a near-threshold attack that did not reach the gate. **HOLD** marks a new edge during Retrigger. **ATTACK** marks above-gate input without enough new attack evidence. Amber **HIT** marks a confirmed hit.

The project saves the calibration, knob settings, and WAV path. Use **Browse** to relink a missing WAV. Hover over the sample status to see the expected path. If MIC IN shows hits but SAMPLE OUT stays silent, check that a WAV is loaded. If the MIDI indicator lights and the track is silent, check the downstream instrument, patch, and track routing.

## Play Vital or sforzando from the pad

### One-track MIDI recording

Import **Padomizer - Melodic Pluck** from **Track → Insert track from template**. Copy the package's `TrackTemplates` files into REAPER's resource folder of the same name. All three starters use mono Input 1, Padomizer in **Input FX**, and stock ReaSynth in regular **FX**. Replace ReaSynth with Vital, sforzando, or another instrument in that chain when you want a different sound.

| Starter | Phrase and sound |
| --- | --- |
| **Melodic Pluck** | C major pentatonic over one octave, Wandering motion, and a fast bright pluck. ReaSynth responds to velocity through level. |
| **Bass Pulse** | C2 and G2, frequent repeats, a sixteenth-note gate, and a short release. Padomizer ends the previous note at each new onset. |
| **Mallet Answer** | C major triad in the fifth octave with nearby pitch movement and a rounded sound with a longer decay. |

Arm the track, confirm the selected mono input carries your mic, and calibrate Padomizer from **Track input FX chain**. Use REAPER's regular **Record** and **Stop** buttons. The track uses **Record: output (MIDI)** with **pre-FX** capture, which records the notes and pitch bend after Padomizer and before the instrument. Double-click the resulting MIDI item to edit it. Disarm the track when you want to play the take without monitoring the live pad. The MIDI item plays through the instrument on the same track. See the [REAPER User Guide](https://dlx.reaper.fm/userguide/html/).

For an editable fixed-note percussion setup, use **Padomizer - Single drum note**. Its initial set contains MIDI note 36 (C2), with a velocity floor of 1 and a 15 ms Retrigger interval. Change the selected key in the note-set keyboard to match your instrument's drum map.

### One-track stereo WAV recording

Import **PadTrigger - One-track stereo** from **Track → Insert track from template**. It contains an uncalibrated Padomizer in **MIDI Notes** mode followed by stock ReaSynth in **Input FX**. The stereo template starts with C Major Pentatonic and an empty pattern. Leave the regular **FX** chain empty.

Arm the track. Right-click its record-arm button and choose **Track input FX chain** to open Pad Trigger for calibration or to change the instrument. Keep Padomizer first in the chain and leave the track in **Record: output (stereo, latency compensated)** with **post-FX/pre-fader** selected. Use REAPER's regular **Record** and **Stop** controls. Keep **Monitor track media when recording** off so an existing take stays out of a new recording. Playback of the printed WAV goes directly through the track's normal playback path. See [REAPER User Guide, sections 3.26 and 3.29](https://dlx.reaper.fm/userguide/html/).

### Separate print track

On REAPER 7.40 or newer, put Padomizer in the regular top-level FX chain. For MIDI Notes mode, put an enabled instrument after it. Select the source track or its print track and run **PadTrigger - Record audio**. It adopts a compatible, unambiguous existing print route or creates one. It configures the stereo channels, tap, and mute of that send, preserving existing send gain. REAPER's Record command records every armed track. Use **PadTrigger - Set up stereo print** to prepare the same route with transport stopped.

Load the scripts from the same folder via **Actions → Show action list → New action → Load ReaScript**. [Record audio](REAPER/PadTrigger%20-%20Record%20audio.lua), [Set up stereo print](REAPER/PadTrigger%20-%20Set%20up%20stereo%20print.lua)

For a ready-to-record sforzando setup, import the **PadTrigger - sforzando Stereo Print** track template. Load your instrument in sforzando, then arm both tracks and record. The mic track is monitoring-only and sends stereo audio after sforzando to **Instrument Print**, where REAPER writes the WAV. Keep recorded audio on the print track for playback.

## Play and keep a phrase

1. Set **Key**, **Scale**, and **Register**. In Explore, **Steady** favors repetitions, **Wandering** favors nearby notes and allows repeats, **Leaps** favors larger jumps, and **Legacy random** avoids immediate repeats where the set allows.
2. Play the pad. The display shows the eight most recent pitches and their velocities. **Keep last 4** captures the recent pitches and begins on step one on the next hit. Use the adjacent selector to capture 1–8 notes. Each hit still supplies its own timing and velocity. **Explore** returns to live pitch selection. The host parameters **Keep Phrase** and **Explore Phrase** respond once to each rising edge and can be assigned to momentary pedals or controller buttons.
3. Click a phrase cell to choose its pitch. **Vary one** changes the selected cell or the next cell if none is selected. **Undo** restores the last pitch edit. Key and register move the phrase. Changing scale matches its pitches to the nearest allowed notes.
4. Choose **Note length** in milliseconds or in host-tempo divisions. At 120 BPM a sixteenth-note gate releases after 125 ms. **Vel Floor** in **Input setup** lifts the softest output notes. The **Mic**, **Hit**, and **MIDI** lights trace signal flow. **Audition** sends C4 at velocity 100 without advancing or capturing a phrase step.
5. Open **Note editor** to choose custom pitches with the keyboard or enable **MIDI Learn** from **Track MIDI** or a directly available MIDI device. **New** empties the working set. **Revert** restores its starting notes. **Save** names a set for reuse in the project. The keyboard **Octave** changes which keys you edit. The main **Register** changes the playing register. For a saved note set, **C** and **Oct 4** are its original pitches.

To use **Follow chords**, send MIDI from a chord track to the track running Padomizer. Put Padomizer first in the normal FX chain so routed MIDI and live mic both arrive at its inputs. The faceplate shows the held pitch classes or the scale fallback. Chord changes guide newly generated pitches. A kept phrase retains its pitches. Turn off **MIDI Learn** when following chords.

## LFO

Open **Modulation** to see the LFO, pitch controls, and bend range. In **Seconds**, enter a cycle length from 0.025 to 120. **Tempo** follows the DAW from four bars through 32nd notes, including dotted and triplet divisions. Each **Target** has its own **Depth**. Active routes are summarized on the main panel.

**Scan phrase** syncs the highlighted step to the song. Your hit sounds the highlighted note. The shortcut selects a tempo-synced Ramp Up at full depth. Adjust the division in **Modulation**. **Reset Pattern** returns to the behavior selected by **Legacy loop**, which supports 0 Free and automatic capture over 1–16 steps. A hit on an empty step generates its pitch. Modulating pattern length can shorten a loop and retain the steps beyond the current length.

Choose **Semitones** or **Scale degrees** as the MIDI pitch unit. Degrees move between allowed pitch classes. **Cents** sends channel 1 pitch bend. Match **MIDI Bend Range** to the instrument (default ±2 semitones). Click a pitch knob label to select its LFO target. WAV pitch changes the playback rate.

The faceplate readouts use [DSEG14 Classic Mini by Keshikan](https://www.keshikan.net/fonts-e.html). The SIL Open Font License is included in [Assets](Assets/DSEG-LICENSE.txt).

## Build from source

Use CMake 3.22 or newer, Git, and Visual Studio 2022 with **Desktop development with C++**.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target PadTrigger_VST3
```

CMake uses a local JUCE checkout at `build/deps/JUCE` if available. You can supply another checkout with `-DJUCE_SOURCE_DIR=C:/path/to/JUCE`. Otherwise CMake fetches the project's pinned revision.

To run the synthetic low-gain, sample playback, MIDI pattern, and recall checks, configure with `-DPADTRIGGER_BUILD_TESTS=ON`, build the `PadTriggerTests` target, and run `ctest --test-dir build -C Release --output-on-failure`.

Run `PadTriggerTests.exe --benchmark` from a Release build to measure callback time at 48 and 96 kHz with 32, 64, and 128 frames per block. The report includes per-block p50, p95, p99, maximum, and deadline overruns. Editor runs draw the faceplate concurrently in the test process. Timing on your full DAW graph depends on its other tracks, effects, and audio driver.

To verify stereo rendering with REAPER installed, run the following from the source checkout. It renders a generated fixture in an isolated REAPER profile under `build/` and checks the result against the expected waveform.

```powershell
python Tests/VerifyReaperRender.py --reaper 'C:/Program Files/REAPER (x64)/reaper.exe' --bundle 'build/package/Pad Trigger.vst3'
```

## Install from a Windows package

Copy the entire `Pad Trigger.vst3` bundle into a VST3 directory scanned by REAPER. Copy the files from `TrackTemplates` into REAPER's `TrackTemplates` directory. If you use the print actions, load the Lua files from `Scripts` using the REAPER Actions list.

To stage a package from a Release build, run `cmake --install build --config Release --component Padomizer --prefix build/package`. The package includes the JUCE revision, build toolchain details, and DSEG font notice. Check it with `./Tests/VerifyRelease.ps1 -PackagePath build/package`.
