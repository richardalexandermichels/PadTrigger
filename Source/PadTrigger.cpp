#include "PadTrigger.h"
#include "PadEditor.h"
#include <algorithm>
#include <cmath>

namespace
{
constexpr double maxSampleSeconds = 10.0;
constexpr double maxDownwardPitchSemitones = 25.0;
constexpr float minDb = -120.0f;

float toDb(float level) { return juce::Decibels::gainToDecibels(level, minDb); }
float toGain(float db) { return juce::Decibels::decibelsToGain(db, -150.0f); }
float calibrationThresholdDb(float noise) { return juce::jmax(-116.0f, noise + 3.0f); }

bool calibrationInProgress(PadTrigger::CalibrationStage stage)
{
    using Stage = PadTrigger::CalibrationStage;
    return stage != Stage::idle && stage != Stage::complete && stage != Stage::weak && stage != Stage::interrupted;
}

std::uint64_t encodeScope(float value)
{
    // Companding keeps quiet mic signals visible without sacrificing headroom above full scale.
    const auto magnitude = std::sqrt(juce::jmin(4.0f, std::abs(value)));
    return static_cast<std::uint64_t>(juce::roundToInt(16383.0f + std::copysign(8191.0f * magnitude, value)));
}

float decodeScope(std::uint64_t bits)
{
    const auto value = (static_cast<float>(bits & 0x7fff) - 16383.0f) / 8191.0f;
    return std::copysign(value * value, value);
}
}

PadTrigger::PadTrigger(InitialSetup setup)
    : AudioProcessor(BusesProperties().withInput("Mic", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Out", juce::AudioChannelSet::stereo(), true)),
      settings(*this, nullptr, "PadTrigger", createParameters(setup)),
      sensitivity(settings.getRawParameterValue("sensitivity")),
      dynamics(settings.getRawParameterValue("dynamics")),
      sampleGain(settings.getRawParameterValue("sampleGain")),
      soundMode(settings.getRawParameterValue("soundMode")),
      patternLength(settings.getRawParameterValue("patternLength")),
      noteLength(settings.getRawParameterValue("noteLengthMs")),
      retriggerMs(settings.getRawParameterValue("retriggerMs")),
      velocityFloor(settings.getRawParameterValue("velocityFloor")),
      pitchSemitones(settings.getRawParameterValue("pitchSemitones")),
      pitchCents(settings.getRawParameterValue("pitchCents")),
      bendRange(settings.getRawParameterValue("bendRange")),
      lfoShape(settings.getRawParameterValue("lfoShape")),
      lfoClock(settings.getRawParameterValue("lfoClock")),
      lfoSeconds(settings.getRawParameterValue("lfoSeconds")),
      lfoDivision(settings.getRawParameterValue("lfoDivision")),
      captureLength(settings.getRawParameterValue("phraseCaptureLength")),
      keepPedal(settings.getRawParameterValue("keepPhrase")),
      explorePedal(settings.getRawParameterValue("explorePhrase")),
      gateDivision(settings.getRawParameterValue("gateDivision")),
      degreePitch(settings.getRawParameterValue("degreePitch")),
      monoMidi(settings.getRawParameterValue("monoMidi"))
{
    for (std::size_t i = 0; i < lfoDepth.size(); ++i)
        lfoDepth[i] = settings.getRawParameterValue(Lfo::targets[i].depth);
    if (setup == InitialSetup::melodic)
        midiNotes.setHarmony({ 0, 1, 4, MidiNotes::Motion::wandering, false });
    startTimerHz(10);
}

PadTrigger::~PadTrigger()
{
    stopTimer();
}

void PadTrigger::notifyStateEdited()
{
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails{}.withNonParameterStateChanged(true));
}

juce::AudioProcessorValueTreeState::ParameterLayout PadTrigger::createParameters(InitialSetup setup)
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    const bool melodic = setup == InitialSetup::melodic;
    const auto decibels = AudioParameterFloatAttributes().withLabel("dB");
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"sensitivity", 1}, "Trigger sensitivity",
                   NormalisableRange<float>{-12.0f, 12.0f, 0.5f}, 0.0f, decibels),
               std::make_unique<AudioParameterFloat>(ParameterID{"dynamics", 1}, "Dynamics",
                   NormalisableRange<float>{0.35f, 2.75f, 0.01f, 0.65f}, 1.0f),
               std::make_unique<AudioParameterFloat>(ParameterID{"sampleGain", 1}, "Output level",
                   NormalisableRange<float>{-48.0f, 6.0f, 0.5f}, -6.0f, decibels),
               std::make_unique<AudioParameterChoice>(ParameterID{"soundMode", 1}, "Sound mode",
                   StringArray{"WAV Sample", "MIDI Notes"}, melodic ? 1 : 0),
               std::make_unique<AudioParameterInt>(ParameterID{"patternLength", 1}, "Pattern length",
                   0, MidiNotes::maxPatternLength, 0),
               std::make_unique<AudioParameterInt>(ParameterID{"noteLengthMs", 1}, "Note length",
                   10, 1000, melodic ? 90 : 140, AudioParameterIntAttributes().withLabel("ms")),
               std::make_unique<AudioParameterInt>(ParameterID{"retriggerMs", 1}, "Retrigger",
                   minRetriggerMs, 120, 50, AudioParameterIntAttributes().withLabel("ms")),
               std::make_unique<AudioParameterInt>(ParameterID{"velocityFloor", 1}, "Velocity floor",
                   1, 127, melodic ? 48 : 72),
               std::make_unique<AudioParameterInt>(ParameterID{"pitchSemitones", 1}, "Semitones",
                   -24, 24, 0, AudioParameterIntAttributes().withLabel("st")),
               std::make_unique<AudioParameterInt>(ParameterID{"pitchCents", 1}, "Cents",
                   -100, 100, 0, AudioParameterIntAttributes().withLabel("ct")),
               std::make_unique<AudioParameterInt>(ParameterID{"bendRange", 1}, "MIDI bend range", 1, 48, 2));
    StringArray shapes, divisions;
    for (const auto* shape : Lfo::shapes)
        shapes.add(shape);
    for (const auto& division : Lfo::divisions)
        divisions.add(division.name);
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID{"lfoShape", 1}, "LFO waveform", shapes, 0),
               std::make_unique<AudioParameterChoice>(ParameterID{"lfoClock", 1}, "LFO clock", StringArray{"Seconds", "Tempo"}, 0),
               std::make_unique<AudioParameterFloat>(ParameterID{"lfoSeconds", 1}, "LFO period",
                   NormalisableRange<float>{0.025f, 120.0f, 0.001f, 0.35f}, 2.0f,
                   AudioParameterFloatAttributes().withLabel("s")),
               std::make_unique<AudioParameterChoice>(ParameterID{"lfoDivision", 1}, "LFO division", divisions, 8));
    for (const auto& target : Lfo::targets)
    {
        const auto units = target.pitchUnit != nullptr ? String(" ") + target.pitchUnit : String("%");
        const auto maximum = target.pitchUnit != nullptr ? (target.maximum - target.minimum) * 0.5f : 100.0f;
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{target.depth, 1},
            String("LFO depth - ") + target.label, NormalisableRange<float>{0.0f, maximum, 1.0f}, 0.0f,
            AudioParameterFloatAttributes().withLabel(units.trim()).withStringFromValueFunction(
                [units](float amount, int) { return String(roundToInt(amount)) + units; })));
    }
    layout.add(std::make_unique<AudioParameterInt>(ParameterID{"phraseCaptureLength", 1}, "Keep last hits", 1, 8, 4),
               std::make_unique<AudioParameterBool>(ParameterID{"keepPhrase", 1}, "Keep Phrase", false),
               std::make_unique<AudioParameterBool>(ParameterID{"explorePhrase", 1}, "Explore Phrase", false),
               std::make_unique<AudioParameterChoice>(ParameterID{"gateDivision", 1}, "Note length unit", Lfo::gateNames, 0),
               std::make_unique<AudioParameterChoice>(ParameterID{"degreePitch", 1}, "MIDI pitch unit", StringArray{"Semitones", "Scale degrees"}, 0),
               std::make_unique<AudioParameterBool>(ParameterID{"monoMidi", 1}, "Mono MIDI", false));
    return layout;
}

