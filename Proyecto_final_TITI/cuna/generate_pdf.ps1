#!/usr/bin/env pwsh
# ==============================================================================
# Script: generate_pdf.ps1
# Descripción: Genera documentación PDF desde Doxygen
# Requisitos: - Doxygen instalado (doxygen.exe en PATH)
#             - MiKTeX o TeX Live instalado (pdflatex.exe en PATH)
#             - make (opcional, viene con MiKTeX o Git Bash)
# ==============================================================================

$ErrorActionPreference = "Stop"

# Colores para output
function Write-Success { Write-Host $args -ForegroundColor Green }
function Write-Info { Write-Host $args -ForegroundColor Cyan }
function Write-Warning { Write-Host $args -ForegroundColor Yellow }
function Write-Error { Write-Host $args -ForegroundColor Red }

Write-Info "=============================================="
Write-Info "  Generador de PDF - Documentación Doxygen"
Write-Info "=============================================="
Write-Host ""

# Verificar que existe Doxyfile
if (-not (Test-Path "Doxyfile")) {
    Write-Error "ERROR: No se encontró Doxyfile en el directorio actual"
    exit 1
}

# Verificar que Doxygen está instalado
try {
    $doxygenVersion = & doxygen --version 2>&1
    Write-Info "Doxygen encontrado: v$doxygenVersion"
} catch {
    Write-Error "ERROR: Doxygen no está instalado o no está en PATH"
    Write-Warning "Instala Doxygen desde: https://www.doxygen.nl/download.html"
    exit 1
}

# Verificar que pdflatex está instalado
try {
    $null = & pdflatex --version 2>&1
    Write-Info "pdflatex encontrado"
} catch {
    Write-Error "ERROR: pdflatex no está instalado o no está en PATH"
    Write-Warning "Instala MiKTeX desde: https://miktex.org/download"
    Write-Warning "O TeX Live desde: https://www.tug.org/texlive/"
    exit 1
}

Write-Host ""
Write-Info "[1/3] Ejecutando Doxygen..."
try {
    & doxygen Doxyfile 2>&1 | Out-Null
    Write-Success "    ✓ Documentación generada"
} catch {
    Write-Error "ERROR al ejecutar Doxygen"
    exit 1
}

# Verificar que se generó el directorio latex
if (-not (Test-Path "docs/latex")) {
    Write-Error "ERROR: No se generó el directorio docs/latex"
    Write-Warning "Verifica que GENERATE_LATEX = YES en Doxyfile"
    exit 1
}

Write-Host ""
Write-Info "[2/3] Compilando LaTeX a PDF..."
Push-Location "docs/latex"

try {
    # Primera pasada: genera archivos auxiliares
    Write-Info "    Pasada 1/3..."
    & pdflatex -interaction=nonstopmode refman.tex 2>&1 | Out-Null
    
    # Segunda pasada: procesa referencias
    Write-Info "    Pasada 2/3..."
    & pdflatex -interaction=nonstopmode refman.tex 2>&1 | Out-Null
    
    # Tercera pasada: finaliza referencias cruzadas
    Write-Info "    Pasada 3/3..."
    & pdflatex -interaction=nonstopmode refman.tex 2>&1 | Out-Null
    
    Write-Success "    ✓ PDF compilado"
} catch {
    Write-Error "ERROR al compilar LaTeX"
    Pop-Location
    exit 1
}

Pop-Location

Write-Host ""
Write-Info "[3/3] Copiando PDF al directorio raíz..."
if (Test-Path "docs/latex/refman.pdf") {
    Copy-Item "docs/latex/refman.pdf" "Documentacion_RTOS.pdf" -Force
    Write-Success "    ✓ PDF copiado como Documentacion_RTOS.pdf"
} else {
    Write-Error "ERROR: No se encontró refman.pdf"
    exit 1
}

Write-Host ""
Write-Success "=============================================="
Write-Success "  ✓ Documentación PDF generada exitosamente"
Write-Success "=============================================="
Write-Host ""
Write-Info "Archivo generado: Documentacion_RTOS.pdf"
Write-Info "Ubicación: $(Resolve-Path 'Documentacion_RTOS.pdf')"

# Preguntar si abrir el PDF
$response = Read-Host "`n¿Deseas abrir el PDF? (S/N)"
if ($response -eq 'S' -or $response -eq 's') {
    Start-Process "Documentacion_RTOS.pdf"
}
