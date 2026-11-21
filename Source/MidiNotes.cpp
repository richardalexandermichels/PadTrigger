#include "MidiNotes.h"
#include <algorithm>
#include <tuple>
#include <utility>

namespace
{
constexpr auto stateType = "MidiNotes";
constexpr auto poolType = "Pool";
constexpr std::array<const char*, 10> scales {{
    "Note set", "Major Pentatonic", "Minor Pentatonic", "Major", "Minor", "Dorian", "Mixolydian",
    "Root + Fifth", "Major Triad", "Minor Triad"
}};
constexpr std::array<unsigned, scales.size()> scaleMasks {{
    0u, 0x295u, 0x4a9u, 0xab5u, 0x5adu, 0x6adu, 0x6b5u, 0x081u, 0x091u, 0x089u
}};

void addScaleState(juce::ValueTree& state, const MidiNotes::Harmony& harmony)
{
    state.setProperty("key", harmony.key, nullptr);
    state.setProperty("scale", harmony.scale, nullptr);
    state.setProperty("octave", harmony.octave, nullptr);
    state.setProperty("motion", static_cast<int>(harmony.motion), nullptr);
    state.setProperty("followChords", harmony.follow, nullptr);
}

int parsePitch(const juce::String& token)
{
    if (token.isEmpty())
        return -1;
    if (token.containsOnly("0123456789") && token.length() <= 3)
    {
        const auto pitch = token.getIntValue();
        return pitch <= 127 ? pitch : -1;
    }

    constexpr std::array<int, 7> naturalPitches { 9, 11, 0, 2, 4, 5, 7 };
    const auto letter = token[0];
    if (letter < 'A' || letter > 'G')
        return -1;
    int octaveStart = 1;
    int accidental = 0;
    if (token[1] == '#' || token[1] == 'B')
    {
        accidental = token[1] == '#' ? 1 : -1;
        ++octaveStart;
    }
    const auto octave = token.substring(octaveStart);
    if (!(octave == "-1" || (octave.length() == 1 && octave.containsOnly("0123456789"))))
        return -1;
    const auto pitch = (octave.getIntValue() + 1) * 12
        + naturalPitches[static_cast<std::size_t>(letter - 'A')] + accidental;
    return juce::isPositiveAndBelow(pitch, 128) ? pitch : -1;
}
}

MidiNotes::MidiNotes()
{
    noteOffs.fill(noDeadline);
    publishPool(Pattern{});
}

void MidiNotes::History::push(int pitch, int output, int velocity)
{
    const auto at = static_cast<std::size_t>(next);
    pitches[at] = pitch;
    sounded[at] = output;
    velocities[at] = velocity;
    next = (next + 1) % historySize;
    count = juce::jmin(historySize, count + 1);
}

juce::StringArray MidiNotes::scaleNames()
{
    juce::StringArray result;
    for (const auto* name : scales)
        result.add(name);
    return result;
}

MidiNotes::Harmony MidiNotes::getHarmony() const
{
    const juce::ScopedLock lock(poolLock);
    return harmony;
}

bool MidiNotes::setHarmony(Harmony next)
{
    next.key = juce::jlimit(0, 11, next.key);
    next.scale = juce::jlimit(0, static_cast<int>(scales.size()) - 1, next.scale);
    next.octave = juce::jlimit(-1, 8, next.octave);
    const juce::ScopedLock lock(poolLock);
    if (std::tie(next.key, next.scale, next.octave, next.motion, next.follow)
        == std::tie(harmony.key, harmony.scale, harmony.octave, harmony.motion, harmony.follow))
        return false;

    const auto shift = next.key - harmony.key + 12 * (next.octave - harmony.octave);
    const bool changesScale = next.scale != harmony.scale;
    harmony = next;
    if (changesScale || shift != 0)
    {
        if (changesScale)
            poolEdits.reset();
        else if (poolEdits)
            *poolEdits = shift > 0 ? *poolEdits << shift : *poolEdits >> -shift;
        publishPool(std::nullopt, true, shift);
    }
    else
    {
        const juce::SpinLock::ScopedLockType guard(patternLock);
        sharedHarmony = harmony;
        ++revision;
    }
    return true;
}

