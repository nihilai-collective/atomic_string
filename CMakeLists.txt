cmake_minimum_required(VERSION 3.28)

project("atomic_string" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

include(FetchContent)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../benchmarksuite/CMakeLists.txt")
	set(FETCHCONTENT_SOURCE_DIR_BENCHMARKSUITE "${CMAKE_CURRENT_SOURCE_DIR}/../benchmarksuite")
endif()
FetchContent_Declare(
	benchmarksuite
	GIT_REPOSITORY https://github.com/nihilai-collective/benchmarksuite.git
	GIT_TAG main
	GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(benchmarksuite)

add_subdirectory("atomic_string")
