@if not defined _echo @echo off
setlocal EnableDelayedExpansion

set __ThisScriptShort=%0

if /i "%1" == "/?"    goto HelpVarCall
if /i "%1" == "-?"    goto HelpVarCall
if /i "%1" == "/h"    goto HelpVarCall
if /i "%1" == "-h"    goto HelpVarCall
if /i "%1" == "/help" goto HelpVarCall
if /i "%1" == "-help" goto HelpVarCall

if defined BUILDVARS_DONE goto :AfterVarSetup

goto :NormalVarCall

:HelpVarCall
call %~dp0buildvars-setup.cmd -help
exit /b 1

:NormalVarCall
call %~dp0buildvars-setup.cmd %*

IF NOT ERRORLEVEL 1 goto AfterVarSetup
echo Setting build variables failed.
exit /b %ERRORLEVEL%

:AfterVarSetup

echo Commencing build of native components for %__BuildOS%.%__BuildArch%.%__BuildType%
echo.

:: Set the environment for the native build
set __VCBuildArch=x86_amd64
if /i "%__BuildArch%" == "x86" (set __VCBuildArch=x86)

:: VS2017 changed the location of vcvarsall.bat.
if /i "%__VSVersion%" == "vs2017" (
    call "!VS%__VSProductVersion%COMNTOOLS!\..\..\VC\Auxiliary\Build\vcvarsall.bat" %__VCBuildArch%
) else (
    call "!VS%__VSProductVersion%COMNTOOLS!\..\..\VC\vcvarsall.bat" %__VCBuildArch%
)

:: Regenerate the VS solution
pushd "%__IntermediatesDir%"
call "%__SourceDir%\Native\gen-buildsys-win.bat" "%__ProjectDir%\src\Native" %__VSVersion% %__BuildArch% 
popd

if exist "%__IntermediatesDir%\install.vcxproj" goto BuildNative
echo Failed to generate native component build project!
exit /b 1

:BuildNative
%_msbuildexe% /ConsoleLoggerParameters:ForceNoAlign "%__IntermediatesDir%\install.vcxproj" %__MSBCleanBuildArgs% %__ExtraMsBuildParams% /nologo /maxcpucount /nodeReuse:false /p:Configuration=%__BuildType% /p:Platform=%__BuildArch% /fileloggerparameters:Verbosity=normal;LogFile="%__NativeBuildLog%"
IF NOT ERRORLEVEL 1 goto AfterNativeBuild
echo Native component build failed. Refer !__NativeBuildLog! for details.
exit /b 1

:AfterNativeBuild
endlocal