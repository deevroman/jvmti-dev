cmake_minimum_required(VERSION 3.10)
project(dumper)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-O2)

find_package(Java REQUIRED)
find_package(JNI REQUIRED)
find_package(Threads REQUIRED)
include(UseJava)

set(SOURCE dumper.cpp
        control_socket.cpp
        control_socket.h
        llm_dump_formatter.cpp
        llm_dump_formatter.h
        dump_serializer.cpp
        dump_serializer.h
        type_utils.cpp
        type_utils.h)

find_package(nlohmann_json QUIET CONFIG)
if (NOT nlohmann_json_FOUND)
    find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp)
    if (NLOHMANN_JSON_INCLUDE_DIR)
        add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
        set_target_properties(nlohmann_json::nlohmann_json PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${NLOHMANN_JSON_INCLUDE_DIR}")
    else ()
        include(FetchContent)
        FetchContent_Declare(
                nlohmann_json
                GIT_REPOSITORY https://github.com/nlohmann/json.git
                GIT_TAG v3.11.3
        )
        FetchContent_MakeAvailable(nlohmann_json)
    endif ()
endif ()

include_directories(${JNI_INCLUDE_DIRS})
add_library(dumper SHARED ${SOURCE})
target_link_libraries(dumper PRIVATE nlohmann_json::nlohmann_json)
#target_link_libraries(${TARGET} ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
