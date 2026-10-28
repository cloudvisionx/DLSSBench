param(
    [string]$CudaInclude = '',
    [string]$KernelDir = 'kernels\generated',
    [string]$OutDir = 'build\Release'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
function Repo-Path([string]$value) {
    if ([IO.Path]::IsPathRooted($value)) { return [IO.Path]::GetFullPath($value) }
    return [IO.Path]::GetFullPath((Join-Path $repoRoot $value))
}
$kernelPath = Repo-Path $KernelDir
$outputPath = Repo-Path $OutDir
if ([string]::IsNullOrEmpty($CudaInclude) -and $env:CUDA_PATH) {
    $CudaInclude = Join-Path $env:CUDA_PATH 'include'
}
if ([string]::IsNullOrEmpty($CudaInclude)) {
    throw 'Developer CUDA headers missing. Set CUDA_PATH or pass -CudaInclude to the directory containing cuda.h. End users do not need the Toolkit.'
}
$cudaHeaders = (Resolve-Path -LiteralPath $CudaInclude).Path
if (!(Test-Path -LiteralPath (Join-Path $cudaHeaders 'cuda.h'))) { throw 'cuda.h not found in -CudaInclude' }
$manifestPath = Join-Path $kernelPath 'manifest.json'
$headerPath = Join-Path $kernelPath 'embedded_kernels.h'
$manifest = Get-Content -Raw -Encoding UTF8 -LiteralPath $manifestPath | ConvertFrom-Json
$kernelSource = Join-Path $repoRoot 'kernels\fp16_native_kernel.py'
$sourceHash = (Get-FileHash -LiteralPath $kernelSource -Algorithm SHA256).Hash.ToLowerInvariant()
$headerHash = (Get-FileHash -LiteralPath $headerPath -Algorithm SHA256).Hash.ToLowerInvariant()
if (!$manifest.passed -or $manifest.kernel_source_sha256 -ne $sourceHash -or $manifest.embedded_header_sha256 -ne $headerHash) {
    throw 'Kernel source/header differs from the manifest. Rebuild kernels into a fresh directory and pass -KernelDir.'
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio 2022 C++ build tools and a Windows SDK on the developer machine.' }
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or !$installation) { throw 'MSVC x64 build tools not found' }
$vcvars = Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'
$installerDirectory = Split-Path -Parent $vswhere
$sourceDirectory = Join-Path $repoRoot 'src'
$objects = Join-Path $outputPath 'obj'
$temporary = Join-Path $outputPath 'tmp'
New-Item -ItemType Directory -Path $outputPath,$objects,$temporary -Force | Out-Null
$exe = Join-Path $outputPath 'DLSSBench.exe'
# Set the UTF-8 code page before any paths in the batch file are interpreted.
# /MT embeds the C++ runtime; no CUDA library is linked.
$commands = @"
@echo off
chcp 65001 >nul
set "PATH=$installerDirectory;%PATH%"
set "TMP=$temporary"
set "TEMP=$temporary"
call "$vcvars" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /MT /O2 /W4 /utf-8 /DUNICODE /D_UNICODE /I"$cudaHeaders" /I"$kernelPath" "$sourceDirectory\benchmark.cpp" "$sourceDirectory\app.cpp" /Fo"$objects/" /Fe"$exe" /link /INCREMENTAL:NO /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS /MANIFEST:EMBED user32.lib gdi32.lib comctl32.lib shell32.lib ole32.lib
if errorlevel 1 exit /b 1
dumpbin /dependents "$exe"
"@
$compile = Join-Path $outputPath 'compile.cmd'
[IO.File]::WriteAllText($compile,$commands,[Text.UTF8Encoding]::new($false))
& $env:ComSpec /d /c $compile 2>&1 | Tee-Object -FilePath (Join-Path $outputPath 'build.log')
if ($LASTEXITCODE -ne 0) { throw 'Build failed; inspect build.log' }
Copy-Item -LiteralPath (Join-Path $repoRoot 'THIRD_PARTY_NOTICES.txt') -Destination $outputPath
Write-Output "Built: $exe"
