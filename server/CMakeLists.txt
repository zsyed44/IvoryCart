cmake_minimum_required(VERSION 3.15)
project(bidding_server)

set(CMAKE_CXX_STANDARD 17)

# IXWebSocket (from local directory)
add_subdirectory(lib/IXWebSocket)

# SQLite3
find_package(SQLite3 REQUIRED)

# Main executable
add_executable(server
    main.cpp
)

target_link_libraries(server
    PRIVATE
    ixwebsocket
    SQLite::SQLite3
    pthread
)

# Include directories
target_include_directories(server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/IXWebSocket
    ${SQLite3_INCLUDE_DIRS}
)

# Compiler options
if(UNIX)
    target_compile_options(server PRIVATE -Wall -Wextra)
endif()