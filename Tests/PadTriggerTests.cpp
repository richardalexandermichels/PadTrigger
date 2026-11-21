#include "PadTrigger.h"
#include "PadEditor.h"
#include "PoolEditor.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
constexpr int rate = 48000;
constexpr int blockSize = 127;
using Stage = PadTrigger::CalibrationStage;

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

float noise(int frame)
{
    const auto bits = (static_cast<std::uint32_t>(frame) * 2654435761u) >> 16;
    return (static_cast<float>(bits) / 32768.0f - 1.0f) * 0.000012f;
}

float stroke(int frame, int onset, float level, float attack = 0.0f)
{
    const auto offset = frame - onset;
    if (offset < 0 || offset >= 300)
        return 0.0f;
    const auto t = static_cast<float>(offset);
    const auto rising = attack > 0.0f ? std::min(1.0f, t / attack) : 1.0f;
    return level * rising * std::exp(-std::max(0.0f, t - attack) / 28.0f) * std::cos(t * 0.72f);
}

template <typename Signal>
std::vector<float> process(PadTrigger& plugin, int frames, Signal input, juce::MidiBuffer* captured = nullptr)
{
    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    std::vector<float> output;
    for (int start = 0; start < frames; start += blockSize)
    {
        const auto size = std::min(blockSize, frames - start);
        buffer.setSize(2, size, false, false, true);
        buffer.clear();
        for (int frame = 0; frame < size; ++frame)
            buffer.setSample(0, frame, input(start + frame));
        plugin.processBlock(buffer, midi);
        if (captured != nullptr)
            captured->addEvents(midi, 0, size, start);
        else
            require(midi.isEmpty(), "The WAV effect must produce no MIDI");
        output.insert(output.end(), buffer.getReadPointer(0), buffer.getReadPointer(0) + size);
    }
    return output;
}