MidiNotes::Notes MidiNotes::parsePitches(const juce::String& text)
{
    Notes result;
    std::array<bool, 128> seen {};
    auto tokens = juce::StringArray::fromTokens(text.toUpperCase(), ",; \t\r\n", "");
    tokens.removeEmptyStrings();
    for (const auto& token : tokens)
    {
        const auto pitch = parsePitch(token);
        if (pitch < 0)
            return {};
        if (!seen[static_cast<std::size_t>(pitch)])
        {
            seen[static_cast<std::size_t>(pitch)] = true;
            result.pitches[static_cast<std::size_t>(result.size++)] = pitch;
        }
    }
    return result;
}

juce::String MidiNotes::formatPitches(const int* pitches, int size)
{
    juce::StringArray names;
    for (int i = 0; i < size; ++i)
        names.add(juce::MidiMessage::getMidiNoteName(pitches[i], true, true, 4));
    return names.joinIntoString(" ");
}

const std::array<MidiNotes::Pool, 6>& MidiNotes::factoryPools()
{
    static const std::array<Pool, 6> pools {{
        { "C Major Pentatonic", parsePitches("C4 D4 E4 G4 A4 C5 D5 E5") },
        { "A Minor Pentatonic", parsePitches("A3 C4 D4 E4 G4 A4 C5 D5") },
        { "D Dorian", parsePitches("D4 E4 F4 G4 A4 B4 C5 D5") },
        { "G Mixolydian", parsePitches("G3 A3 B3 C4 D4 E4 F4 G4") },
        { "C Major Arpeggio", parsePitches("C4 E4 G4 C5 E5 G5") },
        { "A Minor Arpeggio", parsePitches("A3 C4 E4 A4 C5 E5") }
    }};
    return pools;
}

const MidiNotes::Pool& MidiNotes::poolAt(int index) const
{
    const auto factorySize = static_cast<int>(factoryPools().size());
    return index < factorySize ? factoryPools()[static_cast<std::size_t>(index)]
                               : customPools[static_cast<std::size_t>(index - factorySize)];
}

juce::StringArray MidiNotes::poolNames() const
{
    const juce::ScopedLock lock(poolLock);
    juce::StringArray names;
    for (const auto& pool : factoryPools())
        names.add(pool.name);
    for (const auto& pool : customPools)
        names.add(pool.name);
    return names;
}

int MidiNotes::selectedPool() const
{
    const juce::ScopedLock lock(poolLock);
    return selected;
}

bool MidiNotes::selectPool(int index)
{
    const juce::ScopedLock lock(poolLock);
    const auto count = static_cast<int>(factoryPools().size() + customPools.size());
    if (!juce::isPositiveAndBelow(index, count) || (index == selected && harmony.scale == 0))
        return false;
    selected = index;
    harmony.scale = 0;
    harmony.key = 0;
    harmony.octave = 4;
    poolEdits.reset();
    publishPool(Pattern{}, true);
    return true;
}

MidiNotes::PoolDetails MidiNotes::selectedPoolDetails() const
{
    const juce::ScopedLock lock(poolLock);
    const auto& pool = poolAt(selected);
    const bool custom = selected >= static_cast<int>(factoryPools().size());
    const auto unedited = basePoolNotes();
    return { custom && harmony.scale == 0 ? pool.name : juce::String(),
             {unedited.pitches.begin(), unedited.pitches.begin() + unedited.size}, poolEdits };
}

bool MidiNotes::setPoolEdits(std::optional<std::bitset<128>> edits)
{
    const juce::ScopedLock lock(poolLock);
    if (poolEdits == edits)
        return false;
    poolEdits = edits;
    publishPool(std::nullopt, true);
    return true;
}

