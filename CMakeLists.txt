cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0091 NEW)
cmake_policy(SET CMP0042 NEW)
# cmake_policy(SET CMP0167 NEW)

project("module_example")

# node-addon-api uses this define to declare the Node-API entry points as
# imports on Windows.  cmake-js supplies it itself, but keeping it here also
# makes the direct-CMake and MinGW paths deterministic.
add_compile_definitions(BUILDING_NODE_EXTENSION)

set(VCPKG_ROOT $ENV{VCPKG_ROOT})
message("VCPKG_ROOT: " ${VCPKG_ROOT})
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
if(NOT MSVC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
    # Keep GNU/MinGW object layout stable across clean builds so a published
    # artifact can be verified by checksum as well as by behavior.
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -frandom-seed=skyline_shared_memory")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -frandom-seed=skyline_shared_memory")
endif()
set(CMAKE_VERBOSE_MAKEFILE ON)
set(PKG_TOOL pnpm)
set(TARGET_ARCH x64)
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(TARGET_ARCH ia32)
endif()
if(MSVC)
    execute_process(COMMAND
        chcp 65001
    )
    if(TARGET_ARCH STREQUAL "ia32")
        set(VCPKG_TARGET_TRIPLET x86-windows-static)
    else()
        set(VCPKG_TARGET_TRIPLET x64-windows-static)
    endif()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8")
else()
    # Linux specific settings
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_REENTRANT")
endif()
set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
include("${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")

message("LD_LIBRARY_PATH:" ${LD_LIBRARY_PATH})

find_package(spdlog REQUIRED)
find_program(PNPM_EXECUTABLE pnpm)
if(NOT PNPM_EXECUTABLE)
    message(FATAL_ERROR "pnpm not found!")
endif()
if(WIN32 AND NOT MINGW)
    string(TOLOWER "${PNPM_EXECUTABLE}" PNPM_EXECUTABLE_LOWER)
    if(NOT PNPM_EXECUTABLE_LOWER MATCHES "\\.(cmd|exe)$")
        set(PNPM_EXECUTABLE "${PNPM_EXECUTABLE}.CMD")
    endif()
endif()

