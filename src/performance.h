#pragma once
#include <cmath>

namespace fpbench {
struct PeakComparison {
    bool available = false, above_half = false;
    double full_tflops = 0, half_tflops = 0, percent = 0;
};

// Full-rate dense FP16 accumulation, with a multiply-add counted as two ops.
// Ampere GA10x (SM 8.6) / Ada (SM 8.9): 1024 FLOPs per SM per cycle.
// Official basis, device-query sources and model coverage: docs/THEORETICAL_PEAK.md.
// Model names are deliberately not used: SM counts/clocks come from the driver.
// Keep unfamiliar architectures unavailable instead of extrapolating a rate.
inline PeakComparison compare_peak(int major, int minor, int sms, int clock_mhz,
                                   double tflops, bool validated) {
    PeakComparison result;
    if (!(major == 8 && (minor == 6 || minor == 9)) || sms <= 0 || clock_mhz <= 0 ||
        !validated || !std::isfinite(tflops) || tflops < 0) return result;
    result.available = true;
    result.full_tflops = double(sms) * 1024 * clock_mhz / 1e6;
    result.half_tflops = result.full_tflops / 2;
    result.percent = 100 * tflops / result.full_tflops;
    result.above_half = tflops > result.half_tflops;
    return result;
}
}