juce::Result MidiNotes::savePool(juce::String name, juce::String pitches)
{
    name = name.trim();
    if (name.isEmpty() || name.length() > 40)
        return juce::Result::fail("Enter a pool name from 1 to 40 characters.");
    for (const auto& pool : factoryPools())
        if (name.equalsIgnoreCase(pool.name))
            return juce::Result::fail("Choose a different name to save a copy of a factory pool.");

    const auto parsed = parsePitches(pitches);
    if (parsed.size == 0)
        return juce::Result::fail("Select at least one valid note.");

    const juce::ScopedLock lock(poolLock);
    const auto active = activePoolNotes();
    const auto unchanged = active.size == parsed.size
        && std::is_permutation(active.pitches.begin(), active.pitches.begin() + active.size, parsed.pitches.begin());
    const auto existing = std::find_if(customPools.begin(), customPools.end(),
        [&name](const auto& pool) { return pool.name.equalsIgnoreCase(name); });
    selected = static_cast<int>(factoryPools().size() + (existing - customPools.begin()));
    if (existing == customPools.end())
        customPools.push_back({ std::move(name), parsed });
    else
        *existing = { std::move(name), parsed };
    const auto wasTransformed = harmony.scale > 0 || harmony.key != 0 || harmony.octave != 4;
    harmony.scale = 0;
    harmony.key = 0;
    harmony.octave = 4;
    poolEdits.reset();
    if (unchanged)
    {
        if (wasTransformed)
            publishPool(std::nullopt);
        else
            poolChange.fetch_add(1, std::memory_order_relaxed);
    }
    else
        publishPool(Pattern{}, true);
    return juce::Result::ok();
}

MidiNotes::Notes MidiNotes::activePoolNotes() const
{
    if (!poolEdits)
        return basePoolNotes();
    Notes active;
    for (std::size_t pitch = 0; pitch < poolEdits->size(); ++pitch)
        if (poolEdits->test(pitch))
            active.pitches[static_cast<std::size_t>(active.size++)] = static_cast<int>(pitch);
    return active;
}

MidiNotes::Notes MidiNotes::basePoolNotes() const
{
    Notes active;
    if (harmony.scale == 0)
    {
        const auto& saved = poolAt(selected).notes;
        const auto shift = harmony.key + (harmony.octave - 4) * 12;
        for (int i = 0; i < saved.size; ++i)
        {
            const auto pitch = saved.pitches[static_cast<std::size_t>(i)] + shift;
            if (juce::isPositiveAndBelow(pitch, 128))
                active.pitches[static_cast<std::size_t>(active.size++)] = pitch;
        }
        return active;
    }
    const auto root = (harmony.octave + 1) * 12 + harmony.key;
    const auto mask = scaleMasks[static_cast<std::size_t>(harmony.scale)];
    for (int degree = 0; degree < 12 && root + degree <= 127; ++degree)
        if ((mask & (1u << degree)) != 0)
            active.pitches[static_cast<std::size_t>(active.size++)] = root + degree;
    return active;
}

void MidiNotes::publishPool(std::optional<Pattern> replacement, bool clearHistory, std::optional<int> rootShift)
{
    const auto active = activePoolNotes();
    std::bitset<128> allowed;
    if (!replacement)
        for (int i = 0; i < active.size; ++i)
            allowed.set(static_cast<std::size_t>(active.pitches[static_cast<std::size_t>(i)]));
    const juce::SpinLock::ScopedLockType lock(patternLock);
    sharedNotes = active;
    sharedHarmony = harmony;
    if (replacement)
        sharedPattern = *replacement;
    else
        for (auto& pitch : sharedPattern.pitches)
            if (pitch != Pattern::empty)
                pitch = rootShift && active.size > 0 ? nearestPitch(active, pitch + *rootShift)
                    : (allowed.test(static_cast<std::size_t>(pitch)) ? pitch : Pattern::empty);
    if (clearHistory)
    {
        sharedHistory = {};
        ++historyRevision;
    }
    editUndo.reset();
    sharedVelocities.fill(0);
    sharedLast = -1;
    sharedNext = 0;
    ++revision;
    ++phraseRestart;
    poolChange.fetch_add(1, std::memory_order_relaxed);
    lastNote.store(0, std::memory_order_relaxed);
}

void MidiNotes::resetPattern()
{
    const juce::SpinLock::ScopedLockType lock(patternLock);
    sharedPattern = {};
    editUndo.reset();
    ++revision;
    ++phraseRestart;
    lastNote.store(0, std::memory_order_relaxed);
}

