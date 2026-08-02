#pragma once

#include <cmath>
#include <array>
#include <vector>
#include <algorithm>

//==============================================================================
// Shared LFO definitions used by both the audio processor and the editor
// display, so the on-screen waveform always matches what is heard.
//
// A waveform returns an "openness" value in [0, 1]:
//   1 = signal fully open (full volume), 0 = signal fully attenuated.
// The applied gain is  gain = (1 - depth) + depth * openness,
// so depth = 0 leaves the signal untouched and depth = 1 swings the
// gain across the full range of the shape.
//==============================================================================
namespace pulse
{

// Waveform order matches the "wave" AudioParameterChoice in the processor.
enum class Wave : int
{
    triangle   = 0,   // symmetric up/down ramp
    sawUp      = 1,   // ramp up, then snaps back down
    sawDown    = 2,   // ramp down, then snaps back up
    square     = 3,   // hard on/off, duty cycle set by pulse width
    sine       = 4,   // smooth
    sampleHold = 5,   // random level held for each cycle
    numWaves
};

inline const char* const* waveNames()
{
    static const char* names[] = { "Triangle", "Saw Up", "Saw Down", "Square", "Sine", "S&H" };
    return names;
}

namespace detail
{
inline float splitmix01(unsigned long long& s) noexcept
{
    s += 0x9E3779B97F4A7C15ULL;
    unsigned long long z = s;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z =  z ^ (z >> 31);
    return static_cast<float>((z >> 40) / static_cast<double>(1ULL << 24));
}
}

// Sample & Hold levels. Built once as a deterministic random walk: each entry is
// at least kMinStep away from its neighbour, but the step sizes vary (some just
// over the minimum, some large) so it doesn't feel like constant giant jumps.
// It stays a pure function of the cycle index (indexes a fixed table), so the
// audio thread and the scrolling display always agree, including past/future.
inline float sampleHoldValue(long long cycle) noexcept
{
    static const std::vector<float> table = []
    {
        constexpr int   N = 4096;
        constexpr float kMinStep = 0.2f;
        std::vector<float> t(static_cast<size_t>(N));
        unsigned long long s = 0x00C0FFEEULL;   // fixed seed -> reproducible
        float prev = detail::splitmix01(s);
        t[0] = prev;
        for (int i = 1; i < N; ++i)
        {
            const float u  = detail::splitmix01(s);
            const float lo = std::max(0.0f, prev - kMinStep);        // allowed: [0, lo]
            const float hi = std::min(1.0f, prev + kMinStep);        // allowed: [hi, 1]
            const float L1 = lo, L2 = 1.0f - hi, L = L1 + L2;
            const float p  = u * L;
            prev = (p < L1) ? p : hi + (p - L1);
            t[static_cast<size_t>(i)] = prev;
        }
        return t;
    }();

    const int N = static_cast<int>(table.size());
    const int idx = static_cast<int>(((cycle % N) + N) % N);
    return table[static_cast<size_t>(idx)];
}

// Continuous phase (integer part = cycle index) -> openness in [0, 1].
// pulseWidth (0..1) sets the square-wave duty cycle; ignored by other shapes.
inline float waveShape(int waveIndex, double phase, float pulseWidth = 0.5f) noexcept
{
    const Wave wave = static_cast<Wave>(waveIndex);

    // Sample & Hold indexes by cycle, so it needs the phase before wrapping.
    if (wave == Wave::sampleHold)
        return sampleHoldValue(static_cast<long long>(std::floor(phase)));

    phase -= std::floor(phase);
    switch (wave)
    {
        case Wave::triangle: return static_cast<float>(1.0 - std::abs(2.0 * phase - 1.0));
        case Wave::sawDown:  return static_cast<float>(1.0 - phase);
        case Wave::sawUp:    return static_cast<float>(phase);
        case Wave::square:
        {
            const float pw = pulseWidth < 0.01f ? 0.01f : (pulseWidth > 0.99f ? 0.99f : pulseWidth);
            return phase < pw ? 1.0f : 0.0f;
        }
        case Wave::sine:
        case Wave::sampleHold:   // returned above
        case Wave::numWaves:     // not a real shape
        default:            return static_cast<float>(0.5 + 0.5 * std::cos(2.0 * 3.14159265358979323846 * phase));
    }
}

//==============================================================================
// Free-running rate mapping: the single normalised "rate" parameter (0..1)
// maps exponentially onto a musical Hz range.
constexpr double kMinHz = 0.05;
constexpr double kMaxHz = 20.0;

inline double freeHzFromNorm(double norm) noexcept
{
    norm = norm < 0.0 ? 0.0 : (norm > 1.0 ? 1.0 : norm);
    return kMinHz * std::pow(kMaxHz / kMinHz, norm);
}

//==============================================================================
// Low-pass filter envelope. The filter amount is bipolar (-1 .. +1):
//   0  — off, the filter stays wide open at all times
//   >0 — the cutoff tracks the wave: peak = open, trough = closed
//   <0 — inverted: peak = closed, trough = open
// The amount scales how far the cutoff is pulled down, mirroring the gain
// formula so an amount of zero is exactly transparent.
//
// Returns a cutoff *position* in [0, 1], where 1 = fully open. Sharing this
// with the editor keeps the on-screen filter curve honest.
constexpr double kFiltMinHz = 80.0;
constexpr double kFiltMaxHz = 20000.0;
constexpr double kMinQ = 0.70710678118654752;   // Butterworth — flat, no peak
constexpr double kMaxQ = 10.0;

inline float filterEnv(float openness, float amount) noexcept
{
    const float a = std::abs(amount) > 1.0f ? 1.0f : std::abs(amount);
    const float m = (amount >= 0.0f) ? openness : (1.0f - openness);
    return (1.0f - a) + a * m;
}

// Envelope position -> Hz, exponential so the sweep sounds even across the
// range. The Cutoff control sets the ceiling: at position 1 the filter sits
// there, and the envelope pulls it down towards kFiltMinHz.
inline double filterCutoffHz(double pos, double ceilingHz) noexcept
{
    pos = pos < 0.0 ? 0.0 : (pos > 1.0 ? 1.0 : pos);
    ceilingHz = ceilingHz < kFiltMinHz ? kFiltMinHz
                                       : (ceilingHz > kFiltMaxHz ? kFiltMaxHz : ceilingHz);
    return kFiltMinHz * std::pow(ceilingHz / kFiltMinHz, pos);
}

// Hz -> 0..1 across the whole filter range. The display plots this rather than
// the raw envelope position, so lowering the cutoff visibly drops the curve.
inline float filterDisplayPos(double hz) noexcept
{
    hz = hz < kFiltMinHz ? kFiltMinHz : (hz > kFiltMaxHz ? kFiltMaxHz : hz);
    return static_cast<float>(std::log(hz / kFiltMinHz) / std::log(kFiltMaxHz / kFiltMinHz));
}

// Resonance 0..1 -> Q. Starts at Butterworth so a resonance of zero leaves the
// filter exactly as it was before the control existed.
inline double resonanceQ(double norm) noexcept
{
    norm = norm < 0.0 ? 0.0 : (norm > 1.0 ? 1.0 : norm);
    return kMinQ * std::pow(kMaxQ / kMinQ, norm);
}

//==============================================================================
// Host-synced rate mapping: the same normalised parameter selects one of a
// list of note divisions. "beats" is the LFO period length in quarter-notes.
struct Division { const char* label; double beats; };

inline const std::array<Division, 14>& divisions()
{
    static const std::array<Division, 14> table = {{
        { "1/1",   4.0        },
        { "1/2.",  3.0        },
        { "1/2",   2.0        },
        { "1/4.",  1.5        },
        { "1/2T",  4.0 / 3.0  },
        { "1/4",   1.0        },
        { "1/8.",  0.75       },
        { "1/4T",  2.0 / 3.0  },
        { "1/8",   0.5        },
        { "1/16.", 0.375      },
        { "1/8T",  1.0 / 3.0  },
        { "1/16",  0.25       },
        { "1/16T", 1.0 / 6.0  },
        { "1/32",  0.125      },
    }};
    return table;
}

inline int divisionIndexFromNorm(double norm) noexcept
{
    norm = norm < 0.0 ? 0.0 : (norm > 1.0 ? 1.0 : norm);
    const int n = static_cast<int>(divisions().size());
    int idx = static_cast<int>(norm * (n - 1) + 0.5);
    return idx < 0 ? 0 : (idx >= n ? n - 1 : idx);
}

inline const Division& divisionFromNorm(double norm) noexcept
{
    return divisions()[static_cast<size_t>(divisionIndexFromNorm(norm))];
}

} // namespace pulse
