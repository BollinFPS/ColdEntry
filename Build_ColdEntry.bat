@echo off
rem Builds the ColdEntry editor module with UE 5.8 and writes Saved\build.log
setlocal
set "UE=C:\Program Files\Epic Games\UE_5.8"
set "PROJ=%~dp0ColdEntry.uproject"
if not exist "%~dp0Saved" mkdir "%~dp0Saved"
echo Building ColdEntryEditor... (log: Saved\build.log)
call "%UE%\Engine\Build\BatchFiles\Build.bat" ColdEntryEditor Win64 Development -Project="%PROJ%" -WaitMutex -NoHotReloadFromIDE > "%~dp0Saved\build.log" 2>&1
if errorlevel 1 (
  echo BUILD FAILED >> "%~dp0Saved\build.log"
  echo BUILD FAILED - see Saved\build.log
) else (
  echo BUILD SUCCEEDED >> "%~dp0Saved\build.log"
  echo BUILD SUCCEEDED
)
endlocal
