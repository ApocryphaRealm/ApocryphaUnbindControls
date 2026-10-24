@echo off
setlocal
REM Compiles tools\ini_line_check.cpp (include\IniLineCore.h: a key written into another mod's INI leaves the file as it
REM was when the value is already there, and keeps the line's own spacing and comment when it changes - 1.2.0) with MSVC
REM and runs it.
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set OUT=%TEMP%\uvc-ini-line-check-build
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /EHsc /O2 /W4 /Fo"%OUT%\\" /Fe"%OUT%\ini_line_check.exe" "%~dp0ini_line_check.cpp" >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
"%OUT%\ini_line_check.exe" %*
