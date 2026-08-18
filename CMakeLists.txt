cmake_minimum_required(VERSION 3.20)
project(serverlearn VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(blocking_server
    src/blocking_server/main.cpp
    src/sockcppwrap/sockcppwrap.cpp
    src/sockcppwrap/sockcppwrap.h
)

add_executable(non_blocking_server
    src/non_blocking_server/main.cpp
    src/sockcppwrap/sockcppwrap.cpp
    src/sockcppwrap/sockcppwrap.h  
)

target_include_directories(blocking_server PRIVATE
    src
)

# find_library(GUARDANT_LIB
#     NAMES grdlic
#     PATHS ${CMAKE_CURRENT_SOURCE_DIR}/guardant/lib
# )

# target_link_libraries(${PROJECT_NAME} PRIVATE ${GUARDANT_LIB})