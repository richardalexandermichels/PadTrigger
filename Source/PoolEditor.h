#pragma once

#include "PadTrigger.h"
#include "PanelSelector.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <bitset>

class PoolEditor final : public juce::Component, private juce::Timer, private juce::MidiInputCallback
{
public:
    explicit PoolEditor(PadTrigger&);
    ~PoolEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool hasNotes() const { return selected.any(); }

private:
    class Key final : public juce::TextButton
    {
    public:
        bool sharp = false;
        void paintButton(juce::Graphics&, bool hovered, bool down) override;
    };

    void timerCallback() override;
    void readLearnedNotes();
    void reloadPool();
    void applyEdits();
    void updateKeys();
    void refreshMidiDevices();
    void updateMidiSource();
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;
    void save();
    bool hasEdits() const;
    void setStatus(const juce::String&, bool error = false);

    PadTrigger& trigger;
    std::bitset<128> selected, saved;
    juce::String savedName;
    juce::String pitchDisplay;
    unsigned poolVersion = 0;
    int lastRegister = -2;
    std::array<Key, 12> keys;
    juce::TextEditor name;
    PanelSelector pool { "Note set" }, octave { "Octave" }, midiSource { "MIDI input" };
    juce::TextButton learn { "MIDI Learn" }, newPool { "New" }, revert { "Revert" }, saveButton { "Save" };
    juce::Label status;
    juce::Array<juce::MidiDeviceInfo> devices;
    std::unique_ptr<juce::MidiInput> device;
};
