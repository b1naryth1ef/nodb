project(nodb)
cmake_minimum_required (VERSION 3.2)

SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99")

# Build Debug w/ symbols
set(CMAKE_BUILD_TYPE Debug)

# Include our core sources
include_directories(src/include/)
FILE(GLOB_RECURSE CORE_SOURCES src/*.c)

# Include Deps
FILE(GLOB_RECURSE DEP_SOURCES
	deps/sds/*.c
	deps/mpack/src/*.c
  deps/farmhash-c/farmhash.c
	deps/hashmap_c/hashmap.c
)
include_directories(deps/)
include_directories(deps/mpack/src)
include_directories(deps/farmhash-c/)
include_directories(deps/hashmap_c/)

add_executable(nodb ${CORE_SOURCES} ${DEP_SOURCES})
target_link_libraries(nodb uv pthread)
