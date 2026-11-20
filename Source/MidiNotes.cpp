#include "MidiNotes.h"
#include <algorithm>
#include <utility>

namespace
{
constexpr auto stateType = "MidiNotes";
constexpr auto poolType = "Pool";

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

void MidiNotes::selectPool(int index)
{
    const juce::ScopedLock lock(poolLock);
    const auto count = static_cast<int>(factoryPools().size() + customPools.size());
    if (juce::isPositiveAndBelow(index, count) && index != selected)
    {
        selected = index;
        poolEdits.reset();
        publishPool(Pattern{});
    }
}

MidiNotes::PoolDetails MidiNotes::selectedPoolDetails() const
{
    const juce::ScopedLock lock(poolLock);
    const auto& pool = poolAt(selected);
    const bool custom = selected >= static_cast<int>(factoryPools().size());
    return { custom ? pool.name : juce::String(),
             {pool.notes.pitches.begin(), pool.notes.pitches.begin() + pool.notes.size}, poolEdits };
}

void MidiNotes::setPoolEdits(std::optional<std::bitset<128>> edits)
{
    const juce::ScopedLock lock(poolLock);
    if (poolEdits == edits)
        return;
    poolEdits = edits;
    publishPool(std::nullopt);
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
    const auto existing = std::find_if(customPools.begin(), customPools.end(),
        [&name](const auto& pool) { return pool.name.equalsIgnoreCase(name); });
    selected = static_cast<int>(factoryPools().size() + (existing - customPools.begin()));
    if (existing == customPools.end())
        customPools.push_back({ std::move(name), parsed });
    else
        *existing = { std::move(name), parsed };
    poolEdits.reset();
    publishPool(Pattern{});
    return juce::Result::ok();
}

MidiNotes::Notes MidiNotes::activePoolNotes() const
{
    if (!poolEdits)
        return poolAt(selected).notes;
    Notes active;
    for (std::size_t pitch = 0; pitch < poolEdits->size(); ++pitch)
        if (poolEdits->test(pitch))
            active.pitches[static_cast<std::size_t>(active.size++)] = static_cast<int>(pitch);
    return active;
}

void MidiNotes::publishPool(std::optional<Pattern> replacement)
{
    const auto active = activePoolNotes();
    std::bitset<128> allowed;
    if (!replacement)
        for (int i = 0; i < active.size; ++i)
            allowed.set(static_cast<std::size_t>(active.pitches[static_cast<std::size_t>(i)]));
    const juce::SpinLock::ScopedLockType lock(patternLock);
    sharedNotes = active;
    if (replacement)
        sharedPattern = *replacement;
    else
        for (auto& pitch : sharedPattern.pitches)
            if (pitch != Pattern::empty && !allowed.test(static_cast<std::size_t>(pitch)))
                pitch = Pattern::empty;
    ++revision;
    poolChange.fetch_add(1, std::memory_order_relaxed);
    lastNote.store(0, std::memory_order_relaxed);
}

void MidiNotes::resetPattern()
{
    const juce::SpinLock::ScopedLockType lock(patternLock);
    sharedPattern = {};
    ++revision;
    lastNote.store(0, std::memory_order_relaxed);
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
    const auto tokens = juce::StringArray::fromTokens(state.getProperty("pattern", "").toString(), false);
    const auto allowed = activePoolNotes();
    if (restored.length > 0 && tokens.size() <= maxPatternLength)
        for (int i = 0; i < tokens.size(); ++i)
        {
            if (tokens[i] == "-")
                continue;
            const auto pitch = parsePitch(tokens[i]);
            if (std::find(allowed.pitches.begin(), allowed.pitches.begin() + allowed.size, pitch)
                == allowed.pitches.begin() + allowed.size)
            {
                restored = {};
                break;
            }
            restored.pitches[static_cast<std::size_t>(i)] = pitch;
        }
    publishPool(restored);
}

void MidiNotes::beginBlock(juce::MidiBuffer& midi, bool playing, int baseLength)
{
    {
        const juce::SpinLock::ScopedTryLockType lock(patternLock);
        if (lock.isLocked() && revision != seenRevision)
        {
            notes = sharedNotes;
            pattern = sharedPattern;
            seenRevision = revision;
            patternChanged = false;
            cursor = 0;
        }
    }
    if (pattern.length != baseLength)
    {
        pattern = {};
        pattern.length = baseLength;
        patternChanged = true;
        cursor = 0;
    }
    if (!playing || releaseNotes)
    {
        if (hasActiveNotes())
            flush(midi);
        setFinePitch(midi, 0, 0.0f, 1);
    }
    releaseNotes = false;
}

void MidiNotes::setFinePitch(juce::MidiBuffer& midi, int frame, float cents, int bendRange)
{
    constexpr int centre = 8192;
    const auto wheel = juce::jlimit(0, 16383, centre + juce::roundToInt(cents * centre / (100.0f * bendRange)));
    if (wheel != lastWheel)
    {
        midi.addEvent(juce::MidiMessage::pitchWheel(midiChannel, wheel), frame);
        lastWheel = wheel;
    }
}

void MidiNotes::flush(juce::MidiBuffer& midi)
{
    for (std::size_t i = 0; i < noteOffs.size(); ++i)
        if (noteOffs[i] != noDeadline)
            midi.addEvent(juce::MidiMessage::noteOff(midiChannel, static_cast<int>(i)), 0);
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

int MidiNotes::pickPitch(int avoid)
{
    if (notes.size == 1)
        return notes.pitches.front();
    std::array<int, 128> options;
    int count = 0;
    for (int i = 0; i < notes.size; ++i)
    {
        const auto pitch = notes.pitches[static_cast<std::size_t>(i)];
        if (pitch != lastPitch && pitch != avoid)
            options[static_cast<std::size_t>(count++)] = pitch;
    }
    return options[static_cast<std::size_t>(random.nextInt(count))];
}

int MidiNotes::nextPitch(int loopLength, int addressedStep)
{
    if (loopLength == 0)
        return pickPitch();

    if (addressedStep >= 0)
    {
        auto& pitch = pattern.pitches[static_cast<std::size_t>(addressedStep)];
        if (pitch == Pattern::empty)
        {
            pitch = pickPitch();
            patternChanged = true;
        }
        cursor = (addressedStep + 1) % loopLength;
        return pitch;
    }

    const auto end = pattern.pitches.begin() + loopLength;
    const auto empty = std::find(pattern.pitches.begin(), end, Pattern::empty);
    if (empty != end)
    {
        const auto avoidFirst = loopLength > 1 && empty == end - 1 && notes.size > 2;
        *empty = pickPitch(avoidFirst ? pattern.pitches.front() : Pattern::empty);
        patternChanged = true;
        cursor = 0;
        return *empty;
    }
    cursor %= loopLength;
    const auto pitch = pattern.pitches[static_cast<std::size_t>(cursor)];
    cursor = (cursor + 1) % loopLength;
    return pitch;
}

void MidiNotes::trigger(juce::MidiBuffer& midi, int frame, int velocity, int loopLength, int noteSamples,
                        int transpose, int addressedStep)
{
    if (notes.size == 0)
        return;
    const auto basePitch = nextPitch(loopLength, addressedStep);
    const auto pitch = juce::jlimit(0, 127, basePitch + transpose);
    auto& deadline = noteOffs[static_cast<std::size_t>(pitch)];
    if (deadline != noDeadline)
        midi.addEvent(juce::MidiMessage::noteOff(midiChannel, pitch), frame);
    midi.addEvent(juce::MidiMessage::noteOn(midiChannel, pitch, static_cast<juce::uint8>(velocity)), frame);
    deadline = sampleClock + frame + noteSamples;
    if (earliestOff == noDeadline || deadline < earliestOff)
        earliestOff = deadline;
    lastPitch = basePitch;
    lastNote.store((velocity << 8) | (pitch + 1), std::memory_order_relaxed);
    sentNotes.fetch_add(1, std::memory_order_relaxed);
}

void MidiNotes::endBlock(int frames)
{
    sampleClock += frames;
    if (!patternChanged)
        return;
    const juce::SpinLock::ScopedTryLockType lock(patternLock);
    if (lock.isLocked() && revision == seenRevision)
    {
        sharedPattern = pattern;
        patternChanged = false;
    }
}
