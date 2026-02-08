# MFF v11.6.0 Zygisk hook module - ARM64 only
cmake_minimum_required(VERSION 3.22)
project(mff_hooks CXX)

set(CMAKE_CXX_STANDARD 17)

# ARM64 only (Zygisk arm64-v8a)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

add_library(mff_hooks SHARED
  src/mff_11_6_0_hooks.cpp
)

target_include_directories(mff_hooks PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  # Dobby: add path to Dobby include, e.g. ${CMAKE_SOURCE_DIR}/dobby/include
  # And64InlineHook: add path to And64InlineHook include
)

# Link log (Android)
target_link_libraries(mff_hooks PRIVATE log)

# Hook framework: link Dobby or And64InlineHook (set one)
# Option A: Dobby - set DOBBY_LIB to path to libdobby.a, or place at lib/arm64-v8a/libdobby.a
if(DEFINED DOBBY_LIB)
  target_link_libraries(mff_hooks PRIVATE ${DOBBY_LIB})
else()
  target_link_libraries(mff_hooks PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/${ANDROID_ABI}/libdobby.a
  )
endif()

# Option B: And64InlineHook - in src set USE_DOBBY 0, then link:
# target_link_libraries(mff_hooks PRIVATE ${CMAKE_SOURCE_DIR}/lib/${ANDROID_ABI}/libAnd64InlineHook.a)

# Output to zygisk folder for easy copy into Magisk ZIP
set_target_properties(mff_hooks PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/zygisk
  OUTPUT_NAME arm64-v8a
  PREFIX ""
  SUFFIX ".so"
)
