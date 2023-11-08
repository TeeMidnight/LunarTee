
find_path(LIBZIP_INCLUDE_DIR zip.h)
find_path(LIBZIP_INCLUDE_DIR_ZIPCONF zipconf.h)

find_library(LIBZIP_LIBRARY NAMES zip)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibZip DEFAULT_MSG LIBZIP_LIBRARY LIBZIP_INCLUDE_DIR LIBZIP_INCLUDE_DIR_ZIPCONF)

if(LIBZIP_FOUND)
  set( LIBZIP_LIBRARIES ${LIBZIP_LIBRARY} )
else(LIBZIP_FOUND)
  set( LIBZIP_LIBRARIES )
endif(LIBZIP_FOUND)