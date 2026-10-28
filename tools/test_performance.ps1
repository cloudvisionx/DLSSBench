param([string]$Output = 'build\tests')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputPath = [IO.Path]::GetFullPath((Join-Path $repoRoot $Output))
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or !$installation) { throw 'MSVC x64 build tools not found' }
$vcvars = Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'
$installerDirectory = Split-Path -Parent $vswhere
$source = Join-Path $repoRoot 'tests\performance_test.cpp'
$exe = Join-Path $outputPath 'performance_test.exe'
$commands = @"
@echo off
chcp 65001 >nul
set "PATH=$installerDirectory;%PATH%"
call "$vcvars" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /MT /O2 /W4 /utf-8 "$source" /Fo"$outputPath/" /Fe"$exe"
if errorlevel 1 exit /b 1
"$exe"
exit /b %errorlevel%
"@
$compile = Join-Path $outputPath 'compile.cmd'
[IO.File]::WriteAllText($compile,$commands,[Text.UTF8Encoding]::new($false))
& $env:ComSpec /d /c $compile
if ($LASTEXITCODE -ne 0) { throw 'Peak reference tests failed' }
