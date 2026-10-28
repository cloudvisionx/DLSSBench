# 从源码构建 / Build from source

运行成品只需要 Windows x64 和兼容的 NVIDIA 驱动。以下依赖仅用于开发机。

Running the app requires only Windows x64 and a compatible NVIDIA driver. The following dependencies are for developers.

## 普通构建 / Normal build

- Windows x64。
- Visual Studio 2022 或 Build Tools，安装 C++ x64 工具链和 Windows SDK。
- NVIDIA CUDA 开发头文件 `cuda.h`。安装 CUDA Toolkit 后通常由 `CUDA_PATH` 指向其目录，也可用 `-CudaInclude` 明确指定已有头文件目录。

```powershell
git clone https://github.com/cloudvisionx/DLSSBench.git
Set-Location DLSSBench
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build.ps1
```

也可以指定头文件目录，例如：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build.ps1 -CudaInclude 'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8\include'
```

输出：`build\Release\DLSSBench.exe`。示例 CUDA 版本路径应替换为实际安装路径。

仓库自带经过验证的 `kernels\generated\embedded_kernels.h`，普通构建无需 Python、Triton 或 GPU 内核编译。脚本核对内核源码、生成头文件和 manifest 的 SHA-256。通过 `/MT` 静态链接 C++ 运行时，动态调用系统驱动中的 CUDA Driver API，不链接 CUDA Runtime 或 cuBLAS。

Install the MSVC x64 tools, Windows SDK and CUDA development headers. Build using the command above; `-CudaInclude` overrides `CUDA_PATH`. The checked-in generated header contains the validated GPU kernels, so a normal C++ build needs no Python or Triton. The resulting EXE statically links its C++ runtime and dynamically loads the NVIDIA driver.

## 可选：重建 GPU 内核 / Optional kernel rebuild

仅修改 GPU 内核时需要。参考构建环境为 Python 3.10、`triton-windows 3.6.0+git963e615e`，以及 CUDA `nvdisasm`。其他编译器版本可能生成不同指令或 ABI；脚本会检查真实 HMMA 累加精度、五个驱动参数和两个零大小 scratch 参数。

```powershell
python tools\build_kernels.py --output build\kernels-rebuilt --nvdisasm 'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8\bin\nvdisasm.exe'
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build.ps1 -KernelDir build\kernels-rebuilt
```

输出目录必须尚不存在。编译和反汇编不执行 GPU 工作。生成目录包含 cubin、PTX、SASS、Triton IR、头文件和 manifest。

Only kernel developers need Python, Triton and `nvdisasm`. The reference compiler is `triton-windows 3.6.0+git963e615e`. Use a fresh output directory, then pass it to `build.ps1 -KernelDir`. Compilation and disassembly do not execute GPU work. Compiler changes may require a launcher ABI update.

## 验证 / Verification

普通 GUI 操作：双击生成的 EXE，点击 Start，查看数值检查和 Reports。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\test_gui.ps1
```

GUI 验收脚本会执行实际 GPU 测试，检查完成、计时期间取消、资源清理和加载的模块，并保存界面截图。它要求当前 Windows 桌面可交互、有支持的 NVIDIA GPU 和驱动。输出在 `build\qa` 的新目录。该脚本使用 Windows 自带的 PowerShell / .NET，不随成品运行。

The GUI QA script runs actual GPU work, checks completion/cancellation/cleanup and module dependencies, and captures the window. It requires an interactive Windows desktop and supported NVIDIA GPU/driver. Evidence is saved under `build\qa`.

## 源码与发布版 / Source and release

`src` 与 `kernels\fp16_native_kernel.py` 对应已发布的 1.0 预览版实现。导出时保留源码字节和已验证的内核头文件；生成 manifest 记录对应哈希。构建入口已改为独立仓库路径。

当前构建脚本不生成 PDB；既有 1.0 EXE 使用开发构建的调试链接参数。MSVC 版本、链接参数、时间戳和文件路径可能使重新构建的 EXE 哈希不同，因此不宣称逐字节可重复构建。

The app and kernel sources correspond to the published 1.0 implementation. Exported source bytes and the validated generated kernel header are preserved; `.gitattributes` prevents line-ending conversion from invalidating their manifest hashes. The standalone build omits PDB generation; the original release used debug link flags. Toolchain, linker settings, timestamps and paths can change the EXE hash, so this is not a claim of byte-for-byte reproducible builds.

Triton 的 PTX / cubin 含源码路径等调试元数据，移动目录后重建可能改变文件哈希。此次独立目录重建已检查八组真实 SASS 指令，与原始构建一致；文件哈希不作为跨目录重建的相等条件。

Triton PTX/cubin can contain source-path debug metadata, so rebuilding in another directory can change their hashes. All eight rebuilt kernels were checked against the original SASS instructions. File hashes alone are not used as a cross-directory rebuild equality criterion.
