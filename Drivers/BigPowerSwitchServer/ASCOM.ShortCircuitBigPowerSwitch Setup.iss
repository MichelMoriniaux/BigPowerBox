;
; Script generated by the ASCOM Driver Installer Script Generator 6.6.0.0
; Generated by Michel Moriniaux on 3/12/2023 (UTC)
;
[Setup]
AppID={{c3224456-eeb4-4602-ba85-b0c88fcc674c}
AppName=ASCOM ASCOM.ShortCircuitBigPowerSwitch Switch Driver
AppVerName=ASCOM ASCOM.ShortCircuitBigPowerSwitch Switch Driver 0.1.3
AppVersion=0.1.3
AppPublisher=Michel Moriniaux <michel.moriniaux+BPS@gmail.com>
AppPublisherURL=mailto:michel.moriniaux+BPS@gmail.com
AppSupportURL=https://ascomtalk.groups.io/g/Help
AppUpdatesURL=https://ascom-standards.org/
VersionInfoVersion=1.0.0
MinVersion=6.1.7601
DefaultDirName="{cf}\ASCOM\Switch"
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir="."
OutputBaseFilename="ASCOM.ShortCircuitBigPowerSwitch Setup"
Compression=lzma
SolidCompression=yes
; Put there by Platform if Driver Installer Support selected
WizardImageFile="C:\Program Files (x86)\ASCOM\Platform 6 Developer Components\Installer Generator\Resources\WizardImage.bmp"
LicenseFile="C:\Program Files (x86)\ASCOM\Platform 6 Developer Components\Installer Generator\Resources\CreativeCommons.txt"
; {cf}\ASCOM\Uninstall\Switch folder created by Platform, always
UninstallFilesDir="{cf}\ASCOM\Uninstall\Switch\ASCOM.ShortCircuitBigPowerSwitch"

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Dirs]
Name: "{cf}\ASCOM\Uninstall\Switch\ASCOM.ShortCircuitBigPowerSwitch"
; TODO: Add subfolders below {app} as needed (e.g. Name: "{app}\MyFolder")

[Files]
Source: "C:\Users\Mitch\OneDrive\Documents\GitHub\BigPowerBox\Drivers\BigPowerSwitchServer\bin\Release\ASCOM.ShortCircuitBigPowerSwitch.exe"; DestDir: "{app}" ;AfterInstall: RegASCOM()
; Require a read-me HTML to appear after installation, maybe driver's Help doc
Source: "C:\Users\Mitch\OneDrive\Documents\GitHub\BigPowerBox\Drivers\BigPowerSwitchServer\README.MD"; DestDir: "{app}"; Flags: isreadme
; TODO: Add other files needed by your driver here (add subfolders above)

;Only if COM Local Server
[Run]
Filename: "{app}\ASCOM.ShortCircuitBigPowerSwitch.exe"; Parameters: "/regserver"




;Only if COM Local Server
[UninstallRun]
Filename: "{app}\ASCOM.ShortCircuitBigPowerSwitch.exe"; Parameters: "/unregserver"



;  DCOM setup for COM local Server, needed for TheSky
[Registry]
; TODO: If needed set this value to the Switch CLSID of your driver (mind the leading/extra '{')
#define AppClsid "{{7e9afd02-a203-4e48-a51f-b9273422785c}"

