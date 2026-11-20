#include "PadEditor.h"
#include "PanelTheme.h"
#include <cmath>

namespace
{
using namespace PanelTheme;
const auto hitColour = juce::Colour(0xffffcc7c);
const auto danger = juce::Colour(0xffff947d);
const juce::Rectangle<int> micPanel { 24, 68, 456, 132 };
const juce::Rectangle<int> outputPanel { 24, 212, 456, 132 };
const juce::Rectangle<int> controlPanel { 495, 68, 241, 276 };
const juce::Rectangle<int> playbackPanel { 24, 356, 712, 70 };
const juce::Rectangle<int> lfoPanel { 24, 438, 306, 143 };
const juce::Rectangle<int> pitchPanel { 341, 438, 139, 143 };
const juce::Rectangle<int> keyboardPanel { 24, 593, 712, 166 };
constexpr int caseWidth = 760, caseHeight = 779;
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

    resetPattern.onClick = [this] { trigger.midiNotes.resetPattern(); };
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
    loadButton.onClick = [this] { loadWav(wavPath.getText()); };
    addAndMakeVisible(wavPath);
    calibrateButton.onClick = [this] { trigger.advanceCalibration(); updateCalibration(); };
    cancelButton.onClick = [this] { trigger.cancelCalibration(); updateCalibration(); };
    addAndMakeVisible(loadButton);
    addAndMakeVisible(calibrateButton);
    addChildComponent(cancelButton);

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
    setSize(caseWidth, caseHeight);
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
    g.setFont(juce::FontOptions(35.0f, juce::Font::bold));
    g.drawText("Padomizer", 23, 16, 340, 39, juce::Justification::centredLeft);
    g.setColour(muted);
    g.setFont(mono(11.0f));
    g.drawText("MODE", 495, 24, 42, 26, juce::Justification::centredLeft);
    g.setColour(border);
    g.drawHorizontalLine(60, 24.0f, 736.0f);
    g.setColour(juce::Colours::white.withAlpha(0.67f));
    g.drawHorizontalLine(61, 24.0f, 736.0f);
    g.setColour(teal);
    g.fillRect(24, 59, 41, 2);

    drawScope(g, micPanel, true);
    if (showingMidi)
        drawMidiDisplay(g);
    else
        drawScope(g, outputPanel, false);