void setParameter(PadTrigger& plugin, const char* id, float value)
{
    auto* parameter = plugin.settings.getParameter(id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

juce::MidiBuffer playStrokes(PadTrigger& plugin, int count, int spacing = 1600)
{
    juce::MidiBuffer midi;
    const auto levels = std::array<float, 3> { 0.0005f, 0.003f, 0.03f };
    const auto audio = process(plugin, count * spacing + 7000, [&](int frame)
    {
        const auto hit = frame / spacing;
        return hit < count ? stroke(frame, hit * spacing + 240, levels[static_cast<std::size_t>(hit % 3)]) : 0.0f;
    }, &midi);
    require(std::all_of(audio.begin(), audio.end(), [](float value) { return value == 0.0f; }),
            "MIDI mode leaked the input or WAV audio");
    return midi;
}

std::vector<int> notePitches(const juce::MidiBuffer& midi)
{
    std::vector<int> notes;
    for (const auto event : midi)
        if (event.getMessage().isNoteOn())
            notes.push_back(event.getMessage().getNoteNumber());
    return notes;
}

void checkMidiPatterns()
{
    PadTrigger plugin;
    plugin.prepareToPlay(rate, blockSize);
    setParameter(plugin, "soundMode", 1.0f);
    setParameter(plugin, "noteLengthMs", 20.0f);
    setParameter(plugin, "retriggerMs", 8.0f);
    require(plugin.midiNotes.savePool("My Vital pool", "C4 F#4 Bb4 72").wasOk(), "Could not save custom pitch pool");
    require(plugin.midiNotes.savePool("Invalid", "C4 128").failed() && plugin.midiNotes.poolNames().size() == 7,
            "Invalid notes should leave the pool list intact");
    const std::array<int, 4> allowed { 60, 66, 70, 72 };
    const auto free = notePitches(playStrokes(plugin, 40));
    require(free.size() == 40 && std::adjacent_find(free.begin(), free.end()) == free.end(),
            "Free MIDI selection repeated a pitch on adjacent strokes");
    require(std::all_of(free.begin(), free.end(), [&](int pitch)
                       { return std::find(allowed.begin(), allowed.end(), pitch) != allowed.end(); }),
            "A selected note was outside its custom pool");

    setParameter(plugin, "patternLength", 4.0f);
    const auto midi = playStrokes(plugin, 12);
    const auto loop = notePitches(midi);
    require(loop.size() == 12 && std::equal(loop.begin(), loop.begin() + 4, loop.begin() + 4)
            && std::equal(loop.begin(), loop.begin() + 4, loop.begin() + 8),
            "Four-note loop did not repeat as played");
    std::vector<int> velocities, starts, ends;
    for (const auto event : midi)
    {
        const auto message = event.getMessage();
        require(message.getChannel() == 1, "Generated notes must use channel 1");
        if (message.isNoteOn())
        {
            starts.push_back(event.samplePosition);
            velocities.push_back(message.getVelocity());
        }
        else if (message.isNoteOff())
        {
            ends.push_back(event.samplePosition);
        }
    }
    require(ends.size() == starts.size() && ends[0] - starts[0] == 960, "MIDI note length was incorrect");
    require(velocities[0] < velocities[1] && velocities[1] < velocities[2] && velocities[0] != velocities[4],
            "The loop should preserve pitches and use the current stroke velocity");

    juce::MemoryBlock saved;
    plugin.getStateInformation(saved);
    PadTrigger restored;
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    restored.prepareToPlay(rate, blockSize);
    require(restored.midiNotes.poolNames()[restored.midiNotes.selectedPool()] == "My Vital pool",
            "Custom pool was not restored");
    require(notePitches(playStrokes(restored, 12)) == loop, "Restored loop changed its pitch sequence");

    setParameter(restored, "patternLength", 16.0f);
    const auto longest = notePitches(playStrokes(restored, 32));
    require(longest.size() == 32 && std::equal(longest.begin(), longest.begin() + 16, longest.begin() + 16),
            "Sixteen-note loop did not repeat");

    setParameter(restored, "patternLength", 1.0f);
    setParameter(restored, "noteLengthMs", 100.0f);
    const auto rapid = playStrokes(restored, 7, 650);
    const auto single = notePitches(rapid);
    require(single.size() == 7 && std::all_of(single.begin(), single.end(), [&](int pitch)
                                              { return pitch == single.front(); }),
            "One-note loop did not repeat its pitch");
    auto noteIsOn = false;
    for (const auto event : rapid)
    {
        const auto message = event.getMessage();
        if (!message.isNoteOnOrOff())
            continue;
        require(message.isNoteOn() != noteIsOn, "Overlapping repeated pitches must send note-off before note-on");
        noteIsOn = message.isNoteOn();
    }
    require(!noteIsOn, "The last generated note was left on");
    restored.midiNotes.resetPattern();
    const auto reset = notePitches(playStrokes(restored, 2));
    require(reset.size() == 2 && reset[0] != single[0] && reset[0] == reset[1],
            "Reset did not start a new repeating pattern");

    auto oldState = juce::XmlElement("PadTrigger");
    juce::AudioProcessor::copyXmlToBinary(oldState, saved);
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    require(!restored.isMidiMode() && restored.settings.getRawParameterValue("patternLength")->load() == 0,
            "Legacy WAV project inherited MIDI settings from the previous project");
}

void checkRetrigger()
{
    PadTrigger plugin;
    plugin.prepareToPlay(rate, blockSize);
    setParameter(plugin, "soundMode", 1.0f);
    setParameter(plugin, "patternLength", 8.0f);
    const auto signal = [](int frame)
    {
        constexpr int first = 2400;
        return noise(frame) + stroke(frame, first, 0.004f)
            + stroke(frame, first + 528, 0.001f)
            + stroke(frame, first + 1248, 0.0006f)
            + stroke(frame, first + 1920, 0.0004f)
            + stroke(frame, first + 3600, 0.0015f);
    };
    juce::MidiBuffer midi;
    process(plugin, rate / 3, signal, &midi);
    require(notePitches(midi).size() == 2 && plugin.midiNotes.patternDescription(8).startsWith("2/8"),
            "Ringing advanced the MIDI pattern more than once per strike");

    plugin.midiNotes.resetPattern();
    setParameter(plugin, "retriggerMs", 8.0f);
    midi.clear();
    process(plugin, rate / 3, signal, &midi);
    require(notePitches(midi).size() == 5, "Short retrigger setting blocked closely spaced hits");
}

void checkVelocityFloor()
{
    PadTrigger plugin;
    plugin.prepareToPlay(rate, blockSize);
    setParameter(plugin, "soundMode", 1.0f);
    setParameter(plugin, "sensitivity", 12.0f);
    const auto signal = [](int frame)
    {
        return stroke(frame, 4800, 0.00007f) + stroke(frame, 9600, 0.001f) + stroke(frame, 14400, 0.05f);
    };
    const auto velocities = [&]()
    {
        juce::MidiBuffer midi;
        process(plugin, 24000, signal, &midi);
        std::vector<int> result;
        for (const auto event : midi)
            if (event.getMessage().isNoteOn())
                result.push_back(event.getMessage().getVelocity());
        return result;
    };
    const auto normal = velocities();
    require(normal.size() == 3 && normal.front() == 72 && normal.back() == 127
        && normal[1] > 72 && normal[1] < 127, "Default MIDI floor did not lift quiet notes while retaining dynamics");
    setParameter(plugin, "velocityFloor", 96.0f);
    const auto raised = velocities();
    require(raised.size() == 3 && raised.front() == 96 && raised[1] > normal[1] && raised.back() == 127,
            "Raising the MIDI floor affected accents or failed to lift quieter notes");
    setParameter(plugin, "velocityFloor", 127.0f);
    require(velocities() == std::vector<int>{127, 127, 127}, "Maximum floor did not produce fixed velocity");

    juce::MemoryBlock saved;
    plugin.getStateInformation(saved);
    PadTrigger restored;
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    require(restored.settings.getRawParameterValue("velocityFloor")->load() == 127.0f, "MIDI floor did not recall");
    auto oldState = juce::XmlElement("PadTrigger");
    juce::AudioProcessor::copyXmlToBinary(oldState, saved);
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    require(restored.settings.getRawParameterValue("velocityFloor")->load() == 72.0f, "Old projects missed the new default floor");
}

void checkVelocityDecisionWindow()
{
    PadTrigger plugin;
    plugin.prepareToPlay(rate, blockSize);
    setParameter(plugin, "soundMode", 1.0f);
    juce::MidiBuffer midi;
    process(plugin, 14000, [](int frame)
    {
        return stroke(frame, 2000, 0.004f) + stroke(frame, 2032, 0.03f)
            + stroke(frame, 10000, 0.004f) + stroke(frame, 10480, 0.03f);
    }, &midi);
    std::vector<std::pair<int, int>> attacks;
    for (const auto event : midi)
        if (const auto message = event.getMessage(); message.isNoteOn())
            attacks.emplace_back(event.samplePosition, message.getVelocity());
    require(attacks.size() == 2 && attacks[0].second > attacks[1].second
                && attacks[0].first <= 2071 && attacks[1].first == 10071,
            "Crest handling changed the velocity decision window or added a duplicate note");
}

void checkPadRinging()
{
    PadTrigger plugin;
    plugin.prepareToPlay(rate, blockSize);
    setParameter(plugin, "soundMode", 1.0f);
    setParameter(plugin, "retriggerMs", 8.0f);
    juce::MidiBuffer midi;
    process(plugin, rate / 3, [](int frame)
    {
        constexpr int first = 2400;
        const auto elapsed = frame - first;
        const auto ringing = elapsed >= 0 && elapsed < 2000
            ? 0.0005f * std::sin(static_cast<float>(elapsed) * juce::MathConstants<float>::twoPi * 80.0f / rate)
            : 0.0f;
        return noise(frame) + ringing + stroke(frame, first, 0.004f)
            + stroke(frame, first + 768, 0.00055f)
            + stroke(frame, first + 1440, 0.004f)
            + stroke(frame, first + 2380, 0.0003f);
    }, &midi);
    std::vector<int> starts;
    for (auto event : midi)
        if (event.getMessage().isNoteOn())
            starts.push_back(event.samplePosition);
    require(starts == std::vector<int>{ 2471, 3911, 4851 } && plugin.midiNotes.noteCount() == 3,
            "A pad echo triggered a note or an intentional close stroke was dropped");
}

void checkHardStrikeRinging()
{
    for (const float retrigger : {8.0f, 50.0f})
    {
        for (const bool playSecond : {false, true})
        {
            PadTrigger plugin;
            plugin.prepareToPlay(rate, blockSize);
            setParameter(plugin, "soundMode", 1.0f);
            setParameter(plugin, "retriggerMs", retrigger);
            setParameter(plugin, "patternLength", 8.0f);
            juce::MidiBuffer midi;
            process(plugin, rate / 2, [playSecond](int frame)
            {
                constexpr int first = rate / 20;
                constexpr int second = first + rate * 65 / 1000;
                const auto elapsed = static_cast<float>(frame - first) / rate;
                const auto tail = elapsed >= 0.0f
                    ? 0.024f * std::exp(-elapsed / 0.060f)
                        * std::sin(juce::MathConstants<float>::twoPi * 180.0f * elapsed)
                    : 0.0f;
                return noise(frame) + stroke(frame, first, 0.15f) + tail
                    + stroke(frame, first + rate * 55 / 1000, 0.008f)
                    + (playSecond ? stroke(frame, second, 0.025f) : 0.0f);
            }, &midi);

            const int expected = playSecond ? 2 : 1;
            require(notePitches(midi).size() == static_cast<std::size_t>(expected)
                        && plugin.midiNotes.noteCount() == expected,
                    "A hard strike's late ringing produced another note or masked a real follow-up");
        }
    }
}

void checkDetectorTimingAndChannels()
{
    constexpr float gate = 0.00019952623f;
    constexpr float quiet = gate * 0.45f;
    StrokeDetector detector;
    detector.prepare(rate);
    const auto run = [&](const auto& input, int frames, float saturation)
    {
        detector.reset();
        std::vector<int> hits;
        for (int frame = 0; frame < frames; ++frame)
            if (detector.process(input(frame), 2, gate, quiet, 384, saturation).peak > 0.0f)
                hits.push_back(frame);
        return hits;
    };

    const auto splitEvidence = [gate](int frame)
    {
        // The settled channel supplies level; the other supplies only a sub-threshold transient.
        return std::array<float, 2>{ gate * 3.0f, stroke(frame, 2400, gate * 0.8f) };
    };
    require(run(splitEvidence, 4800, 1.0f) == std::vector<int>{71},
            "A transient borrowed the threshold crossing from another channel");
    require(run([](int frame)
    {
        return std::array<float, 2>{ stroke(frame, 500, 0.004f), -stroke(frame, 500, 0.004f) };
    }, 2400, 0.003f) == std::vector<int>{500}, "A saturated stereo accent was delayed or emitted twice");

    require(run([gate](int frame)
    {
        const auto elapsed = frame - 500;
        const auto t = static_cast<float>(juce::jmax(0, elapsed));
        const auto rise = juce::jmin(1.0f, t / 138.0f);
        const auto tail = std::exp(-juce::jmax(t - 138.0f, 0.0f) / 110.0f);
        const auto soft = gate * 1.15f * rise * tail
            * std::cos(juce::MathConstants<float>::twoPi * 200.0f * t / rate);
        return std::array<float, 2>{ 0.0f, soft };
    }, 1600, 1.0f) == std::vector<int>{692}, "A quiet rising stroke was missed or emitted before crossing its gate");
}

void checkWeakFollowUps()
{
    constexpr float gate = 0.00019952623f;
    constexpr int first = 2400;
    const auto run = [=](int gapMs, float tap, float tail, int phase, float frequency)
    {
        StrokeDetector detector;
        detector.prepare(rate);
        std::vector<int> hits;
        const auto second = first + gapMs * rate / 1000;
        for (int frame = 0; frame < second + rate / 10; ++frame)
        {
            const auto elapsed = static_cast<float>(frame - first) / rate;
            const auto ring = elapsed >= 0.0f ? tail * std::exp(-elapsed / 0.06f)
                * std::sin(juce::MathConstants<float>::twoPi * (frequency * elapsed + phase / 24.0f)) : 0.0f;
            const auto sample = stroke(frame, first, 0.15f) + ring + stroke(frame, second, tap);
            if (detector.process({sample, 0.0f}, 1, gate, gate * 0.45f, rate * 8 / 1000, 0.04f).peak > 0.0f)
                hits.push_back(frame);
        }
        return hits;
    };

    require(run(10, 0.00035f, 0.0f, 0, 180.0f) == std::vector<int>{first, first + rate / 100 + 71},
            "A hard accent still suppressed a weak tap after the input rearmed");
    for (const auto gap : {16, 30, 65})
        for (int phase = 0; phase < 24; ++phase)
        {
            require(run(gap, 0.0f, 0.0005f, phase, 180.0f) == std::vector<int>{first},
                    "A low-frequency tail produced a false second note");
            const auto pair = run(gap, 0.0006f, 0.0005f, phase, 180.0f);
            const auto second = first + gap * rate / 1000;
            require(pair.size() == 2 && pair[0] == first && pair[1] >= second && pair[1] <= second + 72,
                    "A real follow-up was missed or delayed over a low-frequency tail");
        }
    for (const auto frequency : {3500.0f, 12000.0f, 15000.0f})
        require(run(65, 0.0f, 0.003f, 0, frequency) == std::vector<int>{first},
                "A bright resonant tail produced a false second note");
}

void checkGradualAttackDuringRinging()
{
    for (const auto sampleRate : { 44100, 48000, 96000 })
    {
        const auto run = [sampleRate](bool playFollowUp)
        {
            StrokeDetector detector;
            detector.prepare(sampleRate);
            std::vector<float> hits;
            const auto oscillation = [](float seconds, float frequency)
            {
                return std::cos(juce::MathConstants<float>::twoPi * seconds * frequency);
            };
            constexpr float gate = 0.000199526f;
            for (int frame = 0; frame < sampleRate / 3; ++frame)
            {
                const auto time = frame / static_cast<float>(sampleRate);
                const auto accentTime = time - 0.05f;
                const auto tapTime = time - 0.135f;
                const auto accent = accentTime >= 0.0f && accentTime < 0.012f
                    ? 0.12f * std::exp(-accentTime / 0.0006f) * oscillation(accentTime, 4800.0f) : 0.0f;
                const auto tail = accentTime >= 0.0f
                    ? 0.00025f * std::exp(-accentTime / 0.25f) * oscillation(accentTime, 180.0f) : 0.0f;
                const auto tap = playFollowUp && tapTime >= 0.0f && tapTime < 0.01f
                    ? 0.001f * std::min(1.0f, tapTime / 0.0003f) * std::exp(-tapTime / 0.0018f)
                        * oscillation(tapTime, 450.0f) : 0.0f;
                if (detector.process({ accent + tail + tap, 0.0f }, 1, gate, gate * 0.45f,
                                     sampleRate / 20, 0.035f).peak > 0.0f)
                    hits.push_back(time);
            }
            return hits;
        };

        require(run(false).size() == 1, "A sustained tail produced a false note at 50 ms Retrigger");
        const auto pair = run(true);
        require(pair.size() == 2 && pair[1] >= 0.135f && pair[1] <= 0.1375f,
                "A gradual attack over a sustained tail was missed or delayed");
    }
}

void checkHostBlockLatency()
{
    for (const auto sampleRate : {44100.0, 48000.0, 96000.0})
    {
        for (const int size : {32, 128, 1024})
        {
            PadTrigger plugin;
            setParameter(plugin, "soundMode", 1.0f);
            // Put the first hit at the end of a block, and the accent on another boundary.
            const int first = size - 1;
            const int second = first + 12 * size + static_cast<int>(sampleRate);
            plugin.prepareToPlay(sampleRate, size);
            juce::AudioBuffer<float> buffer(2, size);
            juce::MidiBuffer midi;
            std::vector<int> hits;
            for (int start = 0; start < second + size * 2 + 200; start += size)
            {
                buffer.clear();
                for (int i = 0; i < size; ++i)
                    buffer.setSample(0, i, stroke(start + i, first, 0.0005f) + stroke(start + i, second, 0.1f));
                plugin.processBlock(buffer, midi);
                for (const auto event : midi)
                    if (event.getMessage().isNoteOn())
                        hits.push_back(start + event.samplePosition);
                require(buffer.getMagnitude(0, size) == 0.0f, "MIDI processing leaked mic audio");
            }
            const auto peakDelay = juce::roundToInt(sampleRate * 0.0015) - 1;
            require(hits == std::vector<int>{first + peakDelay, second},
                    "MIDI onset timing depends on the host buffer size or sample rate");
            require(plugin.getLatencySamples() == 0, "The detector introduced host lookahead");
        }
    }
}

void checkModulatedHitTiming()
{
    const auto render = [](int size)
    {
        PadTrigger plugin;
        setParameter(plugin, "soundMode", 1.0f);
        setParameter(plugin, "lfoShape", 4.0f);
        setParameter(plugin, "lfoSeconds", 0.064f);
        setParameter(plugin, "lfoVelocity", 100.0f);
        setParameter(plugin, "lfoNoteLength", 55.0f);
        setParameter(plugin, "retriggerMs", 8.0f);
        plugin.prepareToPlay(rate, size);
        juce::AudioBuffer<float> audio(2, size);
        juce::MidiBuffer midi, output;
        for (int start = 0; start < rate; start += size)
        {
            audio.clear();
            for (int i = 0; i < size; ++i)
                audio.setSample(0, i, stroke(start + i, 1000, 0.0006f)
                                    + stroke(start + i, 1520, 0.0006f));
            plugin.processBlock(audio, midi);
            output.addEvents(midi, 0, size, start);
        }
        std::vector<std::pair<int, int>> events;
        for (const auto event : output)
        {
            const auto message = event.getMessage();
            if (message.isNoteOn() || message.isNoteOff())
                events.emplace_back(event.samplePosition, message.getVelocity());
        }
        return events;
    };
    const auto expected = render(128);
    require(expected.size() == 4 && expected[0].second == 127 && expected[1].second < expected[0].second,
            "The audio-rate phase was wrong at a MIDI note onset");
    require(render(127) == expected && render(1024) == expected,
            "LFO-modulated velocity or duration depended on the host block size");
}

void checkMidiPitch()
{
    PadTrigger plugin;
    setParameter(plugin, "soundMode", 1.0f);
    setParameter(plugin, "patternLength", 1.0f);
    setParameter(plugin, "lfoShape", 4.0f);
    setParameter(plugin, "lfoSeconds", 0.2f);
    setParameter(plugin, "pitchSemitones", 12.0f);
    setParameter(plugin, "lfoSemitones", 3.0f);
    setParameter(plugin, "lfoCents", 50.0f);
    plugin.prepareToPlay(rate, blockSize);
    juce::MidiBuffer midi;
    process(plugin, 8400, [](int frame)
    {
        return stroke(frame, 1000, 0.1f) + stroke(frame, 6000, 0.1f);
    }, &midi);
    const auto pitches = notePitches(midi);
    require(pitches.size() == 2 && pitches[0] - pitches[1] == 6,
            "Coarse LFO did not transpose a learned pitch by whole semitones");
    int wheel = 8192;
    bool bentDuringHeldNote = false;
    bool releasedFirst = false;
    for (const auto event : midi)
    {
        const auto message = event.getMessage();
        if (message.isPitchWheel())
        {
            wheel = message.getPitchWheelValue();
            bentDuringHeldNote |= event.samplePosition > 1000 && event.samplePosition < 6000 && wheel == 6144;
        }
        if (message.isNoteOn())
            require((event.samplePosition == 1000 && wheel == 10240)
                || (event.samplePosition == 6000 && wheel == 6144), "Fine bend or note timing was wrong at the strike");
        if (message.isNoteOff() && message.getNoteNumber() == pitches[0])
            releasedFirst = event.samplePosition == 7720;
    }
    require(bentDuringHeldNote && releasedFirst, "Continuous fine pitch or transposed note-off was missing");
    setParameter(plugin, "lfoCents", 0.0f);
    midi.clear();
    process(plugin, blockSize, noise, &midi);
    require(!midi.isEmpty(), "No pitch wheel reset was sent");
    const auto reset = (*midi.begin()).getMessage();
    require(reset.isPitchWheel() && reset.getPitchWheelValue() == 8192,
            "Disabling fine modulation left the instrument detuned");

    setParameter(plugin, "pitchCents", 25.0f);
    setParameter(plugin, "bendRange", 12.0f);
    juce::MemoryBlock state;
    plugin.getStateInformation(state);
    PadTrigger restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(restored.settings.getRawParameterValue("pitchCents")->load() == 25.0f
            && restored.settings.getRawParameterValue("pitchSemitones")->load() == 12.0f
            && restored.settings.getRawParameterValue("bendRange")->load() == 12.0f
            && restored.settings.getRawParameterValue("lfoSemitones")->load() == 3.0f,
            "Pitch controls or routing failed to restore");
}

void checkSelfContainedPitchBend()
{
    PadTrigger plugin;
    plugin.prepareToPlay(rate, blockSize);
    setParameter(plugin, "soundMode", 1.0f);
    setParameter(plugin, "pitchCents", 50.0f);
    for (const auto cents : {50.0f, 50.0f, 0.0f, 0.0f})
    {
        setParameter(plugin, "pitchCents", cents);
        const auto take = playStrokes(plugin, 1);
        int lastBendFrame = -1, wheel = -1, count = 0;
        for (const auto event : take)
        {
            const auto message = event.getMessage();
            if (message.isPitchWheel())
            {
                lastBendFrame = event.samplePosition;
                wheel = message.getPitchWheelValue();
            }
            if (message.isNoteOn())
            {
                ++count;
                require(lastBendFrame == event.samplePosition && wheel == (cents == 0.0f ? 8192 : 10240),
                        "A new take could start with a missing or stale pitch bend");
            }
        }
        require(count == 1, "Bend fixture did not generate one note");
    }
}

void checkConcurrentPatternRestore()
{
    const auto makeState = [](int length, int pitch)
    {
        PadTrigger fixture;
        setParameter(fixture, "soundMode", 1.0f);
        setParameter(fixture, "patternLength", static_cast<float>(length));
        setParameter(fixture, "retriggerMs", 8.0f);
        juce::ValueTree notes("MidiNotes");
        notes.setProperty("patternLength", length, nullptr);
        juce::StringArray slots;
        for (int i = 0; i < length; ++i)
            slots.add(juce::String(pitch));
        notes.setProperty("pattern", slots.joinIntoString(" "), nullptr);
        fixture.midiNotes.restoreState(notes);
        juce::MemoryBlock state;
        fixture.getStateInformation(state);
        return state;
    };
    const auto shorter = makeState(1, 60);
    const auto longer = makeState(4, 64);
    PadTrigger plugin;
    plugin.prepareToPlay(rate, blockSize);
    plugin.setStateInformation(shorter.getData(), static_cast<int>(shorter.getSize()));
    std::atomic<bool> started { false };
    std::thread writer([&]
    {
        while (!started.load(std::memory_order_acquire))
            std::this_thread::yield();
        for (int i = 0; i < 500; ++i)
        {
            const auto& state = i % 2 == 0 ? longer : shorter;
            plugin.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        }
    });
    started.store(true, std::memory_order_release);
    juce::AudioBuffer<float> audio(2, blockSize);
    juce::MidiBuffer midi;
    midi.ensureSize(256);
    int count = 0, unexpected = 0;
    for (int start = 0; start < rate * 5; start += blockSize)
    {
        audio.clear();
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const auto frame = (start + sample) % 960;
            audio.setSample(0, sample, stroke(frame, 160, 0.08f));
        }
        plugin.processBlock(audio, midi);
        for (const auto event : midi)
            if (const auto message = event.getMessage(); message.isNoteOn())
            {
                ++count;
                if (message.getNoteNumber() != 60 && message.getNoteNumber() != 64)
                    ++unexpected;
            }
    }
    writer.join();
    require(count > 100 && unexpected == 0, "A concurrent restore discarded saved pattern slots");
    require(notePitches(playStrokes(plugin, 4)) == std::vector<int>(4, 60),
            "The last restored pattern did not survive concurrent processing");

    require(plugin.midiNotes.savePool("Saved during play", "C4 D4 E4 G4 A4 C5 D5 E5").wasOk(),
            "Could not name an unchanged note set");
    require(notePitches(playStrokes(plugin, 4)) == std::vector<int>(4, 60),
            "Saving the unchanged pitch set discarded the learned pattern");
}

void checkNoteRate()
{
    PadTrigger plugin;
    setParameter(plugin, "soundMode", 1.0f);
    setParameter(plugin, "noteLengthMs", 350.0f);
    plugin.prepareToPlay(rate, blockSize);
    const auto notes = playStrokes(plugin, 8, 3000);
    require(plugin.notesPerSecond() == static_cast<int>(notePitches(notes).size()),
            "Rate meter counted something other than outgoing note-ons");
    juce::MidiBuffer releases;
    process(plugin, rate * 3 / 10, noise, &releases);
    require(!releases.isEmpty() && notePitches(releases).empty() && plugin.notesPerSecond() == 8,
            "Note-offs affected the notes-per-second meter");
    process(plugin, rate, noise, &releases);
    require(plugin.notesPerSecond() == 0, "Expired notes stayed in the meter");
}

void checkLfoAndPattern()
{
    struct PlayHead final : juce::AudioPlayHead
    {
        PositionInfo position;
        juce::Optional<PositionInfo> getPosition() const override { return position; }
    } host;
    PadTrigger plugin;
    plugin.prepareToPlay(rate, blockSize);
    require(plugin.settings.getParameter("autoRetrigger") == nullptr, "Removed Auto parameter still exists");
    setParameter(plugin, "lfoSeconds", 0.125f);
    process(plugin, 1500, noise);
    require(std::abs(plugin.lfoPosition() - 0.25f) < 0.001f, "Decimal seconds clock did not advance by a quarter-cycle");
    plugin.setPlayHead(&host);
    host.position.setBpm(90.0);
    host.position.setTimeSignature(juce::AudioPlayHead::TimeSignature{3, 4});
    host.position.setPpqPosition(6.0);
    host.position.setIsPlaying(true);
    setParameter(plugin, "lfoClock", 1.0f);
    setParameter(plugin, "lfoDivision", 0.0f);
    process(plugin, 1, noise);
    require(std::abs(plugin.lfoPosition() - 0.5f) < 0.001f, "4/1 did not follow the host's bar length");
    host.position.setPpqPosition(4.0);
    host.position.setPpqPositionOfLastBarStart(4.0);
    host.position.setBarCount(1);
    setParameter(plugin, "lfoDivision", 3.0f);
    process(plugin, 1, noise);
    require(plugin.lfoPosition() < 0.001f, "The first 3/4 downbeat did not restart the one-bar LFO");
    setParameter(plugin, "lfoDivision", 0.0f);
    process(plugin, 1, noise);
    require(std::abs(plugin.lfoPosition() - 0.25f) < 0.001f, "A meter change lost the bar index within a four-bar LFO");
    host.position.setPpqPosition(0.03125);
    setParameter(plugin, "lfoDivision", 17.0f);
    process(plugin, 1, noise);
    require(std::abs(plugin.lfoPosition() - 0.25f) < 0.001f, "Thirty-second note division did not follow host PPQ");
    plugin.setPlayHead(nullptr);

    setParameter(plugin, "lfoPattern", 100.0f);
    setParameter(plugin, "lfoVelocity", 30.0f);
    setParameter(plugin, "lfoShape", 4.0f);
    setParameter(plugin, "lfoClock", 0.0f);
    setParameter(plugin, "lfoSeconds", 2.0f);
    setParameter(plugin, "patternLength", 8.0f);
    plugin.prepareToPlay(rate, blockSize);
    process(plugin, rate / 2, noise);
    require(plugin.livePatternLength() == 16, "Maximum LFO pattern length did not round up to 16");
    process(plugin, rate * 3 / 4, noise);
    require(plugin.livePatternLength() == 1, "Minimum LFO pattern length jumped into Free mode");
    setParameter(plugin, "patternLength", 0.0f);
    process(plugin, blockSize, noise);
    require(plugin.livePatternLength() == 0, "Free mode was modulated into a loop");

    juce::MemoryBlock state;
    plugin.getStateInformation(state);
    PadTrigger recalled;
    recalled.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(recalled.settings.getRawParameterValue("lfoPattern")->load() == 100.0f
            && recalled.settings.getRawParameterValue("lfoVelocity")->load() == 30.0f,
            "Independent modulation depths did not recall");
    const juce::XmlElement oldState("PadTrigger");
    juce::AudioProcessor::copyXmlToBinary(oldState, state);
    recalled.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(recalled.settings.getRawParameterValue("lfoPattern")->load() == 0.0f,
            "An older project inherited modulation");

    MidiNotes notes;
    juce::MidiBuffer midi;
    notes.beginBlock(midi, true, 3);
    for (int i = 0; i < 6; ++i)
        notes.trigger(midi, 10 * i, 96, 6, 4);
    const auto learned = notePitches(midi);
    midi.clear();
    for (int i = 0; i < 2; ++i)
        notes.trigger(midi, 10 * i, 96, 2, 4);
    require(notePitches(midi) == std::vector<int>{learned[0], learned[1]},
            "A shorter modulated loop did not reuse the learned prefix");
    notes.endBlock(20);
    MidiNotes reloaded;
    reloaded.restoreState(notes.saveState());
    midi.clear();
    reloaded.beginBlock(midi, true, 3);
    for (int i = 0; i < 6; ++i)
        reloaded.trigger(midi, 10 * i, 96, 6, 4);
    require(notePitches(midi) == learned, "An expanded modulated loop did not survive save and recall");
}

void checkLfoPatternPosition()
{
    const auto play = [](PadTrigger& plugin, float depth)
    {
        plugin.prepareToPlay(rate, blockSize);
        setParameter(plugin, "lfoPatternPosition", depth);
        juce::MidiBuffer midi;
        process(plugin, rate * 9 / 10, [](int frame)
        {
            return stroke(frame, rate * 58 / 100, 0.03f)
                + stroke(frame, rate * 63 / 100, 0.03f)
                + stroke(frame, rate * 81 / 100, 0.03f);
        }, &midi);
        return midi;
    };
    PadTrigger plugin;
    setParameter(plugin, "soundMode", 1.0f);
    setParameter(plugin, "patternLength", 4.0f);
    setParameter(plugin, "lfoSeconds", 1.0f);
    setParameter(plugin, "lfoShape", 2.0f);
    const auto midi = play(plugin, 100.0f);
    const auto pitches = notePitches(midi);
    require(pitches.size() == 3 && pitches[0] == pitches[1] && pitches[1] != pitches[2],
            "Ramp did not reuse a visited step and generate a note on the next empty step");
    require(plugin.livePatternStep() == 3 && plugin.midiNotes.patternDescription(4).startsWith("2/4  - - "),
            "Skipped steps were filled or the live step readout was wrong");

    juce::MemoryBlock state;
    plugin.getStateInformation(state);
    PadTrigger recalled;
    recalled.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(recalled.settings.getRawParameterValue("lfoPatternPosition")->load() == 100.0f
            && notePitches(play(recalled, 100.0f)) == pitches,
            "Pattern position depth or sparsely populated notes did not recall");

    recalled.midiNotes.resetPattern();
    const auto sequential = play(recalled, 0.0f);
    const auto sequentialPitches = notePitches(sequential);
    require(sequentialPitches.size() == 3 && sequentialPitches[0] != sequentialPitches[1]
            && recalled.livePatternStep() == -1 && recalled.midiNotes.patternDescription(4).startsWith("3/4  "),
            "Turning off the position route did not restore hit-by-hit progression");
    const auto onsets = [](const juce::MidiBuffer& events)
    {
        std::vector<int> frames;
        for (const auto event : events)
            if (event.getMessage().isNoteOn())
                frames.push_back(event.samplePosition);
        return frames;
    };
    require(onsets(midi) == onsets(sequential), "Position modulation changed the stroke-to-note timing");
    require(Lfo::patternStep(0, 100.0f, 1.0f) == -1
            && Lfo::patternStep(16, 20.0f, 1.0f) == 3
            && Lfo::patternStep(16, 100.0f, -1.0f) == 0,
            "Pattern position did not respect Free mode, depth, or the start of the loop");
}

void checkPoolLearn()
{
    PadTrigger plugin;
    plugin.prepareToPlay(rate, blockSize);
    juce::AudioBuffer<float> input(2, blockSize);
    input.clear();
    juce::MidiBuffer messages;
    const auto send = [&]
    {
        messages.addEvent(juce::MidiMessage::noteOn(3, 60, 0.8f), 0);
        messages.addEvent(juce::MidiMessage::noteOn(4, 60, 0.6f), 1);
        messages.addEvent(juce::MidiMessage::noteOn(1, 127, 0.7f), 2);
        messages.addEvent(juce::MidiMessage::noteOff(1, 61), 3);
        plugin.processBlock(input, messages);
        require(messages.isEmpty(), "Learning notes leaked into the instrument output");
        return plugin.takeLearnedNotes();
    };
    require(send() == std::array<std::uint32_t, 4>{}, "Closed pool editor captured MIDI");
    plugin.setPoolLearn(PadTrigger::PoolLearn::track);
    require(send() == std::array<std::uint32_t, 4>{0, 1u << 28, 0, 1u << 31},
            "Track MIDI learn did not capture unique note-ons");
    plugin.setPoolLearn(PadTrigger::PoolLearn::device);
    require(send() == std::array<std::uint32_t, 4>{}, "Track notes entered direct-device learn");
    plugin.learnPoolPitch(0, PadTrigger::PoolLearn::device);
    plugin.learnPoolPitch(61, PadTrigger::PoolLearn::device);
    require(plugin.takeLearnedNotes() == std::array<std::uint32_t, 4>{1, 1u << 29, 0, 0},
            "Direct-device learning lost pitches");

    juce::ValueTree learnedPattern("MidiNotes");
    learnedPattern.setProperty("patternLength", 3, nullptr);
    learnedPattern.setProperty("pattern", "60 64 67", nullptr);
    plugin.midiNotes.restoreState(learnedPattern);
    PoolEditor editor(plugin);
    const auto click = [&editor](const juce::String& label)
    {
        for (auto* child : editor.getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*>(child); button != nullptr && button->getButtonText() == label)
            {
                if (label == "MIDI Learn")
                    button->setToggleState(true, juce::dontSendNotification);
                button->onClick();
                return;
            }
        throw std::runtime_error("Could not find pool editor button");
    };
    click("E");
    messages.clear();
    plugin.midiNotes.beginBlock(messages, true, 3);
    plugin.midiNotes.trigger(messages, 0, 96, 3, 64, 0, 0);
    plugin.midiNotes.trigger(messages, 64, 96, 3, 64, 0, 1);
    plugin.midiNotes.trigger(messages, 128, 96, 3, 64, 0, 2);
    plugin.midiNotes.endBlock(192);
    const auto editedPattern = notePitches(messages);
    require(editedPattern.size() == 3 && editedPattern.front() == 60 && editedPattern[1] != 64
                && editedPattern.back() == 67,
            "Turning off E4 did not invalidate only its learned slot");

    setParameter(plugin, "soundMode", 1.0f);
    setParameter(plugin, "retriggerMs", 8.0f);
    const auto editedFree = notePitches(playStrokes(plugin, 24));
    require(editedFree.size() == 24 && std::find(editedFree.begin(), editedFree.end(), 64) == editedFree.end(),
            "Free play used a disabled key before the set was saved");
    juce::MemoryBlock state;
    plugin.getStateInformation(state);
    PadTrigger recalled;
    recalled.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    const auto edits = recalled.midiNotes.selectedPoolDetails().edits;
    require(edits && edits->count() == 7 && !edits->test(64), "Project recall lost the working note selection");
    click("Revert");
    require(!plugin.midiNotes.selectedPoolDetails().edits, "Revert did not restore the original active bank");

    click("New");
    require(notePitches(playStrokes(plugin, 4)).empty(), "An empty selection continued sending notes");
    for (auto* child : editor.getChildren())
        if (auto* name = dynamic_cast<juce::TextEditor*>(child))
            name->setText("Learned Piano");
    click("C");
    require(notePitches(playStrokes(plugin, 4)) == std::vector<int>(4, 60),
            "A single selected key did not play on every stroke");
    click("MIDI Learn");
    messages.clear();
    messages.addEvent(juce::MidiMessage::noteOn(1, 65, 0.8f), 0);
    messages.addEvent(juce::MidiMessage::noteOn(1, 83, 0.8f), 1);
    plugin.processBlock(input, messages);
    click("Save");
    const auto savedPool = plugin.midiNotes.selectedPoolDetails();
    require(savedPool.name == "Learned Piano" && savedPool.pitches == std::vector<int>{60, 65, 83},
            "The editor did not save the selected piano key and multi-octave MIDI notes together");
}

void calibrate(PadTrigger& plugin)
{
    plugin.advanceCalibration();
    process(plugin, 2 * rate, noise);
    require(plugin.calibrationStage() == Stage::readySoft, "Noise capture did not advance");

    const auto calibrationPass = [&](float level)
    {
        plugin.advanceCalibration();
        process(plugin, 5 * rate, [&](int frame)
        {
            return noise(frame) + stroke(frame, (frame / rate) * rate + 1000, level);
        });
    };
    calibrationPass(0.00005f);
    require(plugin.calibrationStage() == Stage::readyHard, "Quiet calibration taps did not register");
    calibrationPass(0.002f);
    require(plugin.calibrationStage() == Stage::complete, "Hard accents did not complete calibration");
}

void checkCalibrationWithLateAccentPeak()
{
    PadTrigger plugin;
    plugin.prepareToPlay(rate, blockSize);
    plugin.advanceCalibration();
    process(plugin, 2 * rate, noise);
    const auto padHit = [](int frame, int onset, bool accent)
    {
        return noise(frame) + stroke(frame, onset, accent ? 0.00004f : 0.00045f)
            + (accent ? stroke(frame, onset + 480, 0.003f) : 0.0f);
    };
    const auto capture = [&](bool accent)
    {
        plugin.advanceCalibration();
        const auto spacing = rate / 2;
        process(plugin, 5 * spacing, [&](int frame)
        {
            return padHit(frame, (frame / spacing) * spacing + 4800, accent);
        });
        require(plugin.calibrationHitCount() == 5, "A two-part accent was counted incorrectly");
    };
    capture(false);
    capture(true);
    const auto levels = plugin.measuredLevels();
    require(plugin.calibrationStage() == Stage::complete && levels.hard - levels.soft > 12.0f,
            "Calibration mistook an accent with a quiet leading edge for a softer stroke");

    setParameter(plugin, "soundMode", 1.0f);
    juce::MidiBuffer midi;
    process(plugin, rate / 2, [&](int frame)
    {
        return frame < rate / 4 ? padHit(frame, 4800, false) : padHit(frame, rate / 4 + 4800, true);
    }, &midi);
    std::vector<int> velocities;
    for (const auto event : midi)
        if (const auto message = event.getMessage(); message.isNoteOn())
            velocities.push_back(message.getVelocity());
    require(velocities.size() == 2 && velocities[0] < velocities[1],
            "Live replay did not produce one louder note for the accent");
}

void checkProjectEditNotifications(const juce::File& sample)
{
    struct Listener final : juce::AudioProcessorListener
    {
        void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
        void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& details) override
        {
            changes += details.nonParameterStateChanged ? 1 : 0;
        }
        int changes = 0;
    } listener;

    PadTrigger plugin;
    plugin.addListener(&listener);
    plugin.prepareToPlay(rate, blockSize);
    require(plugin.loadSample(sample).wasOk() && listener.changes == 1,
            "Loading a WAV did not notify the project host");
    require(plugin.loadSample(sample.getSiblingFile("missing-recall-fixture.wav")).failed()
                && listener.changes == 1, "A failed WAV load notified the project host");

    juce::MemoryBlock saved;
    plugin.getStateInformation(saved);
    plugin.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    require(listener.changes == 1, "Restoring a project marked it modified");

    plugin.advanceCalibration();
    plugin.cancelCalibration();
    require(listener.changes == 1, "A cancelled calibration marked the project modified");
    calibrate(plugin);
    require(listener.changes == 1, "Calibration notified the project host during audio processing");
    for (int retry = 0; retry < 25 && listener.changes == 1; ++retry)
    {
        juce::Thread::sleep(10);
        juce::Timer::callPendingTimersSynchronously();
    }
    require(listener.changes == 2, "A completed calibration did not notify the project host");
    plugin.removeListener(&listener);
}