MidiNotes::PhraseDisplay MidiNotes::phraseDisplay(int length) const
{
    const juce::SpinLock::ScopedLockType lock(patternLock);
    PhraseDisplay display;
    display.pitches.fill(Pattern::empty);
    display.historyCount = sharedHistory.count;
    display.repeating = sharedPattern.mode == Pattern::kept
        || (sharedPattern.mode == Pattern::automatic && length > 0);
    display.canUndo = editUndo.has_value();
    if (display.repeating)
    {
        display.length = sharedPattern.mode == Pattern::kept ? sharedPattern.length
            : juce::jlimit(0, maxPatternLength, length);
        display.pitches = sharedPattern.pitches;
        display.velocities = sharedVelocities;
        display.last = sharedLast;
        display.next = sharedNext % juce::jmax(1, display.length);
        const auto end = display.pitches.begin() + display.length;
        const auto blank = std::find(display.pitches.begin(), end, Pattern::empty);
        if (blank != end)
            display.next = static_cast<int>(blank - display.pitches.begin());
    }
    else
    {
        display.length = sharedHistory.count;
        display.last = sharedHistory.count - 1;
        for (int i = 0; i < sharedHistory.count; ++i)
        {
            const auto from = static_cast<std::size_t>(sharedHistory.index(i));
            display.pitches[static_cast<std::size_t>(i)] = sharedHistory.sounded[from];
            display.velocities[static_cast<std::size_t>(i)] = sharedHistory.velocities[from];
        }
    }
    return display;
}

bool MidiNotes::changePhrase(PhraseCommand command, int count, const History& source)
{
    count = juce::jlimit(1, historySize, count);
    if (command == PhraseCommand::none || (command == PhraseCommand::keep && source.count < count))
        return false;
    if (command == PhraseCommand::explore && sharedPattern.mode == Pattern::explore)
        return false;

    sharedPattern = {};
    sharedVelocities.fill(0);
    sharedPattern.mode = command == PhraseCommand::keep ? Pattern::kept : Pattern::explore;
    if (command == PhraseCommand::keep)
    {
        sharedPattern.length = count;
        for (int i = 0; i < count; ++i)
        {
            const auto from = static_cast<std::size_t>(source.index(source.count - count + i));
            sharedPattern.pitches[static_cast<std::size_t>(i)] = source.pitches[from];
            sharedVelocities[static_cast<std::size_t>(i)] = source.velocities[from];
        }
    }
    sharedLast = -1;
    sharedNext = 0;
    editUndo.reset();
    ++revision;
    ++phraseRestart;
    return true;
}

bool MidiNotes::applyPhraseCommand(PhraseCommand command, int count)
{
    const juce::SpinLock::ScopedLockType lock(patternLock);
    return changePhrase(command, count, sharedHistory);
}

bool MidiNotes::editPhraseStep(int step, std::optional<int> pitch)
{
    const juce::SpinLock::ScopedLockType lock(patternLock);
    if (!juce::isPositiveAndBelow(step, sharedPattern.length) || sharedPattern.mode == Pattern::explore)
        return false;
    auto& current = sharedPattern.pitches[static_cast<std::size_t>(step)];
    if (!pitch)
    {
        int total = 0;
        for (int i = 0; i < sharedNotes.size; ++i)
        {
            const auto other = sharedNotes.pitches[static_cast<std::size_t>(i)];
            if (other != current)
            {
                const auto weight = juce::jmax(1, 16 / (1 + std::abs(other - current)));
                total += weight;
                if (variation.nextInt(total) < weight)
                    pitch = other;
            }
        }
    }
    if (!pitch || *pitch == current || !juce::isPositiveAndBelow(*pitch, 128))
        return false;
    if (std::find(sharedNotes.pitches.begin(), sharedNotes.pitches.begin() + sharedNotes.size, *pitch)
        == sharedNotes.pitches.begin() + sharedNotes.size)
        return false;
    editUndo = StepEdit { step, current };
    current = *pitch;
    ++revision;
    return true;
}

bool MidiNotes::undoPhraseEdit()
{
    const juce::SpinLock::ScopedLockType lock(patternLock);
    if (!editUndo)
        return false;
    sharedPattern.pitches[static_cast<std::size_t>(editUndo->step)] = editUndo->pitch;
    editUndo.reset();
    ++revision;
    return true;
}

