cmake_minimum_required(VERSION 3.20)
project(xfce4-agent-prompt-panel VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)
include(CTest)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PANEL REQUIRED IMPORTED_TARGET libxfce4panel-2.0>=4.18)
pkg_check_modules(GTK REQUIRED IMPORTED_TARGET gtk+-3.0>=3.24)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
pkg_check_modules(YAML REQUIRED IMPORTED_TARGET yaml-0.1)
pkg_check_modules(JSON REQUIRED IMPORTED_TARGET json-glib-1.0)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Werror)

add_library(assistant-core STATIC
  src/assistant.c
  src/sessions.c
  src/providers/copilot-sessions.c
  src/providers/junie-sessions.c)
set_target_properties(assistant-core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(assistant-core PUBLIC src)
target_link_libraries(assistant-core PUBLIC PkgConfig::GIO PRIVATE PkgConfig::JSON PkgConfig::YAML)

add_library(agent-ui STATIC src/agent-ui.c src/terminal.c)
set_target_properties(agent-ui PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(agent-ui PUBLIC src)
target_link_libraries(agent-ui PUBLIC assistant-core PkgConfig::GTK PkgConfig::PANEL)

add_library(agent-panel MODULE src/agent-plugin.c)
set_target_properties(agent-panel PROPERTIES OUTPUT_NAME copilot)
target_link_libraries(agent-panel PRIVATE agent-ui)
install(TARGETS agent-panel LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/xfce4/panel/plugins)
configure_file(data/agent.desktop.in agent.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/agent.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/xfce4/panel/plugins RENAME copilot.desktop)

add_executable(agent-panel-preview src/preview.c)
target_link_libraries(agent-panel-preview PRIVATE agent-ui)

if(BUILD_TESTING)
  pkg_check_modules(GMODULE REQUIRED IMPORTED_TARGET gmodule-2.0)
  add_executable(fake-assistant tests/fake-assistant.c)
  target_link_libraries(fake-assistant PRIVATE PkgConfig::JSON)
  add_executable(test-ui tests/test-ui.c)
  target_link_libraries(test-ui PRIVATE agent-ui PkgConfig::JSON)
  add_test(NAME ui COMMAND test-ui $<TARGET_FILE:fake-assistant>)
  set_tests_properties(ui PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
  add_executable(test-panel tests/test-panel.c)
  target_link_libraries(test-panel PRIVATE PkgConfig::GTK PkgConfig::PANEL PkgConfig::GMODULE)
  add_test(NAME panel COMMAND test-panel $<TARGET_FILE:agent-panel>)
  set_tests_properties(panel PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
  foreach(provider copilot junie)
    add_executable(test-${provider}-sessions tests/test-${provider}-sessions.c)
    target_link_libraries(test-${provider}-sessions PRIVATE assistant-core)
    add_test(NAME ${provider}-sessions COMMAND test-${provider}-sessions)
  endforeach()
  add_executable(test-assistant tests/test-assistant.c)
  target_link_libraries(test-assistant PRIVATE assistant-core)
  add_test(NAME assistant COMMAND test-assistant)
endif()
