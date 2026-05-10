# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/clinton/dev/refactor-engine-merger/WildPalms/pilot-link")
  file(MAKE_DIRECTORY "/home/clinton/dev/refactor-engine-merger/WildPalms/pilot-link")
endif()
file(MAKE_DIRECTORY
  "/home/clinton/dev/refactor-engine-merger/WildPalms/build-dev/pilot-link-build"
  "/home/clinton/dev/refactor-engine-merger/WildPalms/build-dev/lib/pilot-link-external-prefix"
  "/home/clinton/dev/refactor-engine-merger/WildPalms/build-dev/lib/pilot-link-external-prefix/tmp"
  "/home/clinton/dev/refactor-engine-merger/WildPalms/build-dev/lib/pilot-link-external-prefix/src/pilot-link-external-stamp"
  "/home/clinton/dev/refactor-engine-merger/WildPalms/build-dev/lib/pilot-link-external-prefix/src"
  "/home/clinton/dev/refactor-engine-merger/WildPalms/build-dev/lib/pilot-link-external-prefix/src/pilot-link-external-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/clinton/dev/refactor-engine-merger/WildPalms/build-dev/lib/pilot-link-external-prefix/src/pilot-link-external-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/clinton/dev/refactor-engine-merger/WildPalms/build-dev/lib/pilot-link-external-prefix/src/pilot-link-external-stamp${cfgdir}") # cfgdir has leading slash
endif()
