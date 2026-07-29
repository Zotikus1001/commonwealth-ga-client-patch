@echo off
setlocal EnableExtensions
chcp 65001 >nul

rem Resolve every build path relative to this repository, not the caller's
rem working directory.
set "REPO=%~dp0"
if "%REPO:~-1%"=="\" set "REPO=%REPO:~0,-1%"

set "MSYS_ROOT=C:\msys64"
set "MSYS_BASH=%MSYS_ROOT%\usr\bin\bash.exe"

:menu
cls
echo Commonwealth Global Agenda Client Patch
echo.
echo Repo: %REPO%
echo Output: %REPO%\out\clientpatch\dinput8.dll
echo.
echo 1. Build DEBUG client DLL
echo 2. Build RELEASE client DLL
echo 3. Exit
echo.
set "choice="
set /p "choice=Choose an option: "
if errorlevel 1 goto done

if "%choice%"=="1" goto build_debug_menu
if "%choice%"=="2" goto build_release_menu
if "%choice%"=="3" goto done
goto menu

:build_debug_menu
call :build_debug
goto menu

:build_release_menu
call :build_release
goto menu

:check_msys
rem Install only the Make and 32-bit MinGW packages used by the root Makefile.
if exist "%MSYS_BASH%" goto ensure_msys_packages

where winget >nul 2>nul
if errorlevel 1 (
    echo Missing MSYS2 build tools and winget is unavailable.
    echo Expected MSYS2 under %MSYS_ROOT%
    pause
    exit /b 1
)

echo Installing MSYS2 under %MSYS_ROOT%...
winget install --id MSYS2.MSYS2 -e --accept-package-agreements --accept-source-agreements
if errorlevel 1 exit /b %ERRORLEVEL%
if not exist "%MSYS_BASH%" (
    echo MSYS2 installed, but bash was not found under %MSYS_ROOT%.
    pause
    exit /b 1
)

:ensure_msys_packages
if exist "%MSYS_ROOT%\usr\bin\make.exe" if exist "%MSYS_ROOT%\mingw32\bin\i686-w64-mingw32-g++.exe" exit /b 0
"%MSYS_BASH%" -lc "unset CONFIG; if [ ! -f /etc/pacman.d/gnupg/pubring.kbx ]; then pacman-key --init && pacman-key --populate msys2; fi; pacman -Sy --needed --noconfirm make mingw-w64-i686-gcc"
if errorlevel 1 (
    echo MSYS2 package installation failed.
    pause
    exit /b 1
)
exit /b 0

:run_make
rem The Make mode target performs its own clean build, preventing debug and
rem release objects from sharing compiler flags.
call :check_msys
if errorlevel 1 exit /b 1
set "MAKE_TARGET=%~1"
"%MSYS_BASH%" -lc "unset CONFIG; REPO_UNIX=$(cygpath -u '%REPO%'); export PATH=/mingw32/bin:/usr/bin:$PATH; cd \"$REPO_UNIX\" && bash ./build-client-patch.sh %MAKE_TARGET%"
if errorlevel 1 (
    echo.
    echo Build failed.
    pause
    exit /b 1
)
echo.
echo Build completed:
echo %REPO%\out\clientpatch\dinput8.dll
pause
exit /b 0

:build_debug
call :run_make "debug"
exit /b %ERRORLEVEL%

:build_release
call :run_make "release"
exit /b %ERRORLEVEL%

:done
endlocal
