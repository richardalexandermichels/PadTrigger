#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <cmath>

namespace Lfo
{
enum Knob : std::size_t
{
    sensitivity, dynamics, output, pattern, noteLength, retrigger, velocity, semitones, cents, patternPosition, targetCount
};
inline constexpr auto knobCount = static_cast<std::size_t>(patternPosition);

struct Target
{
    const char* label;
    const char* depth;
    float minimum;
    float maximum;
    const char* pitchUnit = nullptr;
};

inline constexpr std::array<Target, targetCount> targets {{
    { "Sensitivity", "lfoSensitivity", -12.0f, 12.0f },
    { "Dynamics", "lfoDynamics", 0.35f, 2.75f },
    { "Output", "lfoOutput", -48.0f, 6.0f },
    { "Pattern Length", "lfoPattern", 1.0f, 16.0f },
    { "Note Length", "lfoNoteLength", 10.0f, 1000.0f },
    { "Retrigger", "lfoRetrigger", 8.0f, 120.0f },
    { "Vel Floor", "lfoVelocity", 1.0f, 127.0f },
    { "Semitones", "lfoSemitones", -24.0f, 24.0f, "st" },
    { "Cents", "lfoCents", -100.0f, 100.0f, "ct" },
    { "Scan phrase", "lfoPatternPosition", 0.0f, 100.0f }
}};

inline const juce::StringArray gateNames { "ms", "1/2", "1/4", "1/8", "1/16", "1/16 T", "1/32" };
inline constexpr std::array<double, 7> gateBeats { 0.0, 2.0, 1.0, 0.5, 0.25, 1.0 / 6.0, 0.125 };

inline constexpr std::array<const char*, 7> shapes {{
    "Sine", "Triangle", "Ramp Up", "Ramp Down", "Square", "Pulse", "4 Steps"
}};

struct Division { const char* name; double quarterNotes; };
// A value of zero starts the bar-based divisions, which follow the host time signature.
inline constexpr std::array<Division, 19> divisions {{
    { "4/1", 0.0 }, { "3/1", 0.0 }, { "2/1", 0.0 }, { "1/1", 0.0 },
    { "1/2 D", 3.0 }, { "1/2", 2.0 }, { "1/2 T", 4.0 / 3.0 },
    { "1/4 D", 1.5 }, { "1/4", 1.0 }, { "1/4 T", 2.0 / 3.0 },
    { "1/8 D", 0.75 }, { "1/8", 0.5 }, { "1/8 T", 1.0 / 3.0 },
    { "1/16 D", 0.375 }, { "1/16", 0.25 }, { "1/16 T", 1.0 / 6.0 },
    { "1/32 D", 0.1875 }, { "1/32", 0.125 }, { "1/32 T", 1.0 / 12.0 }
}};

inline double wrap(double phase) { return phase - std::floor(phase); }

inline float wave(int shape, double phase)
{
    const auto p = static_cast<float>(wrap(phase));
    switch (shape)
    {
        case 1: return 1.0f - 4.0f * std::abs(p - 0.5f);
        case 2: return 2.0f * p - 1.0f;
        case 3: return 1.0f - 2.0f * p;
        case 4: return p < 0.5f ? 1.0f : -1.0f;
        case 5: return p < 0.25f ? 1.0f : -1.0f;
        case 6: return (2.0f / 3.0f) * std::floor(4.0f * p) - 1.0f;
        default: return std::sin(p * juce::MathConstants<float>::twoPi);
    }
}

inline float apply(Knob knob, float base, float depth, float signal)
{
    if (depth == 0.0f || (knob == pattern && base == 0.0f))
        return base;
    const auto& target = targets[knob];
    const auto amount = target.pitchUnit != nullptr ? depth : depth * (target.maximum - target.minimum) / 200.0f;
    return juce::jlimit(target.minimum, target.maximum, base + signal * amount);
}

inline int patternStep(int length, float depth, float signal)
{
    if (length == 0 || depth == 0.0f)
        return -1;
    const auto steps = 1 + juce::roundToInt((length - 1) * depth / 100.0f);
    return juce::jlimit(0, steps - 1, static_cast<int>((signal + 1.0f) * 0.5f * steps));
}
}
