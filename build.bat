@echo off
setlocal

rem Usage: build.bat [game folder]
rem Builds every mod in Release. Deploys to the game folder when one is given, or when
rem this repo is cloned into a folder inside the game folder, such as <game>\_repo.

set "ROOT=%~dp0"
set "H3_ROOT=%~1"
if "%H3_ROOT%"=="" if exist "%ROOT%..\h3hota.exe" set "H3_ROOT=%ROOT%.."
if "%H3_ROOT%"=="" if exist "%ROOT%..\Heroes3.exe" set "H3_ROOT=%ROOT%.."

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo vswhere.exe was not found. Install Visual Studio 2022 with the C++ desktop workload.
    exit /b 1
)
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%I"
if not defined MSBUILD (
    echo MSBuild was not found.
    exit /b 1
)

echo Building h3-mods...
"%MSBUILD%" "%ROOT%h3-mods.sln" /p:Configuration=Release /p:Platform=x86 /p:PlatformToolset=v143 /p:WindowsTargetPlatformVersion=10.0 /m /v:minimal
if errorlevel 1 (
    echo Build failed.
    exit /b 1
)

if "%H3_ROOT%"=="" (
    echo.
    echo Built into "%ROOT%bin\Release". No game folder given, so nothing was deployed.
    exit /b 0
)

set "PACKS=%H3_ROOT%\_HD3_Data\Packs"
call :Deploy "%ROOT%bin\Release\H3.KeyRemap.dll" "%PACKS%\KeyRemap"
if errorlevel 1 exit /b 1
call :Deploy "%ROOT%bin\Release\H3.MonsterHoverInfo.dll" "%PACKS%\MonsterHoverInfo"
if errorlevel 1 exit /b 1
call :Deploy "%ROOT%bin\Release\H3.AutoCombatAI.dll" "%PACKS%\AutoCombatAI"
if errorlevel 1 exit /b 1
call :Deploy "%ROOT%bin\Release\version.dll" "%H3_ROOT%"
if errorlevel 1 exit /b 1
if not exist "%H3_ROOT%\verreal.dll" copy /Y "%WINDIR%\SysWOW64\version.dll" "%H3_ROOT%\verreal.dll" >nul

echo.
echo Done. TextColor is built but not deployed, because SoD_SP ships its own copy.
exit /b 0

:Deploy
if not exist "%~2" mkdir "%~2"
copy /Y "%~1" "%~2\" >nul
if errorlevel 1 (
    echo Deploy of %~nx1 failed. Close the game and run this again.
    exit /b 1
)
echo Deployed %~nx1 to %~2
exit /b 0
