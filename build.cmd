@echo off
rem Configures and builds a Release plugin into build\plugin with clang-cl and the MSVC libraries.
setlocal
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if not defined VSDIR (
	echo Visual Studio with C++ tools not found.
	exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || exit /b 1
rem Visual Studio's own clang-cl always matches the version its STL requires.
if exist "%VSDIR%\VC\Tools\Llvm\x64\bin\clang-cl.exe" set CLANG=%VSDIR%\VC\Tools\Llvm\x64\bin\clang-cl.exe
for /f "delims=" %%i in ('where clang-cl 2^>nul') do if not defined CLANG set CLANG=%%i
if not defined CLANG if exist "%ProgramFiles%\LLVM\bin\clang-cl.exe" set CLANG=%ProgramFiles%\LLVM\bin\clang-cl.exe
if not defined CLANG (
	echo clang-cl not found. Install LLVM: winget install LLVM.LLVM
	exit /b 1
)
set CLANG=%CLANG:\=/%
cmake -S "%~dp0." -B "%~dp0build" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_C_COMPILER=%CLANG%" "-DCMAKE_CXX_COMPILER=%CLANG%" "-DCMAKE_ASM_COMPILER=%CLANG%" || exit /b 1
cmake --build "%~dp0build" || exit /b 1
