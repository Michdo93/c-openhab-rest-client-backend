cmake_minimum_required(VERSION 3.16)
project(OpenHABCBackend VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
set(OPENHAB_REST_CLIENT_DIR "" CACHE PATH "Source directory of c-openhab-rest-client (optional)")

# c-openhab-rest-client 2.0.0 (brings cJSON and libcurl)
set(OPENHAB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
if(OPENHAB_REST_CLIENT_DIR)
  add_subdirectory(${OPENHAB_REST_CLIENT_DIR} c-openhab-rest-client)
else()
  FetchContent_Declare(c_openhab_rest_client
    GIT_REPOSITORY https://github.com/Michdo93/c-openhab-rest-client.git
    GIT_TAG        2.0.0)
  FetchContent_MakeAvailable(c_openhab_rest_client)
endif()

# cpp-httplib (header only, no OpenSSL needed – Render terminates TLS)
FetchContent_Declare(httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG        v0.18.3)
FetchContent_MakeAvailable(httplib)

find_package(Threads REQUIRED)

add_executable(openhab_backend src/main.cpp src/calls.c)
target_include_directories(openhab_backend PRIVATE src)
target_link_libraries(openhab_backend PRIVATE openhab_rest_client_static httplib::httplib
                      Threads::Threads)
