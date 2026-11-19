param([string]$OutputDirectory = '')
$ErrorActionPreference = 'Stop'
$softwareRoot = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -version '[16.0,17.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) {
    throw 'Visual Studio 2019 with v142 C++ tools is required.'
}
& (Join-Path $vsRoot 'MSBuild\Current\Bin\MSBuild.exe') (Join-Path $softwareRoot 'pd_8ch_ui_render.vcxproj') /m /nologo /verbosity:minimal /p:Configuration=Release /p:Platform=x64
if ($LASTEXITCODE -ne 0) {
    throw 'Benchmark build failed.'
}
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $softwareRoot 'build\performance_after'
}
& (Join-Path $softwareRoot 'build\PD_8CH_v1.0.0\Release\pd_8ch_ui_render.exe') $OutputDirectory --benchmark
if ($LASTEXITCODE -ne 0) {
    throw 'Performance benchmark failed.'
}
