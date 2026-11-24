add_executable(riverhorse-relay
  relay.cpp
)

target_include_directories(riverhorse-relay PRIVATE "${CMAKE_SOURCE_DIR}/src")