juce::String MidiNotes::patternDescription(int length) const
{
    length = juce::jlimit(0, maxPatternLength, length);
    Pattern copy;
    {
        const juce::SpinLock::ScopedLockType lock(patternLock);
        copy = sharedPattern;
    }
    juce::StringArray entries;
    int filled = 0;
    for (int i = 0; i < length; ++i)
    {
        const auto pitch = copy.pitches[static_cast<std::size_t>(i)];
        if (pitch == Pattern::empty)
            entries.add("-");
        else
        {
            ++filled;
            entries.add(juce::MidiMessage::getMidiNoteName(pitch, true, true, 4));
        }
    }
    if (filled == 0)
        return {};
    while (entries[entries.size() - 1] == "-")
        entries.remove(entries.size() - 1);
    return juce::String(filled) + "/" + juce::String(length) + "  " + entries.joinIntoString(" ");
}

juce::ValueTree MidiNotes::saveState() const
{
    juce::ValueTree state(stateType);
    const juce::ScopedLock lock(poolLock);
    state.setProperty("selected", selected, nullptr);
    addScaleState(state, harmony);
    if (poolEdits)
    {
        const auto active = activePoolNotes();
        state.setProperty("editedNotes", formatPitches(active.pitches.data(), active.size), nullptr);
    }
    for (const auto& pool : customPools)
    {
        juce::ValueTree item(poolType);
        item.setProperty("name", pool.name, nullptr);
        item.setProperty("notes", formatPitches(pool.notes.pitches.data(), pool.notes.size), nullptr);
        state.addChild(item, -1, nullptr);
    }
    Pattern copy;
    {
        const juce::SpinLock::ScopedLockType patternGuard(patternLock);
        copy = sharedPattern;
    }
    juce::StringArray pitches;
    auto end = copy.pitches.end();
    while (end != copy.pitches.begin() && end[-1] == Pattern::empty)
        --end;
    for (auto slot = copy.pitches.begin(); slot != end; ++slot)
        pitches.add(*slot == Pattern::empty ? "-" : juce::String(*slot));
    state.setProperty("patternLength", copy.length, nullptr);
    state.setProperty("phraseMode", static_cast<int>(copy.mode), nullptr);
    state.setProperty("pattern", pitches.joinIntoString(" "), nullptr);
    return state;
}

void MidiNotes::restoreState(const juce::ValueTree& state)
{
    const juce::ScopedLock lock(poolLock);
    customPools.clear();
    for (const auto& item : state)
    {
        if (!item.hasType(poolType))
            continue;
        auto name = item["name"].toString();
        const auto pitches = parsePitches(item["notes"].toString());
        if (name.isNotEmpty() && pitches.size > 0)
            customPools.push_back({ std::move(name), pitches });
    }

    selected = juce::jlimit(0, static_cast<int>(factoryPools().size() + customPools.size()) - 1,
                            static_cast<int>(state.getProperty("selected", 0)));
    harmony.key = juce::jlimit(0, 11, static_cast<int>(state.getProperty("key", 0)));
    harmony.scale = juce::jlimit(0, static_cast<int>(scales.size()) - 1, static_cast<int>(state.getProperty("scale", 0)));
    harmony.octave = juce::jlimit(-1, 8, static_cast<int>(state.getProperty("octave", 4)));
    harmony.motion = static_cast<Motion>(juce::jlimit(0, 3, static_cast<int>(state.getProperty("motion", 0))));
    harmony.follow = static_cast<bool>(state.getProperty("followChords", false));
    poolEdits.reset();
    if (state.hasProperty("editedNotes"))
    {
        const auto text = state["editedNotes"].toString();
        const auto parsed = parsePitches(text);
        if (parsed.size != 0 || text.isEmpty())
        {
            poolEdits.emplace();
            for (int i = 0; i < parsed.size; ++i)
                poolEdits->set(static_cast<std::size_t>(parsed.pitches[static_cast<std::size_t>(i)]));
        }
    }
    Pattern restored;
    restored.length = juce::jlimit(0, maxPatternLength, static_cast<int>(state.getProperty("patternLength", 0)));
    restored.mode = static_cast<Pattern::Mode>(juce::jlimit(0, 2, static_cast<int>(state.getProperty("phraseMode", 0))));
    const auto tokens = juce::StringArray::fromTokens(state.getProperty("pattern", "").toString(), false);
    const auto allowed = activePoolNotes();
    if (restored.length > 0 && tokens.size() <= maxPatternLength)
        for (int i = 0; i < tokens.size(); ++i)
        {
            if (tokens[i] == "-")
                continue;
            const auto pitch = parsePitch(tokens[i]);
            if (pitch < 0 || (restored.mode != Pattern::kept
                && std::find(allowed.pitches.begin(), allowed.pitches.begin() + allowed.size, pitch)
                    == allowed.pitches.begin() + allowed.size))
            {
                restored = {};
                break;
            }
            restored.pitches[static_cast<std::size_t>(i)] = pitch;
        }
    if (restored.mode == Pattern::kept && restored.length == 0)
        restored = {};
    publishPool(restored, true);
}

