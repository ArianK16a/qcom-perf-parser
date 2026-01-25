#pragma once

#include "target_runtime_info.h"
#include <cstdlib>   // std::abs
#include <limits>

// Find closest available frequency (in kHz) for a specific CPU id.
// cpu_id corresponds directly to /sys/devices/system/cpu/cpu<cpu_id>.
// Returns -1 on error.
inline int find_closest_freq_for_cpu(int cpu_id, int target_khz)
{
    if (cpu_id < 0 ||
        static_cast<std::size_t>(cpu_id) >= scaling_available_frequencies.size()) {
        return -1;
    }

    const auto& freqs = scaling_available_frequencies[static_cast<std::size_t>(cpu_id)];
    if (freqs.empty()) {
        return -1;
    }

    int best = freqs[0];
    int best_diff = std::abs(freqs[0] - target_khz);

    for (std::size_t i = 1; i < freqs.size(); ++i) {
        int diff = std::abs(freqs[i] - target_khz);
        if (diff < best_diff) {
            best_diff = diff;
            best = freqs[i];
        }
    }
    return best;
}

// Closest frequency across all CPUs; returns -1 if no frequencies.
inline int find_closest_freq_any_cpu(int target_khz)
{
    int best = -1;
    int best_diff = std::numeric_limits<int>::max();

    for (const auto& freqs : scaling_available_frequencies) {
        for (int f : freqs) {
            int diff = std::abs(f - target_khz);
            if (diff < best_diff) {
                best_diff = diff;
                best = f;
            }
        }
    }
    return best;
}