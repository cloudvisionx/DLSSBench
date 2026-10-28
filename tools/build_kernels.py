"""Optionally rebuild and inspect the embedded SM86 GEMM kernels; no GPU work."""
import argparse
from dataclasses import asdict
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='A fresh output directory')
    parser.add_argument('--nvdisasm', type=Path, help='CUDA nvdisasm executable; otherwise use PATH or CUDA_PATH')
    args = parser.parse_args()
    disassembler = args.nvdisasm or shutil.which('nvdisasm')
    if not disassembler and os.environ.get('CUDA_PATH'):
        disassembler = Path(os.environ['CUDA_PATH']) / 'bin/nvdisasm.exe'
    if not disassembler or not Path(disassembler).is_file():
        parser.error('Install developer CUDA tools and pass --nvdisasm or add nvdisasm to PATH')
    disassembler = Path(disassembler).resolve()
    os.environ.setdefault('TRITON_HOME', str(ROOT / 'build/triton-home'))
    os.environ.setdefault('TRITON_CACHE_DIR', str(ROOT / 'build/triton-cache'))
    sys.path.insert(0, str(ROOT / 'kernels'))
    import triton
    from triton.backends.compiler import GPUTarget
    from triton.compiler import ASTSource
    import fp16_native_kernel as source

    license_text = (ROOT / 'third_party/triton/LICENSE').read_text(encoding='utf-8')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    source_hash = sha(Path(source.__file__).read_bytes())
    records, declarations, entries = [], [], []
    for n in (256, 1024, 4096, 8192):
        for half in (True, False):
            label = f'n{n}_' + ('f16' if half else 'f32')
            folder = output / label
            folder.mkdir()
            compiled = triton.compile(ASTSource(source.fp16_native_gemm,
                signature={'A': '*fp16', 'B': '*fp16', 'C': '*fp16'},
                constexprs={'N': n, 'HALF_ACC': half, 'BM': 128, 'BN': 128, 'BK': 32},
                attrs={(i,): [['tt.divisibility', 16]] for i in range(3)}),
                target=GPUTarget('cuda', 86, 32), options={'num_warps': 4, 'num_stages': 3})
            for kind in ('ptx', 'cubin', 'ttir', 'ttgir'):
                raw = compiled.asm[kind]
                (folder / ('kernel.' + kind)).write_bytes(raw if isinstance(raw, bytes) else raw.encode())
            inspected = subprocess.run([str(disassembler), '--print-code', str(folder / 'kernel.cubin')],
                capture_output=True, check=True, timeout=45,
                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            (folder / 'kernel.sass.txt').write_bytes(inspected.stdout)
            opcodes = sorted(set(re.findall(rb'\bHMMA\.[A-Z0-9_.]+', inspected.stdout)))
            suffix = b'.F16' if half else b'.F32'
            if not opcodes or not all(op.endswith(suffix) for op in opcodes):
                raise RuntimeError(f'Unexpected accumulation precision: {label}, {opcodes}')
            binary, ptx = compiled.asm['cubin'], compiled.asm['ptx']
            metadata = compiled.metadata._asdict()
            metadata['target'] = asdict(metadata['target'])
            metadata['extern_libs'] = [[name, Path(path).name] for name, path in metadata.get('extern_libs', [])]
            count = len(re.findall(r'^\s*\.param\s+\.u64', ptx, re.M))
            if count != 5 or metadata['global_scratch_size'] or metadata['profile_scratch_size']:
                raise RuntimeError(f'Native launcher ABI mismatch: {label}, {count} arguments')
            records.append({'label': label, 'n': n, 'half_acc': half, 'metadata': metadata,
                'driver_argument_count': count, 'zero_scratch_pointers': 2,
                'mma_opcodes': [op.decode() for op in opcodes], 'cubin_sha256': sha(binary),
                'ptx_sha256': sha(ptx.encode()), 'grid': (n // 128) ** 2})
            data = ','.join(str(b) for b in binary)
            ptx_data = ','.join(str(b) for b in ptx.encode() + b'\0')
            declarations.append(f'static const unsigned char {label}_cubin[] = {{{data}}};\n'
                                f'static const char {label}_ptx[] = {{{ptx_data}}};\n')
            entries.append(f'{{{n}, {str(half).lower()}, {label}_cubin, sizeof({label}_cubin), '
                           f'{label}_ptx, "{compiled.name}", {metadata["shared"]}, 128, "{sha(binary)}"}}')
            print(label, 'shared', metadata['shared'], 'MMA', [op.decode() for op in opcodes], flush=True)
    header = '#pragma once\n#include <cstddef>\n'
    header += ('struct EmbeddedKernel { int n; bool half_acc; const unsigned char* cubin; '
               'size_t bytes; const char* ptx; const char* entry; unsigned shared; '
               'unsigned threads; const char* sha256; };\n')
    header += ''.join(declarations)
    header += 'static const EmbeddedKernel kKernels[] = {\n' + ',\n'.join(entries) + '\n};\n'
    header += f'static const char kKernelSourceSha256[] = "{source_hash}";\n'
    header += 'static const char kKernelLicense[] = R"LICENSE(' + license_text + ')LICENSE";\n'
    header_path = output / 'embedded_kernels.h'
    header_path.write_text(header, encoding='utf-8')
    compiler = getattr(triton, '__version__', 'unknown')
    for distribution in ('triton-windows', 'triton'):
        try:
            compiler = importlib.metadata.version(distribution)
            break
        except importlib.metadata.PackageNotFoundError:
            pass
    manifest = {'passed': True, 'gpu_work_executed': False,
        'target': 'sm_86 cubin plus forward-compatible PTX', 'compiler': compiler,
        'kernel_source_sha256': source_hash, 'embedded_header_sha256': sha(header_path.read_bytes()),
        'tutorial_url': 'https://github.com/triton-lang/triton/blob/main/python/tutorials/03-matrix-multiplication.py',
        'records': records, 'runtime_dependencies': 'Windows and NVIDIA driver only'}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
