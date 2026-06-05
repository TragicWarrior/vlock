# Plugin modules.  Each is a shared object dlopen'd by vlock-main; undefined
# symbols (console_switch, etc.) are resolved against vlock-main at load time.

pkg_check_modules(NCURSES IMPORTED_TARGET ncurses)

# Per-module extra link libraries.
set(_libs_train   PkgConfig::NCURSES)
set(_libs_cmatrix PkgConfig::NCURSES)
set(_libs_caca    PkgConfig::NCURSES caca)

# Per-module extra sources.  The screen savers share the info-box overlay.
set(_srcs_cmatrix info_box.c)
set(_srcs_train   info_box.c)

# Privileged modules: installed group=${VLOCK_GROUP}, mode=${VLOCK_MODULE_MODE}.
set(PRIVILEGED_MODULES new nosysrq)

foreach(m IN LISTS MODULES)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${m}.c")
    message(FATAL_ERROR "Unknown module '${m}': ${m}.c does not exist")
  endif()

  add_library(mod_${m} MODULE ${m}.c ${_srcs_${m}})
  set_target_properties(mod_${m} PROPERTIES
    PREFIX ""
    OUTPUT_NAME "${m}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  target_include_directories(mod_${m} PRIVATE ../src ${GLIB_INCLUDE_DIRS})

  if(DEFINED _libs_${m})
    target_link_libraries(mod_${m} PRIVATE ${_libs_${m}})
  endif()

  if(m IN_LIST PRIVILEGED_MODULES)
    install(TARGETS mod_${m} LIBRARY DESTINATION "${MODULEDIR}")
    # chgrp + chmod to the privileged group/mode at install time (needs root,
    # which a real install already has).  Skipped gracefully otherwise.
    install(CODE "
      set(group \"${VLOCK_GROUP}\")
      set(mode \"${VLOCK_MODULE_MODE}\")
      set(target \"\$ENV{DESTDIR}${MODULEDIR}/${m}.so\")
      execute_process(COMMAND id -u OUTPUT_VARIABLE _uid OUTPUT_STRIP_TRAILING_WHITESPACE)
      if(_uid STREQUAL \"0\")
        execute_process(COMMAND getent group \${group} RESULT_VARIABLE _gr OUTPUT_QUIET ERROR_QUIET)
        if(NOT _gr EQUAL 0)
          message(STATUS \"vlock: group '\${group}' missing - creating it\")
          execute_process(COMMAND groupadd -r \${group} RESULT_VARIABLE _ga)
          if(NOT _ga EQUAL 0)
            execute_process(COMMAND pw groupadd \${group} RESULT_VARIABLE _ga)
          endif()
        endif()
        execute_process(COMMAND chgrp \${group} \${target})
        execute_process(COMMAND chmod \${mode} \${target})
      else()
        message(WARNING \"vlock: not root - leaving group/mode of \${target} unchanged (expected group=\${group} mode=\${mode})\")
      endif()
    ")
  else()
    install(TARGETS mod_${m}
      LIBRARY DESTINATION "${MODULEDIR}"
      PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                  GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    )
  endif()
endforeach()
