```bat
@echo off
chcp 65001 >nul
title Compilar UI - CMake

set "CMAKE=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

echo ========================================
echo        CONFIGURANDO O PROJETO
echo ========================================

"%CMAKE%" -S . -B build -G "Visual Studio 18 2026" -A x64

if errorlevel 1 (
    echo.
    echo [ERRO] Falha ao configurar o CMake!
    pause
    exit /b 1
)

echo.
echo ========================================
echo        COMPILANDO A UI - RELEASE
echo ========================================

"%CMAKE%" --build build --config Release --target ui --parallel

if errorlevel 1 (
    echo.
    echo [ERRO] Falha na compilacao!
    pause
    exit /b 1
)

echo.
echo ========================================
echo       COMPILACAO CONCLUIDA!
echo ========================================

pause
```