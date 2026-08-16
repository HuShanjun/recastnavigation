# Find dependencies
# list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(RecastLibs
    RecastNavigation::DebugUtils
    RecastNavigation::Detour
    RecastNavigation::DetourCrowd
    RecastNavigation::DetourTileCache
    RecastNavigation::Recast
)

set(RecastSampleSupportSources
	${CMAKE_CURRENT_SOURCE_DIR}/NavMeshSupport.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/TileCacheSupport.cpp
	${CMAKE_SOURCE_DIR}/RecastDemo/Contrib/fastlz/fastlz.c
)

set(RecastSampleIncludeDirs
	${CMAKE_CURRENT_SOURCE_DIR}/Include
	${CMAKE_SOURCE_DIR}/RecastDemo/Contrib/fastlz
)

function(recast_sample_exe name)
	cmake_parse_arguments(ARG "WITH_SUPPORT" "" "SOURCES" ${ARGN})
	add_executable(${name})
	target_sources(${name} PRIVATE ${ARG_SOURCES})
	if(ARG_WITH_SUPPORT)
		target_sources(${name} PRIVATE ${RecastSampleSupportSources})
	endif()
	target_include_directories(${name} PRIVATE ${RecastSampleIncludeDirs})
	set_target_properties(${name} PROPERTIES
		CXX_STANDARD 20
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
		RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
		RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin
		RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin
	)
	target_link_libraries(${name} PRIVATE ${RecastLibs})
endfunction()

recast_sample_exe(FindPath
	WITH_SUPPORT
	SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/FindPath.cpp
)

recast_sample_exe(DynamicObstacle
	WITH_SUPPORT
	SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/DynamicObstacle.cpp
)

recast_sample_exe(NavCli
	WITH_SUPPORT
	SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/NavCli.cpp
)

if(MSVC)
	set_source_files_properties(
		${CMAKE_SOURCE_DIR}/RecastDemo/Contrib/fastlz/fastlz.c
		PROPERTIES COMPILE_OPTIONS "/WX-"
	)
else()
	set_source_files_properties(
		${CMAKE_SOURCE_DIR}/RecastDemo/Contrib/fastlz/fastlz.c
		PROPERTIES COMPILE_OPTIONS "-w"
	)
endif()
