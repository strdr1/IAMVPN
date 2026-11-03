@echo off
title IAMVPN - Build
cd /d "%~dp0"

echo ============================================================
echo            IAMVPN  -  one-click build
echo ============================================================
echo.

:: ---- Step 1: locate Qt ----
set "QT_PREFIX="
if exist "%~dp0Qt\6.8.1\msvc2022_64\bin\qmake.exe" set "QT_PREFIX=%~dp0Qt\6.8.1\msvc2022_64"
if not defined QT_PREFIX if defined QT_DIR set "QT_PREFIX=%QT_DIR%"
if not defined QT_PREFIX if exist "F:\1\AmSalesVPN\Qt\6.8.1\msvc2022_64\bin\qmake.exe" set "QT_PREFIX=F:\1\AmSalesVPN\Qt\6.8.1\msvc2022_64"
if not defined QT_PREFIX if exist "C:\Qt\6.8.1\msvc2022_64\bin\qmake.exe" set "QT_PREFIX=C:\Qt\6.8.1\msvc2022_64"

if not defined QT_PREFIX (
    echo [ERROR] Qt not found. Install Qt or set QT_DIR. See README.md
    pause
    exit /b 1
)
echo [OK] Qt: %QT_PREFIX%

:: ---- Step 2: locate CMake ----
set "CMAKE_EXE="
where cmake >nul 2>nul && set "CMAKE_EXE=cmake"
if not defined CMAKE_EXE if exist "F:\vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=F:\vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE_EXE if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if not defined CMAKE_EXE (
    echo [ERROR] CMake not found. Install Visual Studio with C++ workload.
    pause
    exit /b 1
)
echo [OK] CMake: %CMAKE_EXE%

:: ---- Step 3: Visual Studio compiler environment (vcvars) ----
set "VCVARS="
if exist "F:\vs\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=F:\vs\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

if not defined VCVARS (
    echo [ERROR] vcvars64.bat not found ^(VS C++ compiler environment^).
    pause
    exit /b 1
)
echo [OK] Compiler env: %VCVARS%
echo.
echo Setting up compiler environment...
call "%VCVARS%" >nul

:: ---- Step 4: configure ----
echo.
echo [1/3] Configuring...
"%CMAKE_EXE%" -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="%QT_PREFIX%" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    echo [ERROR] Configure failed.
    pause
    exit /b 1
)

:: ---- Step 5: compile ----
echo.
echo [2/3] Compiling...
"%CMAKE_EXE%" --build build
if errorlevel 1 (
    echo [ERROR] Build failed.
    pause
    exit /b 1
)

:: ---- Step 6: deploy Qt libraries and run ----
echo.
echo [3/3] Deploying Qt libraries...
set "EXE=build\IAMVPN.exe"
if not exist "%EXE%" set "EXE=build\Release\IAMVPN.exe"

if exist "%EXE%" (
    "%QT_PREFIX%\bin\windeployqt.exe" --qmldir qml "%EXE%" >nul 2>nul

    :: ---- Verify bundled engine copied next to exe ----
    set "ENGINE_OK=1"
    if not exist "build\engine\bin\winws.exe" set "ENGINE_OK=0"
    if not exist "build\engine\vpn\sing-box.exe" set "ENGINE_OK=0"
    if not exist "build\engine\python\python.exe" set "ENGINE_OK=0"
    if not exist "build\engine\tgproxy\proxy\tg_ws_proxy.py" set "ENGINE_OK=0"
    if "%ENGINE_OK%"=="0" (
        echo [WARN] Some engine parts missing in build\engine. Re-copying...
        "%CMAKE_EXE%" -E copy_directory "%~dp0engine" "build\engine"
    )

    echo.
    echo ============================================================
    echo   Done. Built: %CD%\%EXE%
    echo   Bundled engine: zapret + sing-box + python + tg-proxy
    echo ============================================================
    echo.
    echo Launching...
    start "" "%EXE%"
) else (
    echo [ERROR] Built exe not found in build folder.
)

echo.
pause
