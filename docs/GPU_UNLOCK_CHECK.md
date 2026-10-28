# 是否需要 GPU unlock？ / Do I need GPU unlock?

DLSS Bench 的用途是在尝试 GPU unlock 前，测量显卡当前的实际 Tensor Core 吞吐，判断相应的吞吐限制是否仍可能影响性能。这里的 unlock 指解除特定 Tensor 指令的速率限制；本工具执行基准测试，不执行解锁。

DLSS Bench measures current Tensor Core throughput before you attempt GPU unlock, helping you decide whether the relevant rate limit may still affect performance. Here, unlock means removing a particular Tensor instruction throughput cap. The app runs benchmarks, not an unlock procedure.

## 如何判断 / How to decide

1. 在确认本次启动没有执行过 unlock 的情况下测试，包括启动时自动运行的 UEFI 解锁工具。关闭其他 GPU 工作，使用默认的 4096 + 8192、每项 7 轮，确认检查通过，并查看逐轮范围和频率。
2. 按具体型号、实际指令、累加精度和频率，取得对应的受限理论上限。比较相同的稠密 GEMM 口径，乘加计为两次运算；不要使用稀疏宣传峰值或普通 CUDA Core FP32 峰值。
3. 如果有效的持续吞吐明显高于保守估算的受限上限，就说明这项限制无法解释当前性能，无需为了解除这项限制而解锁。
4. 如果结果低于或接近上限，结论是“尚不能判断”。先排查频率、功耗、温度、后台负载、矩阵大小和内核效率；低分本身不是需要 unlock 的证明。

Run the default 4096 + 8192 test with seven trials per configuration in a boot where no unlock tool has run, including any automatic UEFI unlock step. Stop other GPU work and check validation, trial ranges and clocks. Compare with a conservative capped ceiling for the exact GPU, instruction, accumulation precision and clock range, using dense GEMM and two operations per multiply-add. Validated, sustained throughput clearly above that ceiling means this particular cap is not limiting the measured path, so unlocking it is unnecessary. A low or borderline score remains inconclusive; investigate other performance limits first.

两项测试均使用 FP16 输入与输出，但累加精度不同。内置 SM 8.6 内核已核对为 `HMMA.16816.F16` 和 `HMMA.16816.F32`。**FP16 累加达到满速，不能证明 FP32 累加也没有限制。** 两项分数接近也不能单独证明没有限制，因为它们都可能受其他瓶颈影响。

Both tests use FP16 inputs and outputs, with different accumulator precisions. The embedded SM 8.6 kernels were verified as `HMMA.16816.F16` and `HMMA.16816.F32`. **Full-rate FP16 accumulation does not establish unrestricted FP32 accumulation.** Similar scores alone are also insufficient because another bottleneck can slow both modes.

## RTX 3090 计算示例 / RTX 3090 worked example

以下公式限定于此处讨论的 GA10x / RTX 30 系稠密 FP16 Tensor 路径。其他架构、精度及指令应另行核对，不能直接套用。

The following formulas apply to the GA10x / RTX 30-series dense FP16 Tensor path discussed here. Check other architectures, precisions and instructions separately.

```text
Full-rate dense FP16 accumulation TFLOPS = SM count * 1024 * clock_MHz / 1,000,000
Usual capped FP32 accumulation TFLOPS   = SM count *  512 * clock_MHz / 1,000,000
```

RTX 3090 有 82 个 SM。以 1740 MHz 为参考：

An RTX 3090 has 82 SMs. Using 1740 MHz as a reference:

| 项目 / Quantity | TFLOPS |
| --- | ---: |
| 满速 FP16 累加理论峰值 / Full-rate FP16-accumulation theoretical peak | 146.10 |
| 常规半速 FP32 累加理论峰值 / Usual half-rate FP32-accumulation theoretical peak | 73.05 |
| 截图 N=8192，FP16 累加中位数 / Screenshot FP16-accumulation median | 119.00 |
| 截图 N=8192，FP32 累加中位数 / Screenshot FP32-accumulation median | 124.02 |

![RTX 3090 跑分截图 / RTX 3090 benchmark screenshot](images/dlss-bench.png)

截图来自 2026-10-28 的一次 GUI 验证运行，两个 N=8192 配置均显示 1740 MHz 采样频率。它展示了明显高于该参考频率下半速水平的 FP32 累加吞吐。截图仅裁去下方日志及本地报告路径，未修改结果；没有同条件的未解锁对照，因此不能据此计算 unlock 提升比例，也不能证明下次启动可以省略解锁。

The screenshot is from one GUI validation run on 2026-10-28. Both N=8192 rows show a sampled clock of 1740 MHz, with FP32-accumulation throughput well above the half-rate level at that reference clock. Only the lower log and local report path were cropped; results were not edited. There is no matched pre-unlock run, so the screenshot does not establish an unlock speedup or prove that unlock can be skipped on a later boot.

**正式判断时，频率应使用该轮运行可信的 SM 时钟上界，并为测量误差留出余量。** 程序的 MHz 是每个计时批次结束后的 NVML 采样汇总，存在传感器刷新延迟，不是整个计时段的最高频率。若只有这个采样且分数接近阈值，就不能下结论。超频也会提高受限上限，不能把超过标称 Boost 对应的数值直接视为无锁。

**For a decision, use a credible upper bound on SM clocks during the run and allow margin for measurement error.** The displayed MHz summarizes NVML samples taken after timed batches; sensor refresh latency means it is not a maximum-clock measurement over the entire interval. A borderline result with only these samples is inconclusive. Overclocking raises the capped ceiling too, so exceeding a reference-boost figure alone does not establish unrestricted throughput.

## 判断适用范围 / Scope of the conclusion

“无需解锁”仅针对本次启动、已测指令路径的这项吞吐限制。是否提升实际应用，仍要测试目标应用；本工具没有测量 BF16、TF32 或所有其他精度，也不直接读取硬件锁定状态。若测试前已经运行过 unlock，高分只能反映当前会话，应在确认未执行解锁的启动中复测，才能决定是否省略它。

“No unlock needed” applies to this throughput cap, tested instruction path and boot. Application gains require testing the actual application. This tool does not measure BF16, TF32 or every other precision, and it does not directly read hardware lock status. If unlock has already run, a high score describes the current session; repeat in a boot without an unlock step before deciding to skip it.

## 参考 / References

- [NVIDIA Ampere GA102 architecture whitepaper](https://www.nvidia.com/content/PDF/nvidia-ampere-ga-102-gpu-architecture-whitepaper-v2.pdf), page 25, Table 6: dense FP16-accumulation throughput and its distinction from sparse throughput.
- [Zagitta/gpu-unlock](https://github.com/Zagitta/gpu-unlock): upstream description of the FP32-accumulation rate limit and unlock mechanism.
- [Benchmark kernel](../kernels/fp16_native_kernel.py) and [generated manifest](../kernels/generated/manifest.json): accumulation variants and the checked SM 8.6 HMMA instructions.
