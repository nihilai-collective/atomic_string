add_executable(atomic_string "main.cpp" "atomic_string.hpp")

target_link_libraries(atomic_string PRIVATE benchmarksuite::benchmarksuite)

target_compile_options(
	atomic_string PRIVATE
	$<$<CXX_COMPILER_ID:MSVC>:$<$<CONFIG:Release>:/O2> /arch:AVX2 /Zc:preprocessor /EHsc /bigobj>
	$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-O3 -march=native>
)
