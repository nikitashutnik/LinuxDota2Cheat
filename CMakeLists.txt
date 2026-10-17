cmake_minimum_required(VERSION 3.5)
project(Dota2Helper)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
	if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 7.1)
		message(FATAL_ERROR "Your GCC seems outdated. ( Requires >=7.1 for C++17 support )")
	endif()
else()
	message(FATAL_ERROR "You must have GCC >= 7.1 installed in order to compile")
endif()

add_definitions(-D_GLIBCXX_USE_CXX11_ABI=0)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}\
 -O3 \
 -std=c++17 \
 -Wall \
 -Wextra \
 -Wformat \
 -Wno-unused-parameter \
 -Wno-implicit-fallthrough \
 --no-gnu-unique \
 -Wno-pmf-conversions \
 -fno-exceptions \
 -fPIC \
 -shared \
 -ffast-math \
 -fno-math-errno \
 -fno-ident \
 -fno-stack-protector \
 -ffunction-sections \
 -fdata-sections \
 -fno-threadsafe-statics \
 -Wl,-z,now \
 -Wl,-z,norelro \
 -pipe \
 -Wl,--hash-style=gnu")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -ggdb ")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -s -fvisibility=hidden -fvisibility-inlines-hidden")

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")

if(NOT CMAKE_BUILD_TYPE)
	message(STATUS "No build type selected, default to Release")
	set(CMAKE_BUILD_TYPE RELEASE)
endif(NOT CMAKE_BUILD_TYPE)

file(GLOB_RECURSE SOURCE_FILES "${CMAKE_SOURCE_DIR}/src/*.cpp")

# Exclude SDK tier0 memoverride (reference only)
list(FILTER SOURCE_FILES EXCLUDE REGEX ".*memoverride\\.cpp$")

add_library(Dota2Helper SHARED ${SOURCE_FILES})
target_link_libraries(Dota2Helper dl pthread)
