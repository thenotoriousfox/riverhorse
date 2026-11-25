add_library(rainterface
  RA_Consoles.h
  RA_Emulators.h
  RA_Interface.cpp
  RA_Interface.h
)
target_include_directories(rainterface PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
