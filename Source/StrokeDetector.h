#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
#include <vector>

class StrokeDetector
{
public:
    enum class Rejection { none, belowThreshold, attack, retrigger };

    struct Event
    {
        bool onset = false;
        float peak = 0.0f;
        Rejection rejection = Rejection::none;
    };

    void prepare(double rate)
    {
        highPass = static_cast<float>(std::exp(-juce::MathConstants<double>::twoPi * 100.0 / rate));
        onsetRelease = static_cast<float>(std::exp(-1.0 / (0.0018 * rate)));
        levelRelease = static_cast<float>(std::exp(-1.0 / (0.008 * rate)));
        quietGap = juce::jmax(1, juce::roundToInt(rate * 0.0045));
        edgeSpan = static_cast<std::size_t>(juce::jlimit(1, static_cast<int>(edgeHistorySize) - 1,
                                                       juce::roundToInt(rate / 6000.0)));
        residualWindow = juce::jmax(1, juce::roundToInt(rate * 0.003));
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
            lane.previous = lane.filtered = lane.onset = lane.level = lane.edge = 0.0f;
            lane.residual = {};
            lane.edgeHistory = {};
            lane.quiet = lane.pending = 0;
            lane.pendingBelowGate = lane.waitingForQuiet = false;
            lane.hasSignal = false;
            std::fill(lane.history.begin(), lane.history.end(), Envelope{});
        }
        historyIndex = edgeIndex = 0;
        remaining = cooldown = residualRemaining = 0;
    }

    void holdOff(int frames) { cooldown = frames; }
    bool hasRightSignal() const { return lanes[1].hasSignal; }

    // A positive result carries the peak for the one accepted stroke.
    Event process(const std::array<float, 2>& input, int channels, float gate, float quietGate,
                  int holdFrames, float fullVelocityPeak)
    {
        if (cooldown > 0)
            --cooldown;
        if (residualRemaining == 0)
        {
            for (auto& lane : lanes)
                lane.residual = { 0.0f, lane.residual[0], lane.residual[1] };
            residualRemaining = residualWindow;
        }
        --residualRemaining;

        int attackChannel = -1;
        float attackLevel = 0.0f;
        Rejection rejection = Rejection::none;
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
            const auto previousLevel = std::abs(lane.previous);
            const auto change = signal - lane.previous;
            const auto edge = std::abs(signal - lane.edgeHistory[(edgeIndex - edgeSpan) & (edgeHistorySize - 1)]);
            lane.edgeHistory[edgeIndex] = signal;
            if (lane.waitingForQuiet && lane.quiet >= quietGap)
            {
                // Start the attack comparison with the quiet signal, not the old accent.
                lane.waitingForQuiet = false;
                lane.onset = std::abs(lane.filtered);
                lane.level = previousLevel;
                std::fill(lane.history.begin(), lane.history.end(), Envelope{lane.onset, lane.level, lane.edge});
            }
            lane.filtered = highPass * (lane.filtered + change);
            lane.previous = signal;
            const auto novelty = std::abs(lane.filtered);
            const auto earlier = lane.history[historyIndex];
            lane.onset = juce::jmax(novelty, lane.onset * onsetRelease);
            lane.level = juce::jmax(level, lane.level * levelRelease);
            lane.edge = juce::jmax(edge, lane.edge * onsetRelease);
            lane.history[historyIndex] = { lane.onset, lane.level, lane.edge };

            // A sharp change must stand out above the recent waveform while the pad is ringing.
            const auto residual = juce::jmax(lane.residual[0], lane.residual[1], lane.residual[2]);
            lane.residual[0] = juce::jmax(lane.residual[0], level);
            const bool lowExpired = lane.pending == 1 && lane.pendingBelowGate && level < gate;
            if (lane.pending > 0)
                --lane.pending;
            if (level >= gate)
                lane.pendingBelowGate = false;

            const bool crossing = level >= gate && previousLevel < gate;
            const bool newEdge = edge > gate * 0.5f && edge > earlier.edge * 1.85f;
            if (cooldown == 0)
            {
                // During a loud tail, demand stronger evidence to avoid mistaking an echo for a strike.
                const auto residualRatio = residual > gate * 16.0f ? 1.35f : 1.15f;
                const bool onset = lane.waitingForQuiet
                    ? newEdge && edge > residual * residualRatio
                    : novelty > gate * 0.25f && novelty > earlier.onset * 1.55f;
                // Remember a near-threshold attack briefly so a rising stroke can confirm on its crossing.
                // Only arm this path after quiet; ringing still has to pass the stronger same-frame test.
                if (onset && !lane.waitingForQuiet && level >= gate * 0.5f)
                {
                    if (lane.pending == 0)
                        lane.pendingBelowGate = level < gate;
                    lane.pending = crossingWindow + 1;
                }
                const bool rising = lane.pending > 0 && level > earlier.level * 1.7f;
                if (level >= gate && (onset || rising) && level > attackLevel)
                {
                    attackChannel = ch;
                    attackLevel = level;
                }
                else if (level >= gate && !onset && !rising && (newEdge || (crossing && !lane.waitingForQuiet)))
                    rejection = juce::jmax(rejection, Rejection::attack);
                else if (lowExpired && lane.pending == 0)
                    rejection = juce::jmax(rejection, Rejection::belowThreshold);
            }
            else if (remaining == 0 && level >= gate && newEdge)
                rejection = Rejection::retrigger;
            lane.quiet = level < quietGate ? juce::jmin(lane.quiet + 1, quietGap) : 0;
        }

        if (++historyIndex == lanes[0].history.size())
            historyIndex = 0;
        edgeIndex = (edgeIndex + 1) & (edgeHistorySize - 1);
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
                lane.pendingBelowGate = false;
                lane.waitingForQuiet = lane.hasSignal;
            }
        }

        if (remaining == 0)
            return { started, 0.0f, rejection };
        peak = juce::jmax(peak, std::abs(input[peakChannel]));
        if (--remaining == 0 || peak >= fullVelocityPeak)
        {
            remaining = 0;
            return { started, peak };
        }
        return { started, 0.0f };
    }

private:
    static constexpr std::size_t edgeHistorySize = 64;
    struct Envelope { float onset = 0.0f, level = 0.0f, edge = 0.0f; };

    struct Channel
    {
        float previous = 0.0f, filtered = 0.0f, onset = 0.0f, level = 0.0f, edge = 0.0f;
        std::array<float, 3> residual {};
        std::array<float, edgeHistorySize> edgeHistory {};
        int quiet = 0, pending = 0;
        bool waitingForQuiet = false, hasSignal = false, pendingBelowGate = false;
        std::vector<Envelope> history;
    };

    std::array<Channel, 2> lanes;
    std::size_t historyIndex = 0, peakChannel = 0, edgeIndex = 0, edgeSpan = 1;
    float highPass = 0.0f, onsetRelease = 0.0f, levelRelease = 0.0f, peak = 0.0f;
    int residualWindow = 1, residualRemaining = 0;
    int quietGap = 1, peakWindow = 1, crossingWindow = 1, cooldown = 0, remaining = 0;
};
