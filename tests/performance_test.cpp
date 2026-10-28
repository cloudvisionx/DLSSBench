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