void checkCalibrationCapture()
{
    PadTrigger plugin;
    plugin.prepareToPlay(rate, blockSize);
    calibrate(plugin);
    const auto previous = plugin.measuredLevels();
    setParameter(plugin, "sensitivity", -12.0f);
    setParameter(plugin, "dynamics", 2.5f);
    setParameter(plugin, "sampleGain", -48.0f);

    const auto background = [](int frame) { return noise(frame) * 6.0f; };
    plugin.advanceCalibration();
    process(plugin, 2 * rate, background);
    require(plugin.calibrationStage() == Stage::readySoft, "Noise capture did not finish");
    setParameter(plugin, "soundMode", 1.0f);
    juce::MidiBuffer betweenStages;
    process(plugin, rate / 2, [](int frame) { return stroke(frame, 2400, 0.1f); }, &betweenStages);
    require(notePitches(betweenStages).empty(), "Pad Trigger sounded while waiting for a calibration step");
    setParameter(plugin, "soundMode", 0.0f);

    const auto capture = [&](int hits, float peak)
    {
        const auto spacing = rate / 2;
        plugin.advanceCalibration();
        process(plugin, hits * spacing, [&](int frame)
        {
            const auto onset = (frame / spacing) * spacing + 5000;
            // Each stroke has a delayed main peak and a quieter echo.
            return background(frame) + stroke(frame, onset, 0.00018f)
                + stroke(frame, onset + 2000, peak) + stroke(frame, onset + 6500, 0.00018f);
        });
    };

    capture(5, 0.00028f);
    require(plugin.calibrationStage() == Stage::readyHard && plugin.calibrationHitCount() == 5,
            "Soft calibration did not count exactly five taps with ringing");
    capture(3, 0.00041f);
    require(plugin.calibrationStage() == Stage::hard && plugin.calibrationHitCount() == 3,
            "Hard calibration advanced with only three hits");
    process(plugin, PadTrigger::calibrationSeconds * rate, background);
    require(plugin.calibrationStage() == Stage::retryHard && plugin.calibrationHitCount() == 3,
            "Calibration retry did not preserve the actual count");

    capture(5, 0.00041f);
    const auto levels = plugin.measuredLevels();
    require(plugin.calibrationStage() == Stage::weak && plugin.calibrationHitCount() == 5 && levels.calibrated,
            "A hard-to-soft separation below 6 dB should require review");
    require(levels.hard > -69.0f && levels.hard > levels.soft && levels.hard - levels.soft < 6.0f,
            "Calibration missed the full peak of the delayed hard strikes");
    plugin.cancelCalibration();
    const auto cancelled = plugin.measuredLevels();
    require(cancelled.calibrated && cancelled.noise == previous.noise
                && cancelled.soft == previous.soft && cancelled.hard == previous.hard,
            "Cancel did not preserve the previous calibration");
    plugin.advanceCalibration();
    process(plugin, 2 * rate, background);
    capture(5, 0.00028f);
    capture(5, 0.00041f);
    plugin.acceptWeakCalibration();
    require(plugin.calibrationStage() == Stage::complete && plugin.measuredLevels().calibrated,
            "Explicit acceptance did not commit the weak calibration");
    require(plugin.settings.getRawParameterValue("sensitivity")->load() == -12.0f
            && plugin.settings.getRawParameterValue("sampleGain")->load() == -48.0f,
            "Calibration changed the user's playback settings");
}

