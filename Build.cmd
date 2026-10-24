@echo off
setlocal
set "VSROOT=C:\Program Files\Microsoft Visual Studio\2022\Community"
if defined LIGHTSHOT_VSROOT set "VSROOT=%LIGHTSHOT_VSROOT%"
call "%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat" x86 10.0.22621.0 -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
set "CC=C:\msys64\clang64\bin\clang.exe"
set "LD=C:\msys64\clang64\bin\lld-link.exe"
if defined LIGHTSHOT_CLANG set "CC=%LIGHTSHOT_CLANG%"
if defined LIGHTSHOT_LINK set "LD=%LIGHTSHOT_LINK%"
if not exist "%CC%" (echo Missing Clang compiler: "%CC%" & exit /b 2)
if not exist "%LD%" (echo Missing LLD linker: "%LD%" & exit /b 2)
set "SOURCE=%~dp0"
set "OUTPUT=%~dp0build"
if not "%~1"=="" set "OUTPUT=%~f1"
if not exist "%OUTPUT%" mkdir "%OUTPUT%"
pushd "%OUTPUT%"
set "FLAGS=--driver-mode=cl --target=i686-pc-windows-msvc /nologo /std:c++20 /EHsc /MT /O2 /Z7 /W4 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS"
"%CC%" %FLAGS% /c "%SOURCE%scripts\shader_builder.cpp" /Foshader_builder.obj
if errorlevel 1 exit /b 1
"%LD%" /machine:x86 /out:shader_builder.exe shader_builder.obj kernel32.lib d3dcompiler.lib
if errorlevel 1 exit /b 1
shader_builder.exe "%SOURCE%src\gpu_color.hlsl" "%SOURCE%src\gpu_color_bytecode.h"
if errorlevel 1 exit /b 1
"%CC%" %FLAGS% /c "%SOURCE%src\color.cpp" /Focolor.obj
if errorlevel 1 exit /b 1
"%CC%" %FLAGS% /I"%SOURCE%vendor\DirectXMath\Inc" /c "%SOURCE%src\capture_dxgi.cpp" /Focapture_dxgi.obj
if errorlevel 1 exit /b 1
"%CC%" %FLAGS% /c "%SOURCE%src\gpu_color.cpp" /Fogpu_color.obj
if errorlevel 1 exit /b 1
"%CC%" %FLAGS% /I"%SOURCE%vendor\DirectXMath\Inc" /c "%SOURCE%src\dll.cpp" /Fodll.obj
if errorlevel 1 exit /b 1
"%LD%" /dll /debug /pdbaltpath:DXGIODScreenshot.pdb /machine:x86 /dynamicbase /nxcompat /opt:ref /opt:icf /out:DXGIODScreenshot.dll /def:"%SOURCE%src\DXGIODScreenshot.def" color.obj gpu_color.obj capture_dxgi.obj dll.obj kernel32.lib user32.lib gdi32.lib d3d11.lib dxgi.lib windowsapp.lib ole32.lib
if errorlevel 1 exit /b 1
"%CC%" %FLAGS% /c "%SOURCE%tests\color_tests.cpp" /Focolor_tests.obj
if errorlevel 1 exit /b 1
"%LD%" /machine:x86 /subsystem:console /dynamicbase /nxcompat /out:color_tests.exe color.obj color_tests.obj kernel32.lib
if errorlevel 1 exit /b 1
"%CC%" %FLAGS% /c "%SOURCE%tests\capture_probe.cpp" /Focapture_probe.obj
if errorlevel 1 exit /b 1
"%LD%" /debug /machine:x86 /subsystem:console /dynamicbase /nxcompat /out:capture_probe.exe capture_probe.obj kernel32.lib user32.lib gdi32.lib dbghelp.lib
if errorlevel 1 exit /b 1
"%CC%" %FLAGS% /I"%SOURCE%vendor\DirectXMath\Inc" /c "%SOURCE%tests\hdr_calibration.cpp" /Fohdr_calibration.obj
if errorlevel 1 exit /b 1
"%LD%" /debug /machine:x86 /subsystem:console /dynamicbase /nxcompat /out:hdr_calibration.exe hdr_calibration.obj kernel32.lib user32.lib gdi32.lib d3d11.lib dxgi.lib dwmapi.lib
if errorlevel 1 exit /b 1
"%CC%" %FLAGS% /c "%SOURCE%tests\host_capture.cpp" /Fohost_capture.obj
if errorlevel 1 exit /b 1
"C:\msys64\clang64\bin\llvm-rc.exe" /I "%SOURCE%tests" /FO host.res "%SOURCE%tests\host.rc"
if errorlevel 1 exit /b 1
"%LD%" /debug /machine:x86 /subsystem:console /dynamicbase /nxcompat /out:host_capture.exe host_capture.obj host.res kernel32.lib user32.lib gdi32.lib dbghelp.lib
if errorlevel 1 exit /b 1
"%CC%" %FLAGS% /c "%SOURCE%tests\gpu_color_tests.cpp" /Fogpu_color_tests.obj
if errorlevel 1 exit /b 1
"%LD%" /machine:x86 /out:gpu_color_tests.exe color.obj gpu_color.obj gpu_color_tests.obj kernel32.lib d3d11.lib
if errorlevel 1 exit /b 1
gpu_color_tests.exe
if errorlevel 1 exit /b 1
color_tests.exe
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
