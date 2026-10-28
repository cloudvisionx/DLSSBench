"""AOT dense GEMM: Triton tutorial 03 grouped tiling, explicit accumulator type.

Derived from Triton's MIT-licensed matrix multiplication tutorial. Build-time
dependency only; the standalone app embeds cubin/PTX and uses the driver API.
"""
import triton
import triton.language as tl


@triton.jit
def fp16_native_gemm(A, B, C, N: tl.constexpr, HALF_ACC: tl.constexpr,
                     BM: tl.constexpr, BN: tl.constexpr, BK: tl.constexpr):
    pid = tl.program_id(0)
    tiles_m = N // BM
    tiles_n = N // BN
    group_width = 8 * tiles_n
    group = pid // group_width
    first_m = group * 8
    group_m = tl.minimum(tiles_m - first_m, 8)
    pm = first_m + (pid % group_width) % group_m
    pn = (pid % group_width) // group_m
    m = pm * BM + tl.arange(0, BM)
    n = pn * BN + tl.arange(0, BN)
    k = tl.arange(0, BK)
    ap = A + m[:, None] * N + k[None, :]
    bp = B + k[:, None] * N + n[None, :]
    if HALF_ACC:
        acc = tl.full((BM, BN), 0, tl.float16)
    else:
        acc = tl.full((BM, BN), 0, tl.float32)
    for _ in range(N // BK):
        a = tl.load(ap)
        b = tl.load(bp)
        if HALF_ACC:
            acc = tl.dot(a, b, acc, out_dtype=tl.float16)
        else:
            acc = tl.dot(a, b, acc, out_dtype=tl.float32)
        ap += BK
        bp += BK * N
    tl.store(C + m[:, None] * N + n[None, :], acc.to(tl.float16))
