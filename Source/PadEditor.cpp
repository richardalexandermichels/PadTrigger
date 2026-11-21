#include "PadEditor.h"
#include "PanelTheme.h"
#include <cmath>

namespace
{
using namespace PanelTheme;
const auto hitColour = juce::Colour(0xffffcc7c);
const auto danger = juce::Colour(0xffff947d);
constexpr int caseWidth = 760, compactCaseHeight = 450, drawerTop = 437;
constexpr int caseHeight = drawerTop + 276 + 20;
const juce::Rectangle<int> phrasePanel { 24, 105, 712, 161 };
const juce::Rectangle<int> outputPanel { 24, 105, 712, 161 };
const juce::Rectangle<int> micPanel { 24, drawerTop, 456, 165 };
const juce::Rectangle<int> controlPanel { 495, drawerTop, 241, 276 };
const juce::Rectangle<int> playbackPanel { 24, 278, 712, 102 };
const juce::Rectangle<int> lfoPanel { 24, drawerTop, 306, 143 };
const juce::Rectangle<int> pitchPanel { 341, drawerTop, 139, 143 };
const juce::Rectangle<int> keyboardPanel { 24, drawerTop, 712, 166 };
constexpr std::array<std::size_t, 4> mainKnobs { 0, 1, 2, 5 };
constexpr std::size_t sampleOutputKnob = 2;
constexpr std::size_t velocityFloorKnob = 6;

juce::String db(float level)
{
    return level <= -120.0f ? juce::String("-INF") : juce::String(level, 1);
}

juce::Image makeCaseMaterial(int width, int height)
{
    juce::Image material(juce::Image::RGB, width, height, false);
    {
        juce::Image::BitmapData pixels(material, juce::Image::BitmapData::writeOnly);
        juce::Random random(19247);
        for (int y = 0; y < height; ++y)
        {
            const auto grade = static_cast<float>(y) / static_cast<float>(height);
            const auto grain = random.nextFloat() * 0.015f;
            const auto tint = juce::Colour(0xfff1f0ea).interpolatedWith(face, grade);
            for (int x = 0; x < width; ++x)
            {
                const auto light = random.nextFloat() * 0.030f + grain
                    + 0.06f * static_cast<float>(x) / static_cast<float>(width);
                pixels.setPixelColour(x, y, tint.darker(light));
            }
        }
    }
    juce::Image plate(juce::Image::RGB, width, height, true);
    juce::Graphics g(plate);
    g.fillAll(juce::Colour(0xff3e4345));
    const auto frame = juce::Rectangle<float>(static_cast<float>(width), static_cast<float>(height)).reduced(1.0f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xfffafaf5), 0.0f, 0.0f,
        juce::Colour(0xff92958b), 0.0f, frame.getBottom(), false));
    g.fillRoundedRectangle(frame, 12.0f);
    const auto faceBounds = frame.reduced(3.0f).withTrimmedBottom(3.0f);
    {
        const juce::Graphics::ScopedSaveState change(g);
        juce::Path mask;
        mask.addRoundedRectangle(faceBounds, 8.0f);
        g.reduceClipRegion(mask);
        g.drawImageAt(material, 0, 0);
    }
    g.setColour(juce::Colour(0x6cffffff));
    g.drawRoundedRectangle(faceBounds.reduced(0.5f), 8.0f, 1.0f);
    return plate;
}

void screw(juce::Graphics& g, float x, float y)
{
    g.setColour(juce::Colour(0xff8b9087));
    g.fillEllipse(x - 4.7f, y - 4.0f, 9.4f, 9.4f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xfff0f0e6), x - 2.0f, y - 4.0f,
        juce::Colour(0xff969a92), x + 3.0f, y + 4.0f, false));
    g.fillEllipse(x - 3.8f, y - 4.0f, 7.6f, 7.6f);
    g.setColour(juce::Colour(0xff575d55));
    g.drawLine(x - 2.0f, y + 1.5f, x + 2.0f, y - 2.0f, 1.15f);
}

void drawDisplayBezel(juce::Graphics& g, juce::Rectangle<int> panel)
{
    const auto frame = panel.toFloat();
    g.setColour(juce::Colour(0x9cfcfcf6));
    g.fillRoundedRectangle(frame.translated(0.0f, 1.3f).expanded(0.4f), 8.0f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3c4246), frame.getX(), frame.getY(),
        juce::Colour(0xff131819), frame.getX(), frame.getBottom(), false));
    g.fillRoundedRectangle(frame, 8.0f);
    g.setColour(ink);
    g.drawRoundedRectangle(frame.reduced(0.5f), 8.0f, 1.0f);
    const auto glass = frame.reduced(4.0f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff202638), glass.getX(), glass.getY(),
        screen, glass.getRight(), glass.getBottom(), false));
    g.fillRoundedRectangle(glass, 5.0f);
    g.setColour(juce::Colour(0xff5b616b));
    g.drawRoundedRectangle(glass.reduced(0.5f), 4.5f, 0.75f);
    g.setColour(juce::Colour(0x0dffffff));
    g.drawHorizontalLine(panel.getY() + 6, glass.getX() + 8.0f, glass.getRight() - 8.0f);
}

class HardwareReadout final : public juce::Label
{
    juce::TextEditor* createEditorComponent() override
    {
        auto* textEditor = juce::Label::createEditorComponent();
        textEditor->setPopupMenuEnabled(false);
        return textEditor;
    }
};
}

juce::Label* PadEditor::HardwareLook::createSliderTextBox(juce::Slider& slider)
{
    auto* label = new HardwareReadout();
    for (const auto& id : {
            std::pair {juce::Label::textColourId, juce::Slider::textBoxTextColourId},
            std::pair {juce::Label::backgroundColourId, juce::Slider::textBoxBackgroundColourId},
            std::pair {juce::Label::outlineColourId, juce::Slider::textBoxOutlineColourId} })
        label->setColour(id.first, slider.findColour(id.second));
    label->setJustificationType(juce::Justification::centred);
    label->setFont(mono(12.0f));
    return label;
}

void PadEditor::HardwareLook::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                                 const juce::Colour& colour, bool hovered, bool down)
{
    const auto base = button.isEnabled() ? colour : colour.interpolatedWith(face, 0.45f);
    const auto mount = button.getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(juce::Colour(0x9270786c));
    g.fillRoundedRectangle(mount, 3.0f);
    const auto cap = mount.reduced(0.8f).withTrimmedBottom(down ? 0.6f : 2.0f);
    auto material = juce::ColourGradient(base.brighter(hovered ? 0.14f : 0.08f), cap.getX(), cap.getY(),
        base.darker(0.13f), cap.getX(), cap.getBottom(), false);
    material.addColour(0.26, base);
    g.setGradientFill(material);
    g.fillRoundedRectangle(cap, 2.3f);
    g.setColour(juce::Colours::white.withAlpha(0.33f));
    g.drawRoundedRectangle(cap.reduced(0.4f), 2.0f, 0.65f);
    if (button.hasKeyboardFocus(false))
    {
        g.setColour(teal);
        g.drawRoundedRectangle(cap.reduced(0.8f), 2.0f, 1.0f);
    }
}

void PadEditor::HardwareLook::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool down)
{
    const auto colour = button.findColour(button.getToggleState()
        ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
    g.setColour(colour.withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.42f));
    g.setFont(juce::FontOptions(juce::jmin(13.0f, button.getHeight() * 0.65f)));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(2, 2).translated(0, down ? 1 : -1),
        juce::Justification::centred, 1);
}

