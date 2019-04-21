CMAKE_MINIMUM_REQUIRED(VERSION 3.0)

# variables declaration

SET(PROJECT btcaster)

SET(CMAKE_AUTOMOC ON)

SET(DEPENDS_ON_PROJECTS)

FILE(GLOB_RECURSE HEADERS "include/*.h")

FILE(GLOB_RECURSE SOURCES "src/*.c*")

FILE(GLOB_RECURSE MOC_SOURCES "*_automoc.cpp")

# source grouping

source_group ("Header Files" FILES ${HEADERS})
source_group ("Source Files" FILES ${SOURCES})
source_group ("Generated Files" FILES ${MOC_SOURCES})

# project definition

PROJECT(${PROJECT} CXX)

find_path(LIBTORRENT_INCLUDE_DIR libtorrent /usr/include /usr/local/include)
find_library(LIBTORRENT_LIBRARY torrent-rasterbar /usr/lib /usr/local/lib)
find_package(Boost REQUIRED COMPONENTS system)
find_package(Threads REQUIRED)

# includes
include_directories("${LIBTORRENT_INCLUDE_DIR}")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT}")

# linking
add_executable(${PROJECT} ${HEADERS} ${SOURCES} ${MOC_SOURCES})
set_property(TARGET ${PROJECT} PROPERTY CXX_STANDARD 11)
SET_TARGET_PROPERTIES(${PROJECT} PROPERTIES LINKER_LANGUAGE CXX)

target_link_libraries(
    ${PROJECT}
    ${LIBTORRENT_LIBRARY}
    ${Boost_LIBRARIES}
    Qt5::Core
    Threads::Threads
    Qt5SerialPort
    Qt5::Sql)