void createSample(const juce::File& file)
{
    constexpr int sourceRate = 24000;
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    require(stream != nullptr, "Could not create fixture WAV");
    const auto options = juce::AudioFormatWriterOptions{}.withSampleRate(sourceRate)
        .withNumChannels(1).withBitsPerSample(24);
    auto writer = juce::WavAudioFormat().createWriterFor(stream, options);
    require(writer != nullptr, "Could not open fixture WAV");
    juce::AudioBuffer<float> audio(1, sourceRate / 50);
    for (int i = 0; i < audio.getNumSamples(); ++i)
        audio.setSample(0, i, 0.5f * std::exp(-static_cast<float>(i) / 100.0f));
    require(writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()), "WAV write failed");
}

void checkSampleHandoff(const juce::File& sample)
{
    PadTrigger plugin;
    plugin.prepareToPlay(rate, blockSize);
    require(plugin.loadSample(sample).wasOk(), "Could not stage the one-shot");
    const auto startVoice = [&]
    {
        return process(plugin, 300, [](int frame) { return stroke(frame, 32, 0.1f); });
    };
    require(startVoice().back() > 0.01f, "The staged WAV never reached the audio thread");
    juce::XmlElement empty("PadTrigger");
    juce::MemoryBlock state;
    juce::AudioProcessor::copyXmlToBinary(empty, state);
    plugin.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    const auto muted = process(plugin, 300, noise);
    require(std::all_of(muted.begin(), muted.end(), [](float value) { return value == 0.0f; }),
            "A voice from a cleared WAV survived the audio-thread hand-off");
    require(plugin.loadSample(sample).wasOk(), "Could not stage a WAV after clearing it");
    process(plugin, rate / 4, noise);
    require(startVoice().back() > 0.01f, "WAV playback did not recover after replacing a cleared sample");
}

