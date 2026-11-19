param([string]$InnoCompiler = "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe")
$ErrorActionPreference = 'Stop'
$softwareRoot = Split-Path $PSScriptRoot -Parent
& (Join-Path $PSScriptRoot 'build.ps1') -Configuration Release -Test
if (-not (Test-Path -LiteralPath $InnoCompiler)) {
    throw "Inno Setup 6 was not found at $InnoCompiler"
}
$destination = Join-Path $softwareRoot 'publish\AFS_8CH_v1.4.0'
New-Item -ItemType Directory -Force -Path $destination | Out-Null
Copy-Item -LiteralPath (Join-Path $softwareRoot 'build\v1.4.0\Release\afs_8ch.exe') -Destination $destination
Copy-Item -LiteralPath (Join-Path $softwareRoot 'build\v1.4.0\Release\ftd2xx.dll') -Destination $destination
Copy-Item -LiteralPath (Join-Path $softwareRoot 'build\v1.4.0\Release\afs_8ch_simulation_demo.exe') -Destination $destination
foreach ($directory in @('docs', 'driver', 'cfg', 'examples')) {
    Copy-Item -LiteralPath (Join-Path $softwareRoot $directory) -Destination $destination -Recurse -Force
}
$licenses = Join-Path $destination 'licenses'
New-Item -ItemType Directory -Force -Path $licenses | Out-Null
Copy-Item -LiteralPath (Join-Path $softwareRoot 'third_party\imgui\LICENSE.txt') -Destination (Join-Path $licenses 'imgui.txt')
Copy-Item -LiteralPath (Join-Path $softwareRoot 'third_party\implot\LICENSE') -Destination (Join-Path $licenses 'implot.txt')
Copy-Item -LiteralPath (Join-Path $softwareRoot 'README.md') -Destination $destination
& $InnoCompiler (Join-Path $softwareRoot 'installer\afs_8ch.iss')
if ($LASTEXITCODE -ne 0) {
    throw 'Installer compilation failed.'
}
Compress-Archive -LiteralPath $destination -DestinationPath (Join-Path $softwareRoot 'publish\AFS_8CH_v1.4.0_portable.zip') -Force
Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $softwareRoot 'publish\AFS_8CH_Setup_v1.4.0.exe'), (Join-Path $softwareRoot 'publish\AFS_8CH_v1.4.0_portable.zip') |
    Format-List | Out-File -Encoding utf8 (Join-Path $softwareRoot 'publish\SHA256.txt')
