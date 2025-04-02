cmake_minimum_required(VERSION 3.15)
project(ecommerce_server)

set(CMAKE_CXX_STANDARD 17)

# Set path to your local libraries
set(LIB_DIR ${CMAKE_SOURCE_DIR}/lib)

# Crow (header-only) - Local version
add_library(Crow INTERFACE)
target_include_directories(Crow INTERFACE 
    ${LIB_DIR}/Crow/include
)

# sqlite_orm (header-only)
add_library(sqlite_orm INTERFACE)
target_include_directories(sqlite_orm INTERFACE 
    ${LIB_DIR}/sqlite_orm/include
)

# bcrypt
add_library(bcrypt STATIC
    ${LIB_DIR}/libbcrypt/src/bcrypt.c
    ${LIB_DIR}/libbcrypt/include/bcrypt/bcrypt.h
)
target_include_directories(bcrypt PUBLIC
    ${LIB_DIR}/libbcrypt/include/bcrypt
)

# SQLite3
find_package(SQLite3 REQUIRED)

# Main executable
add_executable(server
    src/main.cpp
    src/routes.cpp
    src/database.cpp
)

target_link_libraries(server
    Crow
    sqlite_orm
    bcrypt
    SQLite::SQLite3
    pthread
)

# Include directories
target_include_directories(server PRIVATE
    ${LIB_DIR}/Crow/include
    ${LIB_DIR}/asio/asio/include  
    ${LIB_DIR}/sqlite_orm/include
    ${LIB_DIR}/libbcrypt/include
    include
)