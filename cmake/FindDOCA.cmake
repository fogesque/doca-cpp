# FindDOCA.cmake
# Locates DOCA SDK components
#
# Usage:
#   find_package(DOCA REQUIRED COMPONENTS common gpunetio argp rdma gpunetio_device)
#
# Creates imported targets:
#   DOCA::common           - doca-common library
#   DOCA::gpunetio         - doca-gpunetio library
#   DOCA::argp             - doca-argp library
#   DOCA::rdma             - doca-rdma library
#   DOCA::gpunetio_device  - doca_gpunetio_device library (GPU device code)
#
# Result variables:
#   DOCA_FOUND             - TRUE if all requested components were found
#   DOCA_INCLUDE_DIRS      - DOCA include directories
#   DOCA_LIBRARIES         - All found DOCA libraries

include(FindPackageHandleStandardArgs)
find_package(PkgConfig QUIET)

# Ensure DOCA pkg-config files are discoverable
set(_doca_pkgconfig_dirs
    /opt/mellanox/doca/lib/x86_64-linux-gnu/pkgconfig
    /opt/mellanox/doca/lib/pkgconfig
)
foreach(_dir IN LISTS _doca_pkgconfig_dirs)
    if(IS_DIRECTORY "${_dir}" AND NOT "$ENV{PKG_CONFIG_PATH}" MATCHES "${_dir}")
        set(ENV{PKG_CONFIG_PATH} "${_dir}:$ENV{PKG_CONFIG_PATH}")
    endif()
endforeach()

# Use pkg-config for doca-common to locate the base include/lib directories
if(PkgConfig_FOUND)
    pkg_check_modules(PC_DOCA_COMMON QUIET doca-common)
endif()

# Find DOCA include directory (shared by all components)
find_path(DOCA_INCLUDE_DIR
    NAMES doca_error.h doca_types.h
    HINTS
        ${PC_DOCA_COMMON_INCLUDEDIR}
        ${PC_DOCA_COMMON_INCLUDE_DIRS}
        /opt/mellanox/doca/include
        ENV DOCA_ROOT
    PATH_SUFFIXES include
)

# Default components
if(NOT DOCA_FIND_COMPONENTS)
    set(DOCA_FIND_COMPONENTS common)
endif()

set(DOCA_LIBRARIES "")

