param(
    [Parameter(Mandatory = $true)]
    [string]$PackageDirectory
)

$ErrorActionPreference = 'Stop'
$packageDir = (Resolve-Path -LiteralPath $PackageDirectory).Path
$report = Join-Path $packageDir 'AV1-DECODE-SMOKE.txt'

function Invoke-PackagedCheck {
    param([string]$FileName, [string]$Arguments, [string]$Label)

    $start = [System.Diagnostics.ProcessStartInfo]::new()
    $start.FileName = Join-Path $packageDir $FileName
    $start.Arguments = $Arguments
    $start.WorkingDirectory = $packageDir
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.Environment['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot"
    foreach ($name in @($start.Environment.Keys)) {
        if ($name -match '^(EDEN_|YUZU_|TOTK_|DKB_|VK_|QT_)') {
            [void]$start.Environment.Remove($name)
        }
    }

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $start
    try {
        [void]$process.Start()
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(30000)) {
            $process.Kill($true)
            [void]$process.WaitForExit(5000)
            throw "$Label timed out after 30 seconds"
        }
        $stdout.GetAwaiter().GetResult() | Add-Content -LiteralPath $report
        $stderr.GetAwaiter().GetResult() | Add-Content -LiteralPath $report
        $exitCode = $process.ExitCode
        "${Label} exit code: $exitCode" | Add-Content -LiteralPath $report
        if ($exitCode -ne 0) {
            throw "${Label} failed: $exitCode"
        }
    } finally {
        $process.Dispose()
    }
}

try {
    'GUI loader check omitted: --help reaches StartupChecks and may run Vulkan/UI startup.' |
        Out-File -FilePath $report -Encoding utf8
    # The CLI uses the Windows GUI subsystem. Wait explicitly rather than relying on
    # PowerShell's invocation to populate $LASTEXITCODE for that executable type.
    Invoke-PackagedCheck -FileName 'eden-cli.exe' -Arguments '--help' -Label 'CLI --help'
    Invoke-PackagedCheck -FileName 'av1_decode_smoke.exe' -Arguments '' -Label 'AV1 probe'
} finally {
    if (Test-Path -LiteralPath $report) {
        Get-Content -LiteralPath $report
    }
}