int MidiNotes::beginBlock(juce::MidiBuffer& midi, bool playing, std::optional<int> baseLength,
                          PhraseCommand command, int captureLength)
{
    if (command != PhraseCommand::none)
        queuedCommand.store(command);
    if (baseLength)
    {
        const juce::SpinLock::ScopedTryLockType lock(patternLock);
        if (lock.isLocked())
        {
            if (seenHistoryRevision != historyRevision)
            {
                history = sharedHistory;
                seenHistoryRevision = historyRevision;
            }
            if (changePhrase(queuedCommand.exchange(PhraseCommand::none), captureLength, history))
                hostEdited.store(true);
            if (revision != seenRevision)
            {
                notes = sharedNotes;
                pattern = sharedPattern;
                playingHarmony = sharedHarmony;
                seenRevision = revision;
                patternChanged = false;
                velocities = sharedVelocities;
                if (seenPhraseRestart != phraseRestart)
                {
                    cursor = 0;
                    lastStep = -1;
                    displayChanged = false;
                    seenPhraseRestart = phraseRestart;
                }
            }
            if (pattern.mode == Pattern::automatic && pattern.length != *baseLength)
            {
                pattern = {};
                pattern.length = *baseLength;
                patternChanged = true;
                cursor = 0;
                lastStep = -1;
                velocities.fill(0);
            }
        }
    }
    if (!playing || releaseNotes)
    {
        if (hasActiveNotes())
            flush(midi);
        setFinePitch(midi, 0, 0.0f, 1);
    }
    releaseNotes = false;
    return pattern.length;
}

int MidiNotes::constrainedLength(int length) const
{
    return pattern.mode == Pattern::kept ? juce::jmin(length, pattern.length) : length;
}

void MidiNotes::setFinePitch(juce::MidiBuffer& midi, int frame, float cents, int bendRange, bool forNewNote)
{
    constexpr int centre = 8192;
    const auto wheel = juce::jlimit(0, 16383, centre + juce::roundToInt(cents * centre / (100.0f * bendRange)));
    const auto now = sampleClock + frame;
    if (wheel != lastWheel || (forNewNote && now != lastWheelFrame))
    {
        midi.addEvent(juce::MidiMessage::pitchWheel(midiChannel, wheel), frame);
        lastWheel = wheel;
        lastWheelFrame = now;
    }
}

void MidiNotes::flush(juce::MidiBuffer& midi, int frame)
{
    for (std::size_t i = 0; i < noteOffs.size(); ++i)
        if (noteOffs[i] != noDeadline)
            midi.addEvent(juce::MidiMessage::noteOff(midiChannel, static_cast<int>(i)), frame);
    noteOffs.fill(noDeadline);
    earliestOff = noDeadline;
}

void MidiNotes::emitNoteOffs(juce::MidiBuffer& midi, int frame, std::int64_t now)
{
    earliestOff = noDeadline;
    for (std::size_t i = 0; i < noteOffs.size(); ++i)
    {
        auto& deadline = noteOffs[i];
        if (deadline == noDeadline)
            continue;
        if (deadline <= now)
        {
            midi.addEvent(juce::MidiMessage::noteOff(midiChannel, static_cast<int>(i)), frame);
            deadline = noDeadline;
        }
        else if (earliestOff == noDeadline || deadline < earliestOff)
        {
            earliestOff = deadline;
        }
    }
}

