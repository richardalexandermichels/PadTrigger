#pragma once

#include "PanelTheme.h"
#include <juce_audio_processors/juce_audio_processors.h>

// A pair of tactile controls with an always-visible readout.
class PanelSelector final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit PanelSelector(const juce::String& title)
    {
        setTitle(title);
        back.setTitle("Previous " + title);
        forward.setTitle("Next " + title);
        back.onClick = [this] { step(-1); };
        forward.onClick = [this] { step(1); };
        back.setRepeatSpeed(400, 140);
        forward.setRepeatSpeed(400, 140);
        addAndMakeVisible(back);
        addAndMakeVisible(forward);
    }

    void setItems(juce::StringArray choices, int current = 0)
    {
        items = std::move(choices);
        back.setEnabled(items.size() > 1);
        forward.setEnabled(items.size() > 1);
        setIndex(current);
    }

    void setIndex(int next)
    {
        index = juce::jlimit(0, juce::jmax(0, items.size() - 1), next);
        setDescription(getText());
        repaint();
    }

    void attach(juce::RangedAudioParameter& parameter, int firstValue = 0)
    {
        parameterOffset = firstValue;
        attachment = std::make_unique<juce::ParameterAttachment>(parameter,
            [this](float value)
            {
                setIndex(juce::roundToInt(value) - parameterOffset);
                if (onChange)
                    onChange();
            });
        attachment->sendInitialUpdate();
    }

    int getIndex() const { return index; }
    juce::String getText() const { return items[index]; }
    std::function<void()> onChange;
    std::function<bool()> canChange;

private:
    void step(int delta)
    {
        if (items.size() <= 1 || (canChange && !canChange()))
            return;
        const auto next = (index + delta + items.size()) % items.size();
        if (attachment != nullptr)
            attachment->setValueAsCompleteGesture(static_cast<float>(next + parameterOffset));
        else
        {
            setIndex(next);
            if (onChange)
                onChange();
        }
    }

    void resized() override
    {
        const auto bounds = getLocalBounds();
        back.setBounds(bounds.withWidth(21));
        forward.setBounds(bounds.withLeft(bounds.getRight() - 21));
    }

    void paint(juce::Graphics& g) override
    {
        auto bay = getLocalBounds().reduced(23, 1).toFloat();
        if (bay.isEmpty())
            return;
        using namespace PanelTheme;
        g.setColour(ink.withAlpha(0.58f));
        g.fillRoundedRectangle(bay, 3.0f);
        bay.reduce(1.0f, 1.0f);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xffe5ebeb), bay.getX(), bay.getY(),
            juce::Colour(0xfffbfaf7), bay.getX(), bay.getBottom(), false));
        g.fillRoundedRectangle(bay, 2.0f);
        g.setColour(isEnabled() ? ink : muted);
        g.setFont(juce::FontOptions(12.0f));
        g.drawText(getText(), bay.toNearestInt().reduced(3, 0), juce::Justification::centred, true);
    }

    juce::StringArray items;
    int index = 0;
    int parameterOffset = 0;
    juce::TextButton back { "<" }, forward { ">" };
    std::unique_ptr<juce::ParameterAttachment> attachment;
};
