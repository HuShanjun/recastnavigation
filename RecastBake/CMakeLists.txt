add_executable(RecastBake)

target_sources(RecastBake PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/BakeConfig.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/BakeSupport.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/BakeCommon.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/BakeSolo.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/BakeTile.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/BakeTempObstacles.cpp
	${CMAKE_SOURCE_DIR}/RecastDemo/Source/InputGeom.cpp
	${CMAKE_SOURCE_DIR}/RecastDemo/Source/PartitionedMesh.cpp
	${CMAKE_SOURCE_DIR}/RecastDemo/Source/PerfTimer.cpp
	${CMAKE_SOURCE_DIR}/RecastDemo/Contrib/fastlz/fastlz.c
)

target_include_directories(RecastBake PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/shim
	${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
	${CMAKE_SOURCE_DIR}/RecastDemo/Include
	${CMAKE_SOURCE_DIR}/RecastDemo/Contrib/fastlz
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

target_compile_definitions(RecastBake PRIVATE TOML_EXCEPTIONS=0)

set_target_properties(RecastBake PROPERTIES
	CXX_STANDARD 20
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF
)

target_link_libraries(RecastBake PRIVATE
	RecastNavigation::Recast
	RecastNavigation::Detour
	RecastNavigation::DetourTileCache
	RecastNavigation::DebugUtils
)
