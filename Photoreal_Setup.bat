@echo off
rem COLD ENTRY photoreal pass: downloads CC0 scanned assets from Poly Haven, imports them into /Game/Photoreal.
rem Close the Unreal Editor first. Logs: Saved\photoreal_fetch.log, Saved\photoreal_import.log
setlocal
set "UE=C:\Program Files\Epic Games\UE_5.8"
set "PY=%UE%\Engine\Binaries\ThirdParty\Python3\Win64\python.exe"
set "PROJ=%~dp0ColdEntry.uproject"
if not exist "%~dp0Saved" mkdir "%~dp0Saved"
echo [1/2] Downloading Poly Haven assets...
"%PY%" "%~dp0Tools\polyhaven.py" fetch > "%~dp0Saved\photoreal_fetch.log" 2>&1
echo [2/2] Importing into Unreal and building materials (takes a few minutes)...
"%UE%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJ%" -run=pythonscript -script="%~dp0Tools\ue_import_photoreal.py" -unattended -nosplash -nopause -stdout > "%~dp0Saved\photoreal_import.log" 2>&1
findstr /C:"[CE-Photoreal]" "%~dp0Saved\photoreal_import.log"
echo Done.
endlocal