void MidiNotes::chordNote(int channel, int pitch, bool down)
{
    if (!juce::isPositiveAndBelow(pitch, 128) || !juce::isPositiveAndBelow(channel - 1, 16))
        return;
    auto& channels = chordHeld[static_cast<std::size_t>(pitch)];
    const auto bit = 1u << (channel - 1);
    const auto next = down ? channels | bit : channels & ~bit;
    if (next != channels)
    {
        channels = next;
        refreshChord();
    }
}

void MidiNotes::clearChord(int channel)
{
    const auto mask = juce::isPositiveAndBelow(channel - 1, 16) ? ~(1u << (channel - 1)) : 0u;
    for (auto& channels : chordHeld)
        channels &= mask;
    refreshChord();
}

void MidiNotes::refreshChord()
{
    chordClasses = 0;
    for (std::size_t pitch = 0; pitch < chordHeld.size(); ++pitch)
        if (chordHeld[pitch] != 0)
            chordClasses |= 1u << (pitch % 12);
    shownChord.store(chordClasses, std::memory_order_relaxed);
}

MidiNotes::Notes MidiNotes::playingNotes() const
{
    Notes chord;
    const auto root = (playingHarmony.octave + 1) * 12 + playingHarmony.key;
    if (playingHarmony.follow && chordClasses != 0)
        for (int pitch = root; pitch < root + 12 && pitch <= 127; ++pitch)
            if ((chordClasses & (1u << (pitch % 12))) != 0)
                chord.pitches[static_cast<std::size_t>(chord.size++)] = pitch;
    return chord.size > 0 ? chord : notes;
}

int MidiNotes::nearestPitch(const Notes& available, int pitch)
{
    const auto end = available.pitches.begin() + available.size;
    return *std::min_element(available.pitches.begin(), end,
        [pitch](int a, int b) { return std::abs(a - pitch) < std::abs(b - pitch); });
}

int MidiNotes::transposeByDegree(const Notes& available, int pitch, int degrees)
{
    if (degrees == 0 || available.size == 0)
        return pitch;
    unsigned classes = 0;
    for (int i = 0; i < available.size; ++i)
        classes |= 1u << (available.pitches[static_cast<std::size_t>(i)] % 12);
    const int direction = degrees < 0 ? -1 : 1;
    int lastDegree = pitch;
    while (degrees != 0)
    {
        const auto next = pitch + direction;
        if (next < 0 || next > 127)
            break;
        pitch = next;
        if ((classes & (1u << (pitch % 12))) != 0)
        {
            lastDegree = pitch;
            degrees -= direction;
        }
    }
    return lastDegree;
}

int MidiNotes::pickPitch(const Notes& available, int avoid)
{
    if (available.size == 1)
        return available.pitches.front();
    if (playingHarmony.motion != Motion::random)
    {
        if (lastPitch < 0)
            return available.pitches[static_cast<std::size_t>(random.nextInt(available.size))];
        std::array<int, 128> weights {};
        int total = 0;
        for (int i = 0; i < available.size; ++i)
        {
            const auto distance = std::abs(available.pitches[static_cast<std::size_t>(i)] - lastPitch);
            const auto weight = playingHarmony.motion == Motion::steady ? juce::jmax(1, 25 / (1 + distance * 2))
                : playingHarmony.motion == Motion::leaps ? 1 + distance
                : juce::jmax(1, 15 / (1 + std::abs(distance - 2)));
            total += weight;
            weights[static_cast<std::size_t>(i)] = total;
        }
        const auto choice = random.nextInt(total);
        const auto at = std::upper_bound(weights.begin(), weights.begin() + available.size, choice) - weights.begin();
        return available.pitches[static_cast<std::size_t>(at)];
    }

    std::array<int, 128> options;
    int count = 0;
    for (int i = 0; i < available.size; ++i)
    {
        const auto pitch = available.pitches[static_cast<std::size_t>(i)];
        if (pitch != lastPitch && pitch != avoid)
            options[static_cast<std::size_t>(count++)] = pitch;
    }
    return count > 0 ? options[static_cast<std::size_t>(random.nextInt(count))] : available.pitches.front();
}

