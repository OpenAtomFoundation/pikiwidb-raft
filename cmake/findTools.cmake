FIND_PROGRAM(AUTOCONF autoconf PATHS /usr/bin /usr/local/bin)

IF(${AUTOCONF} MATCHES AUTOCONF-NOTFOUND)
    MESSAGE(FATAL_ERROR "not find autoconf on localhost")
ENDIF()

FIND_PROGRAM(CLANG_FORMAT_BIN
        NAMES clang-format)
IF("${CLANG_FORMAT_BIN}" STREQUAL "CLANG_FORMAT_BIN-NOTFOUND")
    MESSAGE(WARNING "couldn't find clang-format.")
ELSE()
    MESSAGE(STATUS "found clang-format at ${CLANG_FORMAT_BIN}")
ENDIF()

FIND_PROGRAM(CLANG_TIDY_BIN
        NAMES clang-tidy clang-tidy-12 clang-tidy-14)
IF("${CLANG_TIDY_BIN}" STREQUAL "CLANG_TIDY_BIN-NOTFOUND")
    MESSAGE(WARNING "couldn't find clang-tidy.")
ELSE()
    MESSAGE(STATUS "found clang-tidy at ${CLANG_TIDY_BIN}")
ENDIF()

FIND_PROGRAM(CPPLINT_BIN
	NAMES cpplint cpplint.py
	HINTS "${BUILD_SUPPORT_DIR}")
IF("${CPPLINT_BIN}" STREQUAL "CPPLINT_BIN-NOTFOUND")
  MESSAGE(WARNING "couldn't find cpplint.py")
ELSE()
  MESSAGE(STATUS "found cpplint at ${CPPLINT_BIN}")
ENDIF()

FIND_PROGRAM(CLANG_APPLY_REPLACEMENTS_BIN
        NAMES clang-apply-replacements clang-apply-replacements-12 clang-apply-replacements-14)
IF("${CLANG_APPLY_REPLACEMENTS_BIN}" STREQUAL "CLANG_APPLY_REPLACEMENTS_BIN-NOTFOUND")
    MESSAGE(WARNING "couldn't find clang-apply-replacements.")
ELSE()
    MESSAGE(STATUS "found clang-apply-replacements at ${CLANG_APPLY_REPLACEMENTS_BIN}")
ENDIF()

OPTION(WITH_COMMAND_DOCS "build with command docs support" OFF)
IF(WITH_COMMAND_DOCS)
    ADD_DEFINITIONS(-DWITH_COMMAND_DOCS)
ENDIF()

IF(${CMAKE_BUILD_TYPE} MATCHES "RELEASE")
    MESSAGE(STATUS "make RELEASE version")
    ADD_DEFINITIONS(-DBUILD_RELEASE)
    SET(BuildType "Release")
ELSE()
    MESSAGE(STATUS "make DEBUG version")
    ADD_DEFINITIONS(-DBUILD_DEBUG)
    SET(BuildType "Debug")
ENDIF()
