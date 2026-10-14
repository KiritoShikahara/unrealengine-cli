@echo off
setlocal
rem roc launcher for account 2: starts the orchestrator session (roles run as separate processes underneath).
rem Usage: roc-2.cmd [project folder]   (no argument = the folder this file is in)
rem After install.bat, this file alone can be copied into any project folder.
rem Env: CC_MODEL (default opus), CC_ADHD=0 to turn /i-have-adhd off, CC_EXTRA_TOOLS=WebFetch,...
set "ROC_ACCOUNT=2"
if "%CC_ADHD%"=="" set "CC_ADHD=1"
set "ROC_JS=%~dp0bin\roc.mjs"
if not exist "%ROC_JS%" if exist "%USERPROFILE%\.claude-roc\root.txt" set /p ROC_ROOT=<"%USERPROFILE%\.claude-roc\root.txt"
if not exist "%ROC_JS%" set "ROC_JS=%ROC_ROOT%\bin\roc.mjs"
if not exist "%ROC_JS%" (echo roc not found. Run install.bat in the role-orchestrator-cli folder first. & pause & exit /b 1)
if "%~1"=="" (node "%ROC_JS%" start "%~dp0.") else (node "%ROC_JS%" %*)
pause
