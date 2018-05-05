INCLUDE(FindPkgConfig)
PKG_CHECK_MODULES(PC_FDC FDC)

FIND_PATH(
    FDC_INCLUDE_DIRS
    NAMES FDC/api.h
    HINTS $ENV{FDC_DIR}/include
        ${PC_FDC_INCLUDEDIR}
    PATHS ${CMAKE_INSTALL_PREFIX}/include
          /usr/local/include
          /usr/include
)

FIND_LIBRARY(
    FDC_LIBRARIES
    NAMES gnuradio-FDC
    HINTS $ENV{FDC_DIR}/lib
        ${PC_FDC_LIBDIR}
    PATHS ${CMAKE_INSTALL_PREFIX}/lib
          ${CMAKE_INSTALL_PREFIX}/lib64
          /usr/local/lib
          /usr/local/lib64
          /usr/lib
          /usr/lib64
)

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(FDC DEFAULT_MSG FDC_LIBRARIES FDC_INCLUDE_DIRS)
MARK_AS_ADVANCED(FDC_LIBRARIES FDC_INCLUDE_DIRS)