    recessedPanel(g, controlPanel.toFloat());
    recessedPanel(g, playbackPanel.toFloat());
    g.setColour(border);
    g.drawHorizontalLine(196, 510.0f, 721.0f);
    g.setColour(juce::Colour(0xffe9e6f6));
    g.fillRoundedRectangle(511.0f, 221.0f, 211.0f, 19.0f, 3.0f);
    g.setColour(ink);
    g.setFont(mono(13.0f));
    g.drawText("TRIGGER & PLAYBACK", 511, 77, 205, 22, juce::Justification::centredLeft);
    g.drawText("MIC CALIBRATION", 511, 201, 205, 19, juce::Justification::centredLeft);
    g.setFont(mono(11.0f));
    if (showingMidi)
    {
        const auto step = displayedPatternLength > 0 ? trigger.livePatternStep() : -1;
        const auto label = step >= 0 ? "PATTERN    LFO STEP " + juce::String(step + 1) : juce::String("PATTERN");
        g.drawText(label, 37, 361, 311, 16, juce::Justification::centredLeft);
        const auto tag = juce::Rectangle<float>(37.0f, 382.0f, 314.0f, 31.0f);
        g.setColour(screen);
        g.fillRoundedRectangle(tag, 3.5f);
        g.setColour(outputTrace);
        g.setFont(mono(13.0f));
        const auto display = displayedPatternLength == 0 ? juce::String("FREE / NO IMMEDIATE REPEATS")
            : recentPattern.isEmpty() ? "PLAY TO BUILD " + juce::String(displayedPatternLength) + "-NOTE LOOP" : recentPattern;
        g.drawFittedText(display, tag.toNearestInt().reduced(8, 2), juce::Justification::centredLeft, 2);
    }
    else
        g.drawText("WAV SOURCE", 37, 360, 311, 16, juce::Justification::centredLeft);
    recessedPanel(g, pitchPanel.toFloat());
    g.setColour(ink);
    g.setFont(mono(11.0f));
    g.drawText("PITCH", 352, pitchPanel.getY() + 6, 117, 16, juce::Justification::centredLeft);
    if (showingMidi)
        g.drawText("MIDI BEND RANGE", 352, pitchPanel.getY() + 103, 117, 15, juce::Justification::centredLeft);
    drawLfo(g);
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
    juce::Path curve;
    for (int i = 0; i <= 256; ++i)
    {
        const auto x = grid.getX() + grid.getWidth() * i / 256.0f;
        const auto y = ordinate(Lfo::wave(shape, juce::jmin(0.999999, i / 256.0)));
        if (i == 0)
            curve.startNewSubPath(x, y);
        else
            curve.lineTo(x, y);
    }
    g.setColour(micTrace.withAlpha(0.22f));
    g.strokePath(curve, juce::PathStrokeType(4.0f));
    g.setColour(micTrace);
    g.strokePath(curve, juce::PathStrokeType(1.35f));
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
    if (showingMidi && (lfoTarget.getIndex() == Lfo::pattern || lfoTarget.getIndex() == Lfo::patternPosition)
        && controls[3].knob.getValue() == 0.0)
    {
        g.setColour(outputTrace);
        g.drawText("Pattern Length 1-16 enables modulation.",
            bounds.reduced(13, 0).removeFromBottom(18), juce::Justification::centredLeft);
    }

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
    lfoDivision.setVisible(tempo);
    lfoSeconds.setVisible(!tempo);
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

void PadEditor::drawMidiDisplay(juce::Graphics& g) const
{
    drawDisplayBezel(g, outputPanel);
    auto content = outputPanel.reduced(13, 9);
    auto header = content.removeFromTop(24);
    g.setColour(outputTrace);
    g.setFont(lcd(16.5f));
    g.drawText("MIDI OUT", header.removeFromLeft(118), juce::Justification::centredLeft);
    g.setFont(mono(11.0f));
    g.drawText("CH 1", header, juce::Justification::centredRight);
    g.drawText(juce::String(midiNoteCount) + " NOTES SENT", header.withTrimmedRight(52), juce::Justification::centredRight);

    const auto pitch = (recentMidi & 255) - 1;
    const auto note = pitch >= 0 ? juce::MidiMessage::getMidiNoteName(pitch, true, true, 4) : juce::String("--");
    const auto velocity = pitch >= 0 ? juce::String(recentMidi >> 8) : juce::String("--");
    auto pitchCell = content.removeFromLeft(150);
    g.setColour(juce::Colours::white);
    g.setFont(lcd(49.0f));
    if (note.containsChar('#'))
    {
        const auto letter = note.substring(0, 1);
        const auto letterWidth = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), letter) + 4;
        g.drawText(letter, pitchCell.removeFromLeft(letterWidth), juce::Justification::centredLeft, false);
        g.setFont(mono(26.0f));
        g.drawText("#", pitchCell.removeFromLeft(22), juce::Justification::centredLeft, false);
        g.setFont(lcd(49.0f));
        g.drawText(note.substring(2), pitchCell, juce::Justification::centredLeft);
    }
    else
    {
        g.drawText(note, pitchCell, juce::Justification::centredLeft);
    }
    const auto drawMeter = [&g](juce::Rectangle<int> area, const juce::String& value, const juce::String& caption)
    {
        area = area.withSizeKeepingCentre(area.getWidth(), 51);
        g.setColour(outputTrace);
        g.setFont(mono(11.0f));
        g.drawText(caption, area.removeFromBottom(17), juce::Justification::centred);
        g.setColour(juce::Colours::white);
        g.setFont(lcd(29.0f));
        g.drawText(value, area, juce::Justification::centred);
    };
    drawMeter(content.removeFromLeft(112), velocity, "VELOCITY");
    drawMeter(content.removeFromRight(117), juce::String(midiNoteRate), "NOTES/S");
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
    const auto scale = input ? scales[static_cast<std::size_t>(zoom.getIndex())] : 1.0f;
    const auto yFor = [&](float value)
    {
        return grid.getCentreY() - juce::jlimit(-1.0f, 1.0f, value / scale) * (grid.getHeight() * 0.5f - 1.0f);
    };
    const auto gateDb = input ? trigger.scopeThresholdDb() : std::nullopt;
    const auto gate = gateDb ? juce::Decibels::decibelsToGain(*gateDb, -150.0f) : 0.0f;
    if (gateDb && gate <= scale)
    {
        constexpr float dashes[] { 5.0f, 4.0f };
        g.setColour(thresholdColour.withAlpha(0.62f));
        for (const auto level : { gate, -gate })
        {
            const auto y = yFor(level);
            g.drawDashedLine({ grid.getX(), y, grid.getRight(), y }, dashes, 2);
        }
    }

    juce::Path waveform, aboveThreshold, hitMarks;
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
        if (gateDb)
        {
            auto gateLabel = "THRESHOLD " + db(*gateDb) + " dBFS";
            if (gate > scale)
                gateLabel += " (OFF SCALE)";
            g.setColour(thresholdColour);
            g.drawText(gateLabel, footer.withTrimmedLeft(68).withWidth(243), juce::Justification::centred);
        }
        g.setColour(hitColour);
        g.drawText("| = HIT", footer.withTrimmedLeft(312).withWidth(65), juce::Justification::centred);

    }
    else
    {
        g.drawText(juce::String::fromUTF8(u8"FULL SCALE  \u00b11 FS"), footer, juce::Justification::centred);
    }
}