void PadEditor::HardwareLook::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                              float position, float start, float end, juce::Slider& slider)
{
    const juce::Point<float> centre(static_cast<float>(x) + static_cast<float>(width) * 0.5f,
                                   static_cast<float>(y) + static_cast<float>(height) * 0.5f);
    const auto radius = static_cast<float>(juce::jmin(width, height)) * 0.37f;
    const auto accent = slider.findColour(juce::Slider::rotarySliderFillColourId)
        .interpolatedWith(border, slider.isEnabled() ? 0.0f : 0.7f);
    const auto angle = juce::jmap(position, start, end);
    const auto point = [&](float angle, float distance)
    {
        return centre + juce::Point<float>(std::sin(angle), -std::cos(angle)) * distance;
    };

    for (int i = 0; i <= 10; ++i)
    {
        const auto tick = juce::jmap(static_cast<float>(i) / 10.0f, start, end);
        g.setColour(tick <= angle + 0.01f ? accent : border);
        g.drawLine(juce::Line<float>(point(tick, radius + 4.0f), point(tick, radius + 8.0f)),
                   i % 5 == 0 ? 1.7f : 1.0f);
    }

    const auto cap = juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre);
    g.setColour(ink.withAlpha(0.35f));
    g.fillEllipse(cap.translated(0.0f, 2.7f).expanded(1.0f));
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xffb7b9b0), centre.x, cap.getY(),
        juce::Colour(0xff555b55), centre.x, cap.getBottom(), false));
    g.fillEllipse(cap);
    for (int i = 0; i < 24; ++i)
    {
        const auto groove = angle + static_cast<float>(i) * juce::MathConstants<float>::twoPi / 24.0f;
        g.setColour(ink.withAlpha(0.32f));
        g.drawLine(juce::Line<float>(point(groove, radius - 2.9f), point(groove, radius - 0.6f)), 0.9f);
    }
    const auto top = cap.reduced(3.0f);
    auto material = juce::ColourGradient(juce::Colour(0xfffcfbf2), centre.x - radius * 0.3f, top.getY(),
        juce::Colour(0xffc2c4ba), centre.x + radius * 0.2f, top.getBottom(), false);
    material.addColour(0.54, juce::Colour(0xffe7e8e0));
    g.setGradientFill(material);
    g.fillEllipse(top);
    g.setColour(juce::Colours::white.withAlpha(0.7f));
    g.drawEllipse(top.reduced(0.4f), 0.8f);
    g.setColour(accent);
    juce::Path indicator;
    indicator.startNewSubPath(point(angle, radius * 0.37f));
    indicator.lineTo(point(angle, radius * 0.77f));
    g.strokePath(indicator, juce::PathStrokeType(2.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (slider.hasKeyboardFocus(false))
    {
        g.setColour(accent);
        g.drawEllipse(cap.expanded(2.0f), 1.0f);
    }
}

PadEditor::PadEditor(PadTrigger& owner)
    : AudioProcessorEditor(owner), trigger(owner), poolEditor(owner)
{
    hardware.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffe2e2d8));
    hardware.setColour(juce::TextButton::textColourOffId, ink);
    hardware.setColour(juce::TextEditor::highlightColourId, ultraviolet);
    hardware.setColour(juce::TextEditor::highlightedTextColourId, juce::Colours::white);
    for (auto* button : { &loadButton, &calibrateButton })
    {
        button->setColour(juce::TextButton::buttonColourId, ultraviolet);
        button->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    }
    setLookAndFeel(&hardware);

    struct ControlInfo { const char* id; const char* label; const char* suffix; const char* help; };
    const std::array<ControlInfo, Lfo::knobCount> info {{
        { "sensitivity", "Sensitivity", " dB", "Turn up to detect quieter strokes." },
        { "dynamics", "Dynamics", "", "Higher values make ghost notes quieter relative to accents." },
        { "sampleGain", "Output", " dB", "Level of the generated WAV audio. Set MIDI instrument volume in REAPER." },
        { "patternLength", "Pattern Length", "", "0 Free picks fresh notes. Set 1-16 to loop and allow LFO modulation." },
        { "noteLengthMs", "Note Length", " ms", "How long each MIDI note is held." },
        { "retriggerMs", "Retrigger", " ms", "Minimum time between hits. Raise to suppress extra notes, or lower for fast flams." },
        { "velocityFloor", "Vel Floor", "", "Minimum MIDI velocity for a detected hit. Raise to bring up quieter strokes." },
        { "pitchSemitones", "Semitones", "", "Transpose the WAV or each outgoing MIDI note in whole semitones." },
        { "pitchCents", "Cents", "", "Fine tuning in cents. In MIDI mode this bends all held notes on channel 1." }
    }};
    for (std::size_t i = 0; i < controls.size(); ++i)
    {
        auto& control = controls[i];
        control.label.setText(info[i].label, juce::dontSendNotification);
        const auto onMainPanel = i != 3 && i != 4;
        control.label.setFont(juce::FontOptions(onMainPanel ? 11.0f : 12.0f, juce::Font::bold));
        control.label.setJustificationType(juce::Justification::centred);
        control.label.setColour(juce::Label::textColourId, ink);
        if (i != Lfo::semitones && i != Lfo::cents)
            addAndMakeVisible(control.label);

        auto& knob = control.knob;
        knob.setName(info[i].label);
        knob.setTitle(info[i].label);
        knob.setTooltip(info[i].help);
        knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                                 juce::MathConstants<float>::pi * 2.75f, true);
        knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, onMainPanel ? 53 : 66, 22);
        knob.setTextValueSuffix(info[i].suffix);
        knob.setColour(juce::Slider::textBoxTextColourId, ink);
        knob.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::white);
        knob.setColour(juce::Slider::textBoxOutlineColourId, border);
        knob.setColour(juce::Slider::rotarySliderFillColourId, i == 0 ? raspberry : i == 5 ? teal : ultraviolet);
        addAndMakeVisible(knob);
        control.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            trigger.settings, info[i].id, knob);
        const auto* parameter = trigger.settings.getParameter(info[i].id);
        knob.setDoubleClickReturnValue(true, parameter->convertFrom0to1(parameter->getDefaultValue()));
    }
    controls[3].knob.textFromValueFunction = [](double value)
    {
        return value == 0.0 ? juce::String("0 Free") : juce::String(static_cast<int>(value));
    };
    controls[3].knob.updateText();

    mode.setItems({ "WAV Sample", "MIDI Notes" });
    mode.attach(*trigger.settings.getParameter("soundMode"));
    mode.onChange = [this] { updateMode(); };
    addAndMakeVisible(mode);
    addAndMakeVisible(poolEditor);
    setupPhraseControls();

    resetPattern.onClick = [this]
    {
        trigger.midiNotes.resetPattern();
        trigger.notifyStateEdited();
    };
    resetPattern.setTooltip("Clear the loop. Your next stroke starts the new pattern.");
    addAndMakeVisible(resetPattern);

    zoom.setItems({ "+/-1 FS", "+/-0.1 FS", "+/-0.01 FS", "+/-0.001 FS", "+/-0.0001 FS" }, 3);
    zoom.setTooltip("Vertical mic waveform scale in full-scale units. The peak reading stays unscaled.");
    zoom.onChange = [this] { repaint(micPanel); };
    addAndMakeVisible(zoom);

    juce::StringArray shapes, divisions, targets;
    for (const auto* shape : Lfo::shapes)
        shapes.add(shape);
    for (const auto& division : Lfo::divisions)
        divisions.add(division.name);
    for (const auto& target : Lfo::targets)
        targets.add(target.label);
    lfoShape.setItems(shapes);
    lfoClock.setItems({ "Seconds", "Tempo" });
    lfoDivision.setItems(divisions);
    lfoDivision.setTooltip("4/1 to 1/1 are bars. D = dotted and T = triplet.");
    lfoTarget.setItems(targets, trigger.isMidiMode() ? 3 : 1);
    lfoTarget.setTooltip("Pattern Position uses the waveform to select the loop step played by your next hit.");
    for (auto* selector : { &lfoShape, &lfoClock, &lfoDivision, &lfoTarget })
        addAndMakeVisible(*selector);
    lfoShape.attach(*trigger.settings.getParameter("lfoShape"));
    lfoClock.attach(*trigger.settings.getParameter("lfoClock"));
    lfoDivision.attach(*trigger.settings.getParameter("lfoDivision"));
    lfoClock.onChange = [this] { updateLfoControls(); };
    lfoTarget.onChange = [this] { updateLfoControls(); };
    for (const auto& route : { std::pair{ &semitoneTarget, Lfo::semitones }, { &centsTarget, Lfo::cents } })
    {
        auto* button = route.first;
        button->setTitle("Select " + button->getButtonText() + " as LFO target");
        button->setTooltip("Select this knob for LFO Depth.");
        button->setColour(juce::TextButton::buttonOnColourId, ultraviolet);
        button->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        button->onClick = [this, target = route.second]
        {
            lfoTarget.setIndex(static_cast<int>(target));
            updateLfoControls();
        };
        addAndMakeVisible(*button);
    }
    juce::StringArray bendRanges;
    for (int range = 1; range <= 48; ++range)
        bendRanges.add(juce::String::fromUTF8(u8"\u00b1") + juce::String(range) + " st");
    bendRange.setItems(bendRanges);
    bendRange.setTooltip("Match the instrument's pitch bend range. This affects MIDI Cents only.");
    bendRange.attach(*trigger.settings.getParameter("bendRange"), 1);
    addAndMakeVisible(bendRange);
    for (auto& readout : pitchLfoReadouts)
    {
        readout.setFont(mono(9.5f));
        readout.setJustificationType(juce::Justification::centred);
        readout.setColour(juce::Label::textColourId, teal);
        addAndMakeVisible(readout);
    }
    lfoDepth.setTitle("LFO depth");
    lfoDepth.setTooltip("Depth for this target only. Double-click to clear the route.");
    lfoDepth.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    lfoDepth.setDoubleClickReturnValue(true, 0.0);
    lfoDepth.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    lfoDepth.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 47, 19);
    lfoDepth.setColour(juce::Slider::rotarySliderFillColourId, raspberry);
    lfoDepth.setColour(juce::Slider::textBoxTextColourId, ink);
    lfoDepth.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::white);
    lfoDepth.setColour(juce::Slider::textBoxOutlineColourId, border);
    addAndMakeVisible(lfoDepth);
    lfoSeconds.setTitle("LFO period in seconds");
    lfoSeconds.setPopupMenuEnabled(false);
    lfoSeconds.setInputRestrictions(7, "0123456789.");
    lfoSeconds.setJustification(juce::Justification::centredLeft);
    lfoSeconds.setColour(juce::TextEditor::backgroundColourId, panelFill);
    lfoSeconds.setColour(juce::TextEditor::textColourId, ink);
    lfoSeconds.setColour(juce::TextEditor::outlineColourId, border);
    lfoSeconds.setColour(juce::TextEditor::focusedOutlineColourId, ultraviolet);
    lfoSeconds.onTextChange = [this] { lfoPeriodEdited = true; };
    lfoSeconds.onReturnKey = [this] { commitLfoSeconds(); };
    lfoSeconds.onFocusLost = [this] { commitLfoSeconds(); };
    lfoSeconds.onEscapeKey = [this] { lfoPeriodEdited = false; commitLfoSeconds(); };
    addChildComponent(lfoSeconds);
    updateLfoControls();

    wavPath.setTitle("Absolute path to a WAV file");
    wavPath.setTextToShowWhenEmpty("Drop a WAV here or paste its path", muted);
    wavPath.setPopupMenuEnabled(false);
    wavPath.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xfff9faf4));
    wavPath.setColour(juce::TextEditor::textColourId, ink);
    wavPath.setColour(juce::TextEditor::outlineColourId, border);
    wavPath.onReturnKey = [this] { loadWav(wavPath.getText()); };
    loadButton.onClick = [this]
    {
        const auto path = wavPath.getText();
        if (path.trim().isEmpty())
            browseWav();
        else
            loadWav(path);
    };
    addAndMakeVisible(wavPath);
    calibrateButton.onClick = [this] { trigger.advanceCalibration(); updateCalibration(); };
    cancelButton.onClick = [this] { trigger.cancelCalibration(); updateCalibration(); };
    acceptWeakButton.onClick = [this] { trigger.acceptWeakCalibration(); updateCalibration(); };
    addAndMakeVisible(loadButton);
    addAndMakeVisible(calibrateButton);
    addChildComponent(cancelButton);
    addChildComponent(acceptWeakButton);

    for (auto* label : { &sampleStatus, &calibrationStatus, &calibrationLevels })
    {
        label->setColour(juce::Label::textColourId, muted);
        label->setJustificationType(juce::Justification::centredLeft);
        label->setFont(juce::FontOptions(13.0f));
        addAndMakeVisible(*label);
    }
    calibrationLevels.setFont(mono(11.5f));
    sampleStatus.setFont(mono(11.0f));

    caseMaterial = makeCaseMaterial(caseWidth, caseHeight);
    setOpaque(true);
    setSize(caseWidth, compactCaseHeight);
    updateMode();
    timerCallback();
    startTimerHz(30);
}

