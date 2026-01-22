project(CGQuery)

add_executable(
  cgquery
  src/CGQuery.cpp
  src/commands/DomCommand.cpp
  src/commands/PostDomCommand.cpp
  src/commands/ReachesCommand.cpp
)

target_include_directories(cgquery PRIVATE include/)

add_metacg(cgquery)
add_cxxopts(cgquery)
