#include "../src/performance.h"
#include <cstdio>
#include <limits>
#include <stdexcept>

int main() {
    try {
        auto check = [](bool ok) { if (!ok) throw std::runtime_error("Peak comparison check failed"); };
        auto close = [&](double a, double b) { check(std::abs(a - b) < 1e-9); };
        const auto reference = fpbench::compare_peak(8, 6, 82, 1695, 124.02, true);
        check(reference.available && reference.above_half);
        close(reference.full_tflops, 142.325760);
        close(reference.half_tflops, 71.162880);
        const auto at_half = fpbench::compare_peak(8, 6, 82, 1695, 71.162880, true);
        check(at_half.available && !at_half.above_half);
        close(at_half.percent, 50);
        check(fpbench::compare_peak(8, 6, 82, 1695, 71.162881, true).above_half);
        check(!fpbench::compare_peak(8, 6, 82, 1695, 71.162879, true).above_half);
        close(fpbench::compare_peak(8, 6, 28, 1777, 40, true).full_tflops, 50.950144);
        close(fpbench::compare_peak(8, 9, 128, 2520, 200, true).full_tflops, 330.301440);
        // Product specifications supply the SM counts. Normalize at exactly
        // 1000 MHz to avoid confusing a reference calculation with boost clocks.
        // This is test data, not a runtime model-name whitelist.
        // Sources: NVIDIA RTX 30 / 40 product tables, linked in THEORETICAL_PEAK.md.
        struct Case { const char* model; int minor, sms; double peak_at_1ghz; } cases[]{
            {"RTX 3050 6GB / OEM",6,18,18.432}, {"RTX 3050 8GB",6,20,20.480},
            {"RTX 3060 8GB / 12GB",6,28,28.672}, {"RTX 3060 Ti",6,38,38.912},
            {"RTX 3070",6,46,47.104}, {"RTX 3070 Ti",6,48,49.152},
            {"RTX 3080 10GB",6,68,69.632}, {"RTX 3080 12GB",6,70,71.680},
            {"RTX 3080 Ti",6,80,81.920}, {"RTX 3090",6,82,83.968},
            {"RTX 3090 Ti",6,84,86.016}, {"RTX 4060",9,24,24.576},
            {"RTX 4060 Ti 8GB / 16GB",9,34,34.816}, {"RTX 4070",9,46,47.104},
            {"RTX 4070 SUPER",9,56,57.344}, {"RTX 4070 Ti",9,60,61.440},
            {"RTX 4070 Ti SUPER",9,66,67.584}, {"RTX 4080",9,76,77.824},
            {"RTX 4080 SUPER",9,80,81.920}, {"RTX 4090",9,128,131.072}
        };
        for (const auto& c : cases) {
            const auto above = fpbench::compare_peak(8, c.minor, c.sms, 1000, c.peak_at_1ghz * 0.6, true);
            if (!above.available || !above.above_half || std::abs(above.full_tflops - c.peak_at_1ghz) > 1e-9)
                throw std::runtime_error(c.model);
            close(above.percent, 60);
            const auto below = fpbench::compare_peak(8, c.minor, c.sms, 1000, c.peak_at_1ghz * 0.4, true);
            check(below.available && !below.above_half);
        }
        close(fpbench::compare_peak(8, 9, 128, 2000, 200, true).full_tflops, 262.144);
        check(!fpbench::compare_peak(8, 7, 16, 1000, 10, true).available);
        check(!fpbench::compare_peak(9, 0, 132, 1800, 100, true).available);
        check(!fpbench::compare_peak(8, 6, 82, -1, 100, true).available);
        check(!fpbench::compare_peak(8, 6, 82, 0, 100, true).available);
        check(!fpbench::compare_peak(8, 6, 0, 1800, 100, true).available);
        check(!fpbench::compare_peak(8, 6, 82, 1800, 100, false).available);
        check(!fpbench::compare_peak(8, 6, 82, 1800, -1, true).available);
        check(!fpbench::compare_peak(8, 6, 82, 1800, std::numeric_limits<double>::infinity(), true).available);
        check(!fpbench::compare_peak(8, 6, 82, 1800, std::numeric_limits<double>::quiet_NaN(), true).available);
        const auto zero = fpbench::compare_peak(8, 6, 82, 1800, 0, true);
        check(zero.available && !zero.above_half && zero.percent == 0);
        puts("Peak reference and boundary checks passed.");
        return 0;
    } catch (const std::exception& error) {
        puts(error.what());
        return 1;
    }
}
