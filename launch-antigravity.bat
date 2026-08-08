@echo off
setlocal enableextensions enabledelayedexpansion

echo Initializing Visual Studio C++ Developer Environment...

:: 1. Search for Visual Studio vcvars64.bat across standard installation paths
set "VCVARS="

if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "C:\Program Files\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
)

:: 2. Fallback check using vswhere if not found in standard paths
if "%VCVARS%"=="" (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE!" (
        for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -version "[18.0,19.0)" -property installationPath`) do (
            if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" (
                set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
            )
        )
    )
)

:: 3. Verify environment script presence
if "%VCVARS%"=="" (
    echo [ERROR] Could not locate vcvars64.bat. Please verify C++ Desktop Development is installed in Visual Studio Installer.
    pause
    exit /b 1
)

echo Found developer toolchain at: "%VCVARS%"
call "%VCVARS%"

:: 4. Verify compiler availability
where cl.exe >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed to initialize MSVC compiler environment.
    pause
    exit /b 1
)

echo MSVC Environment Loaded: cl.exe and Windows SDK paths ready.
echo Launching Antigravity IDE...

:: 5. Launch Antigravity IDE with inherited MSVC environment variables
set "VSCODE_GALLERY_SERVICE_URL=https://marketplace.visualstudio.com/_apis/public/gallery"
set "VSCODE_GALLERY_ITEM_URL=https://marketplace.visualstudio.com/items"
start "" antigravity-ide %*

endlocal