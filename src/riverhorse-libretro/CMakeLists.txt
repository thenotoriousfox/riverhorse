add_library(riverhorse_libretro SHARED
  libretro_core_options.cpp
  libretro_core_options.h
  libretro_host.cpp
  libretro_input.cpp
  libretro_input.h
  libretro_resources.cpp
  libretro_resources.h
  libretro_rumble.cpp
  libretro_rumble.h
)

# Files under data/resources are built into the core. Add a pattern to the EXCLUDE filter to leave files out.
set(LIBRETRO_RESOURCE_DIR "${CMAKE_SOURCE_DIR}/data/resources")
file(GLOB_RECURSE LIBRETRO_RESOURCES RELATIVE "${LIBRETRO_RESOURCE_DIR}" CONFIGURE_DEPENDS "${LIBRETRO_RESOURCE_DIR}/*")
list(FILTER LIBRETRO_RESOURCES EXCLUDE REGEX "^fonts/NotoSans")
list(SORT LIBRETRO_RESOURCES)
set(LIBRETRO_RESOURCE_ASM "")
set(LIBRETRO_RESOURCE_TABLE "")
set(LIBRETRO_RESOURCE_DECLS "")
set(LIBRETRO_RESOURCE_PATHS "")
if(APPLE)
  set(LIBRETRO_RESOURCE_SECTION "__TEXT,__const")
  set(LIBRETRO_RESOURCE_SYMBOL_PREFIX "_")
elseif(WIN32)
  set(LIBRETRO_RESOURCE_SECTION ".rdata,\\\"dr\\\"")
  set(LIBRETRO_RESOURCE_SYMBOL_PREFIX "")
else()
  set(LIBRETRO_RESOURCE_SECTION ".rodata")
  set(LIBRETRO_RESOURCE_SYMBOL_PREFIX "")
endif()

set(index 0)
foreach(name IN LISTS LIBRETRO_RESOURCES)
  set(path "${LIBRETRO_RESOURCE_DIR}/${name}")
  file(TIMESTAMP "${path}" timestamp "%s" UTC)
  string(APPEND LIBRETRO_RESOURCE_ASM
         "  \".balign 16\\n${LIBRETRO_RESOURCE_SYMBOL_PREFIX}riverhorse_resource_${index}_begin:\\n.incbin \\\"${path}\\\"\\n${LIBRETRO_RESOURCE_SYMBOL_PREFIX}riverhorse_resource_${index}_end:\\n\"\n")
  string(APPEND LIBRETRO_RESOURCE_TABLE
         "  {\"${name}\", riverhorse_resource_${index}_begin, riverhorse_resource_${index}_end, ${timestamp}},\n")
  string(APPEND LIBRETRO_RESOURCE_DECLS
         "extern \"C\" __attribute__((visibility(\"hidden\"))) const unsigned char riverhorse_resource_${index}_begin[], riverhorse_resource_${index}_end[];\n")
  list(APPEND LIBRETRO_RESOURCE_PATHS "${path}")
  math(EXPR index "${index} + 1")
endforeach()
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/libretro_embedded_resources.inc" CONTENT
     "__asm__(\".section ${LIBRETRO_RESOURCE_SECTION}\\n\"\n${LIBRETRO_RESOURCE_ASM}  \".text\\n\");\n${LIBRETRO_RESOURCE_DECLS}\nconstexpr EmbeddedFile s_embedded_files[] = {\n${LIBRETRO_RESOURCE_TABLE}};\n"
     @ONLY)
set_source_files_properties(libretro_resources.cpp PROPERTIES OBJECT_DEPENDS "${LIBRETRO_RESOURCE_PATHS}")

if(ENABLE_OPENGL)
  target_sources(riverhorse_libretro PRIVATE libretro_video_opengl.cpp libretro_video_opengl.h)
  target_link_libraries(riverhorse_libretro PRIVATE glad)
endif()

if(ENABLE_VULKAN)
  target_sources(riverhorse_libretro PRIVATE libretro_video_vulkan.cpp libretro_video_vulkan.h)
endif()

target_include_directories(riverhorse_libretro PRIVATE "${CMAKE_SOURCE_DIR}/dep/libretro/include" "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(riverhorse_libretro PRIVATE core common scmversion)
set_target_properties(riverhorse_libretro PROPERTIES PREFIX "" CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

if(MINGW)
  target_link_options(riverhorse_libretro PRIVATE -static)
endif()

if(LINUX OR BSD)
  target_link_options(riverhorse_libretro PRIVATE "LINKER:--no-undefined"
                      "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/libretro.map"
                      -static-libstdc++ -static-libgcc)
  set_property(TARGET riverhorse_libretro APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libretro.map")
endif()
