# DLSS Bench

Windows 原生 NVIDIA FP16 Tensor Core 算力测试工具。用户只需 Windows x64 和兼容的 NVIDIA 驱动。

A native Windows benchmark for NVIDIA FP16 Tensor Core throughput. Users only need Windows x64 and a compatible NVIDIA driver.

[下载 Windows 预览版 / Download Windows preview](https://github.com/cloudvisionx/DLSSBench/releases/tag/v1.0.0)

## 下载与运行 / Download and run

1. 打开本仓库的 **Releases** 页面，下载 `DLSSBench-1.0-win64.zip`。
2. 解压后双击 `DLSSBench.exe`，选择 GPU，点击 **开始 / Start**。
3. 点击 **Reports** 查看自动保存的 JSON / CSV。

Download `DLSSBench-1.0-win64.zip` from **Releases**, extract it, and run `DLSSBench.exe`. Select a GPU and click **Start**. **Reports** opens the saved JSON and CSV files.

无需安装 Python、CUDA Toolkit、cuBLAS、Qt 或 VC++ Redistributable。程序内置计算内核与 C++ 运行时代码。

No separate Python, CUDA Toolkit, cuBLAS, Qt or Visual C++ Redistributable installation is required. GPU kernels and the C++ runtime are embedded.

## 测量内容 / What it measures

- 稠密矩阵乘法 `C = A × B`，FP16 输入与输出，分别使用 FP16 和 FP32 累加。
- 原生中英文界面，可选择显卡、矩阵规模和测量轮次。
- 显示 TFLOPS、逐轮范围及可用的驱动传感器数据。
- 输出位置抽查、内存保护区检查，以及可取消的后台测试。

Dense FP16-input/output GEMM with separate FP16 and FP32 accumulation. The bilingual GUI supports GPU/size/trial selection, cancellation, sampled numerical checks, memory guards and JSON/CSV export.

名称为 DLSS Bench；当前版本测量矩阵计算吞吐，不测量 DLSS 游戏帧率或图像质量。

The current version measures matrix-compute throughput. It does not benchmark DLSS game frame rates or image quality.

## 兼容性与结果解释 / Compatibility and interpretation

已在 RTX 3090（SM 8.6）、NVIDIA 驱动 616.56 上验证。当前构建要求 SM 8.6 或更新架构；其他架构通过驱动 JIT 执行内嵌 PTX，尚未实测。较旧驱动可能不支持内嵌 PTX 8.7。

Tested on RTX 3090 / SM 8.6 with driver 616.56. This build requires SM 8.6 or newer; driver-JIT execution on other architectures is untested. Older drivers may not support the embedded PTX 8.7.

TFLOPS 来自固定 GEMM 内核，不代表所有算法中的硬件绝对峰值。当前没有同条件的解锁前基线，不能单独据此推导解锁提升比例。数值检查为抽查，不是全矩阵稳定性验证。

Results come from a fixed GEMM backend, not an exhaustive hardware peak search. No matched pre-unlock baseline is available; these results alone do not establish an unlock speedup. Numerical checks are sampled rather than exhaustive.

## 第三方许可 / Third-party notices

内嵌 GEMM 内核参考 Triton 的 MIT 许可矩阵乘法教程；许可保留在 [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt)、发布 ZIP 和程序 About 中。

The embedded GEMM kernels derive from Triton's MIT-licensed matrix multiplication tutorial. Notices are included in this repository, the release ZIP and the app's About dialog.

## 源码与构建 / Source and build

- `src`: Win32 GUI, CUDA Driver API backend, timing, validation and report export.
- `kernels/fp16_native_kernel.py`: FP16-input GEMM with FP16 / FP32 accumulation.
- `kernels/generated`: validated embedded GPU kernels and their manifest.
- `tools/build.ps1`: standalone MSVC build; `tools/build_kernels.py`: optional GPU kernel rebuild.

开发机需要 MSVC C++ 工具、Windows SDK 和 CUDA 开发头文件。普通构建使用仓库内的预编译内核，无需 Python 或 Triton。运行成品仍只需 Windows 和 NVIDIA 驱动。

Developers need MSVC, a Windows SDK and CUDA development headers. Normal builds use the checked-in kernels and need no Python or Triton. End users still need only Windows and the NVIDIA driver.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build.ps1
```

[完整构建与验证说明 / Build and verification guide](docs/BUILD.md)

应用源码目前未另行指定项目级许可证；第三方部分保留其原有 MIT 许可。

No project-wide license has been selected for the application source; third-party components retain their original MIT license.