void checkWavPitch(const juce::File& sample)
{
    PadTrigger plugin;
    require(plugin.loadSample(sample).wasOk(), "Could not load WAV for pitch test");
    setParameter(plugin, "lfoShape", 4.0f);
    setParameter(plugin, "lfoSeconds", 0.08f);
    setParameter(plugin, "lfoSemitones", 12.0f);
    setParameter(plugin, "lfoCents", 100.0f);
    plugin.prepareToPlay(rate, blockSize);
    const auto output = process(plugin, 3600, [](int frame)
    {
        return stroke(frame, 100, 0.1f) + stroke(frame, 2600, 0.1f);
    });
    for (const auto onset : {100, 2600})
    {
        require(output[onset] > 0.2f, "WAV pitch processing delayed a confirmed hard strike");
        const auto actual = output[onset + 100] / output[onset];
        const auto step = 0.5 * std::exp2((onset == 100 ? 13.0 : -13.0) / 12.0);
        require(std::abs(actual - std::exp(-step)) < 0.001, "WAV coarse and fine pitch did not combine at the expected ratio");
    }
}

void checkWavTail()
{
    const juce::TemporaryFile cleanup(juce::String(".wav"));
    auto stream = cleanup.getFile().createOutputStream();
    auto outputStream = std::unique_ptr<juce::OutputStream>(std::move(stream));
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor(outputStream, juce::AudioFormatWriterOptions{}
        .withSampleRate(8000.0).withNumChannels(1).withBitsPerSample(16));
    require(writer != nullptr, "Could not create maximum-length tail fixture");
    juce::AudioBuffer<float> fixture(1, 80000);
    juce::FloatVectorOperations::fill(fixture.getWritePointer(0), 0.2f, fixture.getNumSamples());
    require(writer->writeFromAudioSampleBuffer(fixture, 0, fixture.getNumSamples()), "Could not write tail fixture");
    writer.reset();
    PadTrigger plugin;
    plugin.prepareToPlay(rate, blockSize);
    require(plugin.loadSample(cleanup.getFile()).wasOk(), "Maximum-length sample did not load");
    setParameter(plugin, "pitchSemitones", -24.0f);
    setParameter(plugin, "pitchCents", -100.0f);
    constexpr int onset = 100;
    const auto tail = plugin.getTailLengthSeconds();
    require(tail >= 10.0 * std::exp2(25.0 / 12.0) && tail < 43.0, "Reported tail does not cover minimum pitch");
    const auto output = process(plugin, onset + static_cast<int>(std::ceil(tail * rate)) + 4,
                                [onset](int at) { return stroke(at, onset, 0.1f); });
    require(output[onset + 42 * rate] > 0.05f && output.back() == 0.0f,
            "The pitched voice ended too early or overran the reported tail");
}

