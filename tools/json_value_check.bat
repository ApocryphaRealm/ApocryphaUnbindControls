@echo off
setlocal
REM Compiles tools\json_value_check.cpp (include\JsonValueCore.h: a value written into another mod's JSON leaves the file
REM as it was when the value is already there, and is replaced in place when it changes - 1.2.0) with MSVC and runs it.
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set OUT=%TEMP%\uvc-json-value-check-build
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /EHsc /O2 /W4 /Fo"%OUT%\\" /Fe"%OUT%\json_value_check.exe" "%~dp0json_value_check.cpp" >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
"%OUT%\json_value_check.exe" %*