PadEditor::~PadEditor()
{
    setLookAndFeel(nullptr);
}

void PadEditor::paint(juce::Graphics& g)
{
    g.drawImageAt(caseMaterial, 0, 0);
    for (float x : { 12.0f, 748.0f })
        for (float y : { 12.0f, static_cast<float>(getHeight() - 12) })
            screw(g, x, y);

    g.setColour(ink);
    g.setFont(juce::FontOptions(34.0f, juce::Font::bold));
    g.drawText("Padomizer", 24, 15, 400, 39, juce::Justification::centredLeft);
    g.setColour(border);
    g.drawHorizontalLine(59, 24.0f, 736.0f);
    g.setColour(teal);
    g.fillRect(24, 58, 52, 2);
    drawSignal(g);
    if (showingMidi)
        drawPhrase(g);
    else
        drawScope(g, outputPanel, false);

    recessedPanel(g, playbackPanel.toFloat());
    g.setColour(ink);
    g.setFont(mono(11.0f));
    if (showingMidi)
    {
        for (const auto& label : { std::pair{36, "KEY"}, {133, "SCALE"}, {307, "REGISTER"},
                                   {404, "PITCH MOVEMENT"}, {561, "NOTE LENGTH"} })
            g.drawText(label.second, label.first, 298, 140, 15, juce::Justification::centredLeft);
        const auto harmony = trigger.midiNotes.getHarmony();
        if (harmony.follow)
        {
            const auto chord = trigger.midiNotes.heldChord();
            juce::StringArray tones;
            for (int i = 0; i < 12; ++i)
                if ((chord & (1u << i)) != 0)
                    tones.add(juce::MidiMessage::getMidiNoteName(i, true, false, 4));
            g.setColour(teal);
            g.drawText(tones.isEmpty() ? "No chord held / using the selected scale or set"
                                       : "HELD CHORD   " + tones.joinIntoString("  "),
                       37, 350, 640, 20, juce::Justification::centredLeft);
        }
    }
    else
        g.drawText("WAV SOURCE", 37, 294, 350, 20, juce::Justification::centredLeft);

    g.setColour(muted);
    g.setFont(mono(11.0f));
    g.drawFittedText(modulationSummary(), { 383, 395, 352, 30 }, juce::Justification::centredRight, 2);

    if (drawer == Drawer::input)
    {
        drawScope(g, micPanel, true);
        recessedPanel(g, controlPanel.toFloat());
        g.setColour(border);
        g.drawHorizontalLine(drawerTop + 128, 510.0f, 721.0f);
        g.setColour(juce::Colour(0xffe9e6f6));
        g.fillRoundedRectangle(511.0f, static_cast<float>(drawerTop + 153), 211.0f, 19.0f, 3.0f);
        g.setColour(ink);
        g.setFont(mono(13.0f));
        g.drawText("TRIGGER & PLAYBACK", 511, drawerTop + 9, 205, 22, juce::Justification::centredLeft);
        g.drawText("MIC CALIBRATION", 511, drawerTop + 133, 205, 19, juce::Justification::centredLeft);
        g.setColour(muted);
        g.setFont(juce::FontOptions(13.0f));
        g.drawFittedText(showingMidi ? "Use Audition to check the instrument and routing. The Hit light confirms pad detection."
                                   : "Use the Hit light to confirm pad detection and the WAV light to confirm sample playback.",
                         { 36, drawerTop + 174, 427, 54 }, juce::Justification::topLeft, 3);
    }
    else if (drawer == Drawer::modulation)
    {
        recessedPanel(g, pitchPanel.toFloat());
        g.setColour(ink);
        g.setFont(mono(11.0f));
        g.drawText("PITCH", 352, drawerTop + 6, 117, 16, juce::Justification::centredLeft);
        drawLfo(g);
        recessedPanel(g, juce::Rectangle<float>(24.0f, drawerTop + 151.0f, 712.0f, 65.0f));
        g.setColour(ink);
        g.setFont(mono(10.5f));
        if (showingMidi)
        {
            g.drawText("PITCH UNIT", 38, drawerTop + 155, 125, 17, juce::Justification::centredLeft);
            g.drawText("MIDI BEND RANGE", 195, drawerTop + 155, 126, 17, juce::Justification::centredLeft);
            g.drawText("LEGACY LOOP", 518, drawerTop + 155, 104, 17, juce::Justification::centredLeft);
            g.setFont(juce::FontOptions(11.0f));
            g.drawFittedText("Your hit plays the step highlighted by the song clock.",
                             { 358, drawerTop + 185, 145, 24 }, juce::Justification::centredLeft, 2);
        }
    }
    else if (drawer == Drawer::choices)
    {
        recessedPanel(g, juce::Rectangle<float>(24.0f, drawerTop + 0.0f, 712.0f, 118.0f));
        g.setColour(ink);
        g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        g.drawFittedText(choiceTitle, { 37, drawerTop + 8, 485, 23 }, juce::Justification::centredLeft, 1);
    }
}

void PadEditor::drawSignal(juce::Graphics& g) const
{
    recessedPanel(g, juce::Rectangle<float>(24.0f, 68.0f, 369.0f, 28.0f), 4.0f);
    g.setFont(mono(12.0f));
    const auto stage = trigger.calibrationStage();
    if (stage != PadTrigger::CalibrationStage::idle && stage != PadTrigger::CalibrationStage::complete
        && stage != PadTrigger::CalibrationStage::interrupted && stage != PadTrigger::CalibrationStage::weak)
    {
        g.setColour(raspberry);
        g.drawText("Calibrating / output muted", 37, 68, 348, 28, juce::Justification::centredLeft);
        return;
    }
    const auto light = [&](int x, const juce::String& label, bool on)
    {
        g.setColour(on ? teal : border);
        g.fillEllipse(static_cast<float>(x), 78.0f, 8.0f, 8.0f);
        g.setColour(ink);
        g.drawText(label, x + 14, 68, 64, 28, juce::Justification::centredLeft);
    };
    light(37, "MIC", displayTime < inputUntil);
    light(115, "HIT", displayTime < hitUntil);
    light(193, showingMidi ? "MIDI" : "WAV", displayTime < midiUntil);
    g.setColour(muted);
    g.drawText(showingMidi ? juce::String(trigger.notesPerSecond()) + " NOTES/S" : db(outputPeak > 0.0f
        ? juce::Decibels::gainToDecibels(outputPeak, -120.0f) : -120.0f) + " dBFS",
        260, 68, 119, 28, juce::Justification::centredRight);
}

juce::Rectangle<int> PadEditor::lfoScopeBounds() const
{
    return lfoPanel;
}