void PadEditor::resized()
{
    mode.setBounds(540, 24, 196, 26);
    zoom.setBounds(333, 75, 134, 21);
    for (std::size_t position = 0; position < mainKnobs.size(); ++position)
    {
        const auto x = 502 + static_cast<int>(position) * 57;
        auto& control = controls[mainKnobs[position]];
        control.label.setBounds(x, 101, 57, 18);
        control.knob.setBounds(x, 120, 57, 68);
    }
    controls[velocityFloorKnob].label.setBounds(controls[sampleOutputKnob].label.getBounds());
    controls[velocityFloorKnob].knob.setBounds(controls[sampleOutputKnob].knob.getBounds());
    calibrationLevels.setBounds(511, 221, 211, 19);
    calibrationStatus.setBounds(511, 244, 211, 54);
    cancelButton.setBounds(659, 302, 63, 29);
    wavPath.setBounds(37, 378, 253, 23);
    loadButton.setBounds(296, 378, 55, 23);
    sampleStatus.setBounds(36, 401, 311, 20);
    controls[3].label.setBounds(375, 359, 111, 16);
    controls[3].knob.setBounds(391, 373, 80, 52);
    resetPattern.setBounds(497, 381, 98, 27);
    controls[4].label.setBounds(605, 359, 109, 16);
    controls[4].knob.setBounds(620, 373, 80, 52);
    const auto lfoY = lfoScopeBounds().getY();
    lfoShape.setBounds(507, lfoY + 21, 104, 24);
    lfoClock.setBounds(620, lfoY + 21, 104, 24);
    lfoDivision.setBounds(507, lfoY + 64, 104, 24);
    lfoSeconds.setBounds(lfoDivision.getBounds());
    lfoTarget.setBounds(507, lfoY + 107, 146, 24);
    lfoDepth.setBounds(665, lfoY + 70, 57, 62);
    semitoneTarget.setBounds(348, lfoY + 24, 62, 17);
    centsTarget.setBounds(414, lfoY + 24, 59, 17);
    controls[Lfo::semitones].knob.setBounds(348, lfoY + 41, 59, 49);
    controls[Lfo::cents].knob.setBounds(415, lfoY + 41, 59, 49);
    pitchLfoReadouts[0].setBounds(345, lfoY + 91, 67, 13);
    pitchLfoReadouts[1].setBounds(412, lfoY + 91, 64, 13);
    bendRange.setBounds(352, lfoY + 118, 117, 21);
    poolEditor.setBounds(keyboardPanel);
}

