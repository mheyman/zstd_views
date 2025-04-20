# CMakeList.txt : CMake project for zstd_stream, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.28)
include (GNUInstallDirs)
include (CMakePackageConfigHelpers)

find_package(zstd CONFIG REQUIRED)
set(PORT_NAME sph-zstd)

# Add source to this project's executable.
add_library (${PORT_NAME} INTERFACE)
target_compile_features(${PORT_NAME} INTERFACE cxx_std_23)
if (DEVELOPER_MODE)
	target_compile_options(
		${PORT_NAME}
		INTERFACE
			$<$<OR:$<CXX_COMPILER_FRONTEND_VARIANT:Clang>,$<CXX_COMPILER_FRONTEND_VARIANT:AppleClang>>:-g -Werror -Wall -Wextra -Wshadow  -Wnon-virtual-dtor -Wold-style-cast  -Wcast-align  -Wunused  -Woverloaded-virtual  -Wpedantic  -Wconversion  -Wsign-conversion  -Wnull-dereference  -Wdouble-promotion  -Wformat=2  -Wimplicit-fallthrough  -Wno-c++17-compat -Wno-c++17-compat-pedantic -Wno-c++98-compat -Wno-c++98-compat-pedantic>
			$<$<CXX_COMPILER_FRONTEND_VARIANT:GNU>:-g -Werror -Wall -Wextra -Wshadow  -Wnon-virtual-dtor -Wold-style-cast  -Wcast-align  -Wunused  -Woverloaded-virtual  -Wpedantic  -Wconversion  -Wsign-conversion  -Wnull-dereference  -Wdouble-promotion  -Wformat=2  -Wimplicit-fallthrough -Wmisleading-indentation -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wuseless-cast -Wsuggest-override>
			$<$<CXX_COMPILER_FRONTEND_VARIANT:MSVC>:/WX /W4 /w14242 /w14254 /w14263 /w14265 /w14287 /we4289 /w14296 /w14311 /w14545 /w14546 /w14547 /w14549 /w14555 /w14619 /w14640 /w14826 /w14905  /w14906  /w14928  /permissive->
	)
else()
	target_compile_options(
		${PORT_NAME}
		INTERFACE
			$<$<OR:$<CXX_COMPILER_FRONTEND_VARIANT:Clang>,$<CXX_COMPILER_FRONTEND_VARIANT:AppleClang>>:-g -Wall -Wextra -Wshadow  -Wnon-virtual-dtor -Wold-style-cast  -Wcast-align  -Wunused  -Woverloaded-virtual  -Wpedantic  -Wconversion  -Wsign-conversion  -Wnull-dereference  -Wdouble-promotion  -Wformat=2  -Wimplicit-fallthrough  -Wno-c++17-compat -Wno-c++17-compat-pedantic -Wno-c++98-compat -Wno-c++98-compat-pedantic>
			$<$<CXX_COMPILER_FRONTEND_VARIANT:GNU>:-g -Wall -Wextra -Wshadow  -Wnon-virtual-dtor -Wold-style-cast  -Wcast-align  -Wunused  -Woverloaded-virtual  -Wpedantic  -Wconversion  -Wsign-conversion  -Wnull-dereference  -Wdouble-promotion  -Wformat=2  -Wimplicit-fallthrough -Wmisleading-indentation -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wuseless-cast -Wsuggest-override>
			$<$<CXX_COMPILER_FRONTEND_VARIANT:MSVC>:/W4 /w14242 /w14254 /w14263 /w14265 /w14287 /we4289 /w14296 /w14311 /w14545 /w14546 /w14547 /w14549 /w14555 /w14619 /w14640 /w14826 /w14905  /w14906  /w14928  /permissive->
	)
endif()
target_compile_options(
	${PORT_NAME}
	INTERFACE
		$<$<CXX_COMPILER_ID:MSVC>:/Zc:externC-> # clang-cl doesn't have this but it is needed for MSVC
)

target_compile_options(
	${PORT_NAME}
	INTERFACE
		$<$<AND:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_FRONTEND_VARIANT:MSVC>>:/EHsc> # clang-cl defaults to no exceptions and zstd_views reqauires exceptions
)


target_include_directories(
	${PORT_NAME}
	INTERFACE
		$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
		$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(${PORT_NAME} INTERFACE zstd::libzstd)

set (CONFIG_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PORT_NAME}")

# header-only library doesn't need architecture difference so clear CMAKE_SIZEOF_VOID_P temporarily
set(ORIGINAL_CMAKE_SIZEOF_VOID_P ${CMAKE_SIZEOF_VOID_P})
set(CMAKE_SIZEOF_VOID_P "")
write_basic_package_version_file(
	"${PROJECT_BINARY_DIR}/${PORT_NAME}-version.cmake"
	COMPATIBILITY SameMajorVersion
)
set(CMAKE_SIZEOF_VOID_P ${ORIGINAL_CMAKE_SIZEOF_VOID_P})
unset(ORIGINAL_CMAKE_SIZEOF_VOID_P)

# generate Config.cmake
configure_package_config_file(
	${PROJECT_SOURCE_DIR}/CMakeInstall/${PORT_NAME}-config.cmake.in
	${PROJECT_BINARY_DIR}/${PORT_NAME}-config.cmake
	INSTALL_DESTINATION ${CONFIG_DESTINATION}
)

install(
	TARGETS ${PORT_NAME}
	EXPORT ${PORT_NAME}-targets
	ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
	LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
	RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(
	EXPORT ${PORT_NAME}-targets
	NAMESPACE ${PORT_NAME}::
	DESTINATION "${CONFIG_DESTINATION}"
)

install(
	FILES
		"${PROJECT_BINARY_DIR}/${PORT_NAME}-config.cmake"
		"${PROJECT_BINARY_DIR}/${PORT_NAME}-version.cmake"
	DESTINATION "${CONFIG_DESTINATION}"
)

install(
	DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include"
	DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/.."
)