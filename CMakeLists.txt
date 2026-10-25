cmake_minimum_required(VERSION 3.22)
project(moch VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets WebEngineWidgets Network Sql WebChannel Test)

qt_standard_project_setup()

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)

install(TARGETS moch RUNTIME DESTINATION bin)
