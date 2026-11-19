FT232H USB 2.0 driver - FTDI D2XX / CDM 2.12.36.4

This folder contains the unmodified signed USB driver package in CDM,
the original downloaded archive in downloads, the FTDI license files,
and SOURCE.json with download URLs and SHA256 hashes.

Recommended installation:
Run AFS_8CH_Setup and select Install bundled FTDI D2XX driver on the
dedicated USB 2.0 Driver page. Select Skip if D2XX is already working.

Manual installation:
Use Device Manager > Update driver > Browse, then select this CDM folder.
Alternatively, from an Administrator PowerShell opened in this driver folder:
pnputil.exe /add-driver ".\CDM\ftdibus.inf" /install

The application's ftd2xx.dll is a user-mode library and does not replace
installation of the system driver. Use D2XX for FT232H, not FT601 D3XX.
This package does not change EEPROM or Windows test-signing settings.
