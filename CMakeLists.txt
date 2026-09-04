cmake_minimum_required(VERSION 3.20)
project(mkvtoolnix_api VERSION 101.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(
        /permissive-
        /Zc:preprocessor
        /Zc:__cplusplus
        /utf-8
        /EHsc
        /W3
        /MP
        /wd4244
        /wd4267
        /wd4996
    )
    add_compile_definitions(
        _CRT_SECURE_NO_WARNINGS
        _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
        _SILENCE_ALL_CXX20_DEPRECATION_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        SYS_WINDOWS
        COMP_MSC
        _FILE_OFFSET_BITS=64
    )
else()
    add_compile_options(
        -Wall
        -Wno-sign-compare
        -Wno-unused-parameter
        -Wno-unused-variable
        -fPIC
    )
    add_compile_definitions(
        _FILE_OFFSET_BITS=64
    )
endif()

# Find dependencies via vcpkg / system
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(Boost REQUIRED COMPONENTS filesystem system locale)
find_package(ZLIB REQUIRED)

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND AND NOT WIN32)
    pkg_check_modules(FLAC REQUIRED IMPORTED_TARGET flac)
    pkg_check_modules(Ogg REQUIRED IMPORTED_TARGET ogg)
    pkg_check_modules(Vorbis REQUIRED IMPORTED_TARGET vorbis vorbisfile)
    set(FLAC_TARGET PkgConfig::FLAC)
    set(OGG_TARGET PkgConfig::Ogg)
    set(VORBIS_TARGET PkgConfig::Vorbis)
else()
    find_package(FLAC REQUIRED)
    find_package(Ogg REQUIRED)
    find_package(Vorbis REQUIRED)
    set(FLAC_TARGET FLAC::FLAC)
    set(OGG_TARGET Ogg::ogg)
    set(VORBIS_TARGET Vorbis::vorbis)
endif()

find_package(Iconv)
find_package(Intl)

find_path(GMP_INCLUDE_DIR NAMES gmp.h PATH_SUFFIXES x86_64-linux-gnu aarch64-linux-gnu)
find_library(GMP_LIBRARY NAMES gmp mpir)
if(NOT GMP_INCLUDE_DIR OR NOT GMP_LIBRARY)
    message(FATAL_ERROR "GMP / MPIR library not found. Please ensure gmp or mpir is installed.")
endif()