void PadEditor::drawLfo(juce::Graphics& g) const
{
    const auto bounds = lfoScopeBounds();
    drawDisplayBezel(g, bounds);
    const auto shape = lfoShape.getIndex();
    const auto grid = bounds.reduced(13, 0).withTrimmedTop(35).withTrimmedBottom(18).toFloat();
    g.setColour(outputTrace.withAlpha(0.17f));
    g.drawRect(grid);
    g.drawHorizontalLine(juce::roundToInt(grid.getCentreY()), grid.getX(), grid.getRight());
    for (int i = 1; i < 8; ++i)
        g.drawVerticalLine(juce::roundToInt(grid.getX() + grid.getWidth() * i / 8.0f), grid.getY(), grid.getBottom());
    const auto ordinate = [&grid](float value) { return grid.getCentreY() - value * (grid.getHeight() * 0.37f); };
    if (shape != cachedLfoShape)
    {
        cachedLfoShape = shape;
        lfoCurve.clear();
        for (int i = 0; i <= 256; ++i)
        {
            const auto x = grid.getX() + grid.getWidth() * i / 256.0f;
            const auto y = ordinate(Lfo::wave(shape, juce::jmin(0.999999, i / 256.0)));
            if (i == 0)
                lfoCurve.startNewSubPath(x, y);
            else
                lfoCurve.lineTo(x, y);
        }
    }
    g.setColour(micTrace.withAlpha(0.22f));
    g.strokePath(lfoCurve, juce::PathStrokeType(4.0f));
    g.setColour(micTrace);
    g.strokePath(lfoCurve, juce::PathStrokeType(1.35f));
    const auto phase = trigger.lfoPosition();
    const auto x = grid.getX() + phase * grid.getWidth();
    g.setColour(thresholdColour.withAlpha(0.65f));
    g.drawVerticalLine(juce::roundToInt(x), grid.getY(), grid.getBottom());
    g.setColour(thresholdColour);
    g.fillEllipse(x - 2.5f, ordinate(Lfo::wave(shape, phase)) - 2.5f, 5.0f, 5.0f);
    g.setColour(micTrace);
    g.setFont(lcd(16.5f));
    g.drawText("LFO", bounds.reduced(13, 0).removeFromTop(34), juce::Justification::centredLeft);
    g.setFont(mono(11.0f));
    g.drawText(lfoShape.getText(), bounds.reduced(13, 0).removeFromTop(34), juce::Justification::centredRight);
    const auto isPatternRoute = [](std::size_t target) { return target == Lfo::pattern || target == Lfo::patternPosition; };
    const auto worksInMode = [this, isPatternRoute](std::size_t target)
    {
        if (target == Lfo::output)
            return !showingMidi;
        if (isPatternRoute(target) || target == Lfo::noteLength || target == Lfo::velocity)
            return showingMidi;
        return true;
    };
    const auto selected = static_cast<std::size_t>(lfoTarget.getIndex());
    const bool hasLoop = phrase.repeating;
    int activeRoutes = 0;
    for (std::size_t i = 0; i < Lfo::targets.size(); ++i)
        if (worksInMode(i) && (!isPatternRoute(i) || hasLoop)
            && trigger.getLfoDepth(static_cast<Lfo::Knob>(i)) > 0.0f)
            ++activeRoutes;
    const auto routeStatus = !worksInMode(selected) ? juce::String("Target inactive in this mode")
        : isPatternRoute(selected) && !hasLoop ? juce::String("Keep a phrase to use this route")
        : juce::String(activeRoutes) + (activeRoutes == 1 ? " active route" : " active routes");
    g.setColour(outputTrace);
    g.drawText(routeStatus, bounds.reduced(13, 0).removeFromBottom(18), juce::Justification::centredLeft);

    const auto rack = bounds.withX(495).withWidth(241).toFloat();
    recessedPanel(g, rack);
    g.setColour(ink);
    g.setFont(mono(10.5f));
    g.drawText("WAVEFORM", 507, bounds.getY() + 6, 104, 14, juce::Justification::centredLeft);
    g.drawText("CLOCK", 620, bounds.getY() + 6, 104, 14, juce::Justification::centredLeft);
    g.drawText(lfoClock.getIndex() == 1 ? "DIVISION" : "PERIOD (S)",
        507, bounds.getY() + 49, 104, 14, juce::Justification::centredLeft);
    g.drawText("TARGET", 507, bounds.getY() + 92, 139, 14, juce::Justification::centredLeft);
    g.drawText("DEPTH", 665, bounds.getY() + 56, 57, 14, juce::Justification::centred);
}

void PadEditor::updateLfoControls()
{
    const bool tempo = lfoClock.getIndex() == 1;
    lfoDivision.setVisible(drawer == Drawer::modulation && tempo);
    lfoSeconds.setVisible(drawer == Drawer::modulation && !tempo);
    depthAttachment.reset();
    const auto target = juce::jlimit(0, static_cast<int>(Lfo::targetCount) - 1, lfoTarget.getIndex());
    semitoneTarget.setToggleState(target == Lfo::semitones, juce::dontSendNotification);
    centsTarget.setToggleState(target == Lfo::cents, juce::dontSendNotification);
    depthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        trigger.settings, Lfo::targets[static_cast<std::size_t>(target)].depth, lfoDepth);
    repaint();
}

void PadEditor::updatePitchLfoReadouts()
{
    constexpr std::array<Lfo::Knob, 2> targets { Lfo::semitones, Lfo::cents };
    for (std::size_t i = 0; i < targets.size(); ++i)
    {
        const auto& target = Lfo::targets[targets[i]];
        const auto depth = juce::roundToInt(trigger.settings.getRawParameterValue(target.depth)->load());
        const auto text = depth > 0 ? juce::String::fromUTF8(u8"LFO \u00b1") + juce::String(depth) + " " + target.pitchUnit
                                    : juce::String();
        auto& readout = pitchLfoReadouts[i];
        if (readout.getText() != text)
            readout.setText(text, juce::dontSendNotification);
    }
}

void PadEditor::commitLfoSeconds()
{
    auto* parameter = trigger.settings.getParameter("lfoSeconds");
    const auto text = lfoSeconds.getText();
    if (lfoPeriodEdited && text.containsAnyOf("0123456789") && text.indexOfChar('.') == text.lastIndexOfChar('.'))
    {
        const auto seconds = static_cast<float>(text.getDoubleValue());
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(juce::jlimit(0.025f, 120.0f, seconds)));
        parameter->endChangeGesture();
    }
    lfoPeriodEdited = false;
    lfoSeconds.setText(juce::String(parameter->convertFrom0to1(parameter->getValue()), 3), false);
}

void PadEditor::PhraseCell::paintButton(juce::Graphics& g, bool hover, bool down)
{
    const auto rect = getLocalBounds().toFloat().reduced(0.5f);
    auto tint = juce::Colour(0xff292f42);
    if (played)
        tint = juce::Colour(0xff285453);
    if (getToggleState())
        tint = juce::Colour(0xff4d3968);
    if (isEnabled() && (hover || down))
        tint = tint.brighter(0.22f);
    g.setGradientFill(juce::ColourGradient(tint.brighter(0.12f), rect.getX(), rect.getY(),
        tint.darker(0.23f), rect.getX(), rect.getBottom(), false));
    g.fillRoundedRectangle(rect, 5.0f);
    g.setColour(upcoming || hasKeyboardFocus(false) ? thresholdColour : juce::Colour(0xff535873));
    g.drawRoundedRectangle(rect, 5.0f, upcoming ? 1.8f : 1.0f);
    auto available = getLocalBounds().reduced(6, 4);
    g.setColour(getButtonText() == "-" ? outputTrace.withAlpha(0.5f) : juce::Colours::white);
    g.setFont(getButtonText().containsChar('#') ? mono(getHeight() > 55 ? 23.0f : 19.0f)
                                               : lcd(getHeight() > 55 ? 26.0f : 19.0f));
    g.drawFittedText(getButtonText(), available.withTrimmedBottom(12), juce::Justification::centred, 1);
    const auto meter = available.removeFromBottom(4).toFloat();
    g.setColour(outputTrace.withAlpha(0.16f));
    g.fillRoundedRectangle(meter, 1.5f);
    g.setColour(played ? thresholdColour : outputTrace);
    g.fillRoundedRectangle(meter.withWidth(meter.getWidth() * velocity / 127.0f), 1.5f);
}

void PadEditor::drawPhrase(juce::Graphics& g) const
{
    drawDisplayBezel(g, phrasePanel);
    auto header = phrasePanel.reduced(14, 8).removeFromTop(29);
    g.setColour(thresholdColour);
    g.setFont(lcd(17.0f));
    g.drawText(phrase.repeating ? "REPEAT" : "EXPLORE", header.removeFromLeft(160), juce::Justification::centredLeft);
    g.setColour(outputTrace);
    g.setFont(mono(11.0f));
    const auto last = trigger.midiNotes.lastEvent();
    if (last != 0)
    {
        const auto pitch = juce::MidiMessage::getMidiNoteName((last & 0xff) - 1, true, true, 4);
        g.drawText("LAST   " + pitch + "  /  VEL " + juce::String(last >> 8),
                   header.removeFromLeft(250), juce::Justification::centredLeft);
    }
    const auto scan = trigger.livePatternStep();
    const auto status = phrase.repeating
        ? (scan >= 0 ? "SCAN   STEP " + juce::String(scan + 1) : "NEXT   STEP " + juce::String(phrase.next + 1))
        : juce::String(phrase.historyCount) + " RECENT HITS";
    g.drawText(status, header, juce::Justification::centredRight);
    if (phrase.length <= 8)
    {
        g.setColour(outputTrace.withAlpha(0.85f));
        g.setFont(mono(10.5f));
        const auto tip = !poolEditor.hasNotes() && trigger.midiNotes.heldChord() == 0 ? "CHOOSE NOTES IN THE NOTE EDITOR"
            : phrase.repeating ? "Select a step to change its pitch." : "Play the pad, then keep the pitches you want to repeat.";
        g.drawText(tip, phrasePanel.reduced(14, 8).removeFromBottom(18), juce::Justification::centredLeft);
    }
}

