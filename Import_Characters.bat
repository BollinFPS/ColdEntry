@echo off
rem COLD ENTRY: copies Epic's UE 5.8 template mannequin, animations and weapons into this project
rem (same /Game paths as the templates, so their internal references stay valid), then inspects them.
rem Close the Unreal Editor first. Log: Saved\characters_import.log
setlocal
set "UE=C:\Program Files\Epic Games\UE_5.8"
set "TPL=%UE%\Templates\TemplateResources"
set "PROJ=%~dp0ColdEntry.uproject"
set "LOG=%~dp0Saved\characters_import.log"
if not exist "%~dp0Saved" mkdir "%~dp0Saved"
echo [CE-Copy] start > "%LOG%"
echo [1/2] Copying template characters, animations and weapons...
robocopy "%TPL%\High\Characters\Content" "%~dp0Content\Characters" /E /NP /NFL /NDL >> "%LOG%"
echo [CE-Copy] characters exit %ERRORLEVEL% >> "%LOG%"
robocopy "%TPL%\Standard\Weapons\Content" "%~dp0Content\Weapons" /E /NP /NFL /NDL >> "%LOG%"
echo [CE-Copy] weapons exit %ERRORLEVEL% >> "%LOG%"
robocopy "%TPL%\Standard\Variant_Shooter\Content\Anims" "%~dp0Content\Variant_Shooter\Anims" ABP_TP_Rifle.uasset ABP_TP_Pistol.uasset /NP /NFL /NDL >> "%LOG%"
echo [CE-Copy] anims exit %ERRORLEVEL% >> "%LOG%"
echo [2/2] Inspecting assets (a minute or two)...
"%UE%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJ%" -run=pythonscript -script="%~dp0Tools\inspect_models.py" -unattended -nosplash -nopause -stdout > "%~dp0Saved\characters_inspect.log" 2>&1
echo [CE-Copy] inspect exit %ERRORLEVEL% >> "%LOG%"
echo [CE-Copy] done >> "%LOG%"
echo Done.
endlocal
