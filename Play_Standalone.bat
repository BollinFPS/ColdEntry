@echo off
rem Runs the game standalone (uncooked) from the editor binary. Add -ceauto for a bot-only soak test.
start "" "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%~dp0ColdEntry.uproject" -game -windowed -ResX=1600 -ResY=900 -log %*
