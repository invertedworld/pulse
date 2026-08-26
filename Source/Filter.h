/*
    This file is part of Pulse, an LFO tremolo and resonant filter plugin.
    Copyright (C) 2026 Mark Hammond

    Pulse is free software: you can redistribute it and/or modify it under the
    terms of the GNU Affero General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    Pulse is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
    more details.

    You should have received a copy of the GNU Affero General Public License
    along with Pulse. If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <algorithm>
#include <cmath>

//==============================================================================
// A resonant 12 dB/oct low-pass in topology-preserving transform (TPT)
// state-variable form. This form stays stable when the cutoff is swept quickly,
// which is exactly what the LFO does to it.
//
// Coefficients and state are separate so a single set of coefficients can drive
// every channel (they all track the same envelope).
//==============================================================================
namespace pulse
{

struct LowpassCoeffs
{
    // Defaults are a pass-through (g = 0).
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;

    // q = 1/sqrt(2) is Butterworth (flat, no peak); higher values add a
    // resonant peak at the corner.
    void set(double cutoffHz, double q, double sampleRate) noexcept
    {
        constexpr double kPi = 3.14159265358979323846;

        if (sampleRate <= 0.0)
            return;

        const double maxHz = sampleRate * 0.49;
        cutoffHz = cutoffHz < 5.0 ? 5.0 : (cutoffHz > maxHz ? maxHz : cutoffHz);
        const double k = 1.0 / (q < 0.05 ? 0.05 : q);
        const double g = std::tan(kPi * cutoffHz / sampleRate);
        const double d = 1.0 / (1.0 + g * (g + k));

        a1 = static_cast<float>(d);
        a2 = static_cast<float>(g * d);
        a3 = static_cast<float>(g * g * d);
    }
};

struct LowpassState
{
    void reset() noexcept { ic1_ = ic2_ = 0.0f; }

    float process(const LowpassCoeffs& c, float x) noexcept
    {
        const float v3 = x - ic2_;
        const float v1 = c.a1 * ic1_ + c.a2 * v3;
        const float v2 = ic2_ + c.a2 * ic1_ + c.a3 * v3;
        ic1_ = 2.0f * v1 - ic1_;
        ic2_ = 2.0f * v2 - ic2_;
        return v2;   // low-pass output
    }

private:
    float ic1_ = 0.0f, ic2_ = 0.0f;
};

} // namespace pulse