void benchmarkCallbacks()
{
    const juce::TemporaryFile sample(juce::String(".wav"));
    {
        std::unique_ptr<juce::OutputStream> stream = sample.getFile().createOutputStream();
        juce::WavAudioFormat format;
        auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions{}
            .withSampleRate(rate).withNumChannels(2).withBitsPerSample(16));
        require(writer != nullptr, "Could not create callback benchmark fixture");
        juce::AudioBuffer<float> fixture(2, 10 * rate);
        for (int frame = 0; frame < fixture.getNumSamples(); ++frame)
        {
            fixture.setSample(0, frame, 0.04f * std::sin(frame * 0.042f));
            fixture.setSample(1, frame, 0.02f * std::sin(frame * 0.066f));
        }
        require(writer->writeFromAudioSampleBuffer(fixture, 0, fixture.getNumSamples()),
                "Could not write callback benchmark fixture");
    }

    using Clock = std::chrono::steady_clock;
    constexpr int measurements = 12000;
    const std::array<const char*, 4> scenarios {"WAV idle", "WAV one voice", "WAV dense", "MIDI modulated"};
    std::cout << "rate\tframes\tscenario\teditor\tp50_us\tp95_us\tp99_us\tmax_us\tbudget_us\tover_budget\tmax_midi_bytes\n";
    for (const int sampleRate : { 48000, 96000 })
        for (const int frames : { 32, 64, 128 })
            for (std::size_t scenario = 0; scenario < scenarios.size(); ++scenario)
                for (const bool showEditor : { false, true })
                {
                    PadTrigger plugin;
                    plugin.prepareToPlay(sampleRate, frames);
                    require(plugin.loadSample(sample.getFile()).wasOk(), "Could not load benchmark WAV");
                    setParameter(plugin, "retriggerMs", 8.0f);
                    setParameter(plugin, "sampleGain", -24.0f);
                    if (scenario == 2)
                        setParameter(plugin, "lfoSemitones", 7.0f);
                    if (scenario == 3)
                    {
                        setParameter(plugin, "soundMode", 1.0f);
                        setParameter(plugin, "noteLengthMs", 800.0f);
                        setParameter(plugin, "lfoCents", 80.0f);
                        setParameter(plugin, "lfoSeconds", 0.2f);
                    }
                    std::unique_ptr<juce::AudioProcessorEditor> editor(showEditor ? plugin.createEditor() : nullptr);
                    const auto run = [&]
                    {
                        juce::AudioBuffer<float> buffer(2, frames);
                        juce::MidiBuffer midi;
                        midi.ensureSize(2048); // The pinned JUCE VST3 wrapper reserves the same amount.
                        std::vector<double> timings;
                        timings.reserve(measurements);
                        int maxMidiBytes = 0;
                        const auto warmup = sampleRate / frames;
                        const auto interval = sampleRate * 12 / 1000;
                        const auto hitLength = sampleRate * 4 / 1000;
                        for (int block = 0; block < warmup + measurements; ++block)
                        {
                            buffer.clear();
                            midi.clear();
                            if (scenario != 0)
                                for (int frame = 0; frame < frames; ++frame)
                                {
                                    const auto absolute = block * frames + frame;
                                    const auto age = absolute % (scenario == 1 ? sampleRate * 10 : interval);
                                    if (age < hitLength)
                                    {
                                        const auto hit = 0.05f * std::exp(-age / (0.0005f * sampleRate))
                                            * std::cos(age * (juce::MathConstants<float>::twoPi * 4000.0f / sampleRate));
                                        buffer.setSample(0, frame, hit);
                                        buffer.setSample(1, frame, -0.7f * hit);
                                    }
                                }
                            const auto start = Clock::now();
                            plugin.processBlock(buffer, midi);
                            const auto elapsed = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
                            maxMidiBytes = std::max(maxMidiBytes, midi.data.size());
                            if (block >= warmup)
                                timings.push_back(elapsed);
                        }
                        return std::make_pair(std::move(timings), maxMidiBytes);
                    };
                    auto timed = std::async(std::launch::async, run);
                    if (editor)
                        while (timed.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
                        {
                            juce::Timer::callPendingTimersSynchronously();
                            editor->createComponentSnapshot(editor->getLocalBounds());
                            timed.wait_for(std::chrono::milliseconds(33));
                        }
                    auto [timings, maxMidiBytes] = timed.get();
                    std::sort(timings.begin(), timings.end());
                    const auto budget = 1.0e6 * frames / sampleRate;
                    const auto overruns = timings.end() - std::upper_bound(timings.begin(), timings.end(), budget);
                    std::cout << sampleRate << '\t' << frames << '\t' << scenarios[scenario] << '\t' << showEditor
                              << '\t' << timings[measurements / 2] << '\t' << timings[measurements * 95 / 100]
                              << '\t' << timings[measurements * 99 / 100] << '\t' << timings.back()
                              << '\t' << budget << '\t' << overruns << '\t' << maxMidiBytes << '\n';
                }
}