int MidiNotes::nextPitch(const Notes& available, int loopLength, int addressedStep)
{
    lastStep = -1;
    if (loopLength == 0)
        return pickPitch(available);

    if (addressedStep >= 0)
    {
        auto& pitch = pattern.pitches[static_cast<std::size_t>(addressedStep)];
        if (pitch == Pattern::empty)
        {
            pitch = pickPitch(available);
            patternChanged = true;
        }
        cursor = (addressedStep + 1) % loopLength;
        lastStep = addressedStep;
        return pitch;
    }

    const auto end = pattern.pitches.begin() + loopLength;
    const auto empty = std::find(pattern.pitches.begin(), end, Pattern::empty);
    if (empty != end)
    {
        const auto avoidFirst = loopLength > 1 && empty == end - 1 && available.size > 2;
        *empty = pickPitch(available, avoidFirst ? pattern.pitches.front() : Pattern::empty);
        patternChanged = true;
        cursor = 0;
        lastStep = static_cast<int>(empty - pattern.pitches.begin());
        return *empty;
    }
    cursor %= loopLength;
    lastStep = cursor;
    const auto pitch = pattern.pitches[static_cast<std::size_t>(cursor)];
    cursor = (cursor + 1) % loopLength;
    return pitch;
}

void MidiNotes::trigger(juce::MidiBuffer& midi, int frame, int velocity, int loopLength, int noteSamples,
                        int transpose, int addressedStep, float cents, int bendRange, bool scaleDegrees, bool monophonic)
{
    const auto available = playingNotes();
    if (available.size == 0)
        return;
    const auto basePitch = nextPitch(available, loopLength, addressedStep);
    const auto pitch = scaleDegrees ? transposeByDegree(available, basePitch, transpose)
                                    : juce::jlimit(0, 127, basePitch + transpose);
    if (monophonic && hasActiveNotes())
        flush(midi, frame);
    emitPitch(midi, frame, pitch, velocity, noteSamples, cents, bendRange);
    lastPitch = basePitch;
    history.push(basePitch, pitch, velocity);
    if (lastStep >= 0)
        velocities[static_cast<std::size_t>(lastStep)] = velocity;
    displayChanged = true;
}

void MidiNotes::audition(juce::MidiBuffer& midi, int samples)
{
    emitPitch(midi, 0, 60, 100, samples, 0.0f, 2);
}

void MidiNotes::emitPitch(juce::MidiBuffer& midi, int frame, int pitch, int velocity, int noteSamples,
                          float cents, int bendRange)
{
    auto& deadline = noteOffs[static_cast<std::size_t>(pitch)];
    if (deadline != noDeadline)
        midi.addEvent(juce::MidiMessage::noteOff(midiChannel, pitch), frame);
    // A take may start between any two hits, even if the host is already playing.
    setFinePitch(midi, frame, cents, bendRange, true);
    midi.addEvent(juce::MidiMessage::noteOn(midiChannel, pitch, static_cast<juce::uint8>(velocity)), frame);
    deadline = sampleClock + frame + noteSamples;
    if (earliestOff == noDeadline || deadline < earliestOff)
        earliestOff = deadline;
    lastNote.store((velocity << 8) | (pitch + 1), std::memory_order_relaxed);
    sentNotes.fetch_add(1, std::memory_order_relaxed);
}

void MidiNotes::endBlock(int frames)
{
    sampleClock += frames;
    if (!patternChanged && !displayChanged)
        return;
    const juce::SpinLock::ScopedTryLockType lock(patternLock);
    if (lock.isLocked() && revision == seenRevision)
    {
        if (patternChanged)
            sharedPattern = pattern;
        if (displayChanged)
        {
            sharedHistory = history;
            sharedVelocities = velocities;
            sharedLast = lastStep;
            sharedNext = cursor;
        }
        patternChanged = false;
        displayChanged = false;
    }
}
