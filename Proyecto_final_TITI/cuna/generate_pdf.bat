@echo off
REM ==============================================================================
REM Script: generate_pdf.bat
REM Descripcion: Genera documentacion PDF desde Doxygen (Windows)
REM Requisitos: - Doxygen instalado (doxygen.exe en PATH)
REM             - MiKTeX o TeX Live instalado (pdflatex.exe en PATH)
REM ==============================================================================

setlocal enabledelayedexpansion

echo ==============================================
echo   Generador de PDF - Documentacion Doxygen
echo ==============================================
echo.

REM Verificar que existe Doxyfile
if not exist "Doxyfile" (
    echo ERROR: No se encontro Doxyfile en el directorio actual
    pause
    exit /b 1
)

REM Verificar Doxygen
where doxygen >nul 2>nul
if errorlevel 1 (
    echo ERROR: Doxygen no esta instalado o no esta en PATH
    echo Instala Doxygen desde: https://www.doxygen.nl/download.html
    pause
    exit /b 1
)
echo [OK] Doxygen encontrado

REM Verificar pdflatex
where pdflatex >nul 2>nul
if errorlevel 1 (
    echo ERROR: pdflatex no esta instalado o no esta en PATH
    echo Instala MiKTeX desde: https://miktex.org/download
    pause
    exit /b 1
)
echo [OK] pdflatex encontrado
echo.

echo [1/3] Ejecutando Doxygen...
doxygen Doxyfile >nul 2>&1
if errorlevel 1 (
    echo ERROR al ejecutar Doxygen
    pause
    exit /b 1
)
echo     [OK] Documentacion generada
echo.

REM Verificar directorio latex
if not exist "docs\latex" (
    echo ERROR: No se genero el directorio docs\latex
    echo Verifica que GENERATE_LATEX = YES en Doxyfile
    pause
    exit /b 1
)

echo [2/3] Compilando LaTeX a PDF...
cd docs\latex

echo     Pasada 1/3...
pdflatex -interaction=nonstopmode refman.tex >nul 2>&1

echo     Pasada 2/3...
pdflatex -interaction=nonstopmode refman.tex >nul 2>&1

echo     Pasada 3/3...
pdflatex -interaction=nonstopmode refman.tex >nul 2>&1

if not exist "refman.pdf" (
    echo ERROR al compilar LaTeX
    cd ..\..
    pause
    exit /b 1
)
echo     [OK] PDF compilado

cd ..\..
echo.

echo [3/3] Copiando PDF al directorio raiz...
copy "docs\latex\refman.pdf" "Documentacion_RTOS.pdf" >nul 2>&1
echo     [OK] PDF copiado como Documentacion_RTOS.pdf
echo.

echo ==============================================
echo   [OK] Documentacion PDF generada exitosamente
echo ==============================================
echo.
echo Archivo generado: Documentacion_RTOS.pdf
echo.

REM Abrir PDF
set /p OPEN="Deseas abrir el PDF? (S/N): "
if /i "%OPEN%"=="S" (
    start "" "Documentacion_RTOS.pdf"
)

pause