void checkBusLayouts()
{
    const juce::TemporaryFile cleanup(juce::String(".wav"));
    std::unique_ptr<juce::OutputStream> stream = cleanup.getFile().createOutputStream();
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions{}
        .withSampleRate(rate).withNumChannels(2).withBitsPerSample(16));
    require(writer != nullptr, "Could not create a stereo WAV fixture");
    juce::AudioBuffer<float> fixture(2, 320);
    juce::FloatVectorOperations::fill(fixture.getWritePointer(0), 0.7f, 320);
    juce::FloatVectorOperations::fill(fixture.getWritePointer(1), -0.7f, 160);
    fixture.clear(1, 160, 160);
    require(writer->writeFromAudioSampleBuffer(fixture, 0, fixture.getNumSamples()), "Could not write stereo fixture");
    writer.reset();

    for (const int inputs : {1, 2})
        for (const int outputs : {1, 2})
        {
            PadTrigger plugin;
            juce::AudioProcessor::BusesLayout layout;
            const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();
            layout.inputBuses.add(inputs == 1 ? mono : stereo);
            layout.outputBuses.add(outputs == 1 ? mono : stereo);
            require(plugin.setBusesLayout(layout), "A supported bus layout was rejected");
            plugin.prepareToPlay(rate, 512);
            require(plugin.loadSample(cleanup.getFile()).wasOk(), "Stereo fixture did not load");
            juce::AudioBuffer<float> signal(juce::jmax(inputs, outputs), 512);
            juce::MidiBuffer midi;
            const auto strike = [&]
            {
                signal.clear();
                signal.setSample(inputs - 1, 30, 0.1f);
                plugin.processBlock(signal, midi);
            };
            strike();
            require(midi.isEmpty() && signal.getSample(0, 191) > 0.1f, "WAV trigger failed in a supported bus layout");
            if (outputs == 1)
                require(std::abs(signal.getSample(0, 30)) < 0.001f, "Anti-phase stereo did not cancel on mono fold-down");
            else
                require(signal.getSample(0, 30) > 0.1f && signal.getSample(1, 30) < -0.1f
                        && std::abs(signal.getSample(1, 191)) < 0.001f, "Stereo channel separation was lost");
            setParameter(plugin, "soundMode", 1.0f);
            strike();
            require(notePitches(midi).size() == 1, "MIDI trigger failed in a supported bus layout");
            for (int channel = 0; channel < outputs; ++channel)
                require(signal.getMagnitude(channel, 0, 512) == 0.0f, "MIDI mode leaked mic audio");
        }
}

void checkFrontPanel(const juce::File& sample)
{
    PadTrigger plugin;
    require(plugin.getName() == "Padomizer", "The host-facing name did not change");
    std::unique_ptr<juce::AudioProcessorEditor> base(plugin.createEditor());
    auto& editor = dynamic_cast<PadEditor&>(*base);
    const auto inspect = [](const auto& self, const juce::Component& component) -> int
    {
        require(dynamic_cast<const juce::ComboBox*>(&component) == nullptr, "A popup selector is still on the faceplate");
        int keyboards = dynamic_cast<const PoolEditor*>(&component) != nullptr && component.isVisible() ? 1 : 0;
        for (const auto* child : component.getChildren())
            keyboards += self(self, *child);
        return keyboards;
    };
    require(inspect(inspect, editor) == 1, "The inline keyboard was missing in WAV mode");
    juce::TextButton* toggle = nullptr;
    for (auto* child : editor.getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*>(child); button != nullptr
            && button->getButtonText() == "Hide note set")
            toggle = button;
    require(toggle != nullptr, "The WAV note-set view could not be collapsed");
    const auto expandedHeight = editor.getHeight();
    toggle->onClick();
    require(editor.getHeight() < expandedHeight && inspect(inspect, editor) == 0, "The compact WAV view did not collapse");
    toggle->onClick();
    require(editor.getHeight() == expandedHeight && inspect(inspect, editor) == 1, "The inline keyboard could not be reopened");
    const juce::StringArray files { sample.getFullPathName() };
    require(editor.isInterestedInFileDrag(files), "The faceplate did not accept an inline WAV drop");
    editor.filesDropped(files, 100, 100);
    require(plugin.sampleDescription() == sample.getFileName(), "An inline WAV drop did not load the sample");
    const auto caseSize = editor.getLocalBounds();
    for (auto* component : editor.getChildren())
        if (component->getTitle() == "Sound mode")
            for (auto* button : component->getChildren())
                if (button->getTitle() == "Next Sound mode")
                    dynamic_cast<juce::TextButton&>(*button).onClick();
    require(plugin.isMidiMode() && editor.getLocalBounds() == caseSize && inspect(inspect, editor) == 1,
            "The mode selector resized the case or hid the inline keyboard");
    juce::Slider* depth = nullptr;
    juce::TextButton* coarse = nullptr;
    juce::TextButton* fine = nullptr;
    for (auto* component : editor.getChildren())
    {
        if (component->getTitle() == "LFO depth")
            depth = dynamic_cast<juce::Slider*>(component);
        if (component->getTitle() == "Select Semitones as LFO target")
            coarse = dynamic_cast<juce::TextButton*>(component);
        if (component->getTitle() == "Select Cents as LFO target")
            fine = dynamic_cast<juce::TextButton*>(component);
    }
    require(depth != nullptr && coarse != nullptr && fine != nullptr, "A front-panel pitch route was missing");
    coarse->onClick();
    depth->setValue(3.0, juce::sendNotificationSync);
    fine->onClick();
    depth->setValue(25.0, juce::sendNotificationSync);
    require(plugin.settings.getRawParameterValue("lfoSemitones")->load() == 3.0f
            && plugin.settings.getRawParameterValue("lfoCents")->load() == 25.0f,
            "Changing LFO target overwrote the other pitch depth");
}

