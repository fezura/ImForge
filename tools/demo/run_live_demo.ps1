# runs the live demo unattended (dx11 example + builder with live_demo.txt), exits with the script's code
#   powershell -ExecutionPolicy Bypass -File tools\demo\run_live_demo.ps1 [-Builder exe] [-Client exe] [-TimeoutSec n]
param(
    [string]$Builder = '',
    [string]$Client = '',
    [string]$Script = '',
    [int]$TimeoutSec = 180
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $Builder) { $Builder = Join-Path $root 'build\windows-msvc-builder\builder\Release\imforge_builder.exe' }
if (-not $Client) { $Client = Join-Path $root 'build\win-examples\examples\Release\imforge_example_dx11_win32.exe' }
if (-not $Script) { $Script = Join-Path $PSScriptRoot 'live_demo.txt' }
foreach ($p in @($Builder, $Client, $Script)) {
    if (-not (Test-Path $p)) {
        Write-Host "run_live_demo: missing $p (build it first)"
        exit 2
    }
}
$project = Join-Path $root 'examples\projects\CheatMenu'

Write-Host "run_live_demo: starting app    $Client"
$app = Start-Process -FilePath $Client -WorkingDirectory $root -PassThru
$null = $app.Handle
$code = 1
try {
    Write-Host "run_live_demo: starting builder $Builder"
    $builderArgs = @("`"$project`"", '--script', "`"$Script`"")
    $b = Start-Process -FilePath $Builder -ArgumentList $builderArgs -WorkingDirectory $root -PassThru -NoNewWindow
    $null = $b.Handle
    if (-not $b.WaitForExit($TimeoutSec * 1000)) {
        Write-Host "run_live_demo: builder did not finish within $TimeoutSec s"
        Stop-Process -Id $b.Id -Force -ErrorAction SilentlyContinue
        $code = 1
    } else {
        $b.WaitForExit()
        $code = $b.ExitCode
    }
    if ($app.HasExited) {
        Write-Host "run_live_demo: the app exited early (code $($app.ExitCode))"
        if ($code -eq 0) { $code = 1 }
    }
} finally {
    if (-not $app.HasExited) { Stop-Process -Id $app.Id -Force -ErrorAction SilentlyContinue }
}
Write-Host "run_live_demo: exit code $code"
exit $code
