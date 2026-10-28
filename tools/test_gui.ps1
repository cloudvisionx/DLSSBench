param(
    [string]$Exe = 'build\Release\DLSSBench.exe',
    [string]$Output = ('build\qa\gui_' + (Get-Date -Format 'yyyyMMdd_HHmmss')),
    [ValidateSet('all','cancel')][string]$Mode = 'all'
)
$ErrorActionPreference = 'Stop'
$workspace = Split-Path -Parent $PSScriptRoot
$executable = (Resolve-Path -LiteralPath (Join-Path $workspace $Exe)).Path
$evidence = [IO.Path]::GetFullPath((Join-Path $workspace $Output))
if (Test-Path -LiteralPath $evidence) { throw 'Use a fresh QA directory' }
New-Item -ItemType Directory -Path $evidence | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class Fp16GuiQa {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    public delegate bool EnumProc(IntPtr h, IntPtr p);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc proc, IntPtr p);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h,EnumProc proc,IntPtr p);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h,System.Text.StringBuilder s,int n);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h,System.Text.StringBuilder s,int n);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint flags,uint timeout,out IntPtr result);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h,int id);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int command);
    [DllImport("user32.dll")] public static extern bool UpdateWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out RECT rect);
    public struct RECT {public int Left,Top,Right,Bottom;}
    public static IntPtr Find(uint processId) {
        IntPtr found=IntPtr.Zero;
        EnumWindows(delegate(IntPtr h,IntPtr p) {
            uint id; GetWindowThreadProcessId(h,out id);
            var s=new System.Text.StringBuilder(128); GetClassName(h,s,128);
            if(id==processId && s.ToString()=="DLSSBenchNativeWindow") {found=h;return false;}
            return true;
        },IntPtr.Zero);
        return found;
    }
    public static bool Testing(IntPtr parent) {
        bool found=false;
        EnumChildWindows(parent,delegate(IntPtr h,IntPtr p) {
            var cls=new System.Text.StringBuilder(128); GetClassName(h,cls,128);
            var text=new System.Text.StringBuilder(512); GetWindowText(h,text,512);
            if(cls.ToString()=="Static" && text.ToString().Contains("Testing:")) {found=true;return false;}
            return true;
        },IntPtr.Zero);
        return found;
    }
}
'@
[void][Fp16GuiQa]::SetProcessDPIAware()
function Wait-Window($process) {
    $deadline = (Get-Date).AddSeconds(15)
    do {
        if ($process.HasExited) { throw "App exited: $($process.ExitCode)" }
        $handle = [Fp16GuiQa]::Find($process.Id)
        if ($handle -ne [IntPtr]::Zero) { return $handle }
        Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $deadline)
    throw 'Window not created'
}
function Wait-Report($process,$folder) {
    $deadline = (Get-Date).AddSeconds(30)
    do {
        if ($process.HasExited) { throw "App exited during test: $($process.ExitCode)" }
        $result = Join-Path $folder 'result.json'
        if (Test-Path -LiteralPath $result) {
            try {
                $report = Get-Content -Raw -Encoding UTF8 -LiteralPath $result | ConvertFrom-Json
                if ($report.status -ne 'running') { return $report }
            } catch { }
        }
        Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $deadline)
    throw 'Benchmark did not finish'
}
$checks = @()
$kinds = if ($Mode -eq 'cancel') { @('cancel') } else { @('complete','cancel') }
foreach ($kind in $kinds) {
    $folder = Join-Path $evidence $kind
    $nativeUiProcess = Start-Process -FilePath $executable -ArgumentList "--output `"$folder`"" -WindowStyle Hidden -PassThru
    try {
        $handle = Wait-Window $nativeUiProcess
        Start-Sleep -Milliseconds 400
        if ($kind -eq 'cancel') {
            $sizes = [Fp16GuiQa]::GetDlgItem($handle,104)
            [void][Fp16GuiQa]::SendMessage($sizes,0x14E,[IntPtr]2,[IntPtr]::Zero)
        }
        [void][Fp16GuiQa]::SendMessage($handle,0x111,[IntPtr]100,[IntPtr]::Zero)
        Start-Sleep -Milliseconds 150
        $response = [IntPtr]::Zero
        $responsive = [Fp16GuiQa]::SendMessageTimeout($handle,0,[IntPtr]::Zero,[IntPtr]::Zero,2,1000,[ref]$response) -ne [IntPtr]::Zero
        if (!$responsive) { throw 'GUI stopped responding while benchmarking' }
        if ($kind -eq 'cancel') {
            $deadline = (Get-Date).AddSeconds(15)
            while (![Fp16GuiQa]::Testing($handle)) {
                if ((Get-Date) -gt $deadline) { throw 'Timed GPU phase did not start before cancellation test' }
                Start-Sleep -Milliseconds 20
            }
            [void][Fp16GuiQa]::SendMessage($handle,0x111,[IntPtr]101,[IntPtr]::Zero)
        }
        $report = Wait-Report $nativeUiProcess $folder
        if (!$report.cleanup_ok) { throw 'CUDA resources did not clean up' }
        if ($kind -eq 'complete') {
            if ($report.status -ne 'completed' -or $report.rows.Count -ne 4) { throw 'GUI completion failed' }
            $modules = @($nativeUiProcess.Modules | ForEach-Object { @{name=$_.ModuleName;path=$_.FileName} })
            $modules | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $evidence 'loaded-modules.json')
            if (@($modules | Where-Object { $_.name -match 'python|cublas|cudart|nvrtc|Qt|tcl|tk86|vcruntime|msvcp140' }).Count) { throw 'Unexpected runtime dependency loaded' }
            $rect = New-Object Fp16GuiQa+RECT
            # Hidden windows are not rendered by DWM. Briefly show the product
            # window without activation for visual QA, then hide it again.
            [void][Fp16GuiQa]::ShowWindow($handle,4)
            [void][Fp16GuiQa]::UpdateWindow($handle)
            Start-Sleep -Milliseconds 200
            [void][Fp16GuiQa]::GetWindowRect($handle,[ref]$rect)
            $bitmap = New-Object Drawing.Bitmap ($rect.Right-$rect.Left),($rect.Bottom-$rect.Top)
            $graphics = [Drawing.Graphics]::FromImage($bitmap)
            $dc = $graphics.GetHdc()
            try { $painted = [Fp16GuiQa]::PrintWindow($handle,$dc,2) } finally { $graphics.ReleaseHdc($dc) }
            $graphics.Dispose()
            $bitmap.Save((Join-Path $evidence 'window.png'),[Drawing.Imaging.ImageFormat]::Png)
            $bitmap.Dispose()
            [void][Fp16GuiQa]::ShowWindow($handle,0)
            if (!$painted) { throw 'Window capture failed' }
        } elseif ($report.status -ne 'cancelled') { throw 'Stop button did not cancel the benchmark' }
        $checks += @{kind=$kind;status=$report.status;responsive=$responsive;cleanup_ok=$report.cleanup_ok;rows=$report.rows.Count}
    } finally {
        if (!$nativeUiProcess.HasExited) {
            [void][Fp16GuiQa]::PostMessage($handle,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
            if (!$nativeUiProcess.WaitForExit(10000)) { $nativeUiProcess.Kill(); throw 'Window did not close cooperatively' }
        }
    }
}
@{passed=$true;checks=$checks;exe=$executable;exe_sha256=(Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash.ToLowerInvariant()} |
    ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $evidence 'validation.json')
$checks | Format-Table -AutoSize
