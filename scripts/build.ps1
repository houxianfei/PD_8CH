param([ValidateSet('synth','timing','compile')][string]$Mode = 'synth', [string]$QuartusRoot = 'C:/intelFPGA_lite/22.1std/quartus')
$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '../par')
try {
    if ($Mode -eq 'synth') { & "$QuartusRoot/bin64/quartus_map.exe" AFS --read_settings_files=on --write_settings_files=off }
    elseif ($Mode -eq 'timing') { & "$QuartusRoot/bin64/quartus_sta.exe" -t ../scripts/check_timing.tcl }
    else { & "$QuartusRoot/bin64/quartus_sh.exe" --flow compile AFS }
    if ($LASTEXITCODE -ne 0) { throw "Quartus $Mode failed ($LASTEXITCODE)" }
} finally { Pop-Location }
