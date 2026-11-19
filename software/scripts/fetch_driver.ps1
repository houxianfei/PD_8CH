$ErrorActionPreference = 'Stop'
$softwareRoot = Split-Path $PSScriptRoot -Parent
$downloadRoot = Join-Path $softwareRoot 'driver\downloads'
$archive = Join-Path $downloadRoot 'CDM v2.12.36.4 WHQL Certified.zip'
$expectedHash = 'a8654caa76151e92161e2767294337039189ac3bab3a794dbac791cd1901172b'
New-Item -ItemType Directory -Force -Path $downloadRoot | Out-Null
if (-not (Test-Path -LiteralPath $archive)) {
    $sources = @(
        'https://www.ftdichip.com/Drivers/CDM/CDM%20v2.12.36.4%20WHQL%20Certified.zip',
        'https://ftdichip.cn/Drivers/CDM/CDM%20v2.12.36.4%20WHQL%20Certified.zip'
    )
    $downloaded = $false
    foreach ($source in $sources) {
        try {
            Invoke-WebRequest -Uri $source -OutFile $archive
            $downloaded = $true
            break
        } catch {
            Write-Warning "Download failed: $source"
        }
    }
    if (-not $downloaded) {
        throw 'Could not download the pinned FTDI driver package.'
    }
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedHash) {
    throw 'FTDI archive SHA256 mismatch. No files have been extracted.'
}
$driverRoot = Join-Path $softwareRoot 'driver\CDM'
Expand-Archive -LiteralPath $archive -DestinationPath $driverRoot -Force
$signature = Get-AuthenticodeSignature -LiteralPath (Join-Path $driverRoot 'amd64\ftd2xx64.dll')
if ($signature.Status -ne 'Valid') {
    throw "D2XX x64 DLL signature validation failed: $($signature.Status)"
}
$vendorRoot = Join-Path $softwareRoot 'vendor\ftdi'
New-Item -ItemType Directory -Force -Path $vendorRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $driverRoot 'ftd2xx.h') -Destination $vendorRoot -Force
Copy-Item -LiteralPath (Join-Path $driverRoot 'amd64\ftd2xx.lib') -Destination $vendorRoot -Force
Write-Output 'Pinned FTDI D2XX package verified and extracted. No system driver was installed.'
