cmake_minimum_required(VERSION 3.0.0)

project(l6_z1)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 COMPONENTS Core Widgets Network REQUIRED)

set(SRCS
    main.cpp
    mywidget.ui
    mywidget.cpp
)

add_executable(l6_z1 ${SRCS})

target_link_libraries(l6_z1 Qt5::Widgets Qt5::Network)
