@echo off
rem Copies the built plugin (DPDFNet.dll + DPDFNet folder) into Equalizer APO's VSTPlugins folder.
setlocal
set SRC=%~dp0build\plugin
set DEST=%ProgramFiles%\EqualizerAPO\VSTPlugins
if not exist "%SRC%\DPDFNet.dll" (
	echo Build the plugin first with build.cmd.
	exit /b 1
)
if not exist "%DEST%" (
	echo Equalizer APO not found at %DEST%.
	exit /b 1
)

net session >nul 2>&1
if errorlevel 1 (
	powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs -Wait"
	exit /b
)

copy /y "%SRC%\DPDFNet.dll" "%DEST%\" >nul || goto failed
robocopy "%SRC%\DPDFNet" "%DEST%\DPDFNet" /e /njh /njs /ndl /nfl >nul
if errorlevel 8 goto failed
echo Installed to %DEST%
pause
exit /b 0

:failed
echo Copy failed. If the plugin is in use, comment it out in your Equalizer APO config,
echo wait a few seconds for the audio engine to release it, and run this again.
pause
exit /b 1
