#pragma once

#include <cmath>

namespace ibom::overlay {

/**
 * @brief 1€ filter — speed-adaptive low-pass filter for noisy interactive signals.
 *
 * Casiez, Roussel & Vogel, "1€ Filter: A Simple Speed-based Low-pass Filter
 * for Noisy Input in Interactive Systems" (CHI 2012).
 *
 * At low speed it uses a low cutoff frequency → strong smoothing (kills
 * jitter on a static scene). As the signal's speed rises, the cutoff rises
 * → little smoothing (no lag during real motion). Two intuitive parameters:
 *   - minCutoff: baseline cutoff (Hz). Lower = steadier at rest, but laggier.
 *   - beta:      how much the cutoff opens up with speed. Higher = less lag.
 *
 * Header-only, scalar, copy-assignable (so it can live in a std::vector and
 * be reset by reassignment). Feed it (value, timestamp_seconds).
 */
class OneEuroFilter {
public:
    OneEuroFilter(double minCutoff = 1.0, double beta = 0.0, double dCutoff = 1.0)
        : m_minCutoff(minCutoff), m_beta(beta), m_dCutoff(dCutoff) {}

    double filter(double value, double tSeconds)
    {
        double dt = (m_lastTime >= 0.0 && tSeconds > m_lastTime)
                        ? (tSeconds - m_lastTime)
                        : (1.0 / 30.0);  // assume ~30 fps before the first dt
        m_lastTime = tSeconds;

        // Derivative of the (filtered) signal, itself low-passed.
        double dValue = m_hasPrev ? (value - m_xPrev) / dt : 0.0;
        double edValue = lowpass(m_dxState, m_dxInit, dValue, alpha(m_dCutoff, dt));

        // Speed-adaptive cutoff.
        double cutoff = m_minCutoff + m_beta * std::fabs(edValue);
        double out = lowpass(m_xState, m_xInit, value, alpha(cutoff, dt));

        m_xPrev = value;
        m_hasPrev = true;
        return out;
    }

    void reset()
    {
        m_xInit = m_dxInit = m_hasPrev = false;
        m_lastTime = -1.0;
    }

private:
    static double alpha(double cutoff, double dt)
    {
        const double tau = 1.0 / (2.0 * M_PI * cutoff);
        return 1.0 / (1.0 + tau / dt);
    }
    static double lowpass(double& state, bool& init, double value, double a)
    {
        if (!init) { state = value; init = true; }
        else       { state = a * value + (1.0 - a) * state; }
        return state;
    }

    double m_minCutoff, m_beta, m_dCutoff;
    double m_xState = 0.0, m_dxState = 0.0;
    double m_xPrev  = 0.0;
    double m_lastTime = -1.0;
    bool   m_xInit = false, m_dxInit = false, m_hasPrev = false;
};

} // namespace ibom::overlay
