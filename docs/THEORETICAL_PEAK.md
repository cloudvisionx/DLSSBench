# 理论峰值的来源与 RTX 30 / 40 系覆盖 / Peak sources and RTX 30 / 40 coverage

**1.1 已包含 SM 8.6 和 SM 8.9 的峰值模型，覆盖采用这两种架构的 RTX 30 / 40 系显卡。** 程序按架构和实际硬件参数计算，不依赖逐型号白名单，也不读取网上跑分。模型覆盖不等于所有型号均已真机验证；目前实际运行验证的是 RTX 3090。

**Version 1.1 already contains the SM 8.6 and SM 8.9 peak models used by RTX 30 / 40-series GPUs.** Calculation uses architecture and actual device parameters, not a model-name whitelist or online benchmark scores. Model coverage does not mean every GPU has been tested; execution has been validated on RTX 3090.

## 三项输入的来源 / Where the three inputs come from

| 输入 / Input | 来源 / Source | 程序中的使用 / Usage |
| --- | --- | --- |
| 每 SM 每周期满速稠密 FP16 吞吐 / Full-rate dense FP16 FLOPs per SM per cycle | NVIDIA Ampere GA102、Ada 架构白皮书 / NVIDIA architecture whitepapers | SM 8.6 / 8.9 均为 1024 FLOPs；乘加计两次运算 / 1024 FLOPs, counting multiply-add as two operations |
| 实际 SM 数、计算能力 / Actual SM count and compute capability | CUDA Driver API `cuDeviceGetAttribute` | 查询 `MULTIPROCESSOR_COUNT` 和 `COMPUTE_CAPABILITY_MAJOR/MINOR`，使用实际启用的 SM / Uses enabled SMs reported by the driver |
| 运行时 SM 频率 / Runtime SM clock | NVIDIA 驱动的 NVML `nvmlDeviceGetClockInfo` | 每个计时批次结束后采样 SM 时钟，按各项采样中位数计算 / Uses the median post-batch SM-clock sample for each result row |

```text
Reference full-rate dense FP16 TFLOPS = SM count * 1024 * clock_MHz / 1,000,000
Reference half-rate TFLOPS           = full-rate reference / 2
Measured percentage                 = 100 * measured TFLOPS / full-rate reference
```

FP16 累加和 FP32 累加两行使用同一个满速稠密 FP16 分母。这里的 1024 是架构吞吐常数，SM 数和频率由本机驱动读取，所有计算均可离线完成。

Both accumulator modes use the same full-rate dense FP16 denominator. The value 1024 is an architecture throughput constant; SM count and clocks come from the local driver. Calculation works offline.

## 为什么是 1024 / Why 1024?