bool PadTrigger::isBusesLayoutSupported(const BusesLayout& layout) const
{
    const auto input = layout.getMainInputChannelSet();
    const auto output = layout.getMainOutputChannelSet();
    const auto mono = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();
    return (input == mono || input == stereo) && (output == mono || output == stereo);
}

double PadTrigger::getTailLengthSeconds() const
{
    // Hosts may cache this value across sample or pitch changes.
    return maxSampleSeconds * std::exp2(maxDownwardPitchSemitones / 12.0);
}

void PadTrigger::prepareToPlay(double sampleRate, int)
{
    releaseResources();
    rate = sampleRate;
    midiBendInterval = juce::jmax(1, juce::roundToInt(rate * 0.002));
    detector.prepare(rate);
    noiseWindow = juce::jmax(1, juce::roundToInt(rate * 0.010));
    calibrationPeakSamples = juce::jmax(1, juce::roundToInt(rate * 0.080));
    calibrationHoldSamples = juce::jmax(1, juce::roundToInt(rate * 0.220));
    calibrationQuietSamples = juce::jmax(1, juce::roundToInt(rate * 0.020));
    scopeInterval = juce::jmax(1, juce::roundToInt(rate * 0.002));
    scopeSamples = 0;
    scopeFrame = {};
    audioFrame = 0;
    lfoPhase = 0.0;
    lfoBpm = 120.0;
    lfoBarBeats = 4.0;
    keepWasDown = keepPedal->load() > 0.5f;
    exploreWasDown = explorePedal->load() > 0.5f;
    midiNotes.clearChord();
    shownSensitivity.store(sensitivity->load(), std::memory_order_relaxed);
    shownPatternLength.store(static_cast<int>(patternLength->load()), std::memory_order_relaxed);
    shownPatternStep.store(-1, std::memory_order_relaxed);
    outputGain.reset(rate, 0.012);
    outputGain.setCurrentAndTargetValue(toGain(sampleGain->load()));
}

void PadTrigger::releaseResources()
{
    nextNoteTime = noteTimeCount = 0;
    noteRate.store(0, std::memory_order_relaxed);
    midiNotes.restartAudio();
    resetVoices();
    calibrationRemaining = 0;
    auto stage = calibration.load();
    if (stage == CalibrationStage::noise || stage == CalibrationStage::soft || stage == CalibrationStage::hard)
        calibration.compare_exchange_strong(stage, CalibrationStage::interrupted);
}

