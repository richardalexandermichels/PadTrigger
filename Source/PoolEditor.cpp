#include "PoolEditor.h"

namespace
{
using namespace PanelTheme;
constexpr std::array<const char*, 12> names {{
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
}};
constexpr std::array<int, 12> whiteKeySlots {{ 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6 }};
constexpr int keyWidth = 64;
const juce::Rectangle<int> keybed { 13, 50, 449, 79 };
}

void PoolEditor::Key::paintButton(juce::Graphics& g, bool hovered, bool down)
{
    using namespace PanelTheme;
    const bool active = getToggleState();
    auto body = getLocalBounds().toFloat().reduced(0.7f, 0.5f);
    body = body.withTrimmedBottom(1.2f);
    g.setColour(juce::Colour(0xff171919));
    g.fillRoundedRectangle(body, 3.0f);
    auto cap = body.reduced(0.9f).withTrimmedBottom(down ? 2.0f : sharp ? 5.0f : 6.0f);
    if (down)
        cap.translate(0.0f, 1.7f);
    auto surface = juce::ColourGradient(sharp ? juce::Colour(0xff51565a) : juce::Colour(0xffcacac0),
        cap.getX(), cap.getY(), sharp ? juce::Colour(0xff1f2326) : juce::Colour(0xfffaf8ed),
        cap.getX(), cap.getBottom(), false);
    surface.addColour(0.16, sharp ? juce::Colour(0xff393d40) : juce::Colour(0xffe9e8de));
    g.setGradientFill(surface);
    g.fillRoundedRectangle(cap, 2.2f);
    g.setColour((sharp ? juce::Colours::white : ink).withAlpha(hovered ? 0.30f : 0.12f));
    g.drawRoundedRectangle(cap.reduced(0.5f), 2.0f, 1.0f);
    if (active)
    {
        const auto strip = cap.withTrimmedLeft(8.0f).withTrimmedRight(8.0f)
            .removeFromBottom(4.0f).translated(0.0f, sharp ? 2.5f : 3.5f);
        g.setColour(thresholdColour.withAlpha(0.23f));
        g.fillRoundedRectangle(strip.expanded(1.3f), 2.0f);
        g.setColour(thresholdColour);
        g.fillRoundedRectangle(strip, 1.4f);
    }
    g.setFont(mono(sharp ? 9.5f : 11.0f));
    g.setColour((sharp ? juce::Colours::white : ink).withAlpha(isEnabled() ? 0.90f : 0.35f));
    g.drawText(getButtonText(), cap.toNearestInt().removeFromBottom(sharp ? 19 : 26),
        juce::Justification::centred);
    if (hasKeyboardFocus(false))
    {
        g.setColour(teal);
        g.drawRoundedRectangle(cap.reduced(1.5f), 2.0f, 1.0f);
    }
}