**RTX 30 系：** [Ampere GA102 白皮书](https://www.nvidia.com/content/PDF/nvidia-ampere-ga-102-gpu-architecture-whitepaper-v2.pdf)印刷页 25、表 6 给出 GA10x 每 SM 有 4 个 Tensor Core，每个每周期可完成 128 次稠密 FP16 FMA。因此 `4 × 128 × 2 = 1024 FLOPs/SM/周期`。同页以 RTX 3080 的 68 SM、1710 MHz 得出约 119 TFLOPS，明确为 FP16 累加；稀疏数值约为其两倍。

**RTX 30 series:** The Ampere GA102 whitepaper, printed page 25, Table 6, gives four Tensor Cores per GA10x SM and 128 dense FP16 FMAs per Tensor Core per cycle: `4 * 128 * 2 = 1024 FLOPs/SM/cycle`. The same page cross-checks about 119 TFLOPS for RTX 3080 at 68 SMs / 1710 MHz, explicitly with FP16 accumulation. Sparse throughput is a separate, approximately doubled figure.

**RTX 40 系：** [Ada 白皮书](https://images.nvidia.com/aem-dam/Solutions/geforce/ada/nvidia-ada-gpu-architecture.pdf)印刷页 29–31、表 2 列出 RTX 4090 的 128 SM、2520 MHz，以及约 330.3 TFLOPS 的稠密 FP16 累加峰值。`128 × 1024 × 2520 / 1000000 = 330.30144`，与官方表一致。表中约 165.2 TFLOPS 的 FP32 累加常规峰值是前者的一半。

**RTX 40 series:** The Ada whitepaper, printed pages 29–31, Table 2, lists 128 SMs, 2520 MHz and about 330.3 dense FP16-accumulation TFLOPS for RTX 4090. `128 * 1024 * 2520 / 1000000 = 330.30144`, matching the table. Its usual FP32-accumulation figure of about 165.2 TFLOPS is half that rate.

这两个数值均为稠密 Tensor Core FP16 口径。不能把 AI TOPS、FP8、结构化稀疏峰值或普通 CUDA Core FP32 峰值当成这里的分母。

These are dense Tensor Core FP16 figures. AI TOPS, FP8, structured-sparse peaks and ordinary CUDA Core FP32 peaks use different definitions and are not the denominator here.

## 30 / 40 系如何覆盖 / How RTX 30 / 40 models are covered

[NVIDIA CUDA GPU 列表](https://developer.nvidia.com/cuda/gpus)将常规 GeForce RTX 30 系列为计算能力 8.6，RTX 40 系列为 8.9。最终以驱动查询到的架构为准，型号名称不参与选择峰值系数；型号别名或换芯版本也遵循相同规则。

[NVIDIA's CUDA GPU list](https://developer.nvidia.com/cuda/gpus) identifies the usual GeForce RTX 30 series as compute capability 8.6 and RTX 40 series as 8.9. The detected architecture is authoritative; the product name does not choose the coefficient. Renamed or alternative-chip variants follow the same rule.

| 产品范围 / Product family | 峰值模型处理 / Peak-model handling |
| --- | --- |
| RTX 30 桌面：3050、3060、3060 Ti、3070、3070 Ti、3080、3080 Ti、3090、3090 Ti / RTX 30 desktop | 按实际架构、SM 数和频率计算 / Calculated from actual architecture, SM count and clocks |
| RTX 40 桌面：4060、4060 Ti、4070、4070 SUPER、4070 Ti、4070 Ti SUPER、4080、4080 SUPER、4090 / RTX 40 desktop | 同上；Ti / SUPER 不需要单独的峰值公式 / Same; Ti / SUPER do not require a separate peak formula |
| 上述系列的显存变体、OEM、OC、区域型号 / Memory, OEM, OC and regional variants | 同上；不套用同名其他卡的 SM 数 / Same; does not reuse another card's SM count just because names match |
| RTX 30 / 40 Laptop，包括 3050 Ti、4050 等移动型号 / RTX 30 / 40 Laptop, including mobile-only tiers | 同上；使用笔记本 GPU 实际 SM 数和采样频率，不使用桌面版参数 / Same; uses laptop-device parameters rather than desktop specifications |

例如，3080 10GB / 12GB 的 SM 数不同，4060 Ti 8GB / 16GB 的 SM 数相同，笔记本版与桌面版的 SM 数及频率可能均不同。直接读取硬件参数可处理这些区别，而无需维护每个显卡商品名的固定 TFLOPS 表。

For example, RTX 3080 10GB and 12GB have different SM counts, while RTX 4060 Ti 8GB and 16GB have the same SM count. Laptop and desktop versions may differ in both SM count and clocks. Reading hardware parameters handles these differences without a fixed TFLOPS entry for every product name.

规格测试中的桌面型号 SM 数，可由上述官方规格表的 CUDA Core 数除以 128 核对；这些 GA10x / Ada 型号每 SM 有 128 个 CUDA Core。程序运行时直接查询 SM 数，无需做此换算。

For the desktop specification tests, SM counts can be cross-checked by dividing the official CUDA Core counts by 128, the CUDA Cores per SM on these GA10x / Ada models. The running app queries SM counts directly and does not need this conversion.

## 给定频率的计算示例 / Calculated examples at specified clocks

以下是按表中指定频率计算的示例，不是本项目的实测，也不是每张同名显卡的固定频率。SM 数可在 [RTX 30](https://www.nvidia.com/en-us/geforce/graphics-cards/30-series/) / [RTX 40](https://www.nvidia.com/en-us/geforce/graphics-cards/40-series/) 官方规格中核对；其中 3080 和 4090 两行同时复核上述白皮书的参考峰值。程序使用测试时采样频率，因此结果可能与此表不同。

These examples use the specified clock inputs; they are not project measurements or fixed clocks for every card bearing the same name. SM counts can be checked against NVIDIA's RTX 30 / 40 specifications. The RTX 3080 and 4090 rows also reproduce the whitepaper reference peaks above. The app uses sampled clocks during your test, so its reference may differ.

| GPU | SM | 示例 MHz / Example MHz | 满速稠密 FP16 TFLOPS / Full rate | 一半 / Half |
| --- | ---: | ---: | ---: | ---: |
| RTX 3060 12GB | 28 | 1777 | 50.95 | 25.48 |
| RTX 3070 | 46 | 1725 | 81.25 | 40.63 |
| RTX 3080 10GB | 68 | 1710 | 119.07 | 59.54 |
| RTX 3090 | 82 | 1695 | 142.33 | 71.16 |
| RTX 4060 | 24 | 2460 | 60.46 | 30.23 |
| RTX 4070 | 46 | 2475 | 116.58 | 58.29 |
| RTX 4080 | 76 | 2505 | 194.95 | 97.47 |
| RTX 4090 | 128 | 2520 | 330.30 | 165.15 |

一些产品网页将 Boost 四舍五入显示为 GHz，不应据此推断精确到 MHz 的规格。此表不是程序内置的阈值表。具体卡的 OC、功耗及运行时钟应以本机为准。

Some product pages round boost clocks to GHz; those displays do not establish an exact MHz specification. This is not an embedded threshold table. Local OC settings, power limits and actual clocks remain device-specific.

## 能据此判断什么 / What this comparison establishes

峰值模型覆盖 RTX 30 / 40；实际运行还要求兼容的 NVIDIA 驱动、足够显存和可加载的内核。1.1 含原生 SM 8.6 cubin，SM 8.9 由驱动 JIT 内嵌 PTX；当前仅实测 RTX 3090，不能把公式覆盖说成已经验收全部型号。

The model covers RTX 30 / 40, while execution also needs a compatible NVIDIA driver, sufficient VRAM and loadable kernels. Version 1.1 embeds native SM 8.6 cubins and uses driver-JIT PTX on SM 8.9. Only RTX 3090 has been tested; formula coverage is not a claim that every model has passed hardware validation.

采样频率不是整个计时段的频率上界。超过 50% 表示高于该采样频率的半速参考；接近阈值时，需结合频率范围和波动判断。若要决定省略 unlock，应在本次启动没有执行解锁的前提下测试。完整解释见 [GPU unlock 判断指南](GPU_UNLOCK_CHECK.md)。

Sampled clocks are not an upper bound over the entire timed interval. Above 50% means above the half-rate reference at those sampled clocks; borderline results require examining clock ranges and variation. Test in a boot with no unlock step before deciding to skip unlock. See the [decision guide](GPU_UNLOCK_CHECK.md).

## 可核对的入口 / Verification links

- [CUDA Driver API: cuDeviceGetAttribute](https://docs.nvidia.com/cuda/cuda-driver-api/cuda_driver_api/group__CUDA__DEVICE.html)
- [NVML: nvmlDeviceGetClockInfo](https://docs.nvidia.com/deploy/nvml-api/latest/api/group__nvmlDeviceQueries.html)
- [Runtime device/clock queries](../src/benchmark.cpp), [peak calculation](../src/performance.h), [specification and boundary checks](../tests/performance_test.cpp)
- [Reviewed source URLs and SHA-256 metadata](data/theoretical-peak-sources.json)
