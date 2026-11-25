if(ENABLE_OPENGL)
  message(STATUS "Building with OpenGL support.")
endif()
if(ENABLE_VULKAN)
  message(STATUS "Building with Vulkan support.")
endif()
if(ENABLE_X11)
  message(STATUS "Building with X11 support.")
endif()
if(ENABLE_WAYLAND)
  message(STATUS "Building with Wayland support.")
endif()

if(BUILD_QT_FRONTEND)
  message(STATUS "Building Qt frontend.")
endif()
if(BUILD_NOGUI_FRONTEND)
  message(STATUS "Building NoGUI frontend.")
endif()
if(BUILD_REGTEST)
  message(STATUS "Building RegTest frontend.")
endif()
if(BUILD_LIBRETRO_CORE)
  message(STATUS "Building libretro core.")
endif()
if(BUILD_TESTS)
  message(STATUS "Building unit tests.")
endif()

if(NOT IS_SUPPORTED_COMPILER)
  message(WARNING "
*************** UNSUPPORTED CONFIGURATION ***************
You are not compiling Riverhorse with a supported compiler.
It may not even build successfully.
Riverhorse only supports the Clang and MSVC compilers.
No support will be provided, continue at your own risk.
*********************************************************")
endif()
