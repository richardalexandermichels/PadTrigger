#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
#include <vector>

class StrokeDetector
{
public:
    struct Event
    {
        bool onset = false;
        float peak = 0.0f;
    };

    void prepare(double rate)
    {
        highPass = static_cast<float>(std::exp(-juce::MathConstants<double>::twoPi * 100.0 / rate));
        onsetRelease = static_cast<float>(std::exp(-1.0 / (0.0018 * rate)));
        levelRelease = static_cast<float>(std::exp(-1.0 / (0.008 * rate)));
        ringingRelease = static_cast<float>(std::exp(-1.0 / (0.020 * rate)));
        quietGap = juce::jmax(1, juce::roundToInt(rate * 0.0045));
        peakWindow = juce::jmax(1, juce::roundToInt(rate * 0.0015));
        crossingWindow = juce::jmax(1, juce::roundToInt(rate * 0.00055));
        for (auto& lane : lanes)
            lane.history.resize(static_cast<std::size_t>(juce::jmax(1, juce::roundToInt(rate * 0.00065))));
        reset();
    }

    void reset()
    {
        for (auto& lane : lanes)
        {
            lane.previous = lane.filtered = lane.onset = lane.level = lane.ring = 0.0f;
            lane.quiet = lane.pending = 0;
            lane.waitingForQuiet = false;
            lane.hasSignal = false;
            std::fill(lane.history.begin(), lane.history.end(), Envelope{});
        }
        historyIndex = 0;
        remaining = cooldown = 0;
    }

    void holdOff(int frames) { cooldown = frames; }
    bool hasRightSignal() const { return lanes[1].hasSignal; }

    // A positive result carries the peak for the one accepted stroke.
    Event process(const std::array<float, 2>& input, int channels, float gate, float quietGate,
                  int holdFrames, float fullVelocityPeak)
    {
        if (cooldown > 0)
            --cooldown;

        int attackChannel = -1;
        float attackLevel = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto index = static_cast<std::size_t>(ch);
            auto& lane = lanes[index];
            const auto signal = input[index];
            // Mono mic routing often leaves a second host input silent.
            if (!lane.hasSignal)
            {
                if (signal == 0.0f)
                    continue;
                lane.hasSignal = true;
            }
            const auto level = std::abs(signal);
            lane.filtered = highPass * (lane.filtered + signal - lane.previous);
            lane.previous = signal;
            const auto novelty = std::abs(lane.filtered);
            const auto earlier = lane.history[historyIndex];
            lane.onset = juce::jmax(novelty, lane.onset * onsetRelease);
            lane.level = juce::jmax(level, lane.level * levelRelease);
            lane.history[historyIndex] = { lane.onset, lane.level };

            if (lane.quiet >= quietGap)
                lane.waitingForQuiet = false;
            const bool fresh = !lane.waitingForQuiet || novelty > lane.ring * 1.5f;
            lane.ring = juce::jmax(novelty, lane.ring * ringingRelease);
            if (lane.pending > 0)
                --lane.pending;

            if (cooldown == 0)
            {
                const bool onset = fresh && novelty > gate * 0.25f && novelty > earlier.onset * 1.55f;
                // Remember a near-threshold attack briefly so a rising stroke can confirm on its crossing.
                // Only arm this path after quiet; ringing still has to pass the stronger same-frame test.
                if (onset && !lane.waitingForQuiet && level >= gate * 0.5f)
                    lane.pending = crossingWindow + 1;
                const bool rising = lane.pending > 0 && level > earlier.level * 1.7f;
                if (level >= gate && (onset || rising) && level > attackLevel)
                {
                    attackChannel = ch;
                    attackLevel = level;
                }
            }
            lane.quiet = level < quietGate ? juce::jmin(lane.quiet + 1, quietGap) : 0;
        }

        if (++historyIndex == lanes[0].history.size())
            historyIndex = 0;
        const bool started = attackChannel >= 0;
        if (started)
        {
            remaining = peakWindow;
            peakChannel = static_cast<std::size_t>(attackChannel);
            peak = attackLevel;
            cooldown = holdFrames;
            for (int ch = 0; ch < channels; ++ch)
            {
                auto& lane = lanes[static_cast<std::size_t>(ch)];
                lane.quiet = lane.pending = 0;
                lane.waitingForQuiet = lane.hasSignal;
            }
        }

        if (remaining == 0)
            return { started, 0.0f };
        peak = juce::jmax(peak, std::abs(input[peakChannel]));
        if (--remaining == 0 || peak >= fullVelocityPeak)
        {
            remaining = 0;
            return { started, peak };
        }
        return { started, 0.0f };
    }

private:
    struct Envelope { float onset = 0.0f, level = 0.0f; };

    struct Channel
    {
        float previous = 0.0f, filtered = 0.0f, onset = 0.0f, level = 0.0f, ring = 0.0f;
        int quiet = 0, pending = 0;
        bool waitingForQuiet = false, hasSignal = false;
        std::vector<Envelope> history;
    };

    std::array<Channel, 2> lanes;
    std::size_t historyIndex = 0, peakChannel = 0;
    float highPass = 0.0f, onsetRelease = 0.0f, levelRelease = 0.0f, ringingRelease = 0.0f, peak = 0.0f;
    int quietGap = 1, peakWindow = 1, crossingWindow = 1, cooldown = 0, remaining = 0;
};
