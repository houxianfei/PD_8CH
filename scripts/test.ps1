param([string]$IcarusBin = '')
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (!$IcarusBin) { $IcarusBin = Join-Path $projectRoot '.tools/iverilog/bin' }
$compiler = Join-Path $IcarusBin 'iverilog.exe'
$runtime = Join-Path $IcarusBin 'vvp.exe'
if (!(Test-Path $compiler)) { throw 'Specify -IcarusBin with the Icarus Verilog bin directory.' }
Push-Location $projectRoot
try {
    New-Item -ItemType Directory -Force sim/build | Out-Null
    $sources = @(Get-ChildItem rtl/*.v,rtl/ftdi_245/*.v | ForEach-Object FullName)
    foreach ($test in @('tb_adc','tb_register','tb_core','tb_ft232h')) {
        & $compiler -g2012 -Wall -Wno-timescale -I sim -s $test -o "sim/build/$test.vvp" @sources "sim/$test.sv"
        if ($LASTEXITCODE -ne 0) { throw "$test compilation failed" }
        & $runtime "sim/build/$test.vvp"
        if ($LASTEXITCODE -ne 0) { throw "$test failed" }
    }
} finally { Pop-Location }
