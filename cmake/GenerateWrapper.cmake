# Substitute the %FOO% placeholders in src/vlock.sh, mirroring the sed step
# from the original Makefile.  Invoked via cmake -P with -DIN/-DOUT/... set.

file(READ "${IN}" _contents)
string(REPLACE "%BOURNE_SHELL%" "${BOURNE_SHELL}" _contents "${_contents}")
string(REPLACE "%PREFIX%" "${PREFIX}" _contents "${_contents}")
string(REPLACE "%VLOCK_VERSION%" "${VLOCK_VERSION}" _contents "${_contents}")
string(REPLACE "%VLOCK_ENABLE_PLUGINS%" "${VLOCK_ENABLE_PLUGINS}" _contents "${_contents}")
file(WRITE "${OUT}" "${_contents}")

# Syntax-check the generated script (was: $(BOURNE_SHELL) -n vlock.sh).
execute_process(
  COMMAND "${BOURNE_SHELL}" -n "${OUT}"
  RESULT_VARIABLE _rc
)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "Generated vlock wrapper failed shell syntax check")
endif()
