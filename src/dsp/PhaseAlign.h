#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace psyduck
{

struct Alignment
{
    bool valid = false;     // false when kick and bass never sounded together
    int lag = 0;            // how many samples to delay the bass
    bool invert = false;    // whether to flip the bass polarity
    float before = 0.0f;    // low-end match as recorded, -1..1
    float after = 0.0f;     // low-end match with the suggestion applied, 0..1
};

/** Finds the bass delay (0..maxLag samples) and polarity that make the low
    end of kick and bass add up best. Both inputs must already be low-passed
    and of equal length. Not for the audio thread. */
inline Alignment findBestAlignment (const std::vector<float>& kick, const std::vector<float>& bass, int maxLag)
{
    Alignment result;

    const int n = (int) std::min (kick.size(), bass.size());
    if (n < 64 || maxLag < 0 || maxLag >= n / 2)
        return result;

    double kk = 0.0, bb = 0.0;
    for (int i = 0; i < n; ++i)
    {
        kk += (double) kick[(std::size_t) i] * kick[(std::size_t) i];
        bb += (double) bass[(std::size_t) i] * bass[(std::size_t) i];
    }

    if (kk < 1.0e-6 * n || bb < 1.0e-6 * n)
        return result;

    const double norm = std::sqrt (kk * bb);
    double bestScore = -1.0;

    for (int lag = 0; lag <= maxLag; ++lag)
    {
        double sum = 0.0;
        for (int i = lag; i < n; ++i)
            sum += (double) kick[(std::size_t) i] * bass[(std::size_t) (i - lag)];

        const double corr = sum / norm;
        if (lag == 0)
            result.before = (float) corr;

        // Prefer the smallest delay when two choices are equally good.
        const double score = std::abs (corr) - 0.0005 * lag;
        if (score > bestScore)
        {
            bestScore = score;
            result.lag = lag;
            result.invert = corr < 0.0;
            result.after = (float) std::abs (corr);
        }
    }

    result.valid = true;
    return result;
}

} // namespace psyduck