void PadTrigger::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    struct ChordEvent { int frame, channel, pitch; bool down; };
    std::array<ChordEvent, 256> chordEvents;
    std::size_t chordCount = 0, chordIndex = 0;
    const auto learn = learningPool.load(std::memory_order_acquire);
    for (const auto event : midi)
    {
        if (event.numBytes != 3)
            continue;
        const auto type = event.data[0] & 0xf0;
        const auto pitch = event.data[1] & 0x7f;
        const bool down = type == 0x90 && event.data[2] != 0;
        if (learn == PoolLearn::track && down)
            learnPoolPitch(pitch, PoolLearn::track);
        if (learn != PoolLearn::off)
            continue;
        const bool allOff = type == 0xb0 && (pitch == 120 || pitch == 123);
        if (type == 0x80 || type == 0x90 || allOff)
        {
            if (chordCount == chordEvents.size())
            {
                midiNotes.clearChord();
                chordCount = 0;
                break;
            }
            chordEvents[chordCount++] = { event.samplePosition, (event.data[0] & 0x0f) + 1,
                                         allOff ? -1 : pitch, down };
        }
    }
    if (learn != PoolLearn::off && midiNotes.heldChord() != 0)
        midiNotes.clearChord();
    midi.clear();
    if (buffer.getNumSamples() == 0)
        return;
    auto inputs = getBusBuffer(buffer, true, 0);
    auto outputs = getBusBuffer(buffer, false, 0);
    const auto* micLeft = inputs.getReadPointer(0);
    const auto* micRight = inputs.getNumChannels() > 1 ? inputs.getReadPointer(1) : nullptr;
    auto* outLeft = outputs.getWritePointer(0);
    auto* outRight = outputs.getNumChannels() > 1 ? outputs.getWritePointer(1) : nullptr;
    const auto frames = buffer.getNumSamples();

    const auto stage = calibration.load();
    if (stage == CalibrationStage::startNoise || stage == CalibrationStage::startSoft
        || stage == CalibrationStage::startHard)
        beginCalibration(stage);
    const auto activeStage = calibration.load();
    const bool mutedForCalibration = calibrationInProgress(activeStage);
    if (activeStage != CalibrationStage::noise && activeStage != CalibrationStage::soft && activeStage != CalibrationStage::hard)
        calibrationRemaining = 0;

    const auto levels = unpackLevels(savedLevels.load());
    const auto noiseRelease = toGain(levels.noise + 1.5f);
    const auto calibrationGate = toGain(calibrationThresholdDb(candidate.noise));
    const auto calibrationQuietGate = calibrationGate * 0.8f;
    const auto dynamicsRange = juce::jmax(3.0f, std::abs(levels.hard - levels.soft));
    const auto dynamicsFloor = (levels.soft + levels.hard - dynamicsRange) * 0.5f;
    const auto fullVelocityPeak = toGain(dynamicsFloor + dynamicsRange);
    const auto midiMode = isMidiMode();
    const auto midiPlaying = midiMode && !mutedForCalibration;
    const auto keepDown = keepPedal->load() > 0.5f;
    const auto exploreDown = explorePedal->load() > 0.5f;
    using Command = MidiNotes::PhraseCommand;
    auto command = Command::none;
    if (midiPlaying && exploreDown && !exploreWasDown)
        command = Command::explore;
    else if (midiPlaying && keepDown && !keepWasDown)
        command = Command::keep;
    keepWasDown = keepDown;
    exploreWasDown = exploreDown;
    const auto baseLength = [&]
    {
        const juce::SpinLock::ScopedTryLockType lock(midiRestoreLock);
        const auto length = lock.isLocked() ? std::optional{juce::roundToInt(patternLength->load())} : std::nullopt;
        return midiNotes.beginBlock(midi, midiPlaying, length, command, static_cast<int>(captureLength->load()));
    }();
    const std::array<float, Lfo::knobCount> base {{
        sensitivity->load(), dynamics->load(), sampleGain->load(), static_cast<float>(baseLength),
        noteLength->load(), retriggerMs->load(), velocityFloor->load(), pitchSemitones->load(), pitchCents->load()
    }};
    std::array<float, Lfo::targetCount> depth {};
    for (std::size_t i = 0; i < depth.size(); ++i)
        depth[i] = lfoDepth[i]->load(std::memory_order_relaxed);
    const auto shape = static_cast<int>(lfoShape->load());
    auto phaseStep = 1.0 / (rate * lfoSeconds->load());
    const bool tempoLfo = lfoClock->load() > 0.5f;
    const auto gateIndex = midiMode ? static_cast<std::size_t>(gateDivision->load()) : 0;
    if (tempoLfo || gateIndex != 0)
    {
        const auto* head = getPlayHead();
        const auto position = head != nullptr ? head->getPosition() : juce::Optional<juce::AudioPlayHead::PositionInfo>{};
        if (position)
        {
            if (const auto bpm = position->getBpm(); bpm && std::isfinite(*bpm) && *bpm >= 1.0)
                lfoBpm = *bpm;
            if (const auto signature = position->getTimeSignature(); signature && signature->numerator > 0 && signature->denominator > 0)
                lfoBarBeats = 4.0 * signature->numerator / signature->denominator;
        }
        if (tempoLfo)
        {
            const auto division = static_cast<int>(lfoDivision->load());
            const auto beats = division < 4 ? (4 - division) * lfoBarBeats
                                            : Lfo::divisions[static_cast<std::size_t>(division)].quarterNotes;
            phaseStep = lfoBpm / (60.0 * rate * beats);
            if (position && position->getIsPlaying())
                if (const auto ppq = position->getPpqPosition(); ppq && std::isfinite(*ppq))
                {
                    auto cycles = *ppq / beats;
                    const auto barStart = position->getPpqPositionOfLastBarStart();
                    const auto barIndex = position->getBarCount();
                    if (division < 4 && barStart && std::isfinite(*barStart) && (division == 3 || barIndex))
                    {
                        const auto barsPerCycle = 4 - division;
                        const auto bars = barIndex ? *barIndex % barsPerCycle : 0;
                        cycles = (static_cast<double>(bars) + (*ppq - *barStart) / lfoBarBeats) / barsPerCycle;
                    }
                    lfoPhase = Lfo::wrap(cycles);
                }
        }
    }
    const auto phaseAt = [&](int frame) { return lfoPhase + phaseStep * frame; };
    const auto modulate = [&](Lfo::Knob knob, float signal) { return Lfo::apply(knob, base[knob], depth[knob], signal); };
    const auto samplesFromMs = [this](float ms) { return juce::jmax(1, juce::roundToInt(rate * ms / 1000.0)); };
    const auto beatDurationMs = gateIndex == 0 ? 0.0f
        : static_cast<float>(60000.0 * Lfo::gateBeats[gateIndex] / lfoBpm);
    const auto inDegrees = degreePitch->load() > 0.5f;
    const auto monophonic = monoMidi->load() > 0.5f;
    const auto holdFrames = samplesFromMs(base[Lfo::retrigger]);

    // A pending replacement is a short hand-off. A busy publisher never interrupts the current WAV.
    if (lastMidiMode != midiMode)
    {
        detector.reset();
        resetVoices();
        lastMidiMode = midiMode;
    }
    if (micRight != nullptr && !detector.hasRightSignal()
        && std::none_of(micRight, micRight + frames, [](float sampleValue) { return sampleValue != 0.0f; }))
        micRight = nullptr;
    const auto channels = micRight != nullptr ? 2 : 1;
    if (!midiMode && samplePending.load(std::memory_order_acquire))
    {
        const juce::SpinLock::ScopedTryLockType lock(sampleLock);
        if (lock.isLocked())
        {
            // Leave the previous buffer in the hand-off slot for disposal off the audio thread.
            sample.swap(pendingSample);
            samplePending.store(false, std::memory_order_release);
            resetVoices();
        }
    }
    const auto canPlay = !midiMode && !mutedForCalibration && sample != nullptr;
    const auto sourceRateRatio = canPlay ? sample->rate / rate : 0.0;
    const auto pitchRatio = [](float coarse, float fine)
    {
        return std::exp2((juce::roundToInt(coarse) + fine / 100.0) / 12.0);
    };
    auto playbackStep = sourceRateRatio;
    if (canPlay && (base[Lfo::semitones] != 0.0f || base[Lfo::cents] != 0.0f))
        playbackStep *= pitchRatio(base[Lfo::semitones], base[Lfo::cents]);
    const bool modulateSensitivity = depth[Lfo::sensitivity] > 0.0f;
    const bool modulateGain = canPlay && depth[Lfo::output] > 0.0f;
    const bool modulateWavPitch = canPlay && (depth[Lfo::semitones] > 0.0f || depth[Lfo::cents] > 0.0f);
    const bool updateContinuousControls = modulateSensitivity || modulateGain || modulateWavPitch;
    const bool bendModulated = midiPlaying && depth[Lfo::cents] > 0.0f;
    const bool bendEnabled = midiPlaying && (bendModulated || base[Lfo::cents] != 0.0f);
    const auto positionDepth = midiMode && baseLength > 0 ? depth[Lfo::patternPosition] : 0.0f;
    const auto midiBendRange = static_cast<int>(bendRange->load(std::memory_order_relaxed));
    const bool modulateAtHit = depth[Lfo::dynamics] > 0.0f || (midiMode
        && (depth[Lfo::velocity] > 0.0f || depth[Lfo::noteLength] > 0.0f || depth[Lfo::pattern] > 0.0f
            || depth[Lfo::semitones] > 0.0f || bendModulated || positionDepth > 0.0f));
    const bool fixedVelocity = midiMode && depth[Lfo::velocity] == 0.0f && base[Lfo::velocity] >= 127.0f;
    float sensitivityDb = base[Lfo::sensitivity], gate = 0.0f, quietGate = 0.0f;
    const auto updateGate = [&]
    {
        gate = toGain(sensitivityThresholdDb(levels, sensitivityDb));
        quietGate = juce::jmax(0.45f * gate, juce::jmin(0.85f * gate, noiseRelease));
    };
    updateGate();
    if (canPlay && !modulateGain)
        outputGain.setTargetValue(toGain(base[Lfo::output]));

    // The control grid follows the sample clock so the LFO does not move with host block splits.
    constexpr int controlStep = 16;
    const auto previousStep = static_cast<int>(audioFrame % controlStep);
    const auto updateContinuous = [&](int frame)
    {
        const auto signal = Lfo::wave(shape, phaseAt(frame));
        if (modulateSensitivity)
        {
            sensitivityDb = modulate(Lfo::sensitivity, signal);
            updateGate();
        }
        if (modulateGain)
            outputGain.setTargetValue(toGain(modulate(Lfo::output, signal)));
        if (modulateWavPitch)
            playbackStep = sourceRateRatio * pitchRatio(modulate(Lfo::semitones, signal), modulate(Lfo::cents, signal));
    };
    if (updateContinuousControls)
        updateContinuous(-previousStep);
    if (midiPlaying && !bendModulated && (!bendEnabled || midiNotes.hasActiveNotes()))
        midiNotes.setFinePitch(midi, 0, base[Lfo::cents], midiBendRange);
    if (auditionRequested.exchange(false) && midiPlaying)
        midiNotes.audition(midi, samplesFromMs(150.0f));
    auto nextBendFrame = bendModulated
        ? static_cast<int>((midiBendInterval - audioFrame % midiBendInterval) % midiBendInterval) : 0;

    for (int frame = 0; frame < frames; ++frame)
    {
        while (chordIndex < chordCount && chordEvents[chordIndex].frame <= frame)
        {
            const auto event = chordEvents[chordIndex++];
            if (event.pitch < 0)
                midiNotes.clearChord(event.channel);
            else
                midiNotes.chordNote(event.channel, event.pitch, event.down);
        }
        if (updateContinuousControls && frame > 0 && (previousStep + frame) % controlStep == 0)
            updateContinuous(frame);
        if (midiMode)
            midiNotes.advance(midi, frame);
        if (bendModulated && frame == nextBendFrame)
        {
            nextBendFrame += midiBendInterval;
            if (midiNotes.hasActiveNotes())
                midiNotes.setFinePitch(midi, frame, modulate(Lfo::cents, Lfo::wave(shape, phaseAt(frame))), midiBendRange);
        }
        const auto sanitize = [](float input) { return std::isfinite(input) ? juce::jlimit(-4.0f, 4.0f, input) : 0.0f; };
        const std::array<float, 2> mic {{
            sanitize(micLeft[frame]), micRight != nullptr ? sanitize(micRight[frame]) : 0.0f
        }};
        const auto raw = std::abs(mic[1]) > std::abs(mic[0]) ? mic[1] : mic[0];

        const auto amplitude = std::abs(raw);
        const bool collecting = calibrationRemaining > 0;
        const bool measuringNoise = collecting && activeStage == CalibrationStage::noise;
        const auto minimum = collecting ? calibrationGate : gate;
        const bool aboveThreshold = !measuringNoise && amplitude >= minimum;

        bool hit = false;
        auto rejection = StrokeDetector::Rejection::none;
        if (collecting)
        {
            if (measuringNoise)
                measureNoise(amplitude);
            else
                hit = measureCalibrationHit(amplitude, calibrationGate, calibrationQuietGate);

            if (--calibrationRemaining == 0 || numCalibrationHits == calibrationTarget)
            {
                calibrationRemaining = 0;
                finishCalibration(activeStage);
                // Let the final calibration strike decay before live output resumes.
                detector.holdOff(calibrationCooldown);
            }
        }
        else if (!mutedForCalibration)
        {
            const auto saturation = fixedVelocity ? gate : fullVelocityPeak;
            const auto detected = detector.process(mic, channels, gate, quietGate, holdFrames, saturation);
            if (detected.onset && depth[Lfo::retrigger] > 0.0f)
                detector.holdOff(samplesFromMs(modulate(Lfo::retrigger, Lfo::wave(shape, phaseAt(frame)))));
            hit = detected.peak > 0.0f;
            if (hit)
                hitCount.fetch_add(1, std::memory_order_relaxed);
            rejection = detected.rejection;
            if (hit && (midiMode || canPlay))
            {
                const auto signal = modulateAtHit ? Lfo::wave(shape, phaseAt(frame)) : 0.0f;
                float response = 1.0f;
                if (!fixedVelocity && detected.peak < fullVelocityPeak)
                {
                    const auto strength = juce::jlimit(0.0f, 1.0f, (toDb(detected.peak) - dynamicsFloor) / dynamicsRange);
                    response = std::pow(strength, modulate(Lfo::dynamics, signal));
                }
                if (midiMode)
                {
                    const auto minimumVelocity = modulate(Lfo::velocity, signal);
                    const auto velocity = minimumVelocity + (127.0f - minimumVelocity) * response;
                    const auto length = midiNotes.constrainedLength(juce::roundToInt(modulate(Lfo::pattern, signal)));
                    const auto modulatedMs = modulate(Lfo::noteLength, signal);
                    const auto duration = samplesFromMs(beatDurationMs == 0.0f ? modulatedMs
                        : beatDurationMs * modulatedMs / base[Lfo::noteLength]);
                    const auto transpose = juce::roundToInt(modulate(Lfo::semitones, signal));
                    const auto step = Lfo::patternStep(length, positionDepth, signal);
                    midiNotes.trigger(midi, frame, juce::roundToInt(velocity), length, duration,
                                      transpose, step, modulate(Lfo::cents, signal), midiBendRange, inDegrees, monophonic);
                }
                else
                {
                    voices[nextVoice] = { 0.0, 0.08f + 0.92f * response };
                    activeVoices |= std::uint32_t { 1 } << nextVoice;
                    nextVoice = (nextVoice + 1u) % voices.size();
                }
            }
        }

        float left = 0.0f;
        float right = 0.0f;
        if (canPlay)
        {
            if (activeVoices != 0)
                renderSample(left, right, playbackStep);
            const auto gain = outputGain.getNextValue();
            left *= gain;
            right *= gain;
            if (outRight == nullptr)
                left = right = 0.5f * (left + right);
            else
                outRight[frame] = right;
            outLeft[frame] = left;
        }
        writeScope(raw, left, right, hit, aboveThreshold, rejection);
    }
    if (!canPlay)
        outputs.clear();
    midiNotes.endBlock(frames);
    if (!midiMode)
        nextNoteTime = noteTimeCount = 0;
    measureNoteRate(midi, frames);
    audioFrame += frames;
    lfoPhase = Lfo::wrap(phaseAt(frames));
    shownLfoPhase.store(static_cast<float>(lfoPhase), std::memory_order_relaxed);
    shownSensitivity.store(sensitivityDb, std::memory_order_relaxed);
    const auto lengthModulated = baseLength > 0 && depth[Lfo::pattern] > 0.0f;
    const auto displaySignal = lengthModulated || positionDepth > 0.0f ? Lfo::wave(shape, lfoPhase) : 0.0f;
    const auto displayedLength = midiNotes.constrainedLength(lengthModulated
        ? juce::roundToInt(modulate(Lfo::pattern, displaySignal)) : baseLength);
    shownPatternLength.store(displayedLength, std::memory_order_relaxed);
    shownPatternStep.store(Lfo::patternStep(displayedLength, positionDepth, displaySignal), std::memory_order_relaxed);
}

