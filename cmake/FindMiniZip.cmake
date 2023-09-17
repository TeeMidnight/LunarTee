# https://github.com/ivochkin/cmake/blob/master/FindMiniZip.cmake
# - Find minizip
# Find the native MINIZIP includes and library
#
#  MINIZIP_INCLUDE_DIR - where to find minizip.h, etc.
#  MINIZIP_LIBRARIES   - List of libraries when using minizip.
#  MINIZIP_FOUND       - True if minizip found.


IF (MINIZIP_INCLUDE_DIR)
  # Already in cache, be silent
  SET(MINIZIP_FIND_QUIETLY TRUE)
ENDIF (MINIZIP_INCLUDE_DIR)

FIND_PATH(MINIZIP_INCLUDE_DIR zip.h PATH_SUFFIXES minizip)

SET(MINIZIP_NAMES minizip)
FIND_LIBRARY(MINIZIP_LIBRARY NAMES ${MINIZIP_NAMES} )

# handle the QUIETLY and REQUIRED arguments and set MINIZIP_FOUND to TRUE if 
# all listed variables are TRUE
INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(MiniZip DEFAULT_MSG MINIZIP_LIBRARY MINIZIP_INCLUDE_DIR)

IF(MINIZIP_FOUND)
  SET( MINIZIP_LIBRARIES ${MINIZIP_LIBRARY} )
ELSE(MINIZIP_FOUND)
  SET( MINIZIP_LIBRARIES )
ENDIF(MINIZIP_FOUND)

MARK_AS_ADVANCED( MINIZIP_LIBRARY MINIZIP_INCLUDE_DIR )