void PadEditor::drawScope(juce::Graphics& g, juce::Rectangle<int> panel, bool input) const
{
    const auto colour = input ? micTrace : outputTrace;
    drawDisplayBezel(g, panel);

    const auto header = panel.reduced(13, 0).removeFromTop(34);
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto clip = now < (input ? inputClipUntil : outputClipUntil);
    const auto peak = input ? inputPeak : outputPeak;
    g.setFont(lcd(input ? 16.5f : 14.0f));
    g.setColour(colour);
    g.drawText(input ? "MIC IN" : "SAMPLE OUT", header.withWidth(124), juce::Justification::centredLeft);
    g.setFont(lcd(14.0f));
    g.drawText(db(juce::Decibels::gainToDecibels(peak, -120.0f)),
               header.withX(panel.getX() + 128).withWidth(62), juce::Justification::centredRight);
    g.setFont(mono(10.5f));
    g.drawText("dBFS", header.withX(panel.getX() + 197).withWidth(31), juce::Justification::centredLeft);
    g.setColour(clip ? danger : juce::Colour(0xffaaa1c0));
    g.drawText("CLIP", header.withX(panel.getX() + 234).withWidth(31), juce::Justification::centredLeft);
    if (input)
    {
        g.setColour(micTrace.withAlpha(0.83f));
        g.drawText("ZOOM", header.withX(panel.getX() + 271).withWidth(33), juce::Justification::centredLeft);
    }

    const auto grid = panel.reduced(13, 0).withTrimmedTop(34).withTrimmedBottom(22).toFloat();
    g.setColour(colour.withAlpha(0.048f));
    g.fillRect(grid);
    g.setColour(colour.withAlpha(0.18f));
    g.drawRect(grid, 1.0f);
    for (int i = 1; i < 8; ++i)
    {
        const auto fraction = static_cast<float>(i) / 8.0f;
        g.drawVerticalLine(juce::roundToInt(grid.getX() + fraction * grid.getWidth()), grid.getY(), grid.getBottom());
    }
    for (int i = 1; i < 4; ++i)
    {
        g.setColour(colour.withAlpha(i == 2 ? 0.32f : 0.18f));
        const auto y = grid.getY() + static_cast<float>(i) * grid.getHeight() * 0.25f;
        g.drawHorizontalLine(juce::roundToInt(y), grid.getX(), grid.getRight());
    }

    const std::array<float, 5> scales { 1.0f, 0.1f, 0.01f, 0.001f, 0.0001f };
    const auto amplitudeScale = input ? scales[static_cast<std::size_t>(zoom.getIndex())] : 1.0f;
    const auto yFor = [&](float value)
    {
        return grid.getCentreY() - juce::jlimit(-1.0f, 1.0f, value / amplitudeScale) * (grid.getHeight() * 0.5f - 1.0f);
    };
    const auto gateDb = input ? trigger.scopeThresholdDb() : std::nullopt;
    const auto gateAmplitude = gateDb ? juce::Decibels::decibelsToGain(*gateDb, -150.0f) : 0.0f;
    if (gateDb && gateAmplitude <= amplitudeScale)
    {
        constexpr float dashes[] { 5.0f, 4.0f };
        g.setColour(thresholdColour.withAlpha(0.62f));
        for (const auto level : { gateAmplitude, -gateAmplitude })
        {
            const auto y = yFor(level);
            g.drawDashedLine({ grid.getX(), y, grid.getRight(), y }, dashes, 2);
        }
    }

    juce::Path waveform, aboveThreshold, hitMarks;
    using Rejection = StrokeDetector::Rejection;
    std::array<juce::Path, 3> rejected;
    for (std::size_t i = 0; i < scope.size(); ++i)
    {
        const auto& frame = scope[i];
        const auto x = grid.getX() + (static_cast<float>(i) + 0.5f) * grid.getWidth() / static_cast<float>(scope.size());
        const auto top = yFor(input ? frame.inputMax : frame.outputMax);
        const auto bottom = yFor(input ? frame.inputMin : frame.outputMin);
        auto& trace = input && frame.aboveThreshold ? aboveThreshold : waveform;
        trace.startNewSubPath(x, top);
        trace.lineTo(x, juce::jmax(top + 0.7f, bottom));
        if (input && frame.hit)
        {
            hitMarks.startNewSubPath(x, grid.getY());
            hitMarks.lineTo(x, grid.getBottom());
        }
        else if (input && frame.rejection != Rejection::none)
        {
            auto& marks = rejected[static_cast<std::size_t>(frame.rejection) - 1];
            marks.startNewSubPath(x, grid.getY());
            marks.lineTo(x, grid.getY() + 8.0f);
        }
    }
    g.setColour(colour.withAlpha(0.24f));
    g.strokePath(waveform, juce::PathStrokeType(2.6f));
    g.setColour(colour.withAlpha(0.95f));
    g.strokePath(waveform, juce::PathStrokeType(1.15f));
    g.setColour(thresholdColour);
    g.strokePath(aboveThreshold, juce::PathStrokeType(1.45f));
    g.setColour(hitColour.withAlpha(0.85f));
    g.strokePath(hitMarks, juce::PathStrokeType(1.2f));
    g.setFont(mono(10.5f));
    g.setColour(colour.withAlpha(0.73f));
    const auto footer = panel.reduced(13, 0).removeFromBottom(21);
    g.drawText("-512 ms", footer, juce::Justification::centredLeft);
    g.drawText("NOW", footer, juce::Justification::centredRight);
    if (input)
    {
        const std::array<juce::Colour, 3> rejectionColours { juce::Colour(0xff91cbff), danger, juce::Colour(0xffd8a6ff) };
        for (std::size_t i = 0; i < rejected.size(); ++i)
        {
            g.setColour(rejectionColours[i]);
            g.strokePath(rejected[i], juce::PathStrokeType(1.5f));
        }
        if (gateDb)
        {
            auto gateLabel = "GATE " + db(*gateDb) + " dBFS";
            if (gateAmplitude > amplitudeScale)
                gateLabel += " (OFF SCALE)";
            g.setColour(thresholdColour);
            g.drawText(gateLabel, footer.withTrimmedLeft(65).withWidth(172), juce::Justification::centredLeft);
        }
        g.setColour(hitColour);
        g.drawText("HIT", footer.withTrimmedLeft(239).withWidth(28), juce::Justification::centred);
        g.setColour(rejectionColours[0]);
        g.drawText("LOW", footer.withTrimmedLeft(269).withWidth(29), juce::Justification::centred);
        g.setColour(rejectionColours[2]);
        g.drawText("HOLD", footer.withTrimmedLeft(301).withWidth(34), juce::Justification::centred);
        g.setColour(rejectionColours[1]);
        g.drawText("ATTACK", footer.withTrimmedLeft(339).withWidth(46), juce::Justification::centred);

    }
    else
    {
        g.drawText(juce::String::fromUTF8(u8"FULL SCALE  \u00b11 FS"), footer, juce::Justification::centred);
    }
}

