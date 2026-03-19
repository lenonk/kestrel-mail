#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "mailio::mailio" for configuration "Release"
set_property(TARGET mailio::mailio APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(mailio::mailio PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libmailio.so"
  IMPORTED_SONAME_RELEASE "libmailio.so"
  )

list(APPEND _cmake_import_check_targets mailio::mailio )
list(APPEND _cmake_import_check_files_for_mailio::mailio "${_IMPORT_PREFIX}/lib/libmailio.so" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
