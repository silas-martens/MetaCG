set(PROJECT_NAME collector-tools)
set(TARGETS_EXPORT_NAME ${PROJECT_NAME}-target)

add_executable(cgcollector CGCollector.cpp)
add_executable(cgmerge CGMerge.cpp)
add_executable(cgvalidate CGValidate.cpp)
add_executable(cgutils CGUtils.cpp)



# register_to_clang_tidy(cgcollector)
add_collector_include(cgcollector)
add_collector_lib(cgcollector)
add_config_include(cgcollector)
add_json(cgcollector)
default_compile_options(cgcollector)

add_collector_include(cgmerge)
add_collector_lib(cgmerge)
add_config_include(cgmerge)
add_json(cgmerge)

add_collector_include(cgvalidate)
add_collector_lib(cgvalidate)
add_cube(cgvalidate)
add_config_include(cgvalidate)
add_json(cgvalidate)
add_cxxopts(cgvalidate)
add_clang(cgvalidate)

add_json(cgutils)
add_cxxopts(cgutils)
add_metacg(cgutils)
add_spdlog_libraries(cgutils)
if(LOGLEVEL)
  target_compile_definitions(cgvalidate PRIVATE LOGLEVEL=${LOGLEVEL})
endif()

install(
  TARGETS cgcollector
          cgmerge
          cgvalidate
          cgutils
  EXPORT ${TARGETS_EXPORT_NAME}
  RUNTIME DESTINATION bin
)

configure_package_config_file(
  ${METACG_Directory}/cmake/Config.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
  INSTALL_DESTINATION lib/cmake
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake DESTINATION lib/cmake)

# cgc

# make_executable: Copy the input file to the output file and make it executable Source:
# https://git.rwth-aachen.de/tuda-sc/projects/capi/-/blob/devel/scripts/CMakeLists.txt?ref_type=heads
macro(
  make_executable
  input
  output
)
  # Executable permission: In CMAKE 3.19 we can simply use file(CHMOD) instead of file(copy) workaround
  configure_file(
    ${input}
    ${CMAKE_CURRENT_BINARY_DIR}/tmp/${output}
    @ONLY
  )
  file(
    COPY ${CMAKE_CURRENT_BINARY_DIR}/tmp/${output}
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR}
    FILE_PERMISSIONS
      OWNER_READ
      OWNER_WRITE
      OWNER_EXECUTE
      GROUP_READ
      GROUP_EXECUTE
      WORLD_READ
      WORLD_EXECUTE
  )
  file(REMOVE_RECURSE ${CMAKE_CURRENT_BINARY_DIR}/tmp)
endmacro()

function(
  configure_script
  input
  output
)
  set(CGCOLLECTOR_PATH ${CMAKE_INSTALL_PREFIX}/bin/)
  set(CGCOLLECTOR_LIB ${CMAKE_INSTALL_PREFIX}/lib/)

  make_executable(${input} ${output})
endfunction()

configure_script(cgc.in cgc)

install(
  FILES "${CMAKE_CURRENT_BINARY_DIR}/cgc"
  DESTINATION bin
  PERMISSIONS
    OWNER_READ
    OWNER_WRITE
    OWNER_EXECUTE
    GROUP_READ
    GROUP_EXECUTE
    WORLD_READ
    WORLD_EXECUTE
  # RENAME cgc  # Optionally rename the file during installation
)
