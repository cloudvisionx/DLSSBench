#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include "benchmark.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <stdexcept>
#include <memory>
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {
constexpr UINT WM_PROGRESS = WM_APP + 1, WM_ROW = WM_APP + 2, WM_FINISHED = WM_APP + 3;
enum Id { Start = 100, Stop, OpenReports, DeviceChoice, SizeChoice, TrialCount, About };
const wchar_t* kTitle = L"DLSS Bench — 显卡算力测试";
std::filesystem::path executable_directory() {
    wchar_t name[32768]; DWORD count = GetModuleFileNameW(nullptr, name, DWORD(std::size(name)));
    if (!count || count == std::size(name)) throw std::runtime_error("Cannot find executable path");
    return std::filesystem::path(name).parent_path();
}
std::filesystem::path default_report_root() {
    auto path = executable_directory() / L"Reports";
    std::error_code error; std::filesystem::create_directories(path, error);
    if (!error) return path;
    PWSTR local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
        auto fallback = std::filesystem::path(local) / L"DLSSBench" / L"Reports";
        CoTaskMemFree(local); return fallback;
    }
    throw std::runtime_error("No writable report directory");
}
std::filesystem::path fresh_report(const std::filesystem::path& root) {
    auto name = fpbench::wide(fpbench::utc_now());
    for (auto& ch : name) if (ch == L':' || ch == L'.') ch = L'-';
    return root / name;
}
std::wstring number(double value, int precision = 2) {
    if (value < 0) return L"—";
    std::wostringstream out; out << std::fixed << std::setprecision(precision) << value; return out.str();
}
struct App {
    HWND window = nullptr, title = nullptr, subtitle = nullptr, gpu_label = nullptr, device = nullptr;
    HWND size_label = nullptr, size = nullptr, trial_label = nullptr, trials = nullptr;
    HWND start = nullptr, stop = nullptr, reports = nullptr, about = nullptr, status = nullptr;
    HWND progress = nullptr, table = nullptr, log = nullptr, footer = nullptr;
    HFONT body_font = nullptr, title_font = nullptr;
    HBRUSH background = CreateSolidBrush(RGB(247,249,252));
    std::vector<fpbench::Device> devices;
    std::thread worker;
    std::atomic<bool> cancellation{false};
    bool running = false, closing = false;
    int scale = 96;
    std::filesystem::path output_override, last_report;
    int px(int value) const { return MulDiv(value, scale, 96); }