void PadEditor::setupPhraseControls()
{
    juce::StringArray keys;
    for (int i = 0; i < 12; ++i)
        keys.add(juce::MidiMessage::getMidiNoteName(i, true, false, 4));
    key.setItems(keys);
    scale.setItems(MidiNotes::scaleNames());
    juce::StringArray octaves;
    for (int i = -1; i <= 8; ++i)
        octaves.add("Oct " + juce::String(i));
    registerSelect.setItems(octaves);
    motion.setItems({ "Legacy random", "Steady", "Wandering", "Leaps" });
    capture.setItems({ "1", "2", "3", "4", "5", "6", "7", "8" });
    capture.attach(*trigger.settings.getParameter("phraseCaptureLength"), 1);
    capture.onChange = [this] { keep.setButtonText("Keep last " + capture.getText()); updatePhrase(); };
    gate.setItems(Lfo::gateNames);
    gate.attach(*trigger.settings.getParameter("gateDivision"));
    gate.onChange = [this] { updateLayout(); };
    pitchUnit.setItems({ "Semitones", "Scale degrees" });
    pitchUnit.attach(*trigger.settings.getParameter("degreePitch"));
    pitchUnit.onChange = [this] { updateLayout(); };

    const auto update = [this](auto edit)
    {
        auto harmony = trigger.midiNotes.getHarmony();
        edit(harmony);
        if (trigger.midiNotes.setHarmony(harmony))
            trigger.notifyStateEdited();
        updateHarmony();
        updatePhrase();
    };
    key.onChange = [this, update] { update([this](auto& h) { h.key = key.getIndex(); }); };
    registerSelect.onChange = [this, update]
    {
        update([this](auto& h) { h.octave = registerSelect.getIndex() - 1; });
    };
    scale.onChange = [this, update] { update([this](auto& h) { h.scale = scale.getIndex(); }); };
    motion.onChange = [this, update]
    {
        update([this](auto& h) { h.motion = static_cast<MidiNotes::Motion>(motion.getIndex()); });
    };
    followChords.onClick = [update] { update([](auto& h) { h.follow = !h.follow; }); };
    monoOutput.onClick = [this]
    {
        auto& param = *trigger.settings.getParameter("monoMidi");
        juce::ParameterAttachment attachment(param, [](float) {});
        attachment.setValueAsCompleteGesture(param.getValue() > 0.5f ? 0.0f : 1.0f);
    };
    monoOutput.setTooltip("End the previous MIDI note when a new hit starts.");
    for (auto* selector : { &key, &scale, &registerSelect, &motion, &capture, &gate, &pitchUnit, &bendRange })
    {
        selector->enableDirectChoice([this, selector] { chooseFrom(*selector); });
        addAndMakeVisible(*selector);
    }

    inputSetup.onClick = [this] { openDrawer(Drawer::input); };
    modulation.onClick = [this] { openDrawer(Drawer::modulation); };
    noteEditor.onClick = [this] { openDrawer(Drawer::notes); };
    closeChoices.onClick = [this] { openDrawer(choiceReturn); };
    previousChoices.onClick = [this] { choicePage(-1); };
    nextChoices.onClick = [this] { choicePage(1); };
    audition.setTooltip("Send C4 to the instrument without advancing the phrase.");
    audition.onClick = [this] { trigger.requestAudition(); };
    keep.setTooltip("Repeat your recent pad pitches from the first step. In REAPER, learn Keep Phrase to a pedal.");
    explore.setTooltip("Return to live pitch picking. In REAPER, learn Explore Phrase to a pedal.");
    vary.setTooltip("Change only the selected step. With no selection, vary the next step.");
    undo.setTooltip("Restore the last step edit.");
    followChords.setTooltip("Use held MIDI notes as pitch classes. With no chord, use the scale or note set.");
    scanPhrase.setTooltip("The playhead moves through the phrase with the song. Your hit sounds the highlighted note.");
    keep.onClick = [this]
    {
        if (trigger.midiNotes.applyPhraseCommand(MidiNotes::PhraseCommand::keep, capture.getIndex() + 1))
        {
            selectedStep = -1;
            trigger.notifyStateEdited();
            updatePhrase();
        }
    };
    explore.onClick = [this]
    {
        if (trigger.midiNotes.applyPhraseCommand(MidiNotes::PhraseCommand::explore))
        {
            selectedStep = -1;
            trigger.notifyStateEdited();
            updatePhrase();
        }
    };
    vary.onClick = [this]
    {
        const auto step = selectedStep >= 0 ? selectedStep : phrase.next;
        if (trigger.midiNotes.editPhraseStep(step, std::nullopt))
        {
            selectedStep = step;
            trigger.notifyStateEdited();
            updatePhrase();
        }
    };
    undo.onClick = [this]
    {
        if (trigger.midiNotes.undoPhraseEdit())
        {
            trigger.notifyStateEdited();
            updatePhrase();
        }
    };
    scanPhrase.onClick = [this]
    {
        const auto set = [this](const char* id, float value)
        {
            auto& param = *trigger.settings.getParameter(id);
            juce::ParameterAttachment attachment(param, [](float) {});
            attachment.setValueAsCompleteGesture(value);
        };
        const auto* depth = Lfo::targets[Lfo::patternPosition].depth;
        if (trigger.getLfoDepth(Lfo::patternPosition) > 0.0f)
            set(depth, 0.0f);
        else
        {
            set("lfoShape", 2.0f);
            set("lfoClock", 1.0f);
            set(depth, 100.0f);
            lfoTarget.setIndex(static_cast<int>(Lfo::patternPosition));
            updateLfoControls();
        }
    };

    for (auto* button : { &inputSetup, &modulation, &noteEditor, &audition, &keep, &explore, &vary, &undo,
                          &followChords, &scanPhrase, &monoOutput, &closeChoices, &previousChoices, &nextChoices })
    {
        button->setColour(juce::TextButton::buttonOnColourId, teal);
        button->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible(*button);
    }
    keep.setColour(juce::TextButton::buttonColourId, ultraviolet);
    keep.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    previousChoices.setTitle("Previous choices");
    nextChoices.setTitle("Next choices");

    for (std::size_t i = 0; i < cells.size(); ++i)
    {
        cells[i].setTitle("Phrase step " + juce::String(static_cast<int>(i + 1)));
        cells[i].onClick = [this, i] { chooseStep(static_cast<int>(i)); };
        addChildComponent(cells[i]);
        choiceButtons[i].setColour(juce::TextButton::buttonOnColourId, teal);
        choiceButtons[i].setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        choiceButtons[i].onClick = [this, i]
        {
            if (auto action = choose)
                action(choiceOffset + static_cast<int>(i));
            openDrawer(choiceReturn);
        };
        addChildComponent(choiceButtons[i]);
    }
}

void PadEditor::openDrawer(Drawer next)
{
    drawer = drawer == next ? Drawer::closed : next;
    if (drawer != Drawer::choices)
        choose = {};
    updateLayout();
    updateCalibration();
}

void PadEditor::chooseFrom(PanelSelector& selector)
{
    showChoices(selector.getTitle(), selector.getItems(), selector.getIndex(),
                [&selector](int index) { selector.choose(index); });
}

void PadEditor::showChoices(juce::String title, juce::StringArray entries, int selected, std::function<void(int)> action)
{
    choiceTitle = std::move(title);
    if (drawer != Drawer::choices)
        choiceReturn = drawer;
    choiceNames = std::move(entries);
    currentChoice = selected;
    choiceOffset = juce::jmax(0, selected / static_cast<int>(choiceButtons.size())) * static_cast<int>(choiceButtons.size());
    choose = std::move(action);
    drawer = Drawer::choices;
    updateLayout();
}

void PadEditor::choicePage(int delta)
{
    const auto pageSize = static_cast<int>(choiceButtons.size());
    const auto lastPage = juce::jmax(0, (choiceNames.size() - 1) / pageSize) * pageSize;
    choiceOffset = juce::jlimit(0, lastPage, choiceOffset + delta * pageSize);
    for (int i = 0; i < pageSize; ++i)
    {
        auto& button = choiceButtons[static_cast<std::size_t>(i)];
        const int index = choiceOffset + i;
        button.setVisible(drawer == Drawer::choices && index < choiceNames.size());
        button.setButtonText(choiceNames[index]);
        button.setToggleState(index == currentChoice, juce::dontSendNotification);
    }
    previousChoices.setVisible(drawer == Drawer::choices && choiceOffset > 0);
    nextChoices.setVisible(drawer == Drawer::choices && choiceOffset < lastPage);
    repaint();
}

void PadEditor::chooseStep(int step)
{
    if (!phrase.repeating || !juce::isPositiveAndBelow(step, phrase.length))
        return;
    const auto pool = trigger.midiNotes.selectedPoolDetails();
    auto pitches = pool.pitches;
    if (pool.edits)
    {
        pitches.clear();
        for (int i = 0; i < 128; ++i)
            if (pool.edits->test(static_cast<std::size_t>(i)))
                pitches.push_back(i);
    }
    selectedStep = step;
    const auto current = phrase.pitches[static_cast<std::size_t>(step)];
    juce::StringArray names;
    int selected = -1;
    for (const auto pitch : pitches)
    {
        if (pitch == current)
            selected = names.size();
        names.add(juce::MidiMessage::getMidiNoteName(pitch, true, true, 4));
    }
    if (names.isEmpty())
        return;
    showChoices("Step " + juce::String(step + 1) + " pitch", names, selected,
        [this, step, pitches = std::move(pitches)](int index)
        {
            if (trigger.midiNotes.editPhraseStep(step, pitches[static_cast<std::size_t>(index)]))
            {
                trigger.notifyStateEdited();
                updatePhrase();
            }
        });
}

void PadEditor::updateHarmony()
{
    const auto h = trigger.midiNotes.getHarmony();
    const auto set = [](PanelSelector& selector, int next) { if (selector.getIndex() != next) selector.setIndex(next); };
    set(key, h.key);
    set(scale, h.scale);
    set(registerSelect, h.octave + 1);
    set(motion, static_cast<int>(h.motion));
    followChords.setToggleState(h.follow, juce::dontSendNotification);
}

void PadEditor::updatePhrase()
{
    if (!showingMidi)
        return;
    const auto legacyLength = static_cast<int>(controls[Lfo::pattern].knob.getValue());
    phrase = trigger.midiNotes.phraseDisplay(legacyLength);
    if (selectedStep >= phrase.length || !phrase.repeating)
        selectedStep = -1;
    const auto columns = phrase.repeating ? juce::jlimit(1, 8, phrase.length) : 8;
    const auto width = 678 / columns;
    const auto cellHeight = phrase.length > 8 ? 50 : 83;
    const auto scan = trigger.livePatternStep();
    for (int i = 0; i < static_cast<int>(cells.size()); ++i)
    {
        auto& cell = cells[static_cast<std::size_t>(i)];
        const bool visible = i < (phrase.repeating ? phrase.length : 8);
        cell.setVisible(visible);
        if (!visible)
            continue;
        const auto pitch = phrase.pitches[static_cast<std::size_t>(i)];
        const auto label = pitch < 0 ? juce::String("-") : juce::MidiMessage::getMidiNoteName(pitch, true, true, 4);
        if (cell.getButtonText() != label)
        {
            cell.setButtonText(label);
            cell.setDescription(label);
        }
        const auto velocity = phrase.velocities[static_cast<std::size_t>(i)];
        const auto playing = phrase.last == i;
        const auto next = phrase.repeating && (scan < 0 ? phrase.next : scan) == i;
        if (cell.velocity != velocity || cell.played != playing || cell.upcoming != next)
        {
            cell.velocity = velocity;
            cell.played = playing;
            cell.upcoming = next;
            cell.repaint();
        }
        cell.setBounds(41 + i % columns * width, 144 + i / columns * (cellHeight + 4), width - 6, cellHeight);
        cell.setEnabled(phrase.repeating);
        cell.setToggleState(selectedStep == i, juce::dontSendNotification);
    }
    keep.setEnabled(phrase.historyCount >= capture.getIndex() + 1);
    explore.setToggleState(!phrase.repeating, juce::dontSendNotification);
    vary.setEnabled(phrase.repeating && poolEditor.hasNotes());
    undo.setEnabled(phrase.canUndo);
    scanPhrase.setEnabled(phrase.repeating);
    scanPhrase.setToggleState(phrase.repeating && trigger.getLfoDepth(Lfo::patternPosition) > 0.0f,
                              juce::dontSendNotification);
    repaint(phrasePanel);
}

