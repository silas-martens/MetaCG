set(PROJECT_NAME collector-tests)
set(TARGETS_EXPORT_NAME ${PROJECT_NAME}-target)

add_executable(cgsimpletester CGSimpleTester.cpp)

add_executable(stdtester STDTester.cpp)

# register_to_clang_tidy(cgsimpletester) register_to_clang_tidy(mcgtester)

add_json(cgsimpletester)
default_compile_options(cgsimpletester)

add_json(stdtester)
add_collector_include(stdtester)
add_collector_lib(stdtester)
default_compile_options(stdtester)

install(
  TARGETS cgsimpletester stdtester
  EXPORT ${TARGETS_EXPORT_NAME}
  RUNTIME DESTINATION bin
)

configure_package_config_file(
  ${METACG_Directory}/cmake/Config.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
  INSTALL_DESTINATION lib/cmake
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake DESTINATION lib/cmake)
