#define AppVersion "1.0.0"

[Setup]
AppId={{97A62483-9CD8-4D26-A196-F165A8158FE8}
AppName=PD 8CH
AppVersion={#AppVersion}
AppPublisher=RSIC
DefaultDirName={autopf}\RSIC\PD 8CH
DefaultGroupName=PD 8CH
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\publish
OutputBaseFilename=PD_8CH_Setup_v{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
CloseApplicationsFilter=pd_8ch.exe
InfoBeforeFile=..\docs\INSTALL_NOTICE.txt

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "..\publish\PD_8CH_v{#AppVersion}\*"; DestDir: "{app}"; Excludes: "cfg\*"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\publish\PD_8CH_v{#AppVersion}\cfg\*"; DestDir: "{app}\cfg"; Flags: onlyifdoesntexist uninsneveruninstall

[Dirs]
Name: "{app}\cfg"; Permissions: users-modify; Flags: uninsneveruninstall

[Icons]
Name: "{group}\PD 8CH"; Filename: "{app}\pd_8ch.exe"
Name: "{autodesktop}\PD 8CH"; Filename: "{app}\pd_8ch.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\pd_8ch.exe"; Description: "Launch PD 8CH"; Flags: nowait postinstall skipifsilent

[Code]
var
    UsbDriverPage: TInputOptionWizardPage;

procedure InitializeWizard;
begin
    UsbDriverPage := CreateInputOptionPage(wpSelectDir,
        'USB 2.0 Driver', 'Choose whether to install the FTDI D2XX USB driver',
        'PD 8CH requires the FTDI D2XX driver for FT232H. The signed driver package is included in the driver folder. Select Install for a new computer, or Skip if a working D2XX driver is already installed.',
        True, False);
    UsbDriverPage.Add('Install bundled FTDI D2XX driver (recommended)');
    UsbDriverPage.Add('Skip; a working D2XX driver is already installed');
    UsbDriverPage.Values[0] := True;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
    ExitCode: Integer;
begin
    if (CurStep = ssPostInstall) and UsbDriverPage.Values[0] then
    begin
        if not Exec(ExpandConstant('{sys}\pnputil.exe'),
            '/add-driver "' + ExpandConstant('{app}\driver\CDM\ftdibus.inf') + '" /install',
            '', SW_HIDE, ewWaitUntilTerminated, ExitCode) then
            MsgBox('D2XX driver installation could not start. See docs\INSTALL_NOTICE.txt.', mbError, MB_OK)
        else if (ExitCode <> 0) and (ExitCode <> 3010) then
            MsgBox('D2XX driver installation returned ' + IntToStr(ExitCode) +
                '. See docs\INSTALL_NOTICE.txt.', mbError, MB_OK)
        else if ExitCode = 3010 then
            MsgBox('Windows requires a restart to finish installing the D2XX driver.', mbInformation, MB_OK);
    end;
end;
