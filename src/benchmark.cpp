#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cuda.h>
#include "benchmark.h"
#include "embedded_kernels.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <memory>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace fpbench {
namespace {
using Clock = std::chrono::steady_clock;
struct Cancelled : std::runtime_error { Cancelled() : std::runtime_error("Cancelled by user") {} };
void cancellation(const std::atomic<bool>& stop) { if (stop.load()) throw Cancelled(); }
void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
template<class T> T symbol(HMODULE module, const char* name) {
    auto result = GetProcAddress(module, name);
    require(result != nullptr, std::string("Driver entry point unavailable: ") + name);
    return reinterpret_cast<T>(result);
}
std::string quoted(const std::string& input) {
    std::ostringstream out;
    out << '"';
    for (unsigned char ch : input) {
        if (ch == '"' || ch == '\\') out << '\\' << ch;
        else if (ch < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(ch) << std::dec;
        else out << ch;
    }
    return out.str() + '"';
}
double median(std::vector<double> values) {
    if (values.empty()) return -1;
    std::sort(values.begin(), values.end());
    size_t mid = values.size() / 2;
    return values.size() % 2 ? values[mid] : (values[mid - 1] + values[mid]) / 2;
}

class Cuda {
public:
    HMODULE library = nullptr;
#define API(name) decltype(&name) name##_p = nullptr
    API(cuInit); API(cuDeviceGetCount); API(cuDeviceGet); API(cuDeviceGetName);
    API(cuDeviceGetAttribute); API(cuDeviceGetUuid); API(cuGetErrorString);
    API(cuCtxGetCurrent); API(cuCtxSetCurrent); API(cuDevicePrimaryCtxRetain); API(cuDevicePrimaryCtxRelease);
    API(cuStreamCreate); API(cuStreamDestroy); API(cuStreamSynchronize);
    API(cuMemAlloc); API(cuMemFree); API(cuMemGetInfo); API(cuMemcpyHtoD); API(cuMemcpyDtoH); API(cuMemsetD8);
    API(cuModuleLoadDataEx); API(cuModuleUnload); API(cuModuleGetFunction); API(cuFuncSetAttribute); API(cuLaunchKernel);
    API(cuEventCreate); API(cuEventRecord); API(cuEventSynchronize); API(cuEventElapsedTime); API(cuEventDestroy);
#undef API
    CUcontext context = nullptr, previous = nullptr;
    CUdevice device = 0;
    CUstream stream = nullptr;
    CUevent start = nullptr, end = nullptr;
    std::vector<CUdeviceptr> allocations;
    std::vector<CUmodule> modules;

    Cuda() {
        library = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        require(library != nullptr, "NVIDIA CUDA driver not found. Install a compatible NVIDIA driver.");
        try {
#define LOAD(name, exported) name##_p = symbol<decltype(name##_p)>(library, exported)
            LOAD(cuInit,"cuInit"); LOAD(cuDeviceGetCount,"cuDeviceGetCount"); LOAD(cuDeviceGet,"cuDeviceGet");
            LOAD(cuDeviceGetName,"cuDeviceGetName"); LOAD(cuDeviceGetAttribute,"cuDeviceGetAttribute");
            LOAD(cuDeviceGetUuid,"cuDeviceGetUuid"); LOAD(cuGetErrorString,"cuGetErrorString");
            LOAD(cuCtxGetCurrent,"cuCtxGetCurrent"); LOAD(cuCtxSetCurrent,"cuCtxSetCurrent");
            LOAD(cuDevicePrimaryCtxRetain,"cuDevicePrimaryCtxRetain"); LOAD(cuDevicePrimaryCtxRelease,"cuDevicePrimaryCtxRelease_v2");
            LOAD(cuStreamCreate,"cuStreamCreate"); LOAD(cuStreamDestroy,"cuStreamDestroy_v2"); LOAD(cuStreamSynchronize,"cuStreamSynchronize");
            LOAD(cuMemAlloc,"cuMemAlloc_v2"); LOAD(cuMemFree,"cuMemFree_v2"); LOAD(cuMemGetInfo,"cuMemGetInfo_v2");
            LOAD(cuMemcpyHtoD,"cuMemcpyHtoD_v2"); LOAD(cuMemcpyDtoH,"cuMemcpyDtoH_v2"); LOAD(cuMemsetD8,"cuMemsetD8_v2");
            LOAD(cuModuleLoadDataEx,"cuModuleLoadDataEx"); LOAD(cuModuleUnload,"cuModuleUnload");
            LOAD(cuModuleGetFunction,"cuModuleGetFunction"); LOAD(cuFuncSetAttribute,"cuFuncSetAttribute"); LOAD(cuLaunchKernel,"cuLaunchKernel");
            LOAD(cuEventCreate,"cuEventCreate"); LOAD(cuEventRecord,"cuEventRecord"); LOAD(cuEventSynchronize,"cuEventSynchronize");
            LOAD(cuEventElapsedTime,"cuEventElapsedTime"); LOAD(cuEventDestroy,"cuEventDestroy_v2");
#undef LOAD
            ck(cuInit_p(0), "cuInit");
        } catch (...) { FreeLibrary(library); library = nullptr; throw; }
    }
    void ck(CUresult result, const char* operation) const {
        if (result == CUDA_SUCCESS) return;
        const char* text = nullptr;
        cuGetErrorString_p(result, &text);
        throw std::runtime_error(std::string(operation) + ": " + (text ? text : "CUDA error") + " (" + std::to_string(result) + ")");
    }
    int attribute(CUdevice dev, CUdevice_attribute which) {
        int result = 0; ck(cuDeviceGetAttribute_p(&result, which, dev), "cuDeviceGetAttribute"); return result;
    }
    Device info(int index) {
        CUdevice dev; ck(cuDeviceGet_p(&dev, index), "cuDeviceGet");
        char name[256]{}; ck(cuDeviceGetName_p(name, sizeof(name), dev), "cuDeviceGetName");
        CUuuid id{}; ck(cuDeviceGetUuid_p(&id, dev), "cuDeviceGetUuid");
        std::ostringstream guid; guid << "GPU-";
        for (int i = 0; i < 16; ++i) {
            if (i == 4 || i == 6 || i == 8 || i == 10) guid << '-';
            guid << std::hex << std::setw(2) << std::setfill('0') << unsigned(static_cast<unsigned char>(id.bytes[i]));
        }
        return {index, attribute(dev, CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT),
            attribute(dev, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR),
            attribute(dev, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR), name, guid.str()};
    }
    void open(int index) {
        ck(cuDeviceGet_p(&device, index), "cuDeviceGet");
        ck(cuCtxGetCurrent_p(&previous), "cuCtxGetCurrent");
        ck(cuDevicePrimaryCtxRetain_p(&context, device), "cuDevicePrimaryCtxRetain");
        ck(cuCtxSetCurrent_p(context), "cuCtxSetCurrent");
        ck(cuStreamCreate_p(&stream, CU_STREAM_NON_BLOCKING), "cuStreamCreate");
        ck(cuEventCreate_p(&start, CU_EVENT_DEFAULT), "cuEventCreate");
        ck(cuEventCreate_p(&end, CU_EVENT_DEFAULT), "cuEventCreate");
    }
    CUdeviceptr allocate(size_t bytes) {
        CUdeviceptr ptr; ck(cuMemAlloc_p(&ptr, bytes), "cuMemAlloc"); allocations.push_back(ptr); return ptr;
    }
    void sync() { ck(cuStreamSynchronize_p(stream), "cuStreamSynchronize"); }
    void free_case() {
        sync();
        for (auto ptr : allocations) ck(cuMemFree_p(ptr), "cuMemFree");
        allocations.clear();
        for (auto module : modules) ck(cuModuleUnload_p(module), "cuModuleUnload");
        modules.clear();
    }
    CUfunction kernel(const EmbeddedKernel& blob, bool cubin) {
        CUmodule module = nullptr;
        char log[4096]{};
        CUjit_option options[]{CU_JIT_ERROR_LOG_BUFFER, CU_JIT_ERROR_LOG_BUFFER_SIZE_BYTES};
        void* values[]{log, reinterpret_cast<void*>(sizeof(log))};
        CUresult status = cuModuleLoadDataEx_p(&module, cubin ? static_cast<const void*>(blob.cubin) : blob.ptx, 2, options, values);
        if (status != CUDA_SUCCESS && log[0]) throw std::runtime_error(std::string("Kernel load: ") + log);
        ck(status, "cuModuleLoadDataEx"); modules.push_back(module);
        CUfunction function; ck(cuModuleGetFunction_p(&function, module, blob.entry), "cuModuleGetFunction");
        ck(cuFuncSetAttribute_p(function, CU_FUNC_ATTRIBUTE_MAX_DYNAMIC_SHARED_SIZE_BYTES, int(blob.shared)), "cuFuncSetAttribute");
        return function;
    }
    void launch(CUfunction fn, const EmbeddedKernel& blob, CUdeviceptr a, CUdeviceptr b, CUdeviceptr c) {
        // Triton AOT ABI appends global/profile scratch pointers even when the
        // metadata says both scratch sizes are zero. Supply all five arguments.
        CUdeviceptr scratch = 0, profile = 0;
        void* args[]{&a, &b, &c, &scratch, &profile};
        unsigned tiles = unsigned(blob.n / 128);
        ck(cuLaunchKernel_p(fn, tiles * tiles, 1, 1, blob.threads, 1, 1, blob.shared, stream, args, nullptr), "cuLaunchKernel");
    }
    bool cleanup() noexcept {
        bool ok = true;
        auto success = [&](CUresult result) { if (result != CUDA_SUCCESS) ok = false; };
        if (context) {
            success(cuCtxSetCurrent_p(context));
            if (stream) success(cuStreamSynchronize_p(stream));
            for (auto ptr : allocations) success(cuMemFree_p(ptr));
            allocations.clear();
            for (auto module : modules) success(cuModuleUnload_p(module));
            modules.clear();
            if (start) success(cuEventDestroy_p(start));
            if (end) success(cuEventDestroy_p(end));
            if (stream) success(cuStreamDestroy_p(stream));
            success(cuCtxSetCurrent_p(previous));
            success(cuDevicePrimaryCtxRelease_p(device));
            context = nullptr; stream = nullptr; start = end = nullptr;
        }
        return ok;
    }
    ~Cuda() { cleanup(); if (library) FreeLibrary(library); }
};

struct Nvml {
    using Init = int (__cdecl*)();
    using Handle = int (__cdecl*)(const char*, void**);
    using ClockInfo = int (__cdecl*)(void*, unsigned, unsigned*);
    using Power = int (__cdecl*)(void*, unsigned*);
    HMODULE lib = nullptr;
    void* device = nullptr;
    Init shutdown = nullptr;
    ClockInfo clock = nullptr, temperature = nullptr;
    Power power = nullptr;
    bool initialized = false;
    explicit Nvml(const std::string& id) {
        lib = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!lib) return;
        try {
            auto init = symbol<Init>(lib, "nvmlInit_v2");
            shutdown = symbol<Init>(lib, "nvmlShutdown");
            auto handle = symbol<Handle>(lib, "nvmlDeviceGetHandleByUUID");
            clock = symbol<ClockInfo>(lib, "nvmlDeviceGetClockInfo");
            temperature = symbol<ClockInfo>(lib, "nvmlDeviceGetTemperature");
            power = symbol<Power>(lib, "nvmlDeviceGetPowerUsage");
            initialized = init() == 0;
            if (!initialized || handle(id.c_str(), &device)) device = nullptr;
        } catch (...) { device = nullptr; }
    }
    void sample(Trial& trial) const {
        if (!device) return;
        unsigned value;
        if (!clock(device, 1, &value)) trial.clock = int(value);
        if (!power(device, &value)) trial.power_mw = int(value);
        if (!temperature(device, 0, &value)) trial.temperature = int(value);
    }
    ~Nvml() { if (initialized) shutdown(); if (lib) FreeLibrary(lib); }
};

const EmbeddedKernel& blob_for(int n, bool half) {
    for (const auto& blob : kKernels) if (blob.n == n && blob.half_acc == half) return blob;
    throw std::runtime_error("No embedded kernel for selected size");
}
uint32_t random32(uint32_t& state) { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
std::vector<uint16_t> input(int n, uint32_t seed, const std::atomic<bool>& stop) {
    std::vector<uint16_t> result(size_t(n) * n);
    for (size_t i = 0; i < result.size(); ++i) {
        if (!(i % (1 << 20))) cancellation(stop);
        result[i] = (random32(seed) & 1) ? 0xAC00 : 0x2C00;
    }
    return result;
}
float half_value(uint16_t bits) {
    int exponent = (bits >> 10) & 31, mantissa = bits & 1023;
    float result = exponent == 31 ? (mantissa ? NAN : INFINITY) :
        std::ldexp(float(exponent ? 1024 + mantissa : mantissa), exponent ? exponent - 25 : -24);
    return bits & 0x8000 ? -result : result;
}
void validate(Cuda& cuda, Row& row, CUdeviceptr output, const std::vector<uint16_t>& a,
              const std::vector<uint16_t>& b) {
    const int n = row.n;
    std::vector<std::pair<int,int>> points{{0,0},{n-1,n-1},{0,n-1},{n-1,0},{n/2,n/2}};
    uint32_t seed = 9041;
    while (points.size() < 21) {
        int r = int(random32(seed) % unsigned(n)), c = int(random32(seed) % unsigned(n));
        if (std::find(points.begin(), points.end(), std::make_pair(r,c)) == points.end()) points.emplace_back(r,c);
    }
    for (const auto& point : points) {
        int sum = 0;
        for (int k = 0; k < n; ++k)
            sum += ((a[size_t(point.first) * n + k] ^ b[size_t(k) * n + point.second]) & 0x8000) ? -1 : 1;
        uint16_t actual;
        cuda.ck(cuda.cuMemcpyDtoH_p(&actual, output + 2 * (size_t(point.first) * n + point.second), 2), "validation copy");
        double error = std::abs(double(half_value(actual)) - sum / 256.0);
        require(std::isfinite(error), "Nonfinite numerical result");
        row.max_error = std::max(row.max_error, error);
        ++row.checked;
        require(error <= 1.0 / 256, "Numerical spot check failed");
    }
    unsigned char guards[256];
    bool good = true;
    for (auto offset : {output - 256, output + size_t(n) * n * 2}) {
        cuda.ck(cuda.cuMemcpyDtoH_p(guards, offset, sizeof(guards)), "guard copy");
        good = good && std::all_of(std::begin(guards), std::end(guards), [](unsigned char x) { return x == 0xA5; });
    }
    row.guards = good; row.valid = good;
    require(good, "Output allocation guard modified");
}
Trial timed(Cuda& cuda, CUfunction fn, const EmbeddedKernel& blob, CUdeviceptr a, CUdeviceptr b,
            CUdeviceptr c, int count) {
    cuda.ck(cuda.cuEventRecord_p(cuda.start, cuda.stream), "event start");
    for (int i = 0; i < count; ++i) cuda.launch(fn, blob, a, b, c);
    cuda.ck(cuda.cuEventRecord_p(cuda.end, cuda.stream), "event end");
    cuda.ck(cuda.cuEventSynchronize_p(cuda.end), "event synchronize");
    float ms = 0; cuda.ck(cuda.cuEventElapsedTime_p(&ms, cuda.start, cuda.end), "event elapsed");
    require(ms > 0, "Nonpositive event duration");
    Trial trial; trial.n = blob.n; trial.half = blob.half_acc; trial.gemms = count;
    trial.batch_ms = ms; trial.ms = ms / count;
    trial.tflops = 2.0 * blob.n * blob.n * blob.n / (trial.ms * 1e9);
    return trial;
}

void save(const Report& report) {
    const auto& directory = report.config.output;
    std::ofstream json(directory / L"result.json", std::ios::binary | std::ios::trunc);
    std::ofstream summary(directory / L"summary.csv", std::ios::binary | std::ios::trunc);
    std::ofstream trials(directory / L"trials.csv", std::ios::binary | std::ios::trunc);
    require(bool(json) && bool(summary) && bool(trials), "Unable to write report files");
    json << std::setprecision(15);
    json << "{\n\"schema_version\":1,\"app\":\"DLSS Bench 1.0\",\"status\":" << quoted(report.status)
         << ",\"started_utc\":" << quoted(report.started) << ",\"finished_utc\":" << quoted(report.finished)
         << ",\"error\":" << quoted(report.error) << ",\"cleanup_ok\":" << (report.cleanup_ok ? "true" : "false")
         << ",\"backend\":" << quoted(report.backend) << ",\"kernel_source_sha256\":\"" << kKernelSourceSha256
         << "\",\"device\":{\"name\":" << quoted(report.device.name) << ",\"uuid\":" << quoted(report.device.uuid)
         << ",\"sm_count\":" << report.device.sms << ",\"major\":" << report.device.major << ",\"minor\":" << report.device.minor << "},"
         << "\n\"trials_per_mode\":" << report.config.trials << ",\"sample_ms\":" << report.config.sample_ms
         << ",\"warmup_seconds\":" << report.config.warmup_seconds << ",\"work\":\"row-major C=A*B, FP16 input/output, 2*N^3 FLOPs\","
         << "\"timing\":\"CUDA events around kernel batch; excludes allocation, transfers, validation and warmup\","
         << "\"limitations\":[\"Fixed precompiled GEMM backend, not cuBLAS or an exhaustive peak search\","
         << "\"No matched locked baseline; no unlock speedup inferred\","
         << "\"21 numerical samples checked first and last trial, plus output guards; not exhaustive stability testing\","
         << "\"NVML readings after each batch have sensor refresh latency\"],\n\"rows\":[";
    summary << "\xEF\xBB\xBFN,accumulator,median_TFLOPS,min_TFLOPS,max_TFLOPS,median_ms,SM_clock_MHz,power_W,max_temp_C,validation,max_abs_error\r\n";
    bool first = true;
    for (const auto& row : report.rows) {
        if (!first) json << ','; first = false;
        const char* mode = row.half ? "fp16_acc" : "fp32_acc";
        json << "\n{\"n\":" << row.n << ",\"mode\":\"" << mode << "\",\"median_tflops\":" << row.median
             << ",\"min_tflops\":" << row.minimum << ",\"max_tflops\":" << row.maximum << ",\"median_ms\":" << row.ms
             << ",\"sm_clock_mhz\":" << row.clock << ",\"power_w\":" << row.power_w << ",\"temperature_c\":" << row.temperature
             << ",\"checked_elements\":" << row.checked << ",\"max_abs_error\":" << row.max_error
             << ",\"validation_passed\":" << (row.valid ? "true" : "false") << ",\"guards_intact\":" << (row.guards ? "true" : "false")
             << ",\"cubin_sha256\":\"" << row.cubin_hash << "\"}";
        summary << std::setprecision(10) << row.n << ',' << mode << ',' << row.median << ',' << row.minimum << ',' << row.maximum << ',' << row.ms << ',';
        if (row.clock >= 0) summary << row.clock;
        summary << ','; if (row.power_w >= 0) summary << row.power_w;
        summary << ','; if (row.temperature >= 0) summary << row.temperature;
        summary << ',' << (row.valid ? "PASS" : "FAIL") << ',' << row.max_error << "\r\n";
    }
    json << "\n],\"trials\":["; first = true;
    trials << "N,accumulator,repetition,order,gemms,batch_ms,ms_per_gemm,TFLOPS,SM_clock_MHz,power_mW,temperature_C\r\n";
    for (const auto& trial : report.trials) {
        if (!first) json << ','; first = false;
        const char* mode = trial.half ? "fp16_acc" : "fp32_acc";
        json << "\n{\"n\":" << trial.n << ",\"mode\":\"" << mode << "\",\"repetition\":" << trial.repetition
             << ",\"order\":" << trial.order << ",\"gemms\":" << trial.gemms << ",\"batch_ms\":" << trial.batch_ms
             << ",\"ms_per_gemm\":" << trial.ms << ",\"tflops\":" << trial.tflops << ",\"sm_clock_mhz\":" << trial.clock
             << ",\"power_mw\":" << trial.power_mw << ",\"temperature_c\":" << trial.temperature << '}';
        trials << std::setprecision(12) << trial.n << ',' << mode << ',' << trial.repetition << ',' << trial.order << ',' << trial.gemms
               << ',' << trial.batch_ms << ',' << trial.ms << ',' << trial.tflops << ',' << trial.clock << ',' << trial.power_mw << ',' << trial.temperature << "\r\n";
    }
    json << "\n]}\n";
    require(bool(json) && bool(summary) && bool(trials), "Report write failed");
}
}

std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0'); WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), result.data(), size, nullptr, nullptr); return result;
}
std::wstring wide(const std::string& value) {
    if (value.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), int(value.size()), nullptr, 0);
    std::wstring result(size, L'\0'); MultiByteToWideChar(CP_UTF8, 0, value.data(), int(value.size()), result.data(), size); return result;
}
std::string utc_now() {
    SYSTEMTIME time; GetSystemTime(&time); char text[64];
    sprintf_s(text, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds);
    return text;
}
const char* license_text() { return kKernelLicense; }
std::vector<Device> enumerate_devices() {
    Cuda cuda; int count = 0; cuda.ck(cuda.cuDeviceGetCount_p(&count), "cuDeviceGetCount");
    std::vector<Device> result; for (int i = 0; i < count; ++i) result.push_back(cuda.info(i)); return result;
}

