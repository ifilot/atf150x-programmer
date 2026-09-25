@echo off
setlocal

rem Build matched PLCC-44 validation images with WinCUPL II.
rem CUPL_ROOT may be overridden before invoking this script.

if not defined CUPL_ROOT set "CUPL_ROOT=C:\WINCUPL"

set "CUPL_EXE=%CUPL_ROOT%\Shared\cupl.exe"
set "LIBCUPL=%CUPL_ROOT%\Shared\CUPL.DL"
set "PATH=%PATH%;%CUPL_ROOT%\WinCupl;%CUPL_ROOT%\WinCupl\Fitters;%CUPL_ROOT%\Shared"

if not exist "%CUPL_EXE%" (
    echo Error: cupl.exe not found at "%CUPL_EXE%".
    echo Set CUPL_ROOT to the WinCUPL installation directory.
    exit /B 1
)

pushd "%~dp0"

call :build atf1502as-plcc44 f1502ispplcc44 find1502.exe P1502C44
if errorlevel 1 goto :error

call :build atf1504as-plcc44 f1504ispplcc44 find1504.exe P1504C44
if errorlevel 1 goto :error

echo Built both validation JEDEC files.
popd
exit /B 0

:build
set "BASENAME=%~1"
set "CUPL_DEVICE=%~2"
set "FITTER_EXE=%CUPL_ROOT%\WinCupl\Fitters\%~3"
set "FITTER_DEVICE=%~4"

if not exist "%FITTER_EXE%" (
    echo Error: fitter not found at "%FITTER_EXE%".
    exit /B 1
)

for %%e in (abs doc err fit io jed lst mx pin pla sim tt2 tt3 tmv vt) do (
    if exist "%BASENAME%.%%e" del /Q "%BASENAME%.%%e"
)

echo Compiling %BASENAME%.pld...
"%CUPL_EXE%" -a -l -e -x -f -b -j -m0 -n %CUPL_DEVICE% "%BASENAME%.pld"
if errorlevel 1 exit /B 1

if not exist "%BASENAME%.tt2" (
    echo Error: CUPL did not produce %BASENAME%.tt2.
    exit /B 1
)

echo Fitting %FITTER_DEVICE% with JTAG enabled...
"%FITTER_EXE%" -i "%CD%\%BASENAME%.tt2" -CUPL -dev %FITTER_DEVICE% -str JTAG ON
if errorlevel 1 exit /B 1

if not exist "%BASENAME%.jed" (
    echo Error: fitter did not produce %BASENAME%.jed.
    exit /B 1
)

echo Built %CD%\%BASENAME%.jed
exit /B 0

:error
echo Build failed.
popd
exit /B 1