# ---------------------------------------------------------------------------
# Dependency: MKVToolNix (via git submodule in third_party/mkvtoolnix)
# ---------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mkvtoolnix/CMakeLists.txt" OR EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mkvtoolnix/src/merge/mkvmerge.cpp")
    set(MKV_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mkvtoolnix")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/mkvtoolnix/CMakeLists.txt" OR EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/mkvtoolnix/src/merge/mkvmerge.cpp")
    set(MKV_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/mkvtoolnix")
else()
    message(FATAL_ERROR 
        "MKVToolNix submodule not found at third_party/mkvtoolnix!\n"
        "Please run:\n"
        "  git submodule update --init --recursive\n"
    )
endif()

# Check and apply mkvtoolnix.patch if needed
file(READ "${MKV_ROOT}/src/merge/output_control.h" _OUTPUT_CONTROL_CONTENT)
if(NOT _OUTPUT_CONTROL_CONTENT MATCHES "g_merge_cancel_requested")
    message(STATUS "Applying mkvtoolnix.patch to MKVToolNix submodule...")
    find_package(Git REQUIRED)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} apply "${CMAKE_CURRENT_SOURCE_DIR}/patches/mkvtoolnix.patch"
        WORKING_DIRECTORY "${MKV_ROOT}"
        RESULT_VARIABLE _PATCH_RESULT
    )
    if(NOT _PATCH_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to apply mkvtoolnix.patch to ${MKV_ROOT}")
    endif()
    message(STATUS "Patch applied successfully.")
endif()

# Ensure multiplatform config.h exists in MKV_ROOT
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.h" "${MKV_ROOT}/config.h" COPYONLY)

set(MKV_INCLUDES
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${MKV_ROOT}"
    "${MKV_ROOT}/src"
    "${MKV_ROOT}/lib"
    "${MKV_ROOT}/lib/avilib-0.6.10"
    "${MKV_ROOT}/lib/librmff"
    "${MKV_ROOT}/lib/fmt/include"
    "${MKV_ROOT}/lib/pugixml/src"
    "${MKV_ROOT}/lib/utf8-cpp/source"
    "${MKV_ROOT}/lib/nlohmann-json/include"
    "${MKV_ROOT}/lib/libebml"
    "${MKV_ROOT}/lib/libmatroska"
    ${GMP_INCLUDE_DIR}
)

# 1. Internal third-party libraries
file(GLOB AVILIB_SRCS "${MKV_ROOT}/lib/avilib-0.6.10/*.c" "${MKV_ROOT}/lib/avilib-0.6.10/*.cpp")
file(GLOB RMFF_SRCS "${MKV_ROOT}/lib/librmff/*.c")
set(FMT_SRCS "${MKV_ROOT}/lib/fmt/src/format.cc" "${MKV_ROOT}/lib/fmt/src/os.cc")
set(PUGIXML_SRCS "${MKV_ROOT}/lib/pugixml/src/pugixml.cpp")
file(GLOB MPEGPARSER_SRCS "${MKV_ROOT}/src/mpegparser/*.cpp")
file(GLOB EBML_SRCS "${MKV_ROOT}/lib/libebml/src/*.cpp")
file(GLOB MATROSKA_SRCS "${MKV_ROOT}/lib/libmatroska/src/*.cpp")

# 2. MKVToolNix Core engine sources
file(GLOB_RECURSE COMMON_SRCS "${MKV_ROOT}/src/common/*.cpp")
if(WIN32)
    list(FILTER COMMON_SRCS EXCLUDE REGEX ".*/unix\\.cpp$")
    list(FILTER COMMON_SRCS EXCLUDE REGEX ".*/macos\\.cpp$")
elseif(APPLE)
    list(FILTER COMMON_SRCS EXCLUDE REGEX ".*/windows\\.cpp$")
    list(FILTER COMMON_SRCS EXCLUDE REGEX ".*/unix\\.cpp$")
else()
    # Linux / generic Unix
    list(FILTER COMMON_SRCS EXCLUDE REGEX ".*/windows\\.cpp$")
    list(FILTER COMMON_SRCS EXCLUDE REGEX ".*/macos\\.cpp$")
endif()

file(GLOB_RECURSE INPUT_SRCS "${MKV_ROOT}/src/input/*.cpp")
file(GLOB_RECURSE OUTPUT_SRCS "${MKV_ROOT}/src/output/*.cpp")

file(GLOB_RECURSE MERGE_SRCS "${MKV_ROOT}/src/merge/*.cpp")
# Exclude standalone CLI executable source
list(REMOVE_ITEM MERGE_SRCS "${MKV_ROOT}/src/merge/mkvmerge.cpp")

# 3. MKVToolNix API wrapper sources
set(API_SRCS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mkvtoolnix-api/c_api.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mkvtoolnix-api/merge.cpp"
)

# 4. Target: mkvtoolnix (Shared Library / DLL)
add_library(mkvtoolnix SHARED
    ${AVILIB_SRCS}
    ${RMFF_SRCS}
    ${FMT_SRCS}
    ${PUGIXML_SRCS}
    ${MPEGPARSER_SRCS}
    ${EBML_SRCS}
    ${MATROSKA_SRCS}
    ${COMMON_SRCS}
    ${INPUT_SRCS}
    ${OUTPUT_SRCS}
    ${MERGE_SRCS}
    ${API_SRCS}
)

target_include_directories(mkvtoolnix PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_include_directories(mkvtoolnix PRIVATE
    ${MKV_INCLUDES}
)

target_compile_definitions(mkvtoolnix PRIVATE
    MTX_API_BUILD=1
    PUGIXML_INTERNAL=1
    FMT_INTERNAL=1
    UTF8CPP_INTERNAL=1
    NLOHMANN_JSON_INTERNAL=1
    EBML_MATROSKA_INTERNAL=1
)

set(PLATFORM_LIBS)
if(WIN32)
    list(APPEND PLATFORM_LIBS ws2_32 bcrypt shell32)
else()
    list(APPEND PLATFORM_LIBS ${CMAKE_DL_LIBS} pthread)
endif()

if(TARGET Iconv::Iconv)
    list(APPEND PLATFORM_LIBS Iconv::Iconv)
endif()
if(TARGET Intl::Intl)
    list(APPEND PLATFORM_LIBS Intl::Intl)
endif()

target_link_libraries(mkvtoolnix PRIVATE
    Qt6::Core
    Boost::filesystem
    Boost::system
    Boost::locale
    ZLIB::ZLIB
    ${FLAC_TARGET}
    ${OGG_TARGET}
    ${VORBIS_TARGET}
    ${GMP_LIBRARY}
    ${PLATFORM_LIBS}
)

if(WIN32)
    set_target_properties(mkvtoolnix PROPERTIES
        OUTPUT_NAME "mkvtoolnix"
        PREFIX ""
    )
    add_custom_command(TARGET mkvtoolnix POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Qt6::Core>
            $<TARGET_FILE_DIR:mkvtoolnix>
    )
else()
    set_target_properties(mkvtoolnix PROPERTIES
        OUTPUT_NAME "mkvtoolnix"
    )
endif()

# 5. Target: minimal (Example application consuming the C ABI)
add_executable(minimal
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/minimal.c"
)
target_include_directories(minimal PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(minimal PRIVATE
    mkvtoolnix
)

# 6. Target: identify (Example application inspecting video file tracks and metadata)
add_executable(identify
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/identify.c"
)
target_include_directories(identify PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(identify PRIVATE
    mkvtoolnix
)
