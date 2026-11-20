#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PadomizerAssets.h"

namespace PanelTheme
{
inline const auto face = juce::Colour(0xffd8dad5);
inline const auto panelFill = juce::Colour(0xffecebe4);
inline const auto border = juce::Colour(0xff9a9f98);
inline const auto ink = juce::Colour(0xff252b30);
inline const auto muted = juce::Colour(0xff4e5657);
inline const auto ultraviolet = juce::Colour(0xff534477);
inline const auto raspberry = juce::Colour(0xff9c3968);
inline const auto teal = juce::Colour(0xff146e69);
inline const auto screen = juce::Colour(0xff121922);
inline const auto micTrace = juce::Colour(0xffffaed1);
inline const auto thresholdColour = juce::Colour(0xff76eed2);
inline const auto outputTrace = juce::Colour(0xffcfc5ff);

inline juce::Font mono(float height)
{
    return juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain);
}

inline juce::Font lcd(float height)
{
    static const auto typeface = juce::Typeface::createSystemTypefaceFor(
        PadomizerAssets::DSEG14ClassicMiniRegular_ttf, PadomizerAssets::DSEG14ClassicMiniRegular_ttfSize);
    return juce::FontOptions(typeface).withHeight(height);
}

inline void recessedPanel(juce::Graphics& g, juce::Rectangle<float> frame, float curve = 5.0f)
{
    g.setColour(juce::Colour(0xa440483f));
    g.fillRoundedRectangle(frame, curve);
    g.setColour(juce::Colour(0xaafffff9));
    g.fillRoundedRectangle(frame.reduced(0.8f).translated(0.0f, 0.8f), curve);
    const auto inner = frame.reduced(1.1f);
    auto material = juce::ColourGradient(juce::Colour(0xffd6d9d1), inner.getX(), inner.getY(),
        panelFill, inner.getX(), inner.getBottom(), false);
    material.addColour(0.25, juce::Colour(0xffe6e7df));
    g.setGradientFill(material);
    g.fillRoundedRectangle(inner, curve - 0.5f);
}
}
