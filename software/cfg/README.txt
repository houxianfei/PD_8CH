PD 8CH configuration

pd_8ch.ini is created automatically here when the application is used.
It stores UI parameters, channel names and calibration coefficients,
device selection, file locations, window layout and table widths.
Settings are restored next time. USB is not opened and acquisition is not
started automatically. Closing Main UI flushes the latest settings.

Calibration file: calibration.cfg by default, when Save calibration is used.
Save As... chooses a different file. Load calibration opens an existing
PD8CH_CALIBRATION_V2 .cfg file. The selected full path is shown in Main UI.
default_calibration.cfg is an example with P0=0, P1=1, CH0-CH7.

The formula is Power = P1 * (P0 + ADC).
Legacy AFS/V1 calibration files are rejected; current values are retained.
PD does not import any AFS INI or AppData settings.

The installer preserves existing cfg files and makes this folder writable.
For the portable application, extract to a writable directory. Do not copy
your personal pd_8ch.ini or calibration.cfg into a distribution package.