foreach(_comp IN LISTS DOCA_FIND_COMPONENTS)
    string(TOUPPER "${_comp}" _COMP_UPPER)

    if(_comp STREQUAL "gpunetio_device")
        # Special handling: gpunetio_device is located via doca-gpunetio's
        # pkg-config 'libdir' variable
        # (matches meson: doca_gpu_dep.get_variable(pkgconfig : 'libdir'))
        if(PkgConfig_FOUND)
            pkg_get_variable(DOCA_GPUNETIO_LIBDIR doca-gpunetio libdir)
        endif()

        find_library(DOCA_GPUNETIO_DEVICE_LIBRARY
            NAMES doca_gpunetio_device
            HINTS
                ${DOCA_GPUNETIO_LIBDIR}
                ${PC_DOCA_COMMON_LIBDIR}
                /opt/mellanox/doca/lib/x86_64-linux-gnu
                ENV DOCA_ROOT
            PATH_SUFFIXES lib lib/x86_64-linux-gnu
        )

        if(DOCA_GPUNETIO_DEVICE_LIBRARY AND NOT TARGET DOCA::gpunetio_device)
            add_library(DOCA::gpunetio_device UNKNOWN IMPORTED)
            set_target_properties(DOCA::gpunetio_device PROPERTIES
                IMPORTED_LOCATION "${DOCA_GPUNETIO_DEVICE_LIBRARY}"
            )
        endif()

        if(DOCA_GPUNETIO_DEVICE_LIBRARY)
            set(DOCA_gpunetio_device_FOUND TRUE)
            list(APPEND DOCA_LIBRARIES "${DOCA_GPUNETIO_DEVICE_LIBRARY}")
        else()
            set(DOCA_gpunetio_device_FOUND FALSE)
        endif()
    else()
        # Standard DOCA library component
        if(PkgConfig_FOUND)
            pkg_check_modules(PC_DOCA_${_COMP_UPPER} QUIET doca-${_comp})
        endif()

        find_library(DOCA_${_COMP_UPPER}_LIBRARY
            NAMES doca_${_comp}
            HINTS
                ${PC_DOCA_${_COMP_UPPER}_LIBDIR}
                ${PC_DOCA_${_COMP_UPPER}_LIBRARY_DIRS}
                ${PC_DOCA_COMMON_LIBDIR}
                /opt/mellanox/doca/lib/x86_64-linux-gnu
                ENV DOCA_ROOT
            PATH_SUFFIXES lib lib/x86_64-linux-gnu
        )

        if(DOCA_${_COMP_UPPER}_LIBRARY AND NOT TARGET DOCA::${_comp})
            add_library(DOCA::${_comp} UNKNOWN IMPORTED)
            set_target_properties(DOCA::${_comp} PROPERTIES
                IMPORTED_LOCATION "${DOCA_${_COMP_UPPER}_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${DOCA_INCLUDE_DIR}"
            )

            # For static linking, resolve transitive system dependencies via pkg-config --static
            if(PkgConfig_FOUND)
                execute_process(
                    COMMAND ${PKG_CONFIG_EXECUTABLE} --static --libs-only-l doca-${_comp}
                    OUTPUT_VARIABLE _static_libs_output
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE _pkg_result
                )
                # Also get the library search paths for resolving -l:libXXX.a flags
                execute_process(
                    COMMAND ${PKG_CONFIG_EXECUTABLE} --static --libs-only-L doca-${_comp}
                    OUTPUT_VARIABLE _static_libdirs_output
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                )
                string(REGEX MATCHALL "-L([^ ]+)" _libdir_flags "${_static_libdirs_output}")
                set(_search_dirs "")
                foreach(_flag IN LISTS _libdir_flags)
                    string(REGEX REPLACE "^-L" "" _dir "${_flag}")
                    list(APPEND _search_dirs "${_dir}")
                endforeach()

                if(_pkg_result EQUAL 0 AND _static_libs_output)
                    string(REGEX MATCHALL "-l([^ ]+)" _lib_flags "${_static_libs_output}")
                    set(_transitive_libs "")
                    foreach(_flag IN LISTS _lib_flags)
                        string(REGEX REPLACE "^-l" "" _lib "${_flag}")
                        # Skip DOCA libs (handled as static archives via -l:libXXX.a) and CMake-managed libs
                        if(_lib MATCHES "^doca_" OR _lib MATCHES "^cuda" OR _lib STREQUAL "stdc++")
                            continue()
                        endif()
                        # Handle -l:libXXX.a style (static archive reference)
                        if(_lib MATCHES "^:lib(.+)\\.a$")
                            string(REGEX REPLACE "^:lib(.+)\\.a$" "\\1" _archive_name "${_lib}")
                            # Resolve to full path
                            find_library(_resolved_${_archive_name}
                                NAMES ${_archive_name}
                                HINTS ${_search_dirs}
                                    /opt/mellanox/doca/lib/x86_64-linux-gnu
                                NO_DEFAULT_PATH
                            )
                            if(_resolved_${_archive_name})
                                list(APPEND _transitive_libs "${_resolved_${_archive_name}}")
                            endif()
                            unset(_resolved_${_archive_name} CACHE)
                            continue()
                        endif()
                        list(APPEND _transitive_libs "${_lib}")
                    endforeach()
                    if(_transitive_libs)
                        list(REMOVE_DUPLICATES _transitive_libs)
                        set_property(TARGET DOCA::${_comp} APPEND PROPERTY
                            INTERFACE_LINK_LIBRARIES "${_transitive_libs}")
                    endif()
                endif()
            endif()
        endif()

        if(DOCA_${_COMP_UPPER}_LIBRARY)
            set(DOCA_${_comp}_FOUND TRUE)
            list(APPEND DOCA_LIBRARIES "${DOCA_${_COMP_UPPER}_LIBRARY}")
        else()
            set(DOCA_${_comp}_FOUND FALSE)
        endif()
    endif()
endforeach()

find_package_handle_standard_args(DOCA
    REQUIRED_VARS DOCA_INCLUDE_DIR
    HANDLE_COMPONENTS
)

if(DOCA_FOUND)
    set(DOCA_INCLUDE_DIRS "${DOCA_INCLUDE_DIR}")
    add_compile_definitions(DOCA_ALLOW_EXPERIMENTAL_API)
endif()
