@echo off
rem Compila sem CMake, direto com o g++ (MinGW / TDM-GCC do Dev-C++).
rem Se o g++ nao estiver no PATH, ajuste a linha abaixo.
if exist "C:\Program Files (x86)\Embarcadero\Dev-Cpp\TDM-GCC-64\bin" set "PATH=C:\Program Files (x86)\Embarcadero\Dev-Cpp\TDM-GCC-64\bin;%PATH%"

if not exist build mkdir build
set FLAGS=-std=c++17 -O2 -Wall -Wextra -Wpedantic -static
set CORE=src\adm.cpp src\rpt.cpp src\report.cpp

g++ %FLAGS% -o build\dayzlog.exe src\main.cpp %CORE% || exit /b 1
g++ %FLAGS% -o build\tests.exe tests\tests.cpp %CORE% || exit /b 1
build\tests.exe examples