void PadTrigger::setPoolLearn(PoolLearn source)
{
    learningPool.store(PoolLearn::off, std::memory_order_release);
    takeLearnedNotes();
    learningPool.store(source, std::memory_order_release);
}

void PadTrigger::learnPoolPitch(int pitch, PoolLearn source)
{
    if (learningPool.load(std::memory_order_acquire) == source && juce::isPositiveAndBelow(pitch, 128))
    {
        const auto note = static_cast<unsigned>(pitch);
        learnedNotes[note / 32].fetch_or(std::uint32_t{1} << (note % 32), std::memory_order_relaxed);
    }
}

std::array<std::uint32_t, 4> PadTrigger::takeLearnedNotes()
{
    std::array<std::uint32_t, 4> result {};
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = learnedNotes[i].exchange(0, std::memory_order_relaxed);
    return result;
}

void PadTrigger::measureNoteRate(const juce::MidiBuffer& midi, int frames)
{
    for (const auto event : midi)
    {
        if (!event.getMessage().isNoteOn())
            continue;
        noteTimes[nextNoteTime] = audioFrame + event.samplePosition;
        nextNoteTime = (nextNoteTime + 1) % noteTimes.size();
        noteTimeCount = juce::jmin(noteTimeCount + 1, noteTimes.size());
    }
    const auto oldestFrame = audioFrame + frames - juce::roundToInt(rate);
    while (noteTimeCount > 0
           && noteTimes[(nextNoteTime + noteTimes.size() - noteTimeCount) % noteTimes.size()] < oldestFrame)
        --noteTimeCount;
    noteRate.store(static_cast<int>(noteTimeCount), std::memory_order_relaxed);
}

