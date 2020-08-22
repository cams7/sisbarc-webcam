# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

set(EXTRA_COMPONENT_DIRS 
	C:/My/Desenv/ESP-IDF/ESP32/esp-who/components 
	C:/My/Desenv/My/Workspace/sisbarc200821/components/sisbarc-common
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(app-template)
