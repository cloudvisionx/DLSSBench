#pragma once
#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace fpbench {
struct Device {
    int index = 0, sms = 0, major = 0, minor = 0;
    std::string name, uuid;
    bool supported() const { return major * 10 + minor >= 86; }
};
struct Config {
    int device = 0, trials = 7;
    double sample_ms = 120, warmup_seconds = 0.4;
    std::vector<int> sizes{4096, 8192};
    std::filesystem::path output;
};
struct Trial {
    int n = 0, repetition = 0, order = 0, gemms = 0;
    bool half = true;
    double batch_ms = 0, ms = 0, tflops = 0;
    int clock = -1, power_mw = -1, temperature = -1;
};
struct Row {
    int n = 0, clock = -1, temperature = -1, checked = 0;
    bool half = true, valid = false, guards = false;
    double median = 0, minimum = 0, maximum = 0, ms = 0, power_w = -1, max_error = 0;
    std::string cubin_hash;
};
struct Report {
    Device device;
    Config config;
    std::string status = "running", error, started, finished, backend;
    bool cleanup_ok = false;
    std::vector<Row> rows;
    std::vector<Trial> trials;
};
using Progress = std::function<void(const std::wstring&)>;
using RowReady = std::function<void(const Row&)>;
std::vector<Device> enumerate_devices();
Report run(const Config&, std::atomic<bool>& cancel, Progress = {}, RowReady = {});
std::string utf8(const std::wstring&);
std::wstring wide(const std::string&);
std::string utc_now();
const char* license_text();
}