std::uint32_t PadTrigger::packLevels(Levels levels)
{
    const auto encode = [](float db)
    {
        return static_cast<std::uint32_t>(juce::roundToInt(
            (juce::jlimit(minDb, 7.5f, db) - minDb) * 2.0f));
    };
    return encode(levels.noise) | (encode(levels.soft) << 8) | (encode(levels.hard) << 16)
        | (levels.calibrated ? 1u << 24 : 0u);
}

PadTrigger::Levels PadTrigger::unpackLevels(std::uint32_t bits)
{
    const auto decode = [](std::uint32_t value) { return minDb + 0.5f * static_cast<float>(value & 255u); };
    return { decode(bits), decode(bits >> 8), decode(bits >> 16), (bits & (1u << 24)) != 0 };
}

PadTrigger::Levels PadTrigger::measuredLevels() const
{
    switch (calibration.load())
    {
        case CalibrationStage::readySoft:
        case CalibrationStage::startSoft:
        case CalibrationStage::soft:
        case CalibrationStage::retrySoft:
        case CalibrationStage::readyHard:
        case CalibrationStage::startHard:
        case CalibrationStage::hard:
        case CalibrationStage::retryHard:
        case CalibrationStage::weak:
            return unpackLevels(previewLevels.load());
        default:
            return unpackLevels(savedLevels.load());
    }
}

