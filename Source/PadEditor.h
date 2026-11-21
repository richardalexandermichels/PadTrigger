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

    struct PhraseCell final : juce::TextButton
    {
        int velocity = 0;
        bool played = false, upcoming = false;
        void paintButton(juce::Graphics&, bool, bool) override;
    };

    enum class Drawer { closed, input, modulation, notes, choices };

    void timerCallback() override;
    void loadWav(const juce::String& path);
    void browseWav();
    void updateLfoControls();
    void updatePitchLfoReadouts();
    void commitLfoSeconds();
    juce::Rectangle<int> lfoScopeBounds() const;
    void drawLfo(juce::Graphics&) const;
    void updateMode();
    void setupPhraseControls();
    void updateLayout();
    void updateHarmony();
    void updatePhrase();
    void openDrawer(Drawer);
    void chooseFrom(PanelSelector&);
    void chooseStep(int);
    void choicePage(int delta = 0);
    void showChoices(juce::String title, juce::StringArray, int selected, std::function<void(int)>);
    void updateCalibration();
    void drawScope(juce::Graphics&, juce::Rectangle<int>, bool input) const;
    void drawPhrase(juce::Graphics&) const;
    void drawSignal(juce::Graphics&) const;
    juce::String modulationSummary() const;

    PadTrigger& trigger;
    HardwareLook hardware;
    std::array<Control, Lfo::knobCount> controls;
    PanelSelector mode { "Sound mode" }, zoom { "Mic waveform scale" };
    PanelSelector lfoShape { "Waveform" }, lfoClock { "Clock" }, lfoDivision { "Division" }, lfoTarget { "Target" };
    PanelSelector bendRange { "MIDI bend range" };
    PanelSelector key { "Key" }, scale { "Scale" }, registerSelect { "Register" }, motion { "Pitch movement" };
    PanelSelector capture { "Capture length" }, gate { "Note length division" }, pitchUnit { "Pitch unit" };
    juce::Slider lfoDepth;
    juce::TextEditor lfoSeconds, wavPath;
    std::unique_ptr<juce::FileChooser> sampleChooser;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> depthAttachment;
    juce::TextButton loadButton { "Load" };
    juce::TextButton noteEditor { "Note editor" }, inputSetup { "Input setup" }, modulation { "Modulation" };
    juce::TextButton audition { "Audition" }, keep { "Keep last 4" }, explore { "Explore" }, vary { "Vary one" }, undo { "Undo" };
    juce::TextButton followChords { "Follow chords" }, scanPhrase { "Scan phrase" };
    juce::TextButton monoOutput { "Mono MIDI" };
    juce::TextButton closeChoices { "Done" }, previousChoices { "<" }, nextChoices { ">" };
    std::array<PhraseCell, MidiNotes::maxPatternLength> cells;
    static constexpr int choiceColumns = 6;
    std::array<juce::TextButton, choiceColumns * 2> choiceButtons;
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
    Drawer drawer = Drawer::closed;
    Drawer choiceReturn = Drawer::closed;
    MidiNotes::PhraseDisplay phrase;
    juce::StringArray choiceNames;
    juce::String choiceTitle;
    std::function<void(int)> choose;
    int currentChoice = -1, choiceOffset = 0, selectedStep = -1;
    bool showingMidi = false;
    bool lfoPeriodEdited = false;
    unsigned midiNoteCount = 0, hitCount = 0;
    double inputUntil = 0.0, hitUntil = 0.0, midiUntil = 0.0, displayTime = 0.0;
    mutable juce::Path lfoCurve;
    mutable int cachedLfoShape = -1;
    PadTrigger::Scope scope {};
    std::uint64_t lastScope = 0;
    float inputPeak = 0.0f, outputPeak = 0.0f;
    double inputPeakUntil = 0.0, outputPeakUntil = 0.0;
    double inputClipUntil = 0.0, outputClipUntil = 0.0;
};
