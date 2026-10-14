@echo off
REM ============================================================
REM  SerialMate build / test / run
REM
REM    build.bat          configure + build + launch
REM    build.bat test     configure + build + run unit tests
REM
REM  Qt is located by qt-env.bat (env var, then PATH, then C:\Qt).
REM  Nothing machine-specific is stored in this repository. To
REM  override permanently:  setx QTDIR "C:\Qt\6.12.0\mingw_64"
REM
REM  GCC must come before any other MinGW on PATH, and Qt's bin must
REM  be present or the exe fails with a missing Qt6Core.dll.
REM ============================================================
setlocal EnableDelayedExpansion

call "%~dp0qt-env.bat"
if errorlevel 1 exit /b 1

cd /d "%~dp0"

echo === configure (Qt: !QTROOT!) ===
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=!QTROOT!
if errorlevel 1 goto :fail

echo === build ===
cmake --build build
if errorlevel 1 goto :fail

if /i "%~1"=="test" goto :test

echo.
echo === launching SerialMate ===
start "" "build\SerialMate.exe"
goto :done

:test
echo.
echo === unit tests ===
ctest --test-dir build --output-on-failure
if errorlevel 1 goto :fail
goto :done

:fail
echo.
echo *** FAILED ***
exit /b 1

:done
echo.
echo Done.
endlocal