void checkScopeThreshold()
{
    PadTrigger plugin;
    plugin.prepareToPlay(rate, blockSize);
    PadTrigger::Scope scope {};
    const auto signal = [](int frame)
    {
        if (frame >= 1200 && frame < 2200)
            return 0.0001f;
        if (frame >= 3000 && frame < 4000)
            return -0.0001f;
        return 0.0f;
    };
    require(plugin.scopeThresholdDb() == -74.0f, "Incorrect default scope threshold");
    process(plugin, 5500, signal);
    plugin.readScope(scope);
    require(std::none_of(scope.begin(), scope.end(), [](const auto& bin) { return bin.aboveThreshold; }),
            "Below-threshold mic audio was highlighted");

    auto* sensitivity = plugin.settings.getParameter("sensitivity");
    sensitivity->setValueNotifyingHost(sensitivity->convertTo0to1(12.0f));
    require(plugin.scopeThresholdDb() == -86.0f, "Turning up sensitivity should lower the scope threshold");
    process(plugin, 5500, signal);
    plugin.readScope(scope);
    require(std::any_of(scope.begin(), scope.end(), [](const auto& bin)
                        { return bin.inputMax > 0.0f && bin.aboveThreshold; }),
            "A positive threshold crossing was not highlighted");
    require(std::any_of(scope.begin(), scope.end(), [](const auto& bin)
                        { return bin.inputMin < 0.0f && bin.aboveThreshold; }),
            "A negative threshold crossing was not highlighted");
    require(std::any_of(scope.begin(), scope.end(), [](const auto& bin) { return bin.aboveThreshold && !bin.hit; }),
            "The input threshold should be indicated separately from detected hits");
}

void checkScopeRejectionReasons()
{
    PadTrigger plugin;
    plugin.prepareToPlay(rate, blockSize);
    PadTrigger::Scope scope {};
    process(plugin, 18000, [](int frame)
    {
        const auto ramp = 0.001f * juce::jlimit(0.0f, 1.0f, (frame - 12000) / 2400.0f);
        return stroke(frame, 2400, 0.00013f) + stroke(frame, 5200, 0.02f)
            + stroke(frame, 6200, 0.004f) + ramp;
    });
    plugin.readScope(scope);
    using Rejection = StrokeDetector::Rejection;
    for (auto reason : {Rejection::belowThreshold, Rejection::retrigger, Rejection::attack})
        require(std::any_of(scope.begin(), scope.end(), [reason](const auto& bin) { return bin.rejection == reason; }),
                "The mic scope lost a rejected attack reason");
    require(std::count_if(scope.begin(), scope.end(), [](const auto& bin) { return bin.hit; }) == 1,
            "A rejected attack was counted as a hit");
}

void run(const juce::File& sample)
{
    PadTrigger plugin;
    plugin.prepareToPlay(rate, blockSize);
    const auto empty = process(plugin, 4096, [](int frame) { return stroke(frame, 1500, 0.02f); });
    require(empty == std::vector<float>(empty.size(), 0.0f), "Mic audio leaked without a WAV");
    PadTrigger::Scope scope {};
    plugin.readScope(scope);
    require(std::count_if(scope.begin(), scope.end(), [](const auto& bin) { return bin.hit; }) == 1,
            "The mic scope should mark hits before a WAV is loaded");

    require(plugin.loadSample(sample).wasOk(), "Fixture WAV did not load");
    setParameter(plugin, "retriggerMs", 8.0f);
    calibrate(plugin);
    const auto levels = plugin.measuredLevels();
    require(levels.calibrated && levels.noise < levels.soft && levels.soft < levels.hard,
            "Calibration levels were invalid");

    const std::array<int, 9> onsets { 2200, 4800, 7400, 7880, 8600, 9320, 10040, 10760, 11480 };
    const std::array<float, 9> amplitudes {
        0.000045f, 0.0003f, 0.002f, 0.00015f, 0.0002f, 0.0002f, 0.0002f, 0.0002f, 0.0002f
    };
    const auto signal = [&](int frame)
    {
        auto value = noise(frame);
        for (std::size_t i = 0; i < onsets.size(); ++i)
            value += stroke(frame, onsets[i], amplitudes[i], i == 0 ? 24.0f : 0.0f);
        return value;
    };

    const auto wet = process(plugin, 14500, signal);
    std::vector<int> starts;
    for (int i = 1; i < static_cast<int>(wet.size()); ++i)
        if (wet[static_cast<std::size_t>(i)] - wet[static_cast<std::size_t>(i - 1)] > 0.01f)
            starts.push_back(i);
    require(starts.size() == onsets.size(), "Expected exactly one WAV hit per intended stroke");
    for (std::size_t i = 0; i < starts.size(); ++i)
        require(starts[i] - onsets[i] >= 0 && starts[i] - onsets[i] <= 96, "Onset delay exceeded 2 ms");
    const auto at = [&](std::size_t hit) { return wet[static_cast<std::size_t>(starts[hit])]; };
    require(at(0) < at(1) && at(1) < at(2), "Sample volume did not follow stroke force");
    require(wet[static_cast<std::size_t>(starts[3] - 1)] > 0.0f, "Overlap cut off the accent tail");
    require(wet[static_cast<std::size_t>(starts.back() + 959)] > 0.0f
            && wet[static_cast<std::size_t>(starts.back() + 960)] == 0.0f,
            "24 kHz sample had the wrong duration at 48 kHz");
    require(wet.front() == 0.0f && wet.back() == 0.0f, "Room noise leaked into the WAV output");
    plugin.readScope(scope);
    require(std::count_if(scope.begin(), scope.end(), [](const auto& bin) { return bin.hit; }) == 9,
            "Unexpected hit markers after calibration and test roll");

    juce::MemoryBlock state;
    plugin.getStateInformation(state);
    PadTrigger restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    restored.prepareToPlay(rate, blockSize);
    require(restored.measuredLevels().hard == levels.hard && restored.sampleDescription() == sample.getFileName(),
            "Calibration or sample path failed to restore");
    require(process(restored, 14500, signal) == wet, "Restored project produced different samples");

    require(sample.deleteFile(), "Could not remove the fixture WAV");
    PadTrigger missing;
    missing.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(missing.sampleDescription().startsWith("Missing"), "Missing WAV was not reported");
    createSample(sample);
    require(missing.loadSample(sample).wasOk(), "Missing WAV could not be relinked");
    missing.prepareToPlay(rate, blockSize);
    require(process(missing, 14500, signal) == wet, "Relinked sample produced different audio");
}
}

int main(int argc, char** argv)
{
    try
    {
        juce::ScopedJuceInitialiser_GUI init;
        if (argc == 2 && juce::String(argv[1]) == "--benchmark")
        {
            benchmarkCallbacks();
            return 0;
        }
        const juce::TemporaryFile cleanup(juce::String(".wav"));
        const auto& sample = cleanup.getFile();
        createSample(sample);
        checkScopeThreshold();
        checkScopeRejectionReasons();
        checkMidiPatterns();
        checkRetrigger();
        checkVelocityFloor();
        checkVelocityDecisionWindow();
        checkPadRinging();
        checkHardStrikeRinging();
        checkDetectorTimingAndChannels();
        checkWeakFollowUps();
        checkGradualAttackDuringRinging();
        checkHostBlockLatency();
        checkModulatedHitTiming();
        checkMidiPitch();
        checkSelfContainedPitchBend();
        checkConcurrentPatternRestore();
        checkNoteRate();
        checkLfoAndPattern();
        checkLfoPatternPosition();
        checkPoolLearn();
        checkSampleHandoff(sample);
        checkWavPitch(sample);
        checkWavTail();
        checkBusLayouts();
        checkFrontPanel(sample);
        checkCalibrationCapture();
        checkCalibrationWithLateAccentPeak();
        checkProjectEditNotifications(sample);
        run(sample);
        if (argc == 2)
        {
            PadTrigger plugin;
            plugin.prepareToPlay(rate, blockSize);
            require(plugin.loadSample(sample).wasOk(), "Could not load the fixture for the editor");
            calibrate(plugin);
            const bool midiPreview = juce::String(argv[1]).containsIgnoreCase("midi");
            const bool sharpPreview = juce::String(argv[1]).containsIgnoreCase("sharp");
            juce::MidiBuffer previewMidi;
            if (midiPreview)
            {
                setParameter(plugin, "soundMode", 1.0f);
                setParameter(plugin, "patternLength", 8.0f);
                setParameter(plugin, "lfoClock", 1.0f);
                setParameter(plugin, "lfoShape", 1.0f);
                setParameter(plugin, "lfoPattern", 75.0f);
                if (sharpPreview)
                    require(plugin.midiNotes.savePool("Display check", "C#2 F#2").wasOk(), "Sharp preview pool failed");
            }
            process(plugin, 18000, [](int frame)
            {
                return noise(frame) + stroke(frame, 2600, 0.00008f) + stroke(frame, 8200, 0.0014f)
                    + stroke(frame, 13300, 0.00045f) + stroke(frame, 14200, 0.00014f);
            }, midiPreview ? &previewMidi : nullptr);
            if (sharpPreview)
            {
                setParameter(plugin, "lfoSemitones", 24.0f);
                setParameter(plugin, "lfoCents", 25.0f);
            }
            const std::unique_ptr<juce::AudioProcessorEditor> editor(plugin.createEditor());
            const auto image = editor->createComponentSnapshot(editor->getLocalBounds());
            const auto target = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
            auto stream = target.createOutputStream();
            require(stream != nullptr && stream->setPosition(0) && stream->truncate().wasOk()
                        && juce::PNGImageFormat().writeImageToStream(image, *stream),
                    "Could not write editor preview");
        }
        std::cout << "Pad Trigger low-gain, MIDI pattern, and recall checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
