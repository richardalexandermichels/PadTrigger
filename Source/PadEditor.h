#pragma once

#include "PadTrigger.h"
#include "PoolEditor.h"

class PadEditor final : public juce::AudioProcessorEditor, private juce::Timer, public juce::FileDragAndDropTarget
{
public:
    explicit PadEditor(PadTrigger&);
    ~PadEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;

private:
    class HardwareLook final : public juce::LookAndFeel_V4
    {
    public:
        void drawRotarySlider(juce::Graphics&, int, int, int, int,
                              float, float, float, juce::Slider&) override;
        void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
        void drawButtonText(juce::Graphics&, juce::TextButton&, bool, bool) override;
        juce::Label* createSliderTextBox(juce::Slider&) override;
    };

    struct Control
    {
        juce::Label label;
        juce::Slider knob;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void timerCallback() override;
    void loadWav(const juce::String& path);
    void browseWav();
    void updateLfoControls();
    void updatePitchLfoReadouts();
    void commitLfoSeconds();
    juce::Rectangle<int> lfoScopeBounds() const;
    void drawLfo(juce::Graphics&) const;
    void updateMode();
    void updateNoteSetVisibility();
    void updateCalibration();
    void drawScope(juce::Graphics&, juce::Rectangle<int>, bool input) const;
    void drawMidiDisplay(juce::Graphics&) const;

    PadTrigger& trigger;
    HardwareLook hardware;
    std::array<Control, Lfo::knobCount> controls;
    PanelSelector mode { "Sound mode" }, zoom { "Mic waveform scale" };
    PanelSelector lfoShape { "Waveform" }, lfoClock { "Clock" }, lfoDivision { "Division" }, lfoTarget { "Target" };
    PanelSelector bendRange { "MIDI bend range" };
    juce::Slider lfoDepth;
    juce::TextEditor lfoSeconds, wavPath;
    std::unique_ptr<juce::FileChooser> sampleChooser;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> depthAttachment;
    juce::TextButton loadButton { "Load" };
    juce::TextButton toggleNoteSet { "Hide note set" };
    juce::TextButton calibrateButton { "Calibrate" };
    juce::TextButton cancelButton { "Cancel" };
    juce::TextButton acceptWeakButton { "Use anyway" };
    juce::TextButton resetPattern { "Reset Pattern" };
    juce::TextButton semitoneTarget { "Semitones" }, centsTarget { "Cents" };
    std::array<juce::Label, 2> pitchLfoReadouts;
    juce::Label sampleStatus, calibrationStatus, calibrationLevels;
    PoolEditor poolEditor;
    juce::Image caseMaterial;
    juce::String sampleError;
    bool showingMidi = false;
    bool showNoteSetInWav = true;
    bool lfoPeriodEdited = false;
    int recentMidi = 0;
    unsigned midiNoteCount = 0;
    int midiNoteRate = 0;
    int displayedPatternLength = 0;
    juce::String recentPattern;
    PadTrigger::Scope scope {};
    std::uint64_t lastScope = 0;
    float inputPeak = 0.0f, outputPeak = 0.0f;
    double inputPeakUntil = 0.0, outputPeakUntil = 0.0;
    double inputClipUntil = 0.0, outputClipUntil = 0.0;
};
