cmake_minimum_required(VERSION 3.16)

project(vlock VERSION 2.9.9 LANGUAGES C)

#=============================================================================
# Configuration options (mirror the old ./configure / config.mk knobs)
#=============================================================================

set(AUTH_METHOD "pam" CACHE STRING "Authentication method: pam or shadow")
set_property(CACHE AUTH_METHOD PROPERTY STRINGS pam shadow)

option(ENABLE_PLUGINS "Enable plugin support in vlock-main" ON)
option(ENABLE_ROOT_PASSWORD "Also accept the root password to unlock" ON)

# Which modules to build (only meaningful when ENABLE_PLUGINS is ON).
set(MODULES "all;new;nosysrq;train;cmatrix;wetpipes"
    CACHE STRING "Semicolon-separated list of plugin modules to build")

set(VLOCK_GROUP "vlock" CACHE STRING "Group owning privileged plugins")
set(VLOCK_MODULE_MODE "0750" CACHE STRING "Mode for privileged plugins")

set(BOURNE_SHELL "/bin/sh" CACHE STRING "Shell used to run the vlock wrapper")

# Installation layout.  GNUInstallDirs gives us CMAKE_INSTALL_{BIN,SBIN,LIB,MAN}DIR.
include(GNUInstallDirs)

set(MODULEDIR "${CMAKE_INSTALL_FULL_LIBDIR}/vlock/modules"
    CACHE PATH "Directory where plugin modules are installed")
set(SCRIPTDIR "${CMAKE_INSTALL_FULL_LIBDIR}/vlock/scripts"
    CACHE PATH "Directory where plugin scripts are installed")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
endif()

#=============================================================================
# Language / warning settings (was: -Wall -W -pedantic -std=gnu99)
#=============================================================================

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)        # -std=gnu99 (gnu extensions: explicit_bzero etc.)

add_compile_options(-Wall -Wextra -pedantic)

#=============================================================================
# Dependencies
#=============================================================================

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0 gobject-2.0)

#=============================================================================
# vlock-main (setuid-root core)
#=============================================================================

set(VLOCK_MAIN_SOURCES
  src/vlock-main.c
  src/prompt.c
  src/auth-${AUTH_METHOD}.c
  src/console_switch.c
  src/signals.c
  src/terminal.c
  src/util.c
  src/logging.c
)

if(ENABLE_PLUGINS)
  list(APPEND VLOCK_MAIN_SOURCES
    src/plugins.c
    src/plugin.c
    src/module.c
    src/process.c
    src/script.c
    src/tsort.c
  )
endif()

add_executable(vlock-main ${VLOCK_MAIN_SOURCES})
target_include_directories(vlock-main PRIVATE src)
target_link_libraries(vlock-main PRIVATE PkgConfig::GLIB)

if(NOT ENABLE_ROOT_PASSWORD)
  target_compile_definitions(vlock-main PRIVATE NO_ROOT_PASS)
endif()

if(ENABLE_PLUGINS)
  target_compile_definitions(vlock-main PRIVATE
    USE_PLUGINS
    VLOCK_MODULE_DIR="${MODULEDIR}"
    VLOCK_SCRIPT_DIR="${SCRIPTDIR}"
  )
  # -rdynamic so dlopen'd modules can resolve symbols (e.g. console_switch)
  # exported by vlock-main itself.
  set_target_properties(vlock-main PROPERTIES ENABLE_EXPORTS ON)
  target_link_libraries(vlock-main PRIVATE ${CMAKE_DL_LIBS})
endif()

if(AUTH_METHOD STREQUAL "pam")
  find_library(PAM_LIBRARY pam REQUIRED)
  target_link_libraries(vlock-main PRIVATE ${PAM_LIBRARY} ${CMAKE_DL_LIBS})
elseif(AUTH_METHOD STREQUAL "shadow")
  find_library(CRYPT_LIBRARY crypt REQUIRED)
  target_link_libraries(vlock-main PRIVATE ${CRYPT_LIBRARY})
endif()

#=============================================================================
# vlock wrapper script (generated from src/vlock.sh template)
#=============================================================================

set(VLOCK_WRAPPER "${CMAKE_CURRENT_BINARY_DIR}/vlock")
if(ENABLE_PLUGINS)
  set(VLOCK_ENABLE_PLUGINS "yes")
else()
  set(VLOCK_ENABLE_PLUGINS "no")
endif()

add_custom_command(
  OUTPUT "${VLOCK_WRAPPER}"
  COMMAND ${CMAKE_COMMAND}
    -DIN=${CMAKE_CURRENT_SOURCE_DIR}/src/vlock.sh
    -DOUT=${VLOCK_WRAPPER}
    -DBOURNE_SHELL=${BOURNE_SHELL}
    -DPREFIX=${CMAKE_INSTALL_PREFIX}
    -DVLOCK_VERSION=${PROJECT_VERSION}
    -DVLOCK_ENABLE_PLUGINS=${VLOCK_ENABLE_PLUGINS}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateWrapper.cmake
  DEPENDS src/vlock.sh cmake/GenerateWrapper.cmake
  COMMENT "Generating vlock wrapper script"
  VERBATIM
)
add_custom_target(vlock-wrapper ALL DEPENDS "${VLOCK_WRAPPER}")

#=============================================================================
# Plugins (modules)
#=============================================================================

if(ENABLE_PLUGINS)
  add_subdirectory(modules)
endif()

#=============================================================================
# Tests (CUnit), enabled with -DBUILD_TESTING=ON
#=============================================================================

include(CTest)
if(BUILD_TESTING AND ENABLE_PLUGINS)
  find_library(CUNIT_LIBRARY cunit)
  if(CUNIT_LIBRARY)
    add_executable(vlock-test
      tests/vlock-test.c
      tests/test_tsort.c
      tests/test_util.c
      tests/test_process.c
      src/tsort.c
      src/util.c
      src/process.c
    )
    target_include_directories(vlock-test PRIVATE src tests)
    target_link_libraries(vlock-test PRIVATE PkgConfig::GLIB ${CUNIT_LIBRARY})
    add_test(NAME vlock-test COMMAND vlock-test)
    set_tests_properties(vlock-test PROPERTIES
      ENVIRONMENT "VLOCK_TEST_OUTPUT_MODE=verbose")
  else()
    message(WARNING "CUnit not found; vlock-test will not be built")
  endif()
endif()

#=============================================================================
# Installation
#=============================================================================

# vlock-main: setuid root (mode 4711).  Owner becomes root when installed via
# sudo; the setuid bit is set explicitly here.
install(TARGETS vlock-main
  RUNTIME DESTINATION "${CMAKE_INSTALL_SBINDIR}"
  PERMISSIONS SETUID OWNER_READ OWNER_WRITE OWNER_EXECUTE
              GROUP_EXECUTE WORLD_EXECUTE
)

install(PROGRAMS "${VLOCK_WRAPPER}" DESTINATION "${CMAKE_INSTALL_BINDIR}")

install(FILES man/vlock.1        DESTINATION "${CMAKE_INSTALL_MANDIR}/man1")
install(FILES man/vlock-main.8   DESTINATION "${CMAKE_INSTALL_MANDIR}/man8")
install(FILES man/vlock-plugins.5 DESTINATION "${CMAKE_INSTALL_MANDIR}/man5")