float PadTrigger::sensitivityThresholdDb(Levels levels, float sensitivityDb) const
{
    const auto quietHit = juce::jmin(levels.soft, levels.hard);
    const auto threshold = levels.calibrated ? juce::jmax(levels.noise + 3.0f, quietHit - 6.0f) : -74.0f;
    return juce::jlimit(-116.0f, 0.0f, threshold - sensitivityDb);
}

std::optional<float> PadTrigger::scopeThresholdDb() const
{
    switch (calibration.load())
    {
        case CalibrationStage::noise:
            return std::nullopt;
        case CalibrationStage::soft:
        case CalibrationStage::hard:
            return calibrationThresholdDb(unpackLevels(previewLevels.load()).noise);
        default:
            return sensitivityThresholdDb(unpackLevels(savedLevels.load()),
                lfoDepth[Lfo::sensitivity]->load() > 0.0f ? shownSensitivity.load() : sensitivity->load());
    }
}

void PadTrigger::advanceCalibration()
{
    auto stage = calibration.load();
    auto next = CalibrationStage::startNoise;
    switch (stage)
    {
        case CalibrationStage::idle:
        case CalibrationStage::complete:
        case CalibrationStage::weak:
        case CalibrationStage::interrupted:
            break;
        case CalibrationStage::readySoft:
        case CalibrationStage::retrySoft:
            next = CalibrationStage::startSoft;
            break;
        case CalibrationStage::readyHard:
        case CalibrationStage::retryHard:
            next = CalibrationStage::startHard;
            break;
        default:
            return;
    }
    calibration.compare_exchange_strong(stage, next);
}

void PadTrigger::cancelCalibration()
{
    auto stage = calibration.load();
    while (stage != CalibrationStage::committing
           && !calibration.compare_exchange_weak(stage, CalibrationStage::idle))
    {
    }
}

void PadTrigger::acceptWeakCalibration()
{
    const auto measured = previewLevels.load();
    auto stage = CalibrationStage::weak;
    if (calibration.compare_exchange_strong(stage, CalibrationStage::complete)
        && savedLevels.exchange(measured) != measured)
        notifyStateEdited();
}

void PadTrigger::beginCalibration(CalibrationStage stage)
{
    CalibrationStage next = CalibrationStage::noise;
    double seconds = 2.0;
    if (stage == CalibrationStage::startNoise)
    {
        candidate = Levels { minDb, minDb, minDb, false };
        noiseHistogram.fill(0);
        noisePeak = 0.0f;
        noiseFrames = 0;
    }
    else
    {
        next = stage == CalibrationStage::startSoft ? CalibrationStage::soft : CalibrationStage::hard;
        seconds = calibrationSeconds;
    }
    numCalibrationHits = 0;
    calibrationHitsRecorded.store(0);
    calibrationPeakRemaining = calibrationCooldown = calibrationQuiet = 0;
    calibrationArmed = false;
    calibrationPeak = 0.0f;
    calibrationRemaining = juce::roundToInt(rate * seconds);
    detector.reset();
    resetVoices();
    if (!calibration.compare_exchange_strong(stage, next))
        calibrationRemaining = 0;
}

void PadTrigger::measureNoise(float amplitude)
{
    noisePeak = juce::jmax(noisePeak, amplitude);
    if (++noiseFrames < noiseWindow)
        return;
    const auto index = juce::jlimit(0, 120, juce::roundToInt(toDb(noisePeak) - minDb));
    ++noiseHistogram[static_cast<std::size_t>(index)];
    noiseFrames = 0;
    noisePeak = 0.0f;
}

bool PadTrigger::measureCalibrationHit(float amplitude, float gate, float quietGate)
{
    if (calibrationCooldown > 0)
        --calibrationCooldown;
    calibrationQuiet = amplitude < quietGate ? juce::jmin(calibrationQuiet + 1, calibrationQuietSamples) : 0;
    if (calibrationQuiet == calibrationQuietSamples)
        calibrationArmed = true;
    const bool started = calibrationPeakRemaining == 0 && calibrationCooldown == 0
        && calibrationArmed && amplitude >= gate;
    if (started)
    {
        // Measure the isolated stroke. A quiet leading edge can precede the main impact.
        calibrationPeak = 0.0f;
        calibrationPeakRemaining = calibrationPeakSamples;
        calibrationCooldown = calibrationHoldSamples;
        calibrationArmed = false;
    }
    if (calibrationPeakRemaining > 0)
    {
        calibrationPeak = juce::jmax(calibrationPeak, amplitude);
        if (--calibrationPeakRemaining == 0)
        {
            calibrationHits[static_cast<std::size_t>(numCalibrationHits++)] = toDb(calibrationPeak);
            calibrationHitsRecorded.store(numCalibrationHits);
        }
    }
    return started;
}

