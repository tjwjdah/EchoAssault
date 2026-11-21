#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

// Seconds since this run began. The caller advances Now only while the world is playing.
struct FEchoClock
{
    double Delay = 8;
    double Previous = -std::numeric_limits<double>::epsilon();
    bool bStarted = false;

    template<typename HistoryType, typename Callback>
    void Advance(const HistoryType& History, double Now, double Delta, double TargetDelay, Callback&& Fire)
    {
        Delay = std::max(TargetDelay, Delay - 0.25 * Delta);
        const double Through = Now - Delay;
        if (Through < 0 || History.Poses.IsEmpty()) return;
        const double After = !bStarted && Through > Delta * 1.25 ? std::nextafter(Through, -std::numeric_limits<double>::infinity()) : Previous;
        History.Replay(After, Through, Fire);
        bStarted = true;
        Previous = Through;
    }
};

// The half-open interval (after, through] assigns even equal-time attacks to exactly one tick.
template<typename Iterator, typename Callback>
void ReplayShotRange(Iterator First, Iterator Last, double After, double Through, Callback&& Fire)
{
    auto It = std::upper_bound(First, Last, After, [](double Time, const auto& Shot) { return Time < Shot.Time; });
    while (It != Last && It->Time <= Through) Fire(*It++);
}
