add_executable(riverhorse-regtest
  regtest_host.cpp
)

target_link_libraries(riverhorse-regtest PRIVATE core common scmversion xxhash)

add_core_resources(riverhorse-regtest)