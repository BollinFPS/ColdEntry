@echo off
rem COLD ENTRY: imports the SWAT operator and its animation clips into /Game/Characters/Swat.
rem Sources: SourceArt\Characters\Swat\character.fbx and SourceArt\Characters\Swat\animations\*.fbx
rem Close the Unreal Editor first. Log: Saved\Logs\ColdEntry.log   Report: Saved\Inspect\swat.json
setlocal
set "UE=C:\Program Files\Epic Games\UE_5.8"
set "PROJ=%~dp0ColdEntry.uproject"
if not exist "%~dp0Saved" mkdir "%~dp0Saved"
echo Importing the SWAT character (a minute or two)...
"%UE%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJ%" -run=pythonscript -script="%~dp0Tools\ue_import_swat.py" -unattended -nosplash -nopause -stdout > "%~dp0Saved\swat_import.log" 2>&1
findstr /C:"[CE-Swat]" "%~dp0Saved\Logs\ColdEntry.log"
echo Done.
endlocal
