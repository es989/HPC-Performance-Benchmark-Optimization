<#
        run_bench_runs.ps1
        ------------------
        Helper script to repeatedly run the benchmark executable, collect per-run
        JSON outputs, and aggregate per-bytes statistics into a CSV summary.

        Usage example:
            .\run_bench_runs.ps1 -Runs 5 -BenchPath '..\build\Release\bench.exe' -BenchArgs '--kernel copy --warmup 50 --iters 200'

        This file follows the project's existing comment style: each major block
        includes a short description of purpose and key variables so other
        contributors can quickly understand and modify the behavior.
#>

param(
    [int]$Runs = 5,
    [string]$BenchPath = "..\build\Release\bench.exe",
    [string]$BenchArgs = "--kernel copy --warmup 50 --iters 200",
    [int]$AffinityMask = 1,
    [ValidateSet('Normal','High','RealTime','Idle','BelowNormal','AboveNormal')]
    [string]$Priority = 'High',
    [string]$OutDir = "..\build\Release\runs",
    [int]$SleepBetween = 2
)

<#
    Parameter descriptions:
    - Runs: number of repeated benchmark invocations to perform.
    - BenchPath: path to the `bench` executable to run. Can be relative.
    - BenchArgs: argument string passed verbatim to the bench executable.
    - AffinityMask: processor affinity mask (integer, hex accepted) to pin process.
    - Priority: process priority class to reduce OS scheduling noise.
    - OutDir: directory where per-run JSON and aggregated CSV are written.
    - SleepBetween: seconds to sleep between runs to allow system settling.
#>

# Validate that the benchmark executable exists before starting any runs.
if (!(Test-Path $BenchPath)) {
    Write-Error "bench executable not found at: $BenchPath"
    exit 1
}

# Resolve the absolute directory containing the bench executable. If the
# configured OutDir is relative or doesn't exist, default it to <benchdir>/runs
$BenchDir = Split-Path -Parent (Resolve-Path $BenchPath)
if ($OutDir -like "..*" -or $OutDir -like ".*" -or -not (Test-Path $OutDir)) {
    $OutDir = Join-Path $BenchDir "runs"
}
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

$results = @()
<#
  Run loop:
  - Start the bench executable with the provided arguments.
  - Short sleep to allow process initialization before adjusting affinity.
  - Attempt to set processor affinity & priority; if that fails continue.
  - Wait for process completion and move `results.json` produced by the
    benchmark to a run-specific file under OutDir for later aggregation.
#>
for ($i = 1; $i -le $Runs; $i++) {
    Write-Host ([string]::Format("Run {0}/{1}: starting benchmark...", $i, $Runs))

    $startInfo = @{ FilePath = $BenchPath; ArgumentList = $BenchArgs; PassThru = $true }
    $p = Start-Process @startInfo

    # Allow the process to start so the Process object is usable.
    Start-Sleep -Milliseconds 200

    try {
        # Attempt to pin the process to the given CPU mask and raise its priority
        # to reduce scheduling noise during the measured run. This is best-effort
        # — failures are nonfatal (e.g., when running without sufficient privileges).
        $p.ProcessorAffinity = $AffinityMask
        $p.PriorityClass = $Priority
    } catch {
        Write-Warning "Unable to set affinity/priority on process. Running anyway. ($_ )"
    }

    # Block until the benchmark completes.
    $p.WaitForExit()

    # Move the per-run JSON produced by bench (results.json) into the OutDir
    # and record the path for later aggregation.
    $dest = Join-Path $OutDir ("results_run_{0}.json" -f $i)
    $benchResult = Join-Path $BenchDir "results.json"
    if (Test-Path $benchResult) {
        Move-Item $benchResult $dest -Force
        Write-Host ([string]::Format("Saved run {0} results -> {1}", $i, $dest))
        $results += $dest
    } else {
        Write-Warning ([string]::Format("{0} not found after run {1}", $benchResult, $i))
    }

    # Brief pause between runs to allow system state to settle (e.g., IO flush).
    Start-Sleep -Seconds $SleepBetween
}

if ($results.Count -eq 0) {
    Write-Error "No result files collected. Aborting aggregation."
    exit 1
}

<#
  Aggregation phase:
  - Read each per-run JSON file and extract per-sweep points.
  - Build an in-memory table (`$allData`) with columns: run, bytes, median, p95, stddev.
  - Later grouping computes median-of-medians, mean p95, mean stddev and mean CV.
#>
$allData = @()
foreach ($f in $results) {
    $json = Get-Content $f -Raw | ConvertFrom-Json
    foreach ($pt in $json.stats.sweep) {
        $allData += [pscustomobject]@{
            run = ([System.IO.Path]::GetFileNameWithoutExtension($f) -replace 'results_run_','')
            bytes = [int]$pt.bytes
            median = [double]$pt.median_ns
            p95 = [double]$pt.p95_ns
            stddev = [double]$pt.stddev_ns
        }
    }
}

$grouped = $allData | Group-Object bytes | Sort-Object Name
$csvOut = Join-Path $OutDir "runs_summary.csv"
$summary = @()

<#
  For each bytes-group compute the following summary statistics:
  - median_of_medians: robust central tendency of per-run medians (median of medians)
  - mean_p95: average p95 across runs
  - mean_stddev: average stddev across runs
  - mean_cv: average coefficient of variation (stddev/median) across runs
#>
foreach ($g in $grouped) {
    $bytes = [int]$g.Name
    $medians = $g.Group | Select-Object -ExpandProperty median | Sort-Object
    # median-of-medians (handles even/odd counts)
    if ($medians.Count -gt 0) {
        $mid = [int]([math]::Floor($medians.Count/2))
        $median_of_medians = if ($medians.Count % 2 -eq 1) { $medians[$mid] } else { (($medians[$mid-1] + $medians[$mid]) / 2.0) }
    } else { $median_of_medians = 0 }

    $mean_p95 = ($g.Group | Measure-Object p95 -Average).Average
    $mean_stddev = ($g.Group | Measure-Object stddev -Average).Average
    # compute mean CV across runs (stddev/median per-run then mean)
    $cvs = $g.Group | ForEach-Object { if ($_.median -ne 0) { ($_.stddev / $_.median) } else { 0 } }
    $mean_cv = ($cvs | Measure-Object -Average).Average

    $obj = [pscustomobject]@{
        bytes = $bytes
        median_of_medians = [math]::Round($median_of_medians,4)
        mean_p95 = [math]::Round($mean_p95,4)
        mean_stddev = [math]::Round($mean_stddev,4)
        mean_cv = [math]::Round($mean_cv,6)
    }
    $summary += $obj
}

$summary | Sort-Object bytes | Export-Csv -Path $csvOut -NoTypeInformation -Encoding UTF8
Write-Host "Aggregation complete -> $csvOut"

# Print a short table
$summary | Sort-Object bytes | Format-Table -AutoSize

Write-Host "Done."
