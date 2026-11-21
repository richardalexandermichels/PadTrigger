#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <atomic>
#include <bitset>
#include <cstdint>
#include <optional>
#include <vector>

class MidiNotes
{
public:
    static constexpr int maxPatternLength = 16;

    struct PoolDetails
    {
        juce::String name;
        std::vector<int> pitches;
        std::optional<std::bitset<128>> edits;
    };

    MidiNotes();
    juce::StringArray poolNames() const;
    int selectedPool() const;
    unsigned poolVersion() const { return poolChange.load(std::memory_order_relaxed); }
    bool selectPool(int index);
    PoolDetails selectedPoolDetails() const;
    bool setPoolEdits(std::optional<std::bitset<128>>);
    juce::Result savePool(juce::String name, juce::String pitches);
    void resetPattern();

    juce::String patternDescription(int length) const;
    int lastEvent() const { return lastNote.load(std::memory_order_relaxed); }
    unsigned noteCount() const { return sentNotes.load(std::memory_order_relaxed); }

    juce::ValueTree saveState() const;
    void restoreState(const juce::ValueTree&);

    void restartAudio() { releaseNotes = true; }
    int beginBlock(juce::MidiBuffer&, bool playing, std::optional<int> baseLength);
    void advance(juce::MidiBuffer& midi, int frame)
    {
        const auto now = sampleClock + frame;
        if (earliestOff != noDeadline && earliestOff <= now)
            emitNoteOffs(midi, frame, now);
    }
    void trigger(juce::MidiBuffer&, int frame, int velocity, int loopLength, int noteSamples,
                 int transpose = 0, int addressedStep = -1, float cents = 0.0f, int bendRange = 2);
    void setFinePitch(juce::MidiBuffer&, int frame, float cents, int bendRange, bool forNewNote = false);
    bool hasActiveNotes() const { return earliestOff != noDeadline; }
    void endBlock(int frames);

private:
    static constexpr int midiChannel = 1;
    static constexpr std::int64_t noDeadline = -1;

    struct Notes
    {
        std::array<int, 128> pitches {};
        int size = 0;
    };

    struct Pool
    {
        juce::String name;
        Notes notes;
    };

    struct Pattern
    {
        static constexpr int empty = -1;
        Pattern() { pitches.fill(empty); }
        std::array<int, maxPatternLength> pitches {};
        int length = 0;
    };

    static Notes parsePitches(const juce::String&);
    static juce::String formatPitches(const int*, int);
    static const std::array<Pool, 6>& factoryPools();
    const Pool& poolAt(int) const;
    void publishPool(std::optional<Pattern> replacement);
    Notes activePoolNotes() const;
    void flush(juce::MidiBuffer&);
    void emitNoteOffs(juce::MidiBuffer&, int frame, std::int64_t now);
    int pickPitch(int avoid = -1);
    int nextPitch(int loopLength, int addressedStep);

    mutable juce::CriticalSection poolLock;
    std::vector<Pool> customPools;
    std::optional<std::bitset<128>> poolEdits;
    int selected = 0;
    std::atomic<unsigned> poolChange { 0 };

    mutable juce::SpinLock patternLock;
    Notes sharedNotes;
    Pattern sharedPattern;
    unsigned revision = 0;

    Notes notes;
    Pattern pattern;
    unsigned seenRevision = 0;
    int cursor = 0, lastPitch = -1;
    bool patternChanged = false;
    juce::Random random;
    std::atomic<int> lastNote { 0 };
    std::atomic<unsigned> sentNotes { 0 };

    std::array<std::int64_t, 128> noteOffs;
    std::int64_t sampleClock = 0, earliestOff = noDeadline;
    int lastWheel = 8192;
    std::int64_t lastWheelFrame = noDeadline;
    bool releaseNotes = true;
};