PoolEditor::PoolEditor(PadTrigger& owner) : trigger(owner)
{
    setTitle("Note set controls");
    name.setTitle("Note set name");
    name.setTextToShowWhenEmpty("Name new set", muted);
    name.setInputRestrictions(40);
    name.setPopupMenuEnabled(false);
    name.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xfff9faf4));
    name.setColour(juce::TextEditor::textColourId, ink);
    name.setColour(juce::TextEditor::outlineColourId, border);
    name.setColour(juce::TextEditor::focusedOutlineColourId, ultraviolet);
    addAndMakeVisible(name);
    pool.canChange = [this]
    {
        readLearnedNotes();
        if (!hasEdits())
            return true;
        setStatus("Save or revert before changing sets.", true);
        return false;
    };
    pool.onChange = [this]
    {
        if (trigger.midiNotes.selectPool(pool.getIndex()))
            trigger.notifyStateEdited();
        reloadPool();
    };
    juce::StringArray octaves;
    for (int i = -1; i <= 9; ++i)
        octaves.add(juce::String(i));
    octave.setItems(octaves, 5);
    octave.onChange = [this] { updateKeys(); };
    for (auto* selector : { &pool, &octave, &midiSource })
        addAndMakeVisible(*selector);

    for (std::size_t i = 0; i < keys.size(); ++i)
    {
        auto& key = keys[i];
        key.sharp = whiteKeySlots[i] < 0;
        key.setButtonText(names[i]);
        key.setAlwaysOnTop(key.sharp);
        key.onClick = [this, i]
        {
            const auto pitch = static_cast<std::size_t>(octave.getIndex()) * 12 + i;
            if (pitch < selected.size())
            {
                selected.flip(pitch);
                applyEdits();
            }
        };
        addAndMakeVisible(key);
    }

    learn.setClickingTogglesState(true);
    learn.setColour(juce::TextButton::buttonOnColourId, teal);
    learn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    learn.setTooltip("Add notes from the selected MIDI input.");
    learn.onClick = [this]
    {
        readLearnedNotes();
        if (learn.getToggleState())
            refreshMidiDevices();
        updateMidiSource();
    };
    refreshMidiDevices();
    midiSource.onChange = [this] { readLearnedNotes(); updateMidiSource(); };
    newPool.onClick = [this]
    {
        trigger.takeLearnedNotes();
        selected.reset();
        name.clear();
        applyEdits();
        setStatus("Pick notes, name the set, and save.");
    };
    newPool.setTooltip("Start an empty note set.");
    revert.setTooltip("Reload the current set and discard edits.");
    revert.onClick = [this]
    {
        trigger.takeLearnedNotes();
        if (trigger.midiNotes.setPoolEdits(std::nullopt))
            trigger.notifyStateEdited();
        reloadPool();
        setStatus("Set restored.");
    };
    saveButton.onClick = [this] { save(); };
    saveButton.setColour(juce::TextButton::buttonColourId, ultraviolet);
    saveButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    for (auto* button : { &learn, &newPool, &revert, &saveButton })
        addAndMakeVisible(*button);
    name.onReturnKey = [this] { save(); };
    status.setFont(juce::FontOptions(10.5f));
    status.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(status);
    reloadPool();
    startTimerHz(25);
}

PoolEditor::~PoolEditor()
{
    device.reset();
    trigger.setPoolLearn(PadTrigger::PoolLearn::off);
}

void PoolEditor::resized()
{
    pool.setBounds(13, 22, 228, 23);
    octave.setBounds(252, 22, 87, 23);
    midiSource.setBounds(350, 22, 202, 23);
    learn.setBounds(565, 22, 134, 23);
    int white = 0;
    for (std::size_t i = 0; i < keys.size(); ++i)
        if (!keys[i].sharp)
            keys[i].setBounds(keybed.getX() + white++ * keyWidth + 1, 54, keyWidth - 1, 74);
        else
            keys[i].setBounds(keybed.getX() + white * keyWidth - 18, 53, 38, 47);
    name.setBounds(478, 57, 221, 26);
    newPool.setBounds(478, 89, 68, 24);
    revert.setBounds(549, 89, 69, 24);
    saveButton.setBounds(621, 89, 78, 24);
    status.setBounds(474, 123, 231, 24);
}

void PoolEditor::paint(juce::Graphics& g)
{
    recessedPanel(g, getLocalBounds().toFloat());
    g.setColour(ink);
    g.setFont(mono(10.5f));
    for (const auto& label : { std::pair{13, "NOTE SET"}, {252, "OCTAVE"}, {350, "MIDI INPUT"}, {478, "SET NAME"} })
        g.drawText(label.second, label.first, label.first == 478 ? 45 : 5, 110, 17, juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xff242527));
    g.fillRoundedRectangle(keybed.toFloat().expanded(1.5f), 4.0f);
    g.setColour(juce::Colour(0xff151b20));
    const juce::Rectangle<float> readout { 13.0f, 135.0f, 449.0f, 21.0f };
    g.fillRoundedRectangle(readout, 3.5f);
    g.setColour(thresholdColour);
    g.setFont(lcd(10.5f));
    g.drawText(juce::String(static_cast<int>(selected.count())), 19, 136, 24, 19, juce::Justification::centredRight);
    g.setFont(mono(9.5f));
    g.drawText("NOTES", 48, 136, 42, 19, juce::Justification::centredLeft);
    g.setColour(outputTrace);
    g.setFont(mono(11.5f));
    g.drawText(pitchDisplay, 104, 136, 350, 19, juce::Justification::centredLeft, true);
}

void PoolEditor::reloadPool()
{
    poolVersion = trigger.midiNotes.poolVersion();
    const auto details = trigger.midiNotes.selectedPoolDetails();
    selected.reset();
    for (const auto pitch : details.pitches)
        selected.set(static_cast<std::size_t>(pitch));
    saved = selected;
    if (details.edits)
        selected = *details.edits;
    savedName = details.name;
    name.setText(savedName, false);
    pool.setItems(trigger.midiNotes.poolNames(), trigger.midiNotes.selectedPool());
    updateKeys();
    setStatus(details.edits ? "Playing edited notes. Save as a set." : "");
}