; set the DCOM access control for TheSky on the Switch interface
Root: HKCR; Subkey: CLSID\{#AppClsid}; ValueType: string; ValueName: AppID; ValueData: {#AppClsid}
Root: HKCR; Subkey: AppId\{#AppClsid}; ValueType: string; ValueData: "ASCOM ASCOM.ShortCircuitBigPowerSwitch Switch Driver"
Root: HKCR; Subkey: AppId\{#AppClsid}; ValueType: string; ValueName: AppID; ValueData: {#AppClsid}
Root: HKCR; Subkey: AppId\{#AppClsid}; ValueType: dword; ValueName: AuthenticationLevel; ValueData: 1
; set the DCOM key for the executable as a whole
Root: HKCR; Subkey: AppId\ASCOM.ShortCircuitBigPowerSwitch.exe; ValueType: string; ValueName: AppID; ValueData: {#AppClsid}
; CAUTION! DO NOT EDIT - DELETING ENTIRE APPID TREE WILL BREAK WINDOWS!
Root: HKCR; Subkey: AppId\{#AppClsid}; Flags: uninsdeletekey
Root: HKCR; Subkey: AppId\ASCOM.ShortCircuitBigPowerSwitch.exe; Flags: uninsdeletekey

[Code]
const
   REQUIRED_PLATFORM_VERSION = 6.2;    // Set this to the minimum required ASCOM Platform version for this application

//
// Function to return the ASCOM Platform's version number as a double.
//
function PlatformVersion(): Double;
var
   PlatVerString : String;
begin
   Result := 0.0;  // Initialise the return value in case we can't read the registry
   try
      if RegQueryStringValue(HKEY_LOCAL_MACHINE_32, 'Software\ASCOM','PlatformVersion', PlatVerString) then 
      begin // Successfully read the value from the registry
         Result := StrToFloat(PlatVerString); // Create a double from the X.Y Platform version string
      end;
   except                                                                   
      ShowExceptionMessage;
      Result:= -1.0; // Indicate in the return value that an exception was generated
   end;
end;

//
// Before the installer UI appears, verify that the required ASCOM Platform version is installed.
//
function InitializeSetup(): Boolean;
var
   PlatformVersionNumber : double;
 begin
   Result := FALSE;  // Assume failure
   PlatformVersionNumber := PlatformVersion(); // Get the installed Platform version as a double
   If PlatformVersionNumber >= REQUIRED_PLATFORM_VERSION then	// Check whether we have the minimum required Platform or newer
      Result := TRUE
   else
      if PlatformVersionNumber = 0.0 then
         MsgBox('No ASCOM Platform is installed. Please install Platform ' + Format('%3.1f', [REQUIRED_PLATFORM_VERSION]) + ' or later from https://www.ascom-standards.org', mbCriticalError, MB_OK)
      else 
         MsgBox('ASCOM Platform ' + Format('%3.1f', [REQUIRED_PLATFORM_VERSION]) + ' or later is required, but Platform '+ Format('%3.1f', [PlatformVersionNumber]) + ' is installed. Please install the latest Platform before continuing; you will find it at https://www.ascom-standards.org', mbCriticalError, MB_OK);
end;

// Code to enable the installer to uninstall previous versions of itself when a new version is installed
procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
  UninstallExe: String;
  UninstallRegistry: String;
begin
  if (CurStep = ssInstall) then // Install step has started
	begin
      // Create the correct registry location name, which is based on the AppId
      UninstallRegistry := ExpandConstant('Software\Microsoft\Windows\CurrentVersion\Uninstall\{#SetupSetting("AppId")}' + '_is1');
      // Check whether an extry exists
      if RegQueryStringValue(HKLM, UninstallRegistry, 'UninstallString', UninstallExe) then
        begin // Entry exists and previous version is installed so run its uninstaller quietly after informing the user
          MsgBox('Setup will now remove the previous version.', mbInformation, MB_OK);
          Exec(RemoveQuotes(UninstallExe), ' /SILENT', '', SW_SHOWNORMAL, ewWaitUntilTerminated, ResultCode);
          sleep(1000);    //Give enough time for the install screen to be repainted before continuing
        end
  end;
end;

//
// Register and unregister the driver with the Chooser
// We already know that the Helper is available
//
procedure RegASCOM();
var
   P: Variant;
begin
   P := CreateOleObject('ASCOM.Utilities.Profile');
   P.DeviceType := 'Switch';
   P.Register('ASCOM.ShortCircuitBigPowerSwitch.Switch', 'BigPowerSwitch Exxxtreme');
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
   P: Variant;
begin
   if CurUninstallStep = usUninstall then
   begin
     P := CreateOleObject('ASCOM.Utilities.Profile');
     P.DeviceType := 'Switch';
     P.Unregister('ASCOM.ShortCircuitBigPowerSwitch.Switch');
  end;
end;