void PadTrigger::finishCalibration(CalibrationStage stage)
{
    if (stage == CalibrationStage::noise)
    {
        // A percentile of 10 ms peaks ignores isolated disturbances during the room-noise capture.
        const auto rank = juce::roundToInt(0.9 * (rate * 2.0 / noiseWindow));
        int count = 0;
        for (std::size_t index = 0; index < noiseHistogram.size(); ++index)
        {
            count += noiseHistogram[index];
            if (count >= rank)
            {
                candidate.noise = minDb + static_cast<float>(index);
                break;
            }
        }
        previewLevels.store(packLevels(candidate));
        calibration.compare_exchange_strong(stage, CalibrationStage::readySoft);
        return;
    }

    const bool soft = stage == CalibrationStage::soft;
    if (numCalibrationHits < calibrationTarget)
    {
        calibration.compare_exchange_strong(stage, soft ? CalibrationStage::retrySoft : CalibrationStage::retryHard);
        return;
    }

    auto median = calibrationHits.begin() + numCalibrationHits / 2;
    std::nth_element(calibrationHits.begin(), median, calibrationHits.begin() + numCalibrationHits);
    const auto medianDb = juce::jlimit(minDb, 0.0f, *median);
    if (soft)
    {
        candidate.soft = medianDb;
        previewLevels.store(packLevels(candidate));
        calibration.compare_exchange_strong(stage, CalibrationStage::readyHard);
    }
    else
    {
        candidate.hard = medianDb;
        candidate.calibrated = true;
        const bool weak = candidate.soft - candidate.noise < 9.0f || candidate.hard - candidate.soft < 6.0f;
        if (weak)
        {
            previewLevels.store(packLevels(candidate));
            calibration.compare_exchange_strong(stage, CalibrationStage::weak);
        }
        else if (calibration.compare_exchange_strong(stage, CalibrationStage::committing))
        {
            const auto measured = packLevels(candidate);
            if (savedLevels.exchange(measured) != measured)
                calibrationEdited.store(true);

            // Publish completion after the saved data and host notification are ready.
            calibration.store(CalibrationStage::complete);
        }
    }
}

void PadTrigger::timerCallback()
{
    const auto phraseEdited = midiNotes.takeHostEdit();
    if (calibrationEdited.exchange(false) || phraseEdited)
        notifyStateEdited();
}

void PadTrigger::writeScope(float input, float left, float right, bool hit, bool aboveThreshold,
                            StrokeDetector::Rejection rejection)
{
    if (scopeSamples == 0)
        scopeFrame = { input, input, juce::jmin(left, right), juce::jmax(left, right), hit, aboveThreshold, rejection };
    else
    {
        scopeFrame.inputMin = juce::jmin(scopeFrame.inputMin, input);
        scopeFrame.inputMax = juce::jmax(scopeFrame.inputMax, input);
        scopeFrame.outputMin = juce::jmin(scopeFrame.outputMin, left, right);
        scopeFrame.outputMax = juce::jmax(scopeFrame.outputMax, left, right);
        scopeFrame.hit = scopeFrame.hit || hit;
        scopeFrame.aboveThreshold = scopeFrame.aboveThreshold || aboveThreshold;
        scopeFrame.rejection = juce::jmax(scopeFrame.rejection, rejection);
    }

    if (++scopeSamples != scopeInterval)
        return;
    scopeSamples = 0;
    const auto bits = encodeScope(scopeFrame.inputMin) | (encodeScope(scopeFrame.inputMax) << 15)
        | (encodeScope(scopeFrame.outputMin) << 30) | (encodeScope(scopeFrame.outputMax) << 45)
        | (static_cast<std::uint64_t>(scopeFrame.hit) << 60)
        | (static_cast<std::uint64_t>(scopeFrame.aboveThreshold) << 61)
        | (static_cast<std::uint64_t>(scopeFrame.rejection) << 62);
    scopeFrames[scopeWriteIndex % scopeCapacity].store(bits, std::memory_order_relaxed);
    scopeEnd.store(++scopeWriteIndex, std::memory_order_release);
}

std::uint64_t PadTrigger::readScope(Scope& destination) const
{
    const auto end = scopeEnd.load(std::memory_order_acquire);
    const auto count = juce::jmin(end, static_cast<std::uint64_t>(scopeSize));
    for (std::uint64_t i = 0; i < count; ++i)
    {
        const auto bits = scopeFrames[(end - count + i) % scopeCapacity].load(std::memory_order_relaxed);
        destination[scopeSize - count + i] = {
            decodeScope(bits), decodeScope(bits >> 15), decodeScope(bits >> 30),
            decodeScope(bits >> 45), ((bits >> 60) & 1u) != 0, ((bits >> 61) & 1u) != 0,
            static_cast<StrokeDetector::Rejection>((bits >> 62) & 3u)
        };
    }
    return end;
}

void PadTrigger::resetVoices()
{
    activeVoices = 0;
    nextVoice = 0;
}

void PadTrigger::renderSample(float& left, float& right, double step)
{
    const auto& audio = sample->audio;
    const auto length = audio.getNumSamples();
    const auto* leftData = audio.getReadPointer(0);
    const auto* rightData = audio.getReadPointer(audio.getNumChannels() - 1);
    for (auto pending = activeVoices; pending != 0;)
    {
        const auto slot = juce::findHighestSetBit(pending);
        const auto mask = std::uint32_t{1} << slot;
        pending &= ~mask;
        auto& voice = voices[static_cast<std::size_t>(slot)];
        const auto index = static_cast<int>(voice.position);
        const auto next = index + 1;
        const auto fraction = static_cast<float>(voice.position - index);
        const auto leftNext = next < length ? leftData[next] : 0.0f;
        const auto rightNext = next < length ? rightData[next] : 0.0f;
        const auto level = voice.gain;
        left += level * (leftData[index] + fraction * (leftNext - leftData[index]));
        right += level * (rightData[index] + fraction * (rightNext - rightData[index]));
        voice.position += step;
        if (voice.position >= length)
            activeVoices &= ~mask;
    }
}

