param(
    [ValidateSet('Debug', 'Release', 'All')]
    [string]$Configuration = 'Release',
    [switch]$Test
)
$ErrorActionPreference = 'Stop'
$softwareRoot = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -version '[16.0,17.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) {
    throw 'Visual Studio 2019 with the v142 C++ tools is required.'
}
$msbuild = Join-Path $vsRoot 'MSBuild\Current\Bin\MSBuild.exe'
$configurations = @($Configuration)
if ($Configuration -eq 'All') {
    $configurations = @('Debug', 'Release')
}
foreach ($config in $configurations) {
    $logDirectory = Join-Path $softwareRoot 'build\logs'
    New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
    & $msbuild (Join-Path $softwareRoot 'afs_8ch.sln') /m /nologo /verbosity:minimal "/p:Configuration=$config" /p:Platform=x64 "/flp:logfile=$logDirectory\build_$config.log;verbosity=normal"
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed: $config"
    }
    if ($Test) {
        $vectors = Join-Path (Split-Path $softwareRoot -Parent) 'sim\reference_vectors.vh'
        & (Join-Path $softwareRoot "build\v1.4.0\$config\afs_8ch_tests.exe") $vectors (Join-Path $logDirectory "csv_$config") |
            Tee-Object -FilePath (Join-Path $logDirectory "test_$config.log")
        if ($LASTEXITCODE -ne 0) {
            throw "Tests failed: $config"
        }
        & (Join-Path $softwareRoot "build\v1.4.0\$config\afs_8ch_simulation_demo.exe") (Join-Path $logDirectory "sdk_demo_$config.csv") |
            Tee-Object -FilePath (Join-Path $logDirectory "sdk_demo_$config.log")
        if ($LASTEXITCODE -ne 0) {
            throw "SDK simulation demonstration failed: $config"
        }
    }
}
