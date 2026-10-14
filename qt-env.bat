@echo off
REM ============================================================
REM  Locate the Qt toolchain and put it on PATH.
REM  Shared by build.bat and package.bat. Do not run directly.
REM
REM  Call it with a plain "call"; it deliberately does NOT use
REM  setlocal, so QTDIR / QTROOT / PATH changes reach the caller.
REM  It exits with 1 when Qt cannot be found, after printing why.
REM
REM  No machine-specific path is stored in this repository. Qt is
REM  located in this order:
REM    1. the QTDIR environment variable
REM    2. derived from windeployqt.exe / qmake.exe already on PATH
REM    3. if QTDIR is an installer ROOT, its first <version>\mingw_64 kit
REM    4. the Qt online-installer default C:\Qt
REM  To set it permanently:
REM    setx QTDIR "C:\Qt\6.12.0\mingw_64"
REM ============================================================

if not defined QTDIR for /f "delims=" %%I in ('where windeployqt.exe 2^>nul') do if not defined QTDIR set "QTDIR=%%~dpI"
if not defined QTDIR for /f "delims=" %%I in ('where qmake.exe 2^>nul') do if not defined QTDIR set "QTDIR=%%~dpI"
REM  No trailing-separator normalisation: a value that already lacks one would
REM  lose a real character to %VAR:~0,-1%, and Windows treats "a\\b" as "a\b".
if not defined QTDIR set "QTDIR=C:\Qt"

REM  Descend into a kit if QTDIR turned out to be an installer root.
REM  Two steps on purpose: "for /d" matches one wildcard level only, so
REM  "<root>\*\mingw_64" silently yields nothing.
set "QTROOT=%QTDIR%"
if not exist "!QTROOT!\bin\windeployqt.exe" (
  for /d %%D in ("!QTROOT!\*") do (
    if exist "%%~fD\mingw_64\bin\windeployqt.exe" if not exist "!QTROOT!\bin\windeployqt.exe" set "QTROOT=%%~fD\mingw_64"
  )
)
if not exist "!QTROOT!\bin\windeployqt.exe" (
  echo.
  echo *** FAILED: Qt not found ***
  echo     Looked in: %QTDIR%
  echo     Point QTDIR at your Qt kit, e.g.
  echo         setx QTDIR "C:\Qt\6.12.0\mingw_64"
  echo     It must be a MinGW Qt -- MSVC Qt is not supported yet.
  exit /b 1
)

REM  The Qt online installer keeps cmake/ninja/gcc under <root>\Tools, beside
REM  the kits rather than inside them. If they are already on PATH this is a
REM  no-op. Delayed expansion is mandatory below: inside one parenthesised
REM  block %PATH% would be expanded once at parse time, so each set would
REM  clobber the previous one and only the last entry would survive.
set "QTOOLS="
if exist "%QTDIR%\Tools" set "QTOOLS=%QTDIR%"
if not defined QTOOLS if exist "!QTROOT!\Tools" set "QTOOLS=!QTROOT!"
if not defined QTOOLS if exist "!QTROOT!\..\..\Tools" for %%R in ("!QTROOT!\..\..") do if not defined QTOOLS set "QTOOLS=%%~fR"
if defined QTOOLS (
  for /d %%M in ("!QTOOLS!\Tools\mingw*_64") do set "PATH=%%~fM\bin;!PATH!"
  if exist "!QTOOLS!\Tools\CMake_64\bin" set "PATH=!QTOOLS!\Tools\CMake_64\bin;!PATH!"
  if exist "!QTOOLS!\Tools\Ninja" set "PATH=!QTOOLS!\Tools\Ninja;!PATH!"
)
set "PATH=!QTROOT!\bin;!PATH!"
exit /b 0