    HWND control(const wchar_t* cls, const wchar_t* text, DWORD style, int id = 0, DWORD extra = 0) {
        HWND result = CreateWindowExW(extra, cls, text, WS_CHILD | WS_VISIBLE | style,
            0,0,0,0,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        if (!result) throw std::runtime_error("Unable to create a window control");
        SendMessageW(result, WM_SETFONT, WPARAM(body_font), TRUE); return result;
    }
    void append(const std::wstring& text) {
        SendMessageW(log, EM_SETSEL, WPARAM(-1), LPARAM(-1));
        auto line = text + L"\r\n";
        SendMessageW(log, EM_REPLACESEL, FALSE, LPARAM(line.c_str()));
        SendMessageW(log, EM_SCROLLCARET, 0, 0);
    }
    void create() {
        HDC dc = GetDC(window); scale = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(window,dc);
        body_font = CreateFontW(-px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        title_font = CreateFontW(-px(27),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        title = control(L"STATIC", L"DLSS Bench", SS_LEFT);
        SendMessageW(title, WM_SETFONT, WPARAM(title_font), TRUE);
        subtitle = control(L"STATIC", L"整卡稠密矩阵乘法 · 分别测量 FP16 / FP32 累加\r\nWhole-GPU dense GEMM · FP16 inputs, two accumulation precisions", SS_LEFT);
        gpu_label = control(L"STATIC", L"显卡 / GPU", SS_LEFT);
        device = control(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,DeviceChoice);
        size_label = control(L"STATIC", L"矩阵规模 / Matrix size",SS_LEFT);
        size = control(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST | WS_TABSTOP,SizeChoice);
        for (auto text : {L"4096 + 8192 · 标准 / Standard", L"4096 · 快速 / Quick", L"8192 · 大矩阵 / Large", L"1024 · 小矩阵 / Small"})
            SendMessageW(size,CB_ADDSTRING,0,LPARAM(text));
        SendMessageW(size,CB_SETCURSEL,0,0);
        trial_label = control(L"STATIC",L"测量轮次 / Trials",SS_LEFT);
        trials = control(L"EDIT",L"7",WS_BORDER | ES_NUMBER | WS_TABSTOP,TrialCount,WS_EX_CLIENTEDGE);
        start = control(L"BUTTON",L"开始 / Start",BS_DEFPUSHBUTTON | WS_TABSTOP,Start);
        stop = control(L"BUTTON",L"停止 / Stop",BS_PUSHBUTTON | WS_TABSTOP,Stop);
        reports = control(L"BUTTON",L"打开报告 / Reports",BS_PUSHBUTTON | WS_TABSTOP,OpenReports);
        about = control(L"BUTTON",L"关于 / About",BS_PUSHBUTTON | WS_TABSTOP,About);
        status = control(L"STATIC",L"正在检测显卡 / Detecting GPU…",SS_LEFT);
        progress = control(PROGRESS_CLASSW,L"",PBS_MARQUEE);
        table = control(WC_LISTVIEWW,L"",LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,0,WS_EX_CLIENTEDGE);
        ListView_SetExtendedListViewStyle(table,LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        struct Column { const wchar_t* text; int width; } columns[]{
            {L"规模 / N",100},{L"累加 / Accum.",150},{L"TFLOPS",100},{L"范围 / Range",155},
            {L"MHz",70},{L"功耗 / W",85},{L"温度 / °C",85},{L"校验 / Check",105}};
        for (int i = 0; i < int(std::size(columns)); ++i) {
            LVCOLUMNW col{}; col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            col.pszText = const_cast<wchar_t*>(columns[i].text); col.cx = px(columns[i].width); col.iSubItem = i;
            ListView_InsertColumn(table,i,&col);
        }
        log = control(L"EDIT",L"",ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,0,WS_EX_CLIENTEDGE);
        footer = control(L"STATIC",L"按当前频率与负载测量；报告自动保存为 JSON / CSV。\r\nResults reflect current clocks and load. Reports are saved automatically.",SS_LEFT);
        EnableWindow(start,FALSE); EnableWindow(stop,FALSE);
        try {
            devices = fpbench::enumerate_devices();
            int selection = -1;
            for (size_t i = 0; i < devices.size(); ++i) {
                const auto& gpu = devices[i];
                auto text = std::to_wstring(gpu.index) + L": " + fpbench::wide(gpu.name) + L"  ·  " + std::to_wstring(gpu.sms) + L" SM";
                if (!gpu.supported()) text += L"  (此版本不支持 / Unsupported)";
                SendMessageW(device,CB_ADDSTRING,0,LPARAM(text.c_str()));
                if (selection < 0 && gpu.supported()) selection = int(i);
            }
            if (selection >= 0) {
                SendMessageW(device,CB_SETCURSEL,selection,0); EnableWindow(start,TRUE);
                SetWindowTextW(status,L"准备就绪 / Ready");
            } else SetWindowTextW(status,L"此版本需要 NVIDIA SM 8.6 或更新 GPU / NVIDIA SM 8.6+ required");
            append(L"无需安装 Python 或 CUDA Toolkit / No Python or CUDA Toolkit installation required.");
        } catch (const std::exception& error) {
            SetWindowTextW(status,L"无法检测显卡 / GPU detection failed"); append(fpbench::wide(error.what()));
        }
    }
    void layout(int width, int height) {
        auto place = [&](HWND item, int x,int y,int w,int h) { MoveWindow(item,px(x),px(y),px(w),px(h),TRUE); };
        int w = MulDiv(width,96,scale), h = MulDiv(height,96,scale), margin = 22;
        place(title,margin,16,w-44,36); place(subtitle,margin,58,w-44,48);
        place(gpu_label,margin,112,100,22); place(device,margin,138,w-44,250);
        place(size_label,margin,178,295,22); place(size,margin,204,295,220);
        place(trial_label,337,178,150,22); place(trials,337,204,90,30);
        place(start,w-260,200,115,36); place(stop,w-137,200,115,36);
        place(status,margin,248,w-44,24); place(progress,margin,278,w-44,7);
        int table_height = std::max(100,h-490);
        place(table,margin,300,w-44,table_height);
        int log_y = 300 + table_height + 14;
        place(log,margin,log_y,w-44,72);
        place(footer,margin,h-76,w-360,48);
        place(reports,w-320,h-68,180,34); place(about,w-132,h-68,110,34);
    }
    void set_running(bool value) {
        running = value;
        EnableWindow(start,!value); EnableWindow(stop,value); EnableWindow(device,!value);
        EnableWindow(size,!value); EnableWindow(trials,!value);
        SendMessageW(progress,PBM_SETMARQUEE,value,30);
    }
    void begin() {
        if (running) return;
        int index = int(SendMessageW(device,CB_GETCURSEL,0,0));
        if (index < 0 || index >= int(devices.size()) || !devices[index].supported()) return;
        wchar_t text[32]; GetWindowTextW(trials,text,int(std::size(text)));
        wchar_t* end = nullptr; long count = wcstol(text,&end,10);
        if (*end || count < 1 || count > 50) {
            SetWindowTextW(status,L"轮次请输入 1～50 / Enter 1–50 trials"); return;
        }
        fpbench::Config config; config.device = devices[index].index; config.trials = int(count);
        int choice = int(SendMessageW(size,CB_GETCURSEL,0,0));
        if (choice == 1) config.sizes = {4096};
        if (choice == 2) config.sizes = {8192};
        if (choice == 3) config.sizes = {1024};
        try { config.output = output_override.empty() ? fresh_report(default_report_root()) : output_override; }
        catch (const std::exception& error) { append(fpbench::wide(error.what())); return; }
        last_report = config.output;
        ListView_DeleteAllItems(table); SetWindowTextW(log,L"");
        append(L"报告 / Reports: " + last_report.wstring());
        cancellation.store(false); set_running(true);
        SetWindowTextW(status,L"启动测试 / Starting benchmark…");
        if (worker.joinable()) worker.join();
        worker = std::thread([this,config] {
            auto result = fpbench::run(config,cancellation,
                [this](const std::wstring& message) { PostMessageW(window,WM_PROGRESS,0,LPARAM(new std::wstring(message))); },
                [this](const fpbench::Row& row) { PostMessageW(window,WM_ROW,0,LPARAM(new fpbench::Row(row))); });
            PostMessageW(window,WM_FINISHED,0,LPARAM(new fpbench::Report(std::move(result))));
        });
    }
    void cancel() {
        if (!running) return;
        cancellation.store(true); EnableWindow(stop,FALSE);
        SetWindowTextW(status,L"正在结束当前批次并清理 / Finishing current batch and cleaning up…");
    }
    void add_row(const fpbench::Row& row) {
        int i = ListView_GetItemCount(table);
        auto size_text = std::to_wstring(row.n);
        LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = i; item.pszText = size_text.data();
        ListView_InsertItem(table,&item);
        std::wstring cells[]{row.half ? L"FP16" : L"FP32", number(row.median), number(row.minimum) + L" – " + number(row.maximum),
            row.clock < 0 ? L"—" : std::to_wstring(row.clock),number(row.power_w,1),row.temperature < 0 ? L"—" : std::to_wstring(row.temperature),
            row.valid && row.guards ? L"通过 / PASS" : L"失败 / FAIL"};
        for (int column = 1; column <= int(std::size(cells)); ++column)
            ListView_SetItemText(table,i,column,cells[column-1].data());
    }
    void finished(const fpbench::Report& report) {
        if (worker.joinable()) worker.join(); set_running(false);
        if (report.status == "completed") SetWindowTextW(status,L"测试完成，校验通过 / Completed, validation passed");
        else if (report.status == "cancelled") SetWindowTextW(status,L"已停止，资源已清理 / Stopped, resources cleaned up");
        else SetWindowTextW(status,L"测试失败，请查看日志 / Test failed; see log");
        if (!report.error.empty()) append(fpbench::wide(report.error));
        append(L"结果目录 / Result folder: " + report.config.output.wstring());
        if (closing) DestroyWindow(window);
    }
    ~App() {
        cancellation.store(true); if (worker.joinable()) worker.join();
        if (body_font) DeleteObject(body_font); if (title_font) DeleteObject(title_font); DeleteObject(background);
    }
};

LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        app->window = window; SetWindowLongPtrW(window,GWLP_USERDATA,LONG_PTR(app));
    }
    if (!app) return DefWindowProcW(window,message,wp,lp);
    switch (message) {
    case WM_CREATE: app->create(); return 0;
    case WM_SIZE: app->layout(LOWORD(lp),HIWORD(lp)); return 0;
    case WM_GETMINMAXINFO: {
        auto info = reinterpret_cast<MINMAXINFO*>(lp); info->ptMinTrackSize = {app->px(990),app->px(640)}; return 0;
    }
    case WM_CTLCOLORSTATIC: {
        SetBkMode(HDC(wp),TRANSPARENT); SetTextColor(HDC(wp),RGB(35,49,69)); return LRESULT(app->background);
    }
    case WM_ERASEBKGND: { RECT rect; GetClientRect(window,&rect); FillRect(HDC(wp),&rect,app->background); return 1; }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case Start: app->begin(); return 0;
        case Stop: app->cancel(); return 0;
        case OpenReports: {
            auto folder = app->last_report.empty() ? default_report_root() : app->last_report;
            ShellExecuteW(window,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL); return 0;
        }
        case About:
            MessageBoxW(window,(L"DLSS Bench 1.0\r\n\r\nWindows + NVIDIA 驱动即可运行。\r\nOnly Windows and the NVIDIA driver are required.\r\n\r\n运行时不需要 Python、CUDA Toolkit 或 cuBLAS。\r\nNo Python, CUDA Toolkit or cuBLAS runtime is required.\r\n\r\n当前版本：SM 8.6 原生内核；更新 GPU 通过驱动 JIT，尚未实测。\r\nNative SM 8.6 kernels; newer GPUs use driver JIT and remain unvalidated.\r\n\r\nEmbedded GEMM kernels derive from Triton's MIT-licensed tutorial.\r\n\r\n" + fpbench::wide(fpbench::license_text())).c_str(),L"关于 / About",MB_OK | MB_ICONINFORMATION); return 0;
        case DeviceChoice:
            if (HIWORD(wp) == CBN_SELCHANGE && !app->running) {
                int i = int(SendMessageW(app->device,CB_GETCURSEL,0,0));
                EnableWindow(app->start,i >= 0 && i < int(app->devices.size()) && app->devices[i].supported());
            }
            return 0;
        }
        break;
    case WM_PROGRESS: {
        std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lp));
        if (!app->cancellation.load()) SetWindowTextW(app->status,text->c_str());
        app->append(*text); return 0;
    }
    case WM_ROW: { std::unique_ptr<fpbench::Row> row(reinterpret_cast<fpbench::Row*>(lp)); app->add_row(*row); return 0; }
    case WM_FINISHED: { std::unique_ptr<fpbench::Report> report(reinterpret_cast<fpbench::Report*>(lp)); app->finished(*report); return 0; }
    case WM_CLOSE:
        if (app->running) { app->closing = true; app->cancel(); } else DestroyWindow(window); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
    try {
        SetProcessDPIAware(); InitCommonControls();
        int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(),&argc);
        if (!argv) return 1;
        bool headless = false;
        fpbench::Config config;
        for (int i = 1; i < argc; ++i) {
            std::wstring arg = argv[i];
            auto next = [&]() -> std::wstring { if (++i >= argc) throw std::runtime_error("Missing argument"); return argv[i]; };
            if (arg == L"--headless") headless = true;
            else if (arg == L"--output") config.output = next();
            else if (arg == L"--trials") config.trials = std::stoi(next());
            else if (arg == L"--sample-ms") config.sample_ms = std::stod(next());
            else if (arg == L"--warmup-seconds") config.warmup_seconds = std::stod(next());
            else if (arg == L"--device") config.device = std::stoi(next());
            else if (arg == L"--sizes") {
                config.sizes.clear(); std::wistringstream input(next()); std::wstring token;
                while (std::getline(input,token,L',')) config.sizes.push_back(std::stoi(token));
                if (config.sizes.empty()) throw std::runtime_error("At least one size is required");
            } else throw std::runtime_error("Unknown argument: " + fpbench::utf8(arg));
        }
        LocalFree(argv);
        if (headless) {
            if (config.output.empty()) config.output = fresh_report(default_report_root());
            std::atomic<bool> cancellation{false};
            auto report = fpbench::run(config,cancellation);
            return report.status == "completed" ? 0 : 1;
        }
        App app; app.output_override = config.output;
        HDC screen = GetDC(nullptr); app.scale = GetDeviceCaps(screen,LOGPIXELSY); ReleaseDC(nullptr,screen);
        WNDCLASSEXW cls{}; cls.cbSize = sizeof(cls); cls.hInstance = instance; cls.lpfnWndProc = window_proc;
        cls.lpszClassName = L"DLSSBenchNativeWindow"; cls.hCursor = LoadCursorW(nullptr,IDC_ARROW);
        cls.hIcon = LoadIconW(nullptr,IDI_APPLICATION); cls.hbrBackground = app.background;
        if (!RegisterClassExW(&cls)) throw std::runtime_error("Unable to register window class");
        RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
        int width = std::min<int>(app.px(1040),work.right-work.left-20);
        int height = std::min<int>(app.px(680),work.bottom-work.top-20);
        HWND window = CreateWindowExW(0,cls.lpszClassName,kTitle,WS_OVERLAPPEDWINDOW,
            work.left+(work.right-work.left-width)/2,work.top+(work.bottom-work.top-height)/2,
            width,height,nullptr,nullptr,instance,&app);
        if (!window) throw std::runtime_error("Unable to create application window");
        ShowWindow(window,show); UpdateWindow(window);
        MSG message;
        while (GetMessageW(&message,nullptr,0,0) > 0) {
            if (!IsDialogMessageW(window,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
        }
        return 0;
    } catch (const std::exception& error) {
        MessageBoxW(nullptr,fpbench::wide(error.what()).c_str(),L"DLSS Bench",MB_OK | MB_ICONERROR); return 1;
    }
}
