#pragma once

#include "MidiNotes.h"
#include "Lfo.h"
#include "StrokeDetector.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <vector>

class PadTrigger final : public juce::AudioProcessor, private juce::Timer
{
public:
    enum class CalibrationStage
    {
        idle, startNoise, noise, readySoft, startSoft, soft, retrySoft,
        readyHard, startHard, hard, retryHard, committing, complete, weak, interrupted
    };
    enum class PoolLearn { off, track, device };

    struct Levels
    {
        float noise = -120.0f;
        float soft = -80.0f;
        float hard = -28.0f;
        bool calibrated = false;
    };

    struct ScopeFrame
    {
        float inputMin = 0.0f, inputMax = 0.0f;
        float outputMin = 0.0f, outputMax = 0.0f;
        bool hit = false;
        bool aboveThreshold = false;
        StrokeDetector::Rejection rejection = StrokeDetector::Rejection::none;
    };

    static constexpr int scopeSize = 256;
    static constexpr int calibrationTarget = 5;
    static constexpr int calibrationSeconds = 15;
    using Scope = std::array<ScopeFrame, scopeSize>;

    PadTrigger();
    ~PadTrigger() override;
    void prepareToPlay(double sampleRate, int maxBlockSize) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    using AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Padomizer"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    void notifyStateEdited();

    juce::Result loadSample(const juce::File&);
    juce::String sampleDescription() const;
    juce::File expectedSampleFile() const;
    void advanceCalibration();
    void acceptWeakCalibration();
    void cancelCalibration();
    CalibrationStage calibrationStage() const { return calibration.load(); }
    int calibrationHitCount() const { return calibrationHitsRecorded.load(); }
    Levels measuredLevels() const;
    std::optional<float> scopeThresholdDb() const;
    std::uint64_t readScope(Scope&) const;
    bool isMidiMode() const { return soundMode->load() > 0.5f; }
    int notesPerSecond() const { return noteRate.load(std::memory_order_relaxed); }
    int livePatternLength() const { return shownPatternLength.load(std::memory_order_relaxed); }
    int livePatternStep() const { return shownPatternStep.load(std::memory_order_relaxed); }
    float lfoPosition() const { return shownLfoPhase.load(std::memory_order_relaxed); }
    float getLfoDepth(Lfo::Knob knob) const { return lfoDepth[knob]->load(std::memory_order_relaxed); }
    void setPoolLearn(PoolLearn source);
    void learnPoolPitch(int pitch, PoolLearn source);
    std::array<std::uint32_t, 4> takeLearnedNotes();


    juce::AudioProcessorValueTreeState settings;
    MidiNotes midiNotes;

private:
    struct Sample
    {
        juce::AudioBuffer<float> audio;
        double rate = 0.0;
    };

    struct Voice
    {
        double position = 0.0;
        float gain = 0.0f;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameters();
    static std::uint32_t packLevels(Levels);
    static Levels unpackLevels(std::uint32_t);
    float sensitivityThresholdDb(Levels, float sensitivityDb) const;
    juce::Result replaceSample(const juce::File&, bool rememberMissing);
    void resetVoices();
    void renderSample(float&, float&, double step);
    void beginCalibration(CalibrationStage);
    void finishCalibration(CalibrationStage);
    void timerCallback() override;
    void measureNoise(float);
    bool measureCalibrationHit(float amplitude, float gate, float quietGate);
    void writeScope(float input, float left, float right, bool hit, bool aboveThreshold,
                    StrokeDetector::Rejection rejection);
    void measureNoteRate(const juce::MidiBuffer&, int frames);

    std::atomic<float>* const sensitivity;
    std::atomic<float>* const dynamics;
    std::atomic<float>* const sampleGain;
    std::atomic<float>* const soundMode;
    std::atomic<float>* const patternLength;
    std::atomic<float>* const noteLength;
    std::atomic<float>* const retriggerMs;
    std::atomic<float>* const velocityFloor;
    std::atomic<float>* const pitchSemitones;
    std::atomic<float>* const pitchCents;
    std::atomic<float>* const bendRange;
    std::atomic<float>* const lfoShape;
    std::atomic<float>* const lfoClock;
    std::atomic<float>* const lfoSeconds;
    std::atomic<float>* const lfoDivision;
    juce::SpinLock midiRestoreLock;
    std::array<std::atomic<float>*, Lfo::targetCount> lfoDepth {};
    double lfoPhase = 0.0, lfoBpm = 120.0, lfoBarBeats = 4.0;
    std::atomic<float> shownLfoPhase { 0.0f };
    std::atomic<float> shownSensitivity { 0.0f };
    std::atomic<int> shownPatternLength { 0 };
    std::atomic<int> shownPatternStep { -1 };
    std::atomic<PoolLearn> learningPool { PoolLearn::off };
    std::array<std::atomic<std::uint32_t>, 4> learnedNotes {};
    juce::SmoothedValue<float> outputGain;
    bool lastMidiMode = false;

    mutable juce::CriticalSection sampleDetailsLock;
    juce::SpinLock sampleLock;
    std::unique_ptr<Sample> sample, pendingSample;
    std::atomic<bool> samplePending { false };
    juce::File sampleFile;
    bool sampleAvailable = false;
    std::array<Voice, 32> voices {};
    std::size_t nextVoice = 0;
    std::uint32_t activeVoices = 0;

    double rate = 44100.0;
    int midiBendInterval = 1;
    StrokeDetector detector;
    static constexpr int minRetriggerMs = 8;
    std::int64_t audioFrame = 0;
    // The 8 ms minimum allows at most 125 notes in one second at standard sample rates.
    std::array<std::int64_t, 128> noteTimes {};
    std::size_t nextNoteTime = 0, noteTimeCount = 0;
    std::atomic<int> noteRate { 0 };

    std::atomic<CalibrationStage> calibration { CalibrationStage::idle };
    std::atomic<std::uint32_t> savedLevels { packLevels({}) };
    std::atomic<bool> calibrationEdited { false };
    std::atomic<std::uint32_t> previewLevels { packLevels({}) };
    Levels candidate;
    int calibrationRemaining = 0, noiseWindow = 1, noiseFrames = 0;
    float noisePeak = 0.0f;
    std::array<int, 121> noiseHistogram {};
    std::array<float, calibrationTarget> calibrationHits {};
    std::atomic<int> calibrationHitsRecorded { 0 };
    int numCalibrationHits = 0;
    int calibrationPeakSamples = 1, calibrationHoldSamples = 1, calibrationQuietSamples = 1;
    int calibrationPeakRemaining = 0, calibrationCooldown = 0, calibrationQuiet = 0;
    bool calibrationArmed = false;
    float calibrationPeak = 0.0f;

    static constexpr std::size_t scopeCapacity = 1024;
    std::array<std::atomic<std::uint64_t>, scopeCapacity> scopeFrames {};
    std::atomic<std::uint64_t> scopeEnd { 0 };
    std::uint64_t scopeWriteIndex = 0;
    int scopeInterval = 1, scopeSamples = 0;
    ScopeFrame scopeFrame;
};
