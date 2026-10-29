param(
    [Parameter(Mandatory=$true)][string]$Harness,
    [Parameter(Mandatory=$true)][string]$ReportDirectory
)
$ErrorActionPreference = 'Stop'
$executable = (Resolve-Path -LiteralPath $Harness).Path
$directory = Split-Path -Parent $executable
New-Item -ItemType Directory -Path $ReportDirectory -Force | Out-Null
$reportRoot = (Resolve-Path -LiteralPath $ReportDirectory).Path
$cases = @(
    @{name='normal'; arguments=$null; cpp=0; init_failure=0},
    @{name='construction-exception'; arguments='--fail-construction-once'; cpp=1; init_failure=0},
    @{name='partial-resource-exception'; arguments='--fail-resources-once'; cpp=1; init_failure=0},
    @{name='device-init-failure'; arguments='--fail-device-once'; cpp=0; init_failure=1}
)
$results = @()
foreach ($case in $cases) {
    $start = @{FilePath=$executable; WorkingDirectory=$directory; WindowStyle='Hidden'; PassThru=$true}
    if ($case.arguments) { $start.ArgumentList = $case.arguments }
    $process = Start-Process @start
    if (-not $process.WaitForExit(45000)) {
        Stop-Process -Id $process.Id
        throw "Bounded UI smoke process timed out: $($case.name)"
    }
    $logPath = Join-Path $directory 'nSkinzCS2.preview.log'
    $log = [IO.File]::ReadAllText($logPath)
    Copy-Item -LiteralPath $logPath -Destination (Join-Path $reportRoot "$($case.name).log") -Force
    if ($process.ExitCode -ne 0) { throw "UI smoke failed: $($case.name), exit $($process.ExitCode); see saved log." }
    $cppCount = [regex]::Matches($log, 'UI C\+\+ exception during').Count
    $failureCount = [regex]::Matches($log, 'UI initialization failed after Insert').Count
    if ($cppCount -ne $case.cpp -or $failureCount -ne $case.init_failure -or
        -not $log.Contains('UI controller initialized') -or
        -not $log.Contains('Real UI frame exported successfully.')) {
        throw "UI smoke evidence did not match expected recovery: $($case.name)"
    }
    $pngPath = Join-Path $directory 'control-panel.png'
    Copy-Item -LiteralPath $pngPath -Destination (Join-Path $reportRoot "$($case.name).png") -Force
    $results += @{name=$case.name; passed=$true; exit=$process.ExitCode; cpp_exceptions=$cppCount;
        device_init_failures=$failureCount; image_sha256=(Get-FileHash -LiteralPath $pngPath -Algorithm SHA256).Hash.ToLowerInvariant()}
    Write-Output "PASS: $($case.name); native UI rendered after expected recovery; no keyboard events injected"
}
$report = @{scope='Real UI constructor/device/font/render smoke and injected failure recovery; no CS2 process or keyboard injection'; cases=$results}
[IO.File]::WriteAllText((Join-Path $reportRoot 'report.json'),($report | ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