juce::String PadEditor::modulationSummary() const
{
    juce::StringArray routes;
    const bool degrees = pitchUnit.getIndex() == 1 && showingMidi;
    for (std::size_t i = 0; i < Lfo::targets.size(); ++i)
    {
        const auto knob = static_cast<Lfo::Knob>(i);
        const auto depth = trigger.getLfoDepth(knob);
        if (depth <= 0.0f || (knob == Lfo::output && showingMidi)
            || ((knob == Lfo::noteLength || knob == Lfo::velocity) && !showingMidi)
            || ((knob == Lfo::pattern || knob == Lfo::patternPosition) && !phrase.repeating))
            continue;
        if (knob == Lfo::semitones)
            routes.add("Pitch " + juce::String::fromUTF8(u8"\u00b1") + juce::String(depth, 0) + (degrees ? " steps" : " st"));
        else if (knob == Lfo::patternPosition)
            routes.add("Scan phrase");
        else
            routes.add(Lfo::targets[i].label);
    }
    return routes.isEmpty() ? juce::String() : routes.joinIntoString("  /  ");
}

void PadEditor::resized()
{
    mode.setBounds(541, 23, 195, 27);
    audition.setBounds(515, 68, 90, 28);
    inputSetup.setBounds(615, 68, 121, 28);
    keep.setBounds(24, 270, 116, 27);
    capture.setBounds(147, 270, 76, 27);
    explore.setBounds(239, 270, 84, 27);
    vary.setBounds(330, 270, 91, 27);
    undo.setBounds(428, 270, 67, 27);
    followChords.setBounds(606, 270, 130, 27);
    key.setBounds(36, 318, 85, 27);
    scale.setBounds(133, 318, 162, 27);
    registerSelect.setBounds(307, 318, 85, 27);
    motion.setBounds(404, 318, 145, 27);
    gate.setBounds(561, 318, 101, 27);
    controls[Lfo::noteLength].knob.setBounds(670, 305, 56, 63);
    modulation.setBounds(24, 396, 125, 28);
    noteEditor.setBounds(159, 396, 119, 28);
    wavPath.setBounds(37, 318, 383, 26);
    loadButton.setBounds(428, 318, 75, 26);
    sampleStatus.setBounds(37, 349, 681, 21);

    zoom.setBounds(333, drawerTop + 7, 134, 21);
    for (std::size_t position = 0; position < mainKnobs.size(); ++position)
    {
        const auto x = 502 + static_cast<int>(position) * 57;
        auto& control = controls[mainKnobs[position]];
        control.label.setBounds(x, drawerTop + 33, 57, 18);
        control.knob.setBounds(x, drawerTop + 52, 57, 68);
    }
    controls[velocityFloorKnob].label.setBounds(controls[sampleOutputKnob].label.getBounds());
    controls[velocityFloorKnob].knob.setBounds(controls[sampleOutputKnob].knob.getBounds());
    calibrationLevels.setBounds(511, drawerTop + 153, 211, 19);
    calibrationStatus.setBounds(511, drawerTop + 176, 211, 54);
    cancelButton.setBounds(659, drawerTop + 234, 63, 29);
    const auto y = lfoScopeBounds().getY();
    lfoShape.setBounds(507, y + 21, 104, 24);
    lfoClock.setBounds(620, y + 21, 104, 24);
    lfoDivision.setBounds(507, y + 64, 104, 24);
    lfoSeconds.setBounds(lfoDivision.getBounds());
    lfoTarget.setBounds(507, y + 107, 153, 24);
    lfoDepth.setBounds(665, y + 70, 57, 62);
    semitoneTarget.setBounds(348, y + 24, 62, 17);
    centsTarget.setBounds(414, y + 24, 59, 17);
    controls[Lfo::semitones].knob.setBounds(348, y + 43, 59, 55);
    controls[Lfo::cents].knob.setBounds(415, y + 43, 59, 55);
    pitchLfoReadouts[0].setBounds(345, y + 105, 67, 16);
    pitchLfoReadouts[1].setBounds(412, y + 105, 64, 16);
    pitchUnit.setBounds(38, y + 178, 144, 26);
    bendRange.setBounds(195, y + 178, 145, 26);
    scanPhrase.setBounds(358, y + 151, 140, 28);
    monoOutput.setBounds(353, y + 120, 116, 18);
    controls[Lfo::pattern].knob.setBounds(518, y + 172, 88, 40);
    resetPattern.setBounds(611, y + 179, 112, 26);
    poolEditor.setBounds(keyboardPanel);
    closeChoices.setBounds(661, drawerTop + 8, 60, 24);
    previousChoices.setBounds(595, drawerTop + 8, 26, 24);
    nextChoices.setBounds(626, drawerTop + 8, 26, 24);
    for (std::size_t i = 0; i < choiceButtons.size(); ++i)
        choiceButtons[i].setBounds(37 + static_cast<int>(i % choiceColumns) * 115,
                                   drawerTop + 41 + static_cast<int>(i / choiceColumns) * 32, 107, 26);
}

void PadEditor::updateMode()
{
    showingMidi = trigger.isMidiMode();
    if (drawer == Drawer::choices)
        drawer = Drawer::closed;
    updateLayout();
    updatePhrase();
}

void PadEditor::updateLayout()
{
    const bool input = drawer == Drawer::input, mod = drawer == Drawer::modulation;
    const bool choosing = drawer == Drawer::choices;
    const auto height = input ? caseHeight : mod ? drawerTop + 235 : drawer == Drawer::notes ? drawerTop + 186
                         : choosing ? drawerTop + 137 : compactCaseHeight;
    if (height != getHeight())
        setSize(caseWidth, height);
    inputSetup.setToggleState(input, juce::dontSendNotification);
    modulation.setToggleState(mod, juce::dontSendNotification);
    noteEditor.setToggleState(drawer == Drawer::notes, juce::dontSendNotification);
    poolEditor.setVisible(drawer == Drawer::notes);
    for (auto* selector : { &key, &scale, &registerSelect, &motion, &capture, &gate })
        selector->setVisible(showingMidi);
    for (auto* button : { &audition, &keep, &explore, &vary, &undo, &followChords })
        button->setVisible(showingMidi);
    for (auto& cell : cells)
        if (!showingMidi)
            cell.setVisible(false);
    controls[Lfo::noteLength].knob.setVisible(showingMidi && gate.getIndex() == 0);
    controls[Lfo::noteLength].knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 52, 20);
    controls[Lfo::noteLength].label.setVisible(false);
    controls[Lfo::pattern].label.setVisible(false);
    controls[Lfo::pattern].knob.setVisible(mod && showingMidi);
    resetPattern.setVisible(mod && showingMidi);
    scanPhrase.setVisible(mod && showingMidi);
    monoOutput.setVisible(mod && showingMidi);
    pitchUnit.setVisible(mod && showingMidi);
    bendRange.setVisible(mod && showingMidi);
    semitoneTarget.setButtonText(showingMidi && pitchUnit.getIndex() == 1 ? "Degrees" : "Semitones");
    for (auto* selector : { &lfoShape, &lfoClock, &lfoTarget })
        selector->setVisible(mod);
    lfoDivision.setVisible(mod && lfoClock.getIndex() == 1);
    lfoSeconds.setVisible(mod && lfoClock.getIndex() == 0);
    lfoDepth.setVisible(mod);
    for (const auto knob : { Lfo::semitones, Lfo::cents })
    {
        controls[knob].knob.setVisible(mod);
        pitchLfoReadouts[knob == Lfo::semitones ? 0 : 1].setVisible(mod);
    }
    semitoneTarget.setVisible(mod);
    centsTarget.setVisible(mod);
    for (const auto i : mainKnobs)
    {
        const bool visible = input && (i != sampleOutputKnob || !showingMidi);
        controls[i].knob.setVisible(visible);
        controls[i].label.setVisible(visible);
    }
    controls[velocityFloorKnob].knob.setVisible(input && showingMidi);
    controls[velocityFloorKnob].label.setVisible(input && showingMidi);
    zoom.setVisible(input);
    calibrateButton.setVisible(input);
    calibrationLevels.setVisible(input);
    calibrationStatus.setVisible(input);
    if (!input)
    {
        cancelButton.setVisible(false);
        acceptWeakButton.setVisible(false);
    }
    wavPath.setVisible(!showingMidi);
    loadButton.setVisible(!showingMidi);
    sampleStatus.setVisible(!showingMidi);
    closeChoices.setVisible(choosing);
    choicePage();
    repaint();
}