message("CMAKE_CXX_FLAGS:${CMAKE_CXX_FLAGS}")
message("获取 cmake-js 配置")
message(${PROJECT_SOURCE_DIR})
execute_process(COMMAND
    ${PNPM_EXECUTABLE} --silent cmake-js --version
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    OUTPUT_VARIABLE CMAKE_JS_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(COMMAND
    ${PNPM_EXECUTABLE} cmake-js print-cmakejs-src --arch ${TARGET_ARCH} --silent -l error
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    OUTPUT_VARIABLE CMAKE_JS_SRC
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
message("获取 CMAKE_JS_SRC 配置: ${CMAKE_JS_SRC}")

message("PNPM_EXECUTABLE: " ${PNPM_EXECUTABLE})
execute_process(COMMAND
    ${PNPM_EXECUTABLE} cmake-js print-cmakejs-include --silent -l error
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    OUTPUT_VARIABLE CMAKE_JS_INC
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
message("CMAKE_JS_INC:" ${CMAKE_JS_INC})
foreach(item IN LISTS CMAKE_JS_INC)
    message("include: ${item}")
    include_directories(${item})
endforeach()

message("获取 node-addon-api 配置")
execute_process(COMMAND
    node -p "require('node-addon-api').include_dir"
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    OUTPUT_VARIABLE NODE_ADDON_API_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
message("node-addon-api 路径:${NODE_ADDON_API_DIR}")
get_filename_component(NODE_ADDON_API_DIR "${NODE_ADDON_API_DIR}"
    ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")

message("CMAKE_JS_INC: " ${CMAKE_JS_INC})
message("CMAKE_JS_SRC: " ${CMAKE_JS_SRC})
message("CMAKE_JS_LIB: " ${CMAKE_JS_LIB})

# cmake-js supplies a delay-load hook and a Node-API import library when it
# runs on Windows.  Keep a direct-CMake fallback for the CI script and for
# reproducible cross builds; the old NW.js node64.lib must never be selected
# for the Electron build.
option(SKYLINE_USE_NAPI "Link the stable Node-API surface on Windows" ON)
if(WIN32 AND NOT CMAKE_JS_SRC)
    if(MINGW)
        set(SKYLINE_DELAY_LOAD_HOOK
            "${PROJECT_SOURCE_DIR}/src/win_delay_load_hook.cc")
    else()
        find_file(SKYLINE_DELAY_LOAD_HOOK
            NAMES win_delay_load_hook.cc
            PATHS "${PROJECT_SOURCE_DIR}/node_modules/cmake-js/lib/cpp"
            NO_DEFAULT_PATH)
        if(NOT SKYLINE_DELAY_LOAD_HOOK)
            set(SKYLINE_DELAY_LOAD_HOOK
                "${PROJECT_SOURCE_DIR}/src/win_delay_load_hook.cc")
        endif()
    endif()
    if(SKYLINE_DELAY_LOAD_HOOK)
        set(CMAKE_JS_SRC "${SKYLINE_DELAY_LOAD_HOOK}")
    endif()
endif()

include_directories(${NODE_ADDON_API_DIR})

set(MODULE_NAME sharedMemory)
set(SRC_DIR_LIST
    src/main.cc
    src/logger.cc
    src/memory/set.cc
    src/memory/get.cc
    src/memory/remove.cc
    src/memory/manager.cc
    src/memory/compat.cc
)

add_library(${MODULE_NAME}
    SHARED
    ${SRC_DIR_LIST}
    ${CMAKE_JS_SRC}
    )

if (WIN32 AND SKYLINE_USE_NAPI)
    set(SKYLINE_NODE_API_LIB "${CMAKE_JS_LIB}")
    set(SKYLINE_NODE_API_RESULT 0)

    # cmake-js passes these variables on Windows and expects the project to
    # materialize the import library.  This is the same mechanism documented
    # by cmake-js for node-addon-api projects.
    if(CMAKE_JS_NODELIB_DEF AND CMAKE_JS_NODELIB_TARGET)
        set(SKYLINE_NODE_API_DEF "${CMAKE_JS_NODELIB_DEF}")
        set(SKYLINE_NODE_API_LIB "${CMAKE_JS_NODELIB_TARGET}")
        if(MSVC)
            if(CMAKE_SIZEOF_VOID_P EQUAL 8)
                set(SKYLINE_NODE_API_MACHINE X64)
            else()
                set(SKYLINE_NODE_API_MACHINE X86)
            endif()
            execute_process(
                COMMAND "${CMAKE_AR}"
                    "/def:${SKYLINE_NODE_API_DEF}"
                    "/out:${SKYLINE_NODE_API_LIB}"
                    "/machine:${SKYLINE_NODE_API_MACHINE}"
                    ${CMAKE_STATIC_LINKER_FLAGS}
                RESULT_VARIABLE SKYLINE_NODE_API_RESULT)
        endif()
    endif()

    if(NOT SKYLINE_NODE_API_LIB OR NOT EXISTS "${SKYLINE_NODE_API_LIB}")
        execute_process(
            COMMAND node -p "require('node-api-headers').def_paths.node_api_def"
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            OUTPUT_VARIABLE SKYLINE_NODE_API_DEF
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE SKYLINE_NODE_API_DEF_RESULT)
        if(NOT SKYLINE_NODE_API_DEF_RESULT EQUAL 0 OR
           NOT EXISTS "${SKYLINE_NODE_API_DEF}")
            message(FATAL_ERROR "Unable to locate node-api-headers/def/node_api.def")
        endif()

        if(MSVC)
            if(CMAKE_SIZEOF_VOID_P EQUAL 8)
                set(SKYLINE_NODE_API_MACHINE X64)
            else()
                set(SKYLINE_NODE_API_MACHINE X86)
            endif()
            set(SKYLINE_NODE_API_LIB "${CMAKE_CURRENT_BINARY_DIR}/node_api.lib")
            execute_process(
                COMMAND "${CMAKE_AR}"
                    "/def:${SKYLINE_NODE_API_DEF}"
                    "/out:${SKYLINE_NODE_API_LIB}"
                    "/machine:${SKYLINE_NODE_API_MACHINE}"
                    ${CMAKE_STATIC_LINKER_FLAGS}
                RESULT_VARIABLE SKYLINE_NODE_API_RESULT)
        elseif(MINGW)
            find_program(SKYLINE_DLLTOOL
                NAMES
                    "${CMAKE_CXX_COMPILER_TARGET}-dlltool"
                    "${CMAKE_SYSTEM_PROCESSOR}-w64-mingw32-dlltool"
                    x86_64-w64-mingw32-dlltool
                    i686-w64-mingw32-dlltool
                    dlltool)
            if(NOT SKYLINE_DLLTOOL AND CMAKE_DLLTOOL)
                set(SKYLINE_DLLTOOL "${CMAKE_DLLTOOL}")
            endif()
            if(NOT SKYLINE_DLLTOOL)
                message(FATAL_ERROR "MinGW build requires dlltool to create the Node-API import library")
            endif()
            set(SKYLINE_NODE_API_LIB_NAME libnode_api_delay.a)
            set(SKYLINE_NODE_API_LIB
                "${CMAKE_CURRENT_BINARY_DIR}/${SKYLINE_NODE_API_LIB_NAME}")
            if(CMAKE_SIZEOF_VOID_P EQUAL 8)
                set(SKYLINE_DLLTOOL_MACHINE i386:x86-64)
            else()
                set(SKYLINE_DLLTOOL_MACHINE i386)
            endif()
            execute_process(
                COMMAND "${SKYLINE_DLLTOOL}"
                    -d "${SKYLINE_NODE_API_DEF}"
                    -D NODE.EXE
                    -m "${SKYLINE_DLLTOOL_MACHINE}"
                    -y
                    "${SKYLINE_NODE_API_LIB_NAME}"
                WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
                RESULT_VARIABLE SKYLINE_NODE_API_RESULT)
        endif()
    endif()

    if(NOT SKYLINE_NODE_API_RESULT EQUAL 0 OR
       NOT EXISTS "${SKYLINE_NODE_API_LIB}")
        message(FATAL_ERROR "Unable to generate the Node-API import library")
    endif()

    target_link_libraries(${MODULE_NAME} PRIVATE "${SKYLINE_NODE_API_LIB}")
    if(MSVC)
        # N-API symbols are delay-loaded from node.exe.  cmake-js's hook maps
        # that name to Electron's already-loaded node.dll, so no node.dll is
        # linked directly into this addon.
        target_link_options(${MODULE_NAME} PRIVATE "/DELAYLOAD:NODE.EXE")
        target_link_libraries(${MODULE_NAME} PRIVATE delayimp)
    elseif(MINGW)
        # Keep the cross-compiled addon self-contained; Electron/Wine should
        # only need the host's system DLLs and node.dll.
        target_link_options(${MODULE_NAME} PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
            -Wl,--no-insert-timestamp
        )
    endif()
elseif (WIN32)
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/thirds/nwjs/node64.lib")
        message(FATAL_ERROR "NW.js build requested but thirds/nwjs/node64.lib is missing")
    endif()
    message(WARNING "Building the legacy NW.js ABI; set SKYLINE_USE_NAPI=ON for Electron")
    target_link_libraries(${MODULE_NAME} PRIVATE "${PROJECT_SOURCE_DIR}/thirds/nwjs/node64.lib")
else()
    # Linux specific libraries
    target_link_libraries(${MODULE_NAME} PRIVATE rt pthread)
endif()

target_link_libraries(${MODULE_NAME} PRIVATE spdlog::spdlog)
if (NOT WIN32)
    target_link_libraries(${MODULE_NAME} PRIVATE ${CMAKE_JS_LIB})
endif()

set_target_properties(${MODULE_NAME} PROPERTIES PREFIX "" SUFFIX ".node")

################test##################
