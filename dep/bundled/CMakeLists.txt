set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  string(REPLACE "/D_HAS_EXCEPTIONS=0" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc /GR")
else()
  string(REPLACE "-fno-exceptions -fno-rtti" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

add_library(bundled_zlib STATIC
  zlib/adler32.c zlib/compress.c zlib/crc32.c zlib/deflate.c zlib/gzclose.c zlib/gzlib.c zlib/gzread.c
  zlib/gzwrite.c zlib/infback.c zlib/inffast.c zlib/inflate.c zlib/inftrees.c zlib/trees.c zlib/uncompr.c
  zlib/zutil.c
)
target_include_directories(bundled_zlib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/zlib")
if(NOT MSVC)
  target_compile_definitions(bundled_zlib PRIVATE HAVE_UNISTD_H)
endif()
add_library(ZLIB::ZLIB ALIAS bundled_zlib)

set(ZSTD_BUILD_PROGRAMS OFF CACHE INTERNAL "")
set(ZSTD_BUILD_TESTS OFF CACHE INTERNAL "")
set(ZSTD_BUILD_SHARED OFF CACHE INTERNAL "")
set(ZSTD_BUILD_STATIC ON CACHE INTERNAL "")
set(ZSTD_LEGACY_SUPPORT OFF CACHE INTERNAL "")
add_subdirectory(zstd/build/cmake EXCLUDE_FROM_ALL)
target_include_directories(libzstd_static INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/zstd/lib>")
add_library(Zstd::Zstd ALIAS libzstd_static)

set(PNG_BUILD_ZLIB ON CACHE INTERNAL "")
set(ZLIB_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/zlib" CACHE INTERNAL "")
set(ZLIB_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/zlib" CACHE INTERNAL "")
set(PNG_SHARED OFF CACHE INTERNAL "")
set(PNG_STATIC ON CACHE INTERNAL "")
set(PNG_TESTS OFF CACHE INTERNAL "")
set(PNG_TOOLS OFF CACHE INTERNAL "")
set(PNG_FRAMEWORK OFF CACHE INTERNAL "")
set(SKIP_INSTALL_ALL ON CACHE INTERNAL "")
add_subdirectory(libpng EXCLUDE_FROM_ALL)
target_include_directories(png_static INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/libpng>"
                           "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/libpng>")
add_library(PNG::PNG ALIAS png_static)

add_subdirectory(libjpeg EXCLUDE_FROM_ALL)
add_library(JPEG::JPEG ALIAS jpeg)

set(WEBP_BUILD_ANIM_UTILS OFF CACHE INTERNAL "")
set(WEBP_BUILD_CWEBP OFF CACHE INTERNAL "")
set(WEBP_BUILD_DWEBP OFF CACHE INTERNAL "")
set(WEBP_BUILD_GIF2WEBP OFF CACHE INTERNAL "")
set(WEBP_BUILD_IMG2WEBP OFF CACHE INTERNAL "")
set(WEBP_BUILD_VWEBP OFF CACHE INTERNAL "")
set(WEBP_BUILD_WEBPINFO OFF CACHE INTERNAL "")
set(WEBP_BUILD_LIBWEBPMUX OFF CACHE INTERNAL "")
set(WEBP_BUILD_WEBPMUX OFF CACHE INTERNAL "")
set(WEBP_BUILD_EXTRAS OFF CACHE INTERNAL "")
add_subdirectory(libwebp EXCLUDE_FROM_ALL)
target_include_directories(webp INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/libwebp/src>")
add_library(WebP::libwebp ALIAS webp)

set(FT_DISABLE_ZLIB ON CACHE INTERNAL "")
set(FT_DISABLE_BZIP2 ON CACHE INTERNAL "")
set(FT_DISABLE_PNG ON CACHE INTERNAL "")
set(FT_DISABLE_HARFBUZZ ON CACHE INTERNAL "")
set(FT_DISABLE_BROTLI ON CACHE INTERNAL "")
add_subdirectory(freetype EXCLUDE_FROM_ALL)
add_library(Freetype::Freetype ALIAS freetype)

set(SOUNDTOUCH_SOURCES
  soundtouch/source/SoundTouch/AAFilter.cpp soundtouch/source/SoundTouch/BPMDetect.cpp
  soundtouch/source/SoundTouch/cpu_detect_x86.cpp soundtouch/source/SoundTouch/FIFOSampleBuffer.cpp
  soundtouch/source/SoundTouch/FIRFilter.cpp soundtouch/source/SoundTouch/InterpolateCubic.cpp
  soundtouch/source/SoundTouch/InterpolateLinear.cpp soundtouch/source/SoundTouch/InterpolateShannon.cpp
  soundtouch/source/SoundTouch/mmx_optimized.cpp soundtouch/source/SoundTouch/PeakFinder.cpp
  soundtouch/source/SoundTouch/RateTransposer.cpp soundtouch/source/SoundTouch/SoundTouch.cpp
  soundtouch/source/SoundTouch/sse_optimized.cpp soundtouch/source/SoundTouch/TDStretch.cpp
  soundtouch/source/SoundTouchDLL/SoundTouchDLL.cpp
)
add_library(bundled_soundtouch STATIC ${SOUNDTOUCH_SOURCES})
foreach(header SoundTouch.h FIFOSampleBuffer.h FIFOSamplePipe.h STTypes.h BPMDetect.h)
  configure_file("soundtouch/include/${header}" "${CMAKE_CURRENT_BINARY_DIR}/soundtouch_include/soundtouch/${header}" COPYONLY)
endforeach()
configure_file("soundtouch/source/SoundTouchDLL/SoundTouchDLL.h"
               "${CMAKE_CURRENT_BINARY_DIR}/soundtouch_include/soundtouch/SoundTouchDLL.h" COPYONLY)
target_include_directories(bundled_soundtouch
  PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/soundtouch_include"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/soundtouch/include" "${CMAKE_CURRENT_SOURCE_DIR}/soundtouch/source/SoundTouchDLL")
target_compile_definitions(bundled_soundtouch PUBLIC SOUNDTOUCH_FLOAT_SAMPLES)
if(WIN32)
  target_compile_definitions(bundled_soundtouch PUBLIC DLL_EXPORTS)
endif()
if(NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i.86")
  target_compile_options(bundled_soundtouch PRIVATE -msse2)
endif()
add_library(SoundTouch::SoundTouchDLL ALIAS bundled_soundtouch)

set(CPUINFO_LIBRARY_TYPE static CACHE INTERNAL "")
set(CPUINFO_RUNTIME_TYPE static CACHE INTERNAL "")
set(CPUINFO_LOG_LEVEL error CACHE INTERNAL "")
set(CPUINFO_BUILD_TOOLS OFF CACHE INTERNAL "")
set(CPUINFO_BUILD_UNIT_TESTS OFF CACHE INTERNAL "")
set(CPUINFO_BUILD_MOCK_TESTS OFF CACHE INTERNAL "")
set(CPUINFO_BUILD_BENCHMARKS OFF CACHE INTERNAL "")
set(USE_SYSTEM_LIBS ON CACHE INTERNAL "")
add_subdirectory(cpuinfo EXCLUDE_FROM_ALL)
add_library(cpuinfo::cpuinfo ALIAS cpuinfo)

if(ENABLE_VULKAN)
  set(SHADERC_SKIP_INSTALL ON CACHE INTERNAL "")
  set(SHADERC_SKIP_TESTS ON CACHE INTERNAL "")
  set(SHADERC_SKIP_EXAMPLES ON CACHE INTERNAL "")
  set(SHADERC_SKIP_COPYRIGHT_CHECK ON CACHE INTERNAL "")
  set(SHADERC_ENABLE_WERROR_COMPILE OFF CACHE INTERNAL "")
  set(SKIP_GLSLANG_INSTALL ON CACHE INTERNAL "")
  set(SKIP_SPIRV_TOOLS_INSTALL ON CACHE INTERNAL "")
  set(SPIRV_SKIP_EXECUTABLES ON CACHE INTERNAL "")
  set(SPIRV_SKIP_TESTS ON CACHE INTERNAL "")
  set(ENABLE_GLSLANG_BINARIES OFF CACHE INTERNAL "")
  set(ENABLE_CTEST OFF CACHE INTERNAL "")
  add_subdirectory(shaderc EXCLUDE_FROM_ALL)
  add_library(bundled_shaderc STATIC shaderc/libshaderc/src/shaderc.cc)
  target_include_directories(bundled_shaderc
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/shaderc/libshaderc/include"
    PRIVATE "${glslang_SOURCE_DIR}" "${SPIRV-Headers_SOURCE_DIR}/include")
  target_compile_definitions(bundled_shaderc PRIVATE SHADERC_IMPLEMENTATION)
  target_link_libraries(bundled_shaderc PRIVATE glslang shaderc_util SPIRV SPIRV-Tools Threads::Threads)
  add_library(Shaderc::shaderc_combined ALIAS bundled_shaderc)

  set(SPIRV_CROSS_SHARED OFF CACHE INTERNAL "")
  set(SPIRV_CROSS_STATIC ON CACHE INTERNAL "")
  set(SPIRV_CROSS_CLI OFF CACHE INTERNAL "")
  set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE INTERNAL "")
  set(SPIRV_CROSS_ENABLE_GLSL ON CACHE INTERNAL "")
  set(SPIRV_CROSS_ENABLE_HLSL OFF CACHE INTERNAL "")
  set(SPIRV_CROSS_ENABLE_MSL OFF CACHE INTERNAL "")
  set(SPIRV_CROSS_ENABLE_CPP OFF CACHE INTERNAL "")
  set(SPIRV_CROSS_ENABLE_REFLECT OFF CACHE INTERNAL "")
  set(SPIRV_CROSS_ENABLE_C_API ON CACHE INTERNAL "")
  set(SPIRV_CROSS_ENABLE_UTIL ON CACHE INTERNAL "")
  set(SPIRV_CROSS_SKIP_INSTALL ON CACHE INTERNAL "")
  add_subdirectory(spirv_cross EXCLUDE_FROM_ALL)
  target_include_directories(spirv-cross-c INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
endif()