bool PoolEditor::hasEdits() const { return selected != saved || name.getText() != savedName; }

void PoolEditor::applyEdits()
{
    if (trigger.midiNotes.setPoolEdits(selected != saved ? std::optional{selected} : std::nullopt))
        trigger.notifyStateEdited();
    poolVersion = trigger.midiNotes.poolVersion();
    updateKeys();
    setStatus(selected != saved ? "Playing edited notes. Save as a set." : "");
}

void PoolEditor::updateKeys()
{
    const auto base = static_cast<std::size_t>(octave.getIndex()) * 12;
    for (std::size_t i = 0; i < keys.size(); ++i)
    {
        const auto pitch = base + i;
        const bool playable = pitch < selected.size();
        keys[i].setEnabled(playable);
        keys[i].setToggleState(playable && selected.test(pitch), juce::dontSendNotification);
        if (playable)
            keys[i].setTitle(juce::MidiMessage::getMidiNoteName(static_cast<int>(pitch), true, true, 4));
    }
    juce::StringArray notes;
    for (std::size_t i = 0; i < selected.size(); ++i)
        if (selected.test(i))
            notes.add(juce::MidiMessage::getMidiNoteName(static_cast<int>(i), true, true, 4));
    pitchDisplay = notes.joinIntoString(" ");
    setDescription(pitchDisplay);
    repaint();
}

void PoolEditor::setStatus(const juce::String& message, bool error)
{
    status.setColour(juce::Label::textColourId, error ? raspberry : muted);
    status.setText(message, juce::dontSendNotification);
}

void PoolEditor::timerCallback()
{
    if (trigger.midiNotes.poolVersion() != poolVersion)
        reloadPool();
    readLearnedNotes();
}

void PoolEditor::readLearnedNotes()
{
    const auto learned = trigger.takeLearnedNotes();
    const auto old = selected;
    for (std::size_t i = 0; i < learned.size(); ++i)
        selected |= std::bitset<128>(learned[i]) << (i * 32);
    if (old != selected)
        applyEdits();
}

void PoolEditor::updateMidiSource()
{
    device.reset();
    const auto index = midiSource.getIndex() - 1;
    if (!learn.getToggleState())
    {
        trigger.setPoolLearn(PadTrigger::PoolLearn::off);
        setStatus({});
        return;
    }
    if (juce::isPositiveAndBelow(index, devices.size()))
        device = juce::MidiInput::openDevice(devices.getReference(index).identifier, this);
    if (device != nullptr)
    {
        trigger.setPoolLearn(PadTrigger::PoolLearn::device);
        device->start();
        setStatus("Play notes on the selected device.");
    }
    else
    {
        trigger.setPoolLearn(PadTrigger::PoolLearn::track);
        midiSource.setIndex(0);
        setStatus(index >= 0 ? "Device busy. Listening to Track MIDI." : "Listening to Track MIDI.", index >= 0);
    }
}

void PoolEditor::refreshMidiDevices()
{
    const auto previous = midiSource.getIndex() - 1;
    const auto identifier = juce::isPositiveAndBelow(previous, devices.size())
        ? devices.getReference(previous).identifier : juce::String();
    devices = juce::MidiInput::getAvailableDevices();
    juce::StringArray sources { "Track MIDI" };
    int current = 0;
    for (const auto& input : devices)
    {
        if (identifier.isNotEmpty() && input.identifier == identifier)
            current = sources.size();
        sources.add(input.name);
    }
    midiSource.setItems(sources, current);
}

void PoolEditor::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message)
{
    if (message.isNoteOn())
        trigger.learnPoolPitch(message.getNoteNumber(), PadTrigger::PoolLearn::device);
}

void PoolEditor::save()
{
    readLearnedNotes();
    juce::StringArray pitches;
    for (std::size_t i = 0; i < selected.size(); ++i)
        if (selected.test(i))
            pitches.add(juce::String(static_cast<int>(i)));
    const auto result = trigger.midiNotes.savePool(name.getText(), pitches.joinIntoString(" "));
    if (result.wasOk())
    {
        trigger.notifyStateEdited();
        reloadPool();
        setStatus("Set saved.");
    }
    else
        setStatus(result.getErrorMessage(), true);
}
