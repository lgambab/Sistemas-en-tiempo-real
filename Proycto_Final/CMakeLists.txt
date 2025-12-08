# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Bring in SPIFFS CMake helpers even when the build is trimmed down.
include($ENV{IDF_PATH}/components/spiffs/project_include.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
project(Project_7_Retro_Web_Terminal)

# Esto crea la imagen SPIFFS desde la carpeta "front"
# FLASH_IN_APP debería incluirla automáticamente en idf.py flash
spiffs_create_partition_image(storage front FLASH_IN_APP)