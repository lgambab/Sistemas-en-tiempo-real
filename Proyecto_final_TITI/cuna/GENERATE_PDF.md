# Generación de Documentación PDF

Scripts para generar documentación en formato PDF usando Doxygen y LaTeX.

## 📋 Requisitos Previos

### 1. Doxygen
- **Descargar:** https://www.doxygen.nl/download.html
- **Windows:** Instalar y agregar a PATH
- **Verificar:** `doxygen --version`

### 2. LaTeX (MiKTeX o TeX Live)
- **MiKTeX (recomendado para Windows):** https://miktex.org/download
- **TeX Live:** https://www.tug.org/texlive/
- **Verificar:** `pdflatex --version`

## 🚀 Uso

### Opción 1: PowerShell (recomendado)
```powershell
.\generate_pdf.ps1
```

### Opción 2: Batch (doble clic)
```cmd
generate_pdf.bat
```
O simplemente hacer doble clic en `generate_pdf.bat` desde el explorador.

### Opción 3: Manual
```bash
# 1. Generar documentación
doxygen Doxyfile

# 2. Compilar LaTeX
cd docs/latex
pdflatex -interaction=nonstopmode refman.tex
pdflatex -interaction=nonstopmode refman.tex
pdflatex -interaction=nonstopmode refman.tex

# 3. Copiar PDF
copy refman.pdf ..\..\Documentacion_RTOS.pdf
```

## 📄 Resultado

El script genera `Documentacion_RTOS.pdf` en el directorio raíz del proyecto.

## 🔧 Solución de Problemas

### "Doxygen no encontrado"
- Instala Doxygen desde el link oficial
- Agrega el directorio de instalación al PATH de Windows
- Reinicia la terminal

### "pdflatex no encontrado"
- Instala MiKTeX
- Durante instalación, selecciona "Install missing packages on-the-fly: Yes"
- Agrega al PATH: `C:\Program Files\MiKTeX\miktex\bin\x64`
- Reinicia la terminal

### "Missing packages" durante compilación
- MiKTeX instalará automáticamente paquetes faltantes
- Si usa TeX Live, ejecuta: `tlmgr install <paquete>`

### PDF con errores de formato
- Verifica que todos los archivos .c/.h tengan comentarios Doxygen válidos
- Revisa el log en `docs/latex/refman.log`

## 📝 Configuración

La generación de LaTeX está configurada en `Doxyfile`:
```ini
GENERATE_LATEX         = YES
USE_PDFLATEX           = YES
PDF_HYPERLINKS         = YES
PAPER_TYPE             = a4
```

## 🎯 Características del PDF

- **Formato:** A4
- **Idioma:** Español (OUTPUT_LANGUAGE = Spanish)
- **Hipervínculos:** Activados (clickeable)
- **Índice:** Automático con referencias cruzadas
- **Diagramas:** Incluye call graphs y diagramas de clase
- **Código fuente:** Resaltado de sintaxis

## 📦 Contenido Generado

El PDF incluye:
- Descripción general del proyecto
- Documentación de todos los módulos
- API completa con parámetros y valores de retorno
- Diagramas de llamadas entre funciones
- Estructuras de datos y enumeraciones
- Índice alfabético de funciones

## ⏱️ Tiempo de Generación

- **Doxygen:** ~10-30 segundos
- **LaTeX (3 pasadas):** ~1-3 minutos
- **Total:** ~2-4 minutos

## 🔄 Regeneración

Para actualizar el PDF después de cambios en el código:
```powershell
.\generate_pdf.ps1
```

El script regenera todo desde cero automáticamente.
