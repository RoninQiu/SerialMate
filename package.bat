@echo off
REM ============================================================
REM  SerialMate packaging
REM  ->  dist\SerialMate\   (runnable folder)
REM  ->  dist\SerialMate-<ver>.zip   (same folder, ready to send)
REM
REM    package.bat        build Release + stage a folder + zip it
REM
REM  Output is a folder, not an installer: SerialMate.exe, the
REM  Qt runtime it actually needs and SerialMate.ini side by side.
REM  No registry, no %APPDATA%, no Program Files. Copy the folder
REM  to another machine and double-click SerialMate.exe. The zip
REM  holds exactly that folder, SerialMate\ as its top level, so
REM  unpacking it does not scatter 11 files into the download dir.
REM
REM  windeployqt is conservative -- it ships Qt6Network, Qt6Svg,
REM  tls\, networkinformation\, iconengines\ and imageformats\
REM  even though none of them are in our dependency closure:
REM
REM    objdump -p SerialMate.exe -> Qt6Core Qt6Gui Qt6SerialPort
REM                                 Qt6Widgets libgcc libstdc++
REM    Qt6SerialPort  -> Qt6Core        (NOT Qt6Network)
REM    Qt6Gui        -> Qt6Core
REM    Qt6Widgets    -> Qt6Core Qt6Gui
REM
REM  They are excluded at deploy time rather than deleted
REM  afterwards, so THIS SCRIPT NEVER REMOVES ANYTHING. The
REM  trade-off is that dist\SerialMate is never cleaned for you:
REM  if you switch Qt versions, delete that folder by hand first,
REM  otherwise DLLs from the old runtime pile up next to the new
REM  ones and the exe will load whichever it finds first.
REM
REM  The zip step inherits that trade-off, and it matters more
REM  there: a delivered archive is not supposed to carry junk.
REM  So the step prints the staged file count next to the archive
REM  size. A number that jumps between runs means something got
REM  left behind -- delete dist\SerialMate and run this again.
REM
REM  Qt is located by qt-env.bat (env var, then PATH, then C:\Qt).
REM  Nothing machine-specific is stored in this repository. To
REM  override permanently:  setx QTDIR "C:\Qt\6.12.0\mingw_64"
REM ============================================================
setlocal EnableDelayedExpansion

REM  Anchor to the script's own folder first, before anything
REM  relative is touched. Double-clicking the .bat starts in
REM  C:\Windows\System32, and the version probe below reads
REM  CMakeLists.txt out of the current directory.
cd /d "%~dp0"

call "%~dp0qt-env.bat"
if errorlevel 1 goto :fail

set "OUT=dist\SerialMate"

REM  Zip via %WINDIR%\System32\tar.exe -- that is bsdtar, which
REM  has shipped inside Windows since 10 1803. -a picks the codec
REM  from the .zip extension. No 7-Zip, no PowerShell, no extra
REM  dependency for anyone who just wants to send the file.
set "TAR=%WINDIR%\System32\tar.exe"

REM  The version comes out of project() in CMakeLists.txt so the
REM  two cannot drift apart. cmake_minimum_required also contains
REM  the word VERSION, but it yields no third token, so the loop
REM  skips that line and only the project() value survives.
set "VER="
for /f "tokens=3" %%V in ('findstr /c:"VERSION" CMakeLists.txt') do set "VER=%%~V"
if not defined VER goto :nover
set "PKG=dist\SerialMate-%VER%.zip"

echo === configure + build (Release) ===
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=!QTROOT!
if errorlevel 1 goto :fail
cmake --build build
if errorlevel 1 goto :fail

echo.
echo === staging SerialMate.exe ===
if not exist "%OUT%" mkdir "%OUT%"
if errorlevel 1 goto :fail
copy /y "build\SerialMate.exe" "%OUT%\" >nul
if errorlevel 1 goto :fail

echo === staging Qt runtime (pruned to the real dependency closure) ===
"!QTROOT!\bin\windeployqt.exe" --release ^
  --no-translations ^
  --no-system-d3d-compiler ^
  --no-opengl-sw ^
  --no-quick-import ^
  --no-network ^
  --no-svg ^
  --skip-plugin-types tls,networkinformation,iconengines,generic,imageformats ^
  "%OUT%\SerialMate.exe"
if errorlevel 1 goto :fail

echo.
echo === staging SerialMate.ini (data yes, session state no) ===
REM  The ini is shipped on purpose: the multi-string rows with their
REM  per-item intervals are the reason to run this build instead of a
REM  stock SSCOM. But three keys under [window] are dropped:
REM
REM    state         the saveState() blob -- splitter widths and the
REM                  multi-string panel's visibility, as they happened
REM                  to be on the machine that last closed the program
REM    geometry      last window size/position
REM    layoutVersion  layout structure version
REM
REM  They are session furniture, not user data. Copying them verbatim
REM  shipped a package whose multi-string panel was invisible, which
REM  defeats the point of shipping the ini at all. Everything that
REM  matters ([serial] [sendItems] [send] [loop] [recv]) is kept.
REM
REM  findstr is used rather than a PowerShell one-liner because it is
REM  byte-transparent: the sendItems rows hold Chinese comments, and
REM  verified byte count 13623 -> 13300 with the CJK character count
REM  unchanged at 360. It removes exactly the three lines named above.
REM
REM  A fresh clone has no ini yet, so its absence is normal.
if exist "build\SerialMate.ini" (
  findstr /v /b /c:"state=" /b /c:"geometry=" /b /c:"layoutVersion=" ^
    "build\SerialMate.ini" > "%OUT%\SerialMate.ini"
  if errorlevel 1 goto :fail
) else (
  echo     build\SerialMate.ini not found - shipping without it.
  echo     SerialMate creates a default one on first run.
)

echo.
echo === zipping: %PKG% ===
if not exist "%TAR%" (
  echo     %TAR% not found.
  goto :fail
)
REM  tar -c truncates an existing archive, so a rerun of the same
REM  version overwrites cleanly instead of appending a second copy.
REM  Verified: two -c passes over the same tree leave one entry.
"%TAR%" -a -c -f "%PKG%" -C dist SerialMate
if errorlevel 1 goto :fail

set "CNT=?"
REM  /s so the count is recursive. Stale DLLs left over from an
REM  older Qt land in subdirectories just as easily as at the top,
REM  and those are exactly what this number is meant to expose.
for /f %%N in ('dir /b /s /a-d "%OUT%" ^| find /c /v ""') do set "CNT=%%N"

echo.
echo === staged: %OUT%  (%CNT% files) ===
for %%F in ("%OUT%") do echo     %%~nxF
echo.
echo === zipped: %PKG% ===
for %%F in ("%PKG%") do echo     %%~nxF   %%~zF bytes
echo.
echo     The archive holds SerialMate\ as its top level. Unpack it
echo     anywhere and double-click SerialMate.exe.
echo.
echo     Run it with Qt NOT on PATH to prove it is self-contained:
echo         cd /d %OUT% ^& SerialMate.exe
echo.

:done
endlocal
exit /b 0

:nover
echo.
echo *** FAILED: no project VERSION found in CMakeLists.txt ***
echo     The zip name comes from it. Expected a line like:
echo         project(SerialMate VERSION x.y.z LANGUAGES CXX)
endlocal
exit /b 1

:fail
echo.
echo *** FAILED ***
endlocal
exit /b 1