void PadEditor::updateMode()
{
    showingMidi = trigger.isMidiMode();
    bendRange.setVisible(showingMidi);
    wavPath.setVisible(!showingMidi);
    loadButton.setVisible(!showingMidi);
    sampleStatus.setVisible(!showingMidi);
    resetPattern.setEnabled(showingMidi && controls[3].knob.getValue() > 0.0);
    controls[sampleOutputKnob].knob.setVisible(!showingMidi);
    controls[sampleOutputKnob].label.setVisible(!showingMidi);
    controls[velocityFloorKnob].knob.setVisible(showingMidi);
    controls[velocityFloorKnob].label.setVisible(showingMidi);
    for (const auto i : { std::size_t { 3 }, std::size_t { 4 } })
    {
        controls[i].label.setEnabled(showingMidi);
        controls[i].knob.setEnabled(showingMidi);
    }
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
            title = "Capturing hard hits";
            status = (stage == Stage::startHard ? juce::String("Waiting for mic input.")
                                               : "Captured " + count + " hard hits.")
                + " Play about half a second apart.";
            enabled = false;
            break;
        case Stage::complete:
        case Stage::weak:
            title = "Recalibrate";
            status = savedStatus();
            break;
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
        || stage == Stage::startHard || stage == Stage::hard || stage == Stage::retryHard;
    const bool haveSoft = levels.calibrated || stage == Stage::readyHard || stage == Stage::startHard
        || stage == Stage::hard || stage == Stage::retryHard;
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
    const bool active = stage != Stage::idle && stage != Stage::complete && stage != Stage::weak
        && stage != Stage::interrupted;
    cancelButton.setVisible(active);
    calibrateButton.setBounds(511, 302, active ? 141 : 211, 29);
}

void PadEditor::timerCallback()
{
    if (showingMidi != trigger.isMidiMode())
        updateMode();
    if (showingMidi)
    {
        recentMidi = trigger.midiNotes.lastEvent();
        midiNoteCount = trigger.midiNotes.noteCount();
        const auto baseLength = static_cast<int>(controls[3].knob.getValue());
        displayedPatternLength = baseLength > 0 && trigger.settings.getRawParameterValue("lfoPattern")->load() > 0.0f
            ? juce::jmax(1, trigger.livePatternLength()) : baseLength;
        recentPattern = trigger.midiNotes.patternDescription(displayedPatternLength);
        resetPattern.setEnabled(controls[3].knob.getValue() > 0.0);
    }
    midiNoteRate = trigger.notesPerSecond();
    updatePitchLfoReadouts();
    if (!lfoSeconds.hasKeyboardFocus(false))
    {
        const auto seconds = juce::String(trigger.settings.getRawParameterValue("lfoSeconds")->load(), 3);
        if (lfoSeconds.getText() != seconds)
            lfoSeconds.setText(seconds, false);
    }
    const auto now = juce::Time::getMillisecondCounterHiRes();
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

    const auto description = sampleError.isEmpty() ? trigger.sampleDescription() : sampleError;
    if (sampleStatus.getText() != description)
    {
        sampleStatus.setText(description, juce::dontSendNotification);
        sampleStatus.setTooltip(description);
    }
    updateCalibration();
    repaint(micPanel);
    repaint(outputPanel);
    repaint(lfoScopeBounds());
    if (showingMidi)
        repaint(playbackPanel.withWidth(342));
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
    sampleError = result.getErrorMessage();
    if (result.wasOk())
        wavPath.clear();
}
