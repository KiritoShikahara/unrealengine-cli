@echo off
setlocal
cd /d "%~dp0"

where cargo >nul 2>nul
if %errorlevel%==0 goto build

if exist "%USERPROFILE%\.cargo\bin\cargo.exe" (
    set "PATH=%USERPROFILE%\.cargo\bin;%PATH%"
    goto build
)

echo [unrealengine-cli] cargo not found.
echo   Y: install Rust ^(rustup^) and build from this source ^(several minutes^)
echo   N: download the published release ^(scripts\install.ps1, fast^)
choice /c YN /m "Install Rust?"
if errorlevel 2 goto release

call :ensure_msvc
if errorlevel 1 goto fail

echo [unrealengine-cli] Downloading rustup-init.exe ...
curl -fsSL -o "%TEMP%\rustup-init.exe" https://win.rustup.rs/x86_64
if errorlevel 1 goto fail
"%TEMP%\rustup-init.exe" -y --default-toolchain stable --profile minimal
if errorlevel 1 goto fail
del "%TEMP%\rustup-init.exe"
set "PATH=%USERPROFILE%\.cargo\bin;%PATH%"

:build
echo [unrealengine-cli] Installing from local source: cargo install --path . --locked
cargo install --path . --locked
if errorlevel 1 goto fail
goto done

:release
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\install.ps1"
if errorlevel 1 goto fail

:done
echo.
echo [unrealengine-cli] Install complete.
where unrealengine-cli >nul 2>nul && (unrealengine-cli --version) || (
    if exist "%USERPROFILE%\.cargo\bin\unrealengine-cli.exe" ("%USERPROFILE%\.cargo\bin\unrealengine-cli.exe" --version) else ("%LOCALAPPDATA%\unrealengine-cli\bin\unrealengine-cli.exe" --version)
    echo Open a new terminal to refresh PATH.
)
pause
exit /b 0

:ensure_msvc
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        echo [unrealengine-cli] MSVC C++ build tools found: %%i
        exit /b 0
    )
)
echo [unrealengine-cli] Installing Visual Studio Build Tools 2022 ^(C++^). Approve the admin prompt. This takes 10-30 minutes.
echo [unrealengine-cli] Downloading vs_buildtools.exe ...
curl -fsSL -o "%TEMP%\vs_buildtools.exe" https://aka.ms/vs/17/release/vs_buildtools.exe
if errorlevel 1 exit /b 1
"%TEMP%\vs_buildtools.exe" --quiet --wait --norestart --nocache --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended
set "VSRC=%errorlevel%"
del "%TEMP%\vs_buildtools.exe"
if "%VSRC%"=="3010" (
    echo [unrealengine-cli] Build Tools installed. A reboot is recommended later.
    exit /b 0
)
if not "%VSRC%"=="0" (
    echo [unrealengine-cli] Build Tools installer exit code %VSRC%
    exit /b 1
)
exit /b 0

:fail
echo.
echo [unrealengine-cli] Install FAILED.
echo   If the error mentions link.exe, install "Visual Studio Build Tools" ^(Desktop development with C++^).
pause
exit /b 1
