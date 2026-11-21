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
    static constexpr int historySize = 8;
    enum class Motion { random, steady, wandering, leaps };
    enum class PhraseCommand { none, keep, explore };

    struct Harmony
    {
        int key = 0, scale = 0, octave = 4;
        Motion motion = Motion::random;
        bool follow = false;
    };

    struct PhraseDisplay
    {
        std::array<int, maxPatternLength> pitches {};
        std::array<int, maxPatternLength> velocities {};
        int length = 0, last = -1, next = -1, historyCount = 0;
        bool repeating = false, canUndo = false;
    };

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
    Harmony getHarmony() const;
    bool setHarmony(Harmony);
    static juce::StringArray scaleNames();
    PhraseDisplay phraseDisplay(int length) const;
    bool applyPhraseCommand(PhraseCommand, int count = 4);
    bool editPhraseStep(int step, std::optional<int> pitch);
    bool undoPhraseEdit();
    void chordNote(int channel, int pitch, bool down);
    void clearChord(int channel = -1);
    unsigned heldChord() const { return shownChord.load(std::memory_order_relaxed); }
    bool takeHostEdit() { return hostEdited.exchange(false); }
    void audition(juce::MidiBuffer&, int samples);

    juce::String patternDescription(int length) const;
    int lastEvent() const { return lastNote.load(std::memory_order_relaxed); }
    unsigned noteCount() const { return sentNotes.load(std::memory_order_relaxed); }

    juce::ValueTree saveState() const;
    void restoreState(const juce::ValueTree&);

    void restartAudio() { releaseNotes = true; }
    int beginBlock(juce::MidiBuffer&, bool playing, std::optional<int> baseLength,
                   PhraseCommand = PhraseCommand::none, int captureLength = 4);
    int constrainedLength(int length) const;
    void advance(juce::MidiBuffer& midi, int frame)
    {
        const auto now = sampleClock + frame;
        if (earliestOff != noDeadline && earliestOff <= now)
            emitNoteOffs(midi, frame, now);
    }
    void trigger(juce::MidiBuffer&, int frame, int velocity, int loopLength, int noteSamples,
                 int transpose = 0, int addressedStep = -1, float cents = 0.0f, int bendRange = 2,
                 bool scaleDegrees = false, bool monophonic = false);
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
        enum Mode { automatic, explore, kept };
        static constexpr int empty = -1;
        Pattern() { pitches.fill(empty); }
        std::array<int, maxPatternLength> pitches {};
        int length = 0;
        Mode mode = automatic;
    };

    struct History
    {
        std::array<int, historySize> pitches {}, sounded {}, velocities {};
        int count = 0, next = 0;
        void push(int pitch, int output, int velocity);
        int index(int offset) const { return (next - count + offset + historySize) % historySize; }
    };
    struct StepEdit { int step, pitch; };

    static Notes parsePitches(const juce::String&);
    static juce::String formatPitches(const int*, int);
    static const std::array<Pool, 6>& factoryPools();
    const Pool& poolAt(int) const;
    void publishPool(std::optional<Pattern> replacement, bool clearHistory = false,
                     std::optional<int> rootShift = std::nullopt);
    Notes activePoolNotes() const;
    Notes basePoolNotes() const;
    Notes playingNotes() const;
    void refreshChord();
    static int nearestPitch(const Notes&, int);
    static int transposeByDegree(const Notes&, int pitch, int degrees);
    bool changePhrase(PhraseCommand, int count, const History&);
    void emitPitch(juce::MidiBuffer&, int frame, int pitch, int velocity, int noteSamples, float cents, int bendRange);
    void flush(juce::MidiBuffer&, int frame = 0);
    void emitNoteOffs(juce::MidiBuffer&, int frame, std::int64_t now);
    int pickPitch(const Notes&, int avoid = -1);
    int nextPitch(const Notes&, int loopLength, int addressedStep);

    mutable juce::CriticalSection poolLock;
    std::vector<Pool> customPools;
    std::optional<std::bitset<128>> poolEdits;
    int selected = 0;
    Harmony harmony;
    std::atomic<unsigned> poolChange { 0 };

    mutable juce::SpinLock patternLock;
    Notes sharedNotes;
    Pattern sharedPattern;
    History sharedHistory;
    Harmony sharedHarmony;
    std::optional<StepEdit> editUndo;
    juce::Random variation;
    std::array<int, maxPatternLength> sharedVelocities {};
    int sharedLast = -1, sharedNext = 0;
    unsigned revision = 0;
    unsigned historyRevision = 0;
    unsigned phraseRestart = 0;
    std::atomic<bool> hostEdited { false };
    std::atomic<PhraseCommand> queuedCommand { PhraseCommand::none };

    Notes notes;
    Pattern pattern;
    History history;
    Harmony playingHarmony;
    std::array<int, maxPatternLength> velocities {};
    std::array<unsigned, 128> chordHeld {};
    unsigned chordClasses = 0, seenHistoryRevision = 0;
    std::atomic<unsigned> shownChord { 0 };
    int lastStep = -1;
    unsigned seenRevision = 0;
    unsigned seenPhraseRestart = 0;
    int cursor = 0, lastPitch = -1;
    bool patternChanged = false;
    bool displayChanged = false;
    juce::Random random;
    std::atomic<int> lastNote { 0 };
    std::atomic<unsigned> sentNotes { 0 };

    std::array<std::int64_t, 128> noteOffs;
    std::int64_t sampleClock = 0, earliestOff = noDeadline;
    int lastWheel = 8192;
    std::int64_t lastWheelFrame = noDeadline;
    bool releaseNotes = true;
};
