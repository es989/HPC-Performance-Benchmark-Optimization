$ErrorActionPreference = "Stop"
$env:OMP_NUM_THREADS = 4
$bench = ".\build\Release\bench.exe"
$out   = "results\raw"
$flags = @("--warmup","50","--iters","200","--prefault","--aligned")

$kernels = @(
    @{ name="scale";   args=$flags },
    @{ name="add";     args=$flags },
    @{ name="triad";   args=$flags },
    @{ name="fma";     args=$flags },
    @{ name="flops";   args=$flags },
    @{ name="dot";     args=$flags },
    @{ name="saxpy";   args=$flags },
    @{ name="latency"; args=@("--warmup","50","--iters","200","--prefault") }
)

foreach ($k in $kernels) {
    $outFile = "$out\$($k.name).json"
    Write-Host "[$([datetime]::Now.ToString('HH:mm:ss'))] Running $($k.name)..."
    & $bench --kernel $k.name @($k.args) --out $outFile
    if ($LASTEXITCODE -ne 0) {
        Write-Error "FAILED: $($k.name)"
    } else {
        Write-Host "  -> Written: $outFile"
    }
}

Write-Host ""
Write-Host "=== ALL DONE ==="
Get-ChildItem results\raw\*.json | Select-Object Name, Length | Format-Table