juce::Result PadTrigger::loadSample(const juce::File& file)
{
    const auto result = replaceSample(file, false);
    if (result.wasOk())
        notifyStateEdited();
    return result;
}

juce::Result PadTrigger::replaceSample(const juce::File& file, bool rememberMissing)
{
    std::unique_ptr<Sample> loaded;
    auto result = juce::Result::ok();
    if (file != juce::File())
    {
        juce::WavAudioFormat wav;
        auto stream = file.createInputStream();
        std::unique_ptr<juce::AudioFormatReader> reader(
            stream != nullptr ? wav.createReaderFor(stream.release(), true) : nullptr);
        if (reader == nullptr || !std::isfinite(reader->sampleRate) || reader->sampleRate < 1000.0
            || reader->sampleRate > 384000.0 || reader->lengthInSamples < 1)
            result = juce::Result::fail("Could not read this WAV file.");
        else if (reader->numChannels < 1 || reader->numChannels > 2)
            result = juce::Result::fail("Choose a mono or stereo WAV file.");
        else if (static_cast<double>(reader->lengthInSamples) > maxSampleSeconds * reader->sampleRate)
            result = juce::Result::fail("Choose a WAV file that is 10 seconds or shorter.");
        else
        {
            loaded = std::make_unique<Sample>();
            loaded->rate = reader->sampleRate;
            loaded->audio.setSize(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
            if (!reader->read(&loaded->audio, 0, loaded->audio.getNumSamples(), 0, true, reader->numChannels == 2))
            {
                loaded.reset();
                result = juce::Result::fail("The WAV file could not be decoded.");
            }
            else
            {
                for (int channel = 0; channel < loaded->audio.getNumChannels(); ++channel)
                {
                    auto* data = loaded->audio.getWritePointer(channel);
                    for (int frame = 0; frame < loaded->audio.getNumSamples(); ++frame)
                        if (!std::isfinite(data[frame]))
                            data[frame] = 0.0f;
                }
            }
        }
    }

    if (result.wasOk() || rememberMissing)
    {
        const juce::ScopedLock details(sampleDetailsLock);
        {
            const juce::SpinLock::ScopedLockType lock(sampleLock);
            pendingSample.swap(loaded);
            samplePending.store(true, std::memory_order_release);
        }
        sampleFile = file;
        sampleAvailable = result.wasOk() && file != juce::File();
    }
    return result;
}

juce::String PadTrigger::sampleDescription() const
{
    const juce::ScopedLock details(sampleDetailsLock);
    if (sampleFile == juce::File())
        return "No WAV loaded";
    return sampleAvailable ? sampleFile.getFileName() : "Missing or unreadable: " + sampleFile.getFileName();
}

juce::File PadTrigger::expectedSampleFile() const
{
    const juce::ScopedLock details(sampleDetailsLock);
    return sampleFile;
}

void PadTrigger::getStateInformation(juce::MemoryBlock& data)
{
    auto state = [&]
    {
        const juce::SpinLock::ScopedLockType lock(midiRestoreLock);
        auto copy = settings.copyState();
        copy.appendChild(midiNotes.saveState(), nullptr);
        return copy;
    }();
    {
        const juce::ScopedLock details(sampleDetailsLock);
        state.setProperty("samplePath", sampleFile.getFullPathName(), nullptr);
    }
    state.setProperty("calibratedLevels", static_cast<juce::int64>(savedLevels.load()), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, data);
}

void PadTrigger::setStateInformation(const void* data, int size)
{
    const auto xml = getXmlFromBinary(data, size);
    if (xml == nullptr || !xml->hasTagName(settings.state.getType()))
        return;
    auto state = juce::ValueTree::fromXml(*xml);
    cancelCalibration();
    const auto path = state["samplePath"].toString();
    const auto file = juce::File::isAbsolutePath(path) ? juce::File(path) : juce::File();
    replaceSample(file, true);
    state.removeProperty("samplePath", nullptr);
    const auto levels = state.hasProperty("calibratedLevels")
        ? static_cast<std::uint32_t>(static_cast<juce::int64>(state["calibratedLevels"])) : packLevels({});
    savedLevels.store(levels);
    state.removeProperty("calibratedLevels", nullptr);
    const auto notesState = state.getChildWithName("MidiNotes");
    if (notesState.isValid())
        state.removeChild(notesState, nullptr);
    if (const auto legacyAuto = state.getChildWithProperty("id", "autoRetrigger"); legacyAuto.isValid())
        state.removeChild(legacyAuto, nullptr);
    const auto restoreDefault = [this, &state](const char* id, std::optional<float> legacy = std::nullopt)
    {
        if (!state.getChildWithProperty("id", id).isValid())
        {
            juce::ValueTree parameter("PARAM");
            parameter.setProperty("id", id, nullptr);
            const auto* setting = settings.getParameter(id);
            parameter.setProperty("value", legacy.value_or(setting->convertFrom0to1(setting->getDefaultValue())), nullptr);
            state.appendChild(parameter, nullptr);
        }
    };
    restoreDefault("soundMode", 0.0f);
    restoreDefault("noteLengthMs", 140.0f);
    restoreDefault("velocityFloor", 72.0f);
    for (const auto* id : { "patternLength", "retriggerMs",
                           "pitchSemitones", "pitchCents", "bendRange", "lfoShape", "lfoClock", "lfoSeconds", "lfoDivision",
                           "phraseCaptureLength", "gateDivision", "degreePitch", "monoMidi" })
        restoreDefault(id);
    for (const auto* id : { "keepPhrase", "explorePhrase" })
    {
        if (const auto pedal = state.getChildWithProperty("id", id); pedal.isValid())
            state.removeChild(pedal, nullptr);
        restoreDefault(id);
    }
    for (const auto& target : Lfo::targets)
        restoreDefault(target.depth);
    const juce::SpinLock::ScopedLockType lock(midiRestoreLock);
    settings.replaceState(state);
    midiNotes.restoreState(notesState);
}

juce::AudioProcessorEditor* PadTrigger::createEditor()
{
    return new PadEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PadTrigger();
}