void PadEditor::updateCalibration()
{
    using Stage = PadTrigger::CalibrationStage;
    const auto stage = trigger.calibrationStage();
    const auto levels = trigger.measuredLevels();
    juce::String title = "Calibrate";
    juce::String status = "Set your interface gain first. The plugin knobs do not affect calibration.";
    bool enabled = true;
    const auto count = juce::String(trigger.calibrationHitCount()) + " of " + juce::String(PadTrigger::calibrationTarget);
    const auto savedStatus = [&levels]() -> juce::String
    {
        if (levels.hard <= levels.soft)
            return "Saved. Hard hits measured no louder than soft taps. Check mic position and input gain.";
        if (levels.hard - levels.soft < 6.0f)
            return "Saved. Soft and hard hits were only " + juce::String(levels.hard - levels.soft, 1) + " dB apart.";
        if (levels.soft - levels.noise < 9.0f)
            return "Saved. Soft taps are close to room noise. Try moving the mic closer.";
        return "Calibration saved. Adjust Sensitivity if quiet taps are missed.";
    };

    switch (stage)
    {
        case Stage::startNoise:
        case Stage::noise:
            title = "Measuring noise...";
            status = "Stay quiet for two seconds. Enable track monitoring if the display is still.";
            enabled = false;
            break;
        case Stage::readySoft:
        case Stage::retrySoft:
            title = "Capture soft taps";
            status = stage == Stage::retrySoft
                ? "Captured " + count + " soft taps. Retry, or move the mic closer and recalibrate."
                : "Play five soft taps about half a second apart. Keep the interface gain the same.";
            break;
        case Stage::startSoft:
        case Stage::soft:
            title = "Capturing soft taps";
            status = (stage == Stage::startSoft ? juce::String("Waiting for mic input.")
                                               : "Captured " + count + " soft taps.")
                + " Play about half a second apart.";
            enabled = false;
            break;
        case Stage::readyHard:
        case Stage::retryHard:
            title = "Capture hard hits";
            status = stage == Stage::retryHard
                ? "Captured " + count + " hard hits. Retry with hits about half a second apart."
                : "Play five hard accents about half a second apart. Keep the interface gain the same.";
            break;
        case Stage::startHard:
        case Stage::hard:
        case Stage::committing:
            title = "Capturing hard hits";
            status = (stage == Stage::startHard ? juce::String("Waiting for mic input.")
                                               : "Captured " + count + " hard hits.")
                + " Play about half a second apart.";
            enabled = false;
            break;
        case Stage::complete:
            title = "Recalibrate";
            status = savedStatus();
            break;
        case Stage::weak:
        {
            title = "Retry";
            const auto separation = levels.hard - levels.soft;
            const auto aboveNoise = levels.soft - levels.noise;
            if (separation < 0.0f)
                status = "Accents measured " + juce::String(-separation, 1) + " dB quieter than the taps.";
            else if (aboveNoise < 9.0f)
                status = "Soft taps measured only " + juce::String(aboveNoise, 1) + " dB above room noise.";
            else
                status = "Accents measured only " + juce::String(separation, 1) + " dB louder than the taps.";
            break;
        }
        case Stage::interrupted:
            status = "Audio stopped during capture. Enable monitoring and restart.";
            break;
        case Stage::idle:
            if (levels.calibrated)
            {
                title = "Recalibrate";
                status = savedStatus();
            }
            break;
    }

    const bool haveNoise = levels.calibrated || stage == Stage::readySoft || stage == Stage::startSoft
        || stage == Stage::soft || stage == Stage::retrySoft || stage == Stage::readyHard
        || stage == Stage::startHard || stage == Stage::hard || stage == Stage::retryHard || stage == Stage::committing;
    const bool haveSoft = levels.calibrated || stage == Stage::readyHard || stage == Stage::startHard
        || stage == Stage::hard || stage == Stage::retryHard || stage == Stage::committing;
    const auto read = [](float value, bool available) { return available ? db(value) : juce::String("-"); };
    const auto values = "N " + read(levels.noise, haveNoise) + "   S " + read(levels.soft, haveSoft)
        + "   H " + read(levels.hard, levels.calibrated);
    const auto changeText = [](juce::Label& label, const juce::String& value)
    {
        if (label.getText() != value)
            label.setText(value, juce::dontSendNotification);
    };
    changeText(calibrationLevels, values);
    calibrationLevels.setTooltip("Noise floor, median soft tap, and median hard hit in dBFS.");
    changeText(calibrationStatus, status);
    calibrateButton.setButtonText(title);
    calibrateButton.setEnabled(enabled);
    for (const auto i : mainKnobs)
    {
        controls[i].knob.setEnabled(enabled);
        controls[i].label.setEnabled(enabled);
    }
    controls[velocityFloorKnob].knob.setEnabled(enabled);
    controls[velocityFloorKnob].label.setEnabled(enabled);
    const bool input = drawer == Drawer::input;
    const bool weak = stage == Stage::weak;
    const bool active = stage != Stage::idle && stage != Stage::complete && stage != Stage::interrupted;
    cancelButton.setVisible(input && active);
    acceptWeakButton.setVisible(input && weak);
    acceptWeakButton.setBounds(569, drawerTop + 234, 84, 29);
    calibrateButton.setBounds(511, drawerTop + 234, weak ? 52 : active ? 141 : 211, 29);
}

void PadEditor::timerCallback()
{
    if (showingMidi != trigger.isMidiMode())
        updateMode();
    if (showingMidi)
    {
        updateHarmony();
        updatePhrase();
    }
    const bool mod = drawer == Drawer::modulation;
    if (mod)
    {
        updatePitchLfoReadouts();
        monoOutput.setToggleState(trigger.settings.getRawParameterValue("monoMidi")->load() > 0.5f,
                                  juce::dontSendNotification);
    }
    if (mod && !lfoSeconds.hasKeyboardFocus(false))
    {
        const auto seconds = juce::String(trigger.settings.getRawParameterValue("lfoSeconds")->load(), 3);
        if (lfoSeconds.getText() != seconds)
            lfoSeconds.setText(seconds, false);
    }
    const auto now = juce::Time::getMillisecondCounterHiRes();
    displayTime = now;
    const auto end = trigger.readScope(scope);
    const auto fresh = static_cast<std::size_t>(juce::jmin(end - lastScope, static_cast<std::uint64_t>(scope.size())));
    lastScope = end;

    float in = 0.0f, out = 0.0f;
    for (std::size_t i = scope.size() - fresh; i < scope.size(); ++i)
    {
        in = juce::jmax(in, std::abs(scope[i].inputMin), std::abs(scope[i].inputMax));
        out = juce::jmax(out, std::abs(scope[i].outputMin), std::abs(scope[i].outputMax));
    }
    const auto updatePeak = [now](float next, float& peak, double& hold, double& clip)
    {
        if (next >= peak || now >= hold)
        {
            peak = next;
            hold = now + 850.0;
        }
        if (next >= 1.0f)
            clip = now + 1500.0;
    };
    updatePeak(in, inputPeak, inputPeakUntil, inputClipUntil);
    updatePeak(out, outputPeak, outputPeakUntil, outputClipUntil);
    if (in >= 0.00001f)
        inputUntil = now + 180.0;
    const auto accepted = trigger.detectedHits();
    if (accepted != hitCount)
    {
        hitCount = accepted;
        hitUntil = now + 220.0;
    }
    const auto sent = trigger.midiNotes.noteCount();
    if ((showingMidi && sent != midiNoteCount) || (!showingMidi && out >= 0.00001f))
        midiUntil = now + 220.0;
    midiNoteCount = sent;

    if (!showingMidi)
    {
        const auto activeSample = trigger.sampleDescription();
        const bool typedPath = wavPath.getText().trim().isNotEmpty();
        loadButton.setButtonText(typedPath ? "Load" : "Browse");
        const auto description = sampleError.isEmpty() ? activeSample : activeSample + "  |  " + sampleError;
        if (sampleStatus.getText() != description)
        {
            sampleStatus.setText(description, juce::dontSendNotification);
        }
        const auto expected = trigger.expectedSampleFile();
        sampleStatus.setTooltip(expected == juce::File() ? description : description + "\n" + expected.getFullPathName());
        repaint(outputPanel);
    }
    if (drawer == Drawer::input)
    {
        updateCalibration();
        repaint(micPanel);
    }
    if (mod)
        repaint(lfoScopeBounds());
    repaint(24, 68, 370, 29);
    repaint(24, 349, 712, 27);
    repaint(383, 395, 352, 30);
}

bool PadEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    return !showingMidi && files.size() == 1 && files[0].endsWithIgnoreCase(".wav");
}

void PadEditor::filesDropped(const juce::StringArray& files, int, int)
{
    if (isInterestedInFileDrag(files))
        loadWav(files[0]);
}

void PadEditor::loadWav(const juce::String& path)
{
    const auto filePath = path.trim().unquoted();
    if (!juce::File::isAbsolutePath(filePath))
    {
        sampleError = "Drop a WAV or enter its full path.";
        return;
    }
    const auto result = trigger.loadSample(juce::File(filePath));
    sampleError = result.wasOk() ? juce::String() : juce::File(filePath).getFileName() + " failed. " + result.getErrorMessage();
    if (result.wasOk())
        wavPath.clear();
}

void PadEditor::browseWav()
{
    sampleChooser = std::make_unique<juce::FileChooser>("Load or relink a WAV sample", trigger.expectedSampleFile(), "*.wav");
    const juce::Component::SafePointer<PadEditor> self(this);
    sampleChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [self](const juce::FileChooser& chooser)
        {
            const auto file = chooser.getResult();
            if (self != nullptr && file != juce::File())
                self->loadWav(file.getFullPathName());
        });
}
