cmake_minimum_required(VERSION 3.28)
include (GNUInstallDirs)

find_package(doctest CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

add_executable(unit_tests)

target_sources(
	unit_tests 
	PRIVATE
		main.cpp
		unit_tests.cpp
		
)

target_compile_options(unit_tests PRIVATE "$<$<C_COMPILER_ID:MSVC>:/utf-8>")
target_compile_options(unit_tests PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
target_compile_options(
	unit_tests
	INTERFACE
		$<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-g -Werror -Wall -Wextra -Wshadow  -Wnon-virtual-dtor -Wold-style-cast  -Wcast-align  -Wunused  -Woverloaded-virtual  -Wpedantic  -Wconversion  -Wsign-conversion  -Wnull-dereference  -Wdouble-promotion  -Wformat=2  -Wimplicit-fallthrough -Wno-c++17-compat -Wno-c++17-compat-pedantic -Wno-c++98-compat -Wno-c++98-compat-pedantic>
		$<$<CXX_COMPILER_ID:GNU>:-g -Werror -Wall -Wextra -Wshadow  -Wnon-virtual-dtor -Wold-style-cast  -Wcast-align  -Wunused  -Woverloaded-virtual  -Wpedantic  -Wconversion  -Wsign-conversion  -Wnull-dereference  -Wdouble-promotion  -Wformat=2  -Wimplicit-fallthrough -Wmisleading-indentation -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wuseless-cast -Wsuggest-override>
		$<$<CXX_COMPILER_ID:MSVC>:/Zc:externC- /WX /W4 /w14242 /w14254 /w14263 /w14265 /w14287 /we4289 /w14296 /w14311 /w14545 /w14546 /w14547 /w14549 /w14555 /w14619 /w14640 /w14826 /w14905  /w14906  /w14928  /permissive->
)
target_compile_options(
	unit_tests
	INTERFACE
		$<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<$<CXX_COMPILER_ID:GNU>>:-fstack-protector-strong -fcf-protection -fstack-clash-protection -fsanitize=undefined -fsanitize-minimal-runtime -fno-sanitize-recover=undefined>
		$<$<CXX_COMPILER_ID:MSVC>:/sdl /DYNAMICBASE /guard:cf>
)
target_compile_definitions(
	unit_tests
	INTERFACE
		$<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<$<CXX_COMPILER_ID:GNU>>:D_GLIBCXX_ASSERTIONS>
		$<$<CXX_COMPILER_ID:MSVC>:>
)
target_link_options(
	unit_tests
	INTERFACE
		$<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<$<CXX_COMPILER_ID:GNU>>:-fstack-protector-strong -fcf-protection -fstack-clash-protection -fsanitize=undefined -fsanitize-minimal-runtime -fno-sanitize-recover=undefined>
		$<$<CXX_COMPILER_ID:MSVC>:/NXCOMPAT /CETCOMPAT>
)

target_link_libraries(
	unit_tests 
	PRIVATE
		sph-zstd
		doctest::doctest
		fmt::fmt
)

