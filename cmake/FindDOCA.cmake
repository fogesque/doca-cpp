# FindDOCA.cmake
# Locates DOCA SDK installation

if(DOCA_FOUND)
    return()
endif()

# Try pkg-config first
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PC_DOCA QUIET doca-common)
endif()

# Find include directory
find_path(DOCA_INCLUDE_DIR
    NAMES doca_error.h doca_types.h
    HINTS
        ${PC_DOCA_INCLUDEDIR}
        ${PC_DOCA_INCLUDE_DIRS}
        /opt/mellanox/doca/include
        ENV DOCA_ROOT
    PATH_SUFFIXES include
)

# Find library directory
find_library(DOCA_COMMON_LIBRARY
    NAMES doca_common
    HINTS
        ${PC_DOCA_LIBDIR}
        ${PC_DOCA_LIBRARY_DIRS}
        /opt/mellanox/doca/lib/x86_64-linux-gnu
        ENV DOCA_ROOT
    PATH_SUFFIXES lib lib/x86_64-linux-gnu
)

# Handle other DOCA libraries
foreach(lib rdma dma flow)
    find_library(DOCA_${lib}_LIBRARY
        NAMES doca_${lib}
        HINTS
            ${PC_DOCA_LIBDIR}
            /opt/mellanox/doca/lib/x86_64-linux-gnu
    )
endforeach()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(DOCA
    REQUIRED_VARS DOCA_INCLUDE_DIR DOCA_COMMON_LIBRARY
)

if(DOCA_FOUND)
    add_compile_definitions(DOCA_ALLOW_EXPERIMENTAL_API)

    set(DOCA_INCLUDE_DIRS ${DOCA_INCLUDE_DIR})
    set(DOCA_LIBRARIES ${DOCA_COMMON_LIBRARY})

    # Create imported target
    if(NOT TARGET DOCA::common)
        add_library(DOCA::common UNKNOWN IMPORTED)
        set_target_properties(DOCA::common PROPERTIES
            IMPORTED_LOCATION "${DOCA_COMMON_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${DOCA_INCLUDE_DIR}"
        )
    endif()
endif()