Report run(const Config& config, std::atomic<bool>& stop, Progress progress, RowReady ready) {
    Report report; report.config = config; report.started = utc_now();
    std::unique_ptr<Cuda> cuda;
    bool owns_output = false;
    auto log = [&](const std::wstring& message) { if (progress) progress(message); };
    try {
        require(config.trials >= 1 && config.trials <= 50 && config.sample_ms >= 10 && config.sample_ms <= 1000,
                "Use 1..50 trials and 10..1000 ms per sample");
        require(!config.output.empty(), "Report directory is required");
        owns_output = std::filesystem::create_directories(config.output);
        require(owns_output, "Report directory already exists; choose a fresh directory");
        cuda = std::make_unique<Cuda>(); report.device = cuda->info(config.device);
        require(report.device.supported(), "This build requires NVIDIA SM 8.6 or newer (RTX 30-series or newer)");
        report.backend = report.device.major == 8 && report.device.minor == 6 ? "embedded sm_86 cubin" : "driver-JIT embedded sm_86 PTX (not validated on this device)";
        cuda->open(config.device); Nvml nvml(report.device.uuid);
        for (int n : config.sizes) {
            cancellation(stop);
            (void)blob_for(n, true);
            log(L"准备矩阵 / Preparing matrices: " + std::to_wstring(n));
            const size_t bytes = size_t(n) * n * 2;
            size_t free = 0, total = 0; cuda->ck(cuda->cuMemGetInfo_p(&free, &total), "cuMemGetInfo");
            require(double(bytes) * 3 + 512 < double(free) * 0.7, "Selected matrices would use over 70% of free VRAM");
            auto a = input(n, 20261028, stop), b = input(n, 20261029, stop);
            CUdeviceptr da = cuda->allocate(bytes), db = cuda->allocate(bytes), base = cuda->allocate(bytes + 512), dc = base + 256;
            cuda->ck(cuda->cuMemcpyHtoD_p(da, a.data(), bytes), "input copy A");
            cuda->ck(cuda->cuMemcpyHtoD_p(db, b.data(), bytes), "input copy B");
            cuda->ck(cuda->cuMemsetD8_p(base, 0xA5, bytes + 512), "output initialization");
            CUfunction functions[2]{}; int counts[2]{}; Row rows[2];
            for (int mode = 0; mode < 2; ++mode) {
                cancellation(stop);
                const auto& blob = blob_for(n, mode == 0);
                functions[mode] = cuda->kernel(blob, report.device.major == 8 && report.device.minor == 6);
                rows[mode].n = n; rows[mode].half = mode == 0; rows[mode].cubin_hash = blob.sha256;
                log(L"预热 / Warming up: " + std::to_wstring(n) + (mode ? L" · FP32 累加 / accumulation" : L" · FP16 累加 / accumulation"));
                auto deadline = Clock::now() + std::chrono::duration<double>(config.warmup_seconds);
                do {
                    cancellation(stop);
                    for (int j = 0; j < 3; ++j) cuda->launch(functions[mode], blob, da, db, dc);
                    cuda->sync();
                } while (Clock::now() < deadline);
                auto calibration = timed(*cuda, functions[mode], blob, da, db, dc, 3);
                counts[mode] = std::clamp(int(std::ceil(config.sample_ms / calibration.ms)), 1, 2000);
            }
            for (int repetition = 0; repetition < config.trials; ++repetition) {
                log(L"测试 / Testing: " + std::to_wstring(n) + L" · " + std::to_wstring(repetition + 1) + L" / " + std::to_wstring(config.trials));
                for (int order = 0; order < 2; ++order) {
                    cancellation(stop);
                    int mode = (repetition % 2) ^ order;
                    const auto& blob = blob_for(n, mode == 0);
                    Trial trial = timed(*cuda, functions[mode], blob, da, db, dc, counts[mode]);
                    trial.repetition = repetition; trial.order = order; nvml.sample(trial); report.trials.push_back(trial);
                    if (repetition == 0 || repetition == config.trials - 1) validate(*cuda, rows[mode], dc, a, b);
                }
            }
            for (auto& row : rows) {
                std::vector<double> rates, times, clocks, powers;
                for (const auto& trial : report.trials) if (trial.n == n && trial.half == row.half) {
                    rates.push_back(trial.tflops); times.push_back(trial.ms);
                    if (trial.clock >= 0) clocks.push_back(trial.clock);
                    if (trial.power_mw >= 0) powers.push_back(trial.power_mw / 1000.0);
                    row.temperature = std::max(row.temperature, trial.temperature);
                }
                row.median = median(rates); row.ms = median(times);
                row.minimum = *std::min_element(rates.begin(), rates.end()); row.maximum = *std::max_element(rates.begin(), rates.end());
                row.clock = int(median(clocks)); row.power_w = median(powers);
                report.rows.push_back(row); save(report); if (ready) ready(row);
            }
            cuda->free_case();
        }
        cancellation(stop); report.status = "completed";
    } catch (const Cancelled& error) { report.status = "cancelled"; report.error = error.what(); }
      catch (const std::exception& error) { report.status = "failed"; report.error = error.what(); }
    report.cleanup_ok = !cuda || cuda->cleanup();
    if (!report.cleanup_ok) { report.status = "failed"; report.error += " CUDA cleanup failed."; }
    report.finished = utc_now();
    try { if (owns_output) save(report); }
    catch (const std::exception& error) { report.status = "failed"; report.error += error.what(); }
    return report;
}
}
