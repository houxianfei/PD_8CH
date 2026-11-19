param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$softwareRoot = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -version '[16.0,17.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) {
    throw 'Visual Studio 2019 with v142 C++ tools is required.'
}
& (Join-Path $vsRoot 'MSBuild\Current\Bin\MSBuild.exe') (Join-Path $softwareRoot 'afs_8ch_ui_render.vcxproj') /m /nologo /verbosity:minimal "/p:Configuration=$Configuration" /p:Platform=x64
if ($LASTEXITCODE -ne 0) {
    throw 'UI renderer build failed.'
}
$output = Join-Path $softwareRoot 'build\ui_qa'
New-Item -ItemType Directory -Force -Path $output | Out-Null
& (Join-Path $softwareRoot "build\v1.4.0\$Configuration\afs_8ch_ui_render.exe") $output
if ($LASTEXITCODE -ne 0) {
    throw 'UI rendering verification failed.'
}
