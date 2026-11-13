@echo off
rem COLD ENTRY visual review: runs a bot-only round, photographs a fixed set of views, then quits.
rem Images: Saved\Shots\*.png   Log: Saved\shots.log
rem Add -celineup to also photograph one of every scanned prop (unrotated, outside the south wall).
setlocal
set "UE=C:\Program Files\Epic Games\UE_5.8"
if not exist "%~dp0Saved" mkdir "%~dp0Saved"
start "" /wait "%UE%\Engine\Binaries\Win64\UnrealEditor.exe" "%~dp0ColdEntry.uproject" -game -windowed -ResX=1600 -ResY=900 -ceshots -nosplash -unattended -nosound -abslog="%~dp0Saved\shots.log" %*
echo Shots written to Saved\Shots
endlocal
