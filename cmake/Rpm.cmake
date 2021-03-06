find_program(LSB_RELEASE_EXEC lsb_release)

if (LSB_RELEASE_EXEC)
  execute_process(COMMAND /bin/sh "-c" "${LSB_RELEASE_EXEC} -is | tr '[:upper:]' '[:lower:]' | cut -d' ' -f1"
    OUTPUT_VARIABLE DIST_NAME
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  execute_process(COMMAND /bin/sh "-c" "${LSB_RELEASE_EXEC} -ds | tr '[:upper:]' '[:lower:]' | sed 's/\"//g' | cut -d' ' -f2"
    OUTPUT_VARIABLE DIST_RELEASE
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  execute_process(COMMAND /bin/sh "-c" "${LSB_RELEASE_EXEC} -ds | tr '[:upper:]' '[:lower:]' | sed 's/\"//g' | sed 's/\\.//g' | cut -d' ' -f3"
    OUTPUT_VARIABLE DIST_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if (DIST_NAME)
    message(STATUS "Distro Name: ${DIST_NAME}")
    if (DIST_RELEASE)
      message(STATUS "Distro Release: ${DIST_RELEASE}")
    endif()
    if (DIST_VERSION)
      message(STATUS "Distro Version: ${DIST_VERSION}")
    endif()
    set(RPM_ARCH x86_64 CACHE STRING "Architecture of the rpm file")
    set(RPMBUILD_DIR ~/rpmbuild CACHE STRING "Rpmbuild directory, for the rpm target")
    if (${DIST_NAME} STREQUAL "opensuse")
      if (DIST_RELEASE)
        if (${DIST_RELEASE} STREQUAL "leap")
          if (DIST_VERSION)
            set(RPM_DISTRO "lp${DIST_VERSION}" CACHE STRING "Suffix of the rpm file")
          else()
            set(RPM_DISTRO ${DIST_RELEASE} CACHE STRING "Suffix of the rpm file")
          endif()
        elseif (${DIST_RELEASE} STREQUAL "tumbleweed")
          set(RPM_DISTRO ${DIST_RELEASE} CACHE STRING "Suffix of the rpm file")
        else ()
          set(RPM_DISTRO ${DIST_NAME} CACHE STRING "Suffix of the rpm file")
        endif()
      else()
        set(RPM_DISTRO ${DIST_NAME} CACHE STRING "Suffix of the rpm file")
      endif()
      add_custom_target(rpm
        COMMAND ${CMAKE_SOURCE_DIR}/dist/scripts/maketarball.sh
        COMMAND ${CMAKE_COMMAND} -E copy strawberry-${STRAWBERRY_VERSION_PACKAGE}.tar.xz ${RPMBUILD_DIR}/SOURCES/
        COMMAND rpmbuild -bs ${CMAKE_SOURCE_DIR}/dist/opensuse/strawberry.spec
        COMMAND rpmbuild -bb ${CMAKE_SOURCE_DIR}/dist/opensuse/strawberry.spec
      )
    elseif (${DIST_NAME} STREQUAL "fedora")
      if (DIST_VERSION)
        set(RPM_DISTRO "${DIST_NAME}${DIST_VERSION}" CACHE STRING "Suffix of the rpm file")
      else ()
        set(RPM_DISTRO ${DIST_NAME} CACHE STRING "Suffix of the rpm file")
      endif()
      add_custom_target(rpm
        COMMAND ${CMAKE_SOURCE_DIR}/dist/scripts/maketarball.sh
        COMMAND ${CMAKE_COMMAND} -E copy strawberry-${STRAWBERRY_VERSION_PACKAGE}.tar.xz ${RPMBUILD_DIR}/SOURCES/
        COMMAND rpmbuild -bs ${CMAKE_SOURCE_DIR}/dist/fedora/strawberry.spec
        COMMAND rpmbuild -bb ${CMAKE_SOURCE_DIR}/dist/fedora/strawberry.spec
      )
    else()
      set(RPM_DISTRO ${DIST_NAME} CACHE STRING "Suffix of the rpm file")
    endif()
    message(STATUS "RPM Suffix: ${RPM_DISTRO}")
  endif()
endif()
