if(MSVC)
	# Root CMake disables exceptions globally (/EHs-c-, _HAS_EXCEPTIONS=0).
	# Restore /EHsc and _HAS_EXCEPTIONS for this directory (std::thread, STL).
	string(REPLACE "/EHs-c-" "/EHsc" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
	add_compile_definitions(_HAS_EXCEPTIONS=1)
endif()

add_library(RecastServerNav STATIC)

target_sources(RecastServerNav PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/Source/ServerNav.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/Source/TileCacheSupport.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/Source/TileRebuilder.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/Source/RebuildQueue.cpp
	${CMAKE_SOURCE_DIR}/RecastBake/BakeSupport.cpp
	${CMAKE_SOURCE_DIR}/RecastDemo/Source/InputGeom.cpp
	${CMAKE_SOURCE_DIR}/RecastDemo/Source/PartitionedMesh.cpp
	${CMAKE_SOURCE_DIR}/RecastDemo/Source/PerfTimer.cpp
	${CMAKE_SOURCE_DIR}/RecastDemo/Contrib/fastlz/fastlz.c
)

target_include_directories(RecastServerNav PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/Include
	${CMAKE_CURRENT_SOURCE_DIR}/Source
)

target_include_directories(RecastServerNav PRIVATE
	${CMAKE_SOURCE_DIR}/RecastBake/shim
	${CMAKE_SOURCE_DIR}/RecastDemo/Include
	${CMAKE_SOURCE_DIR}/RecastDemo/Contrib/fastlz
)

set_target_properties(RecastServerNav PROPERTIES
	CXX_STANDARD 20
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF
)

if(MSVC)
	target_compile_options(RecastServerNav PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
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

target_link_libraries(RecastServerNav PUBLIC
	RecastNavigation::Detour
	RecastNavigation::DetourTileCache
	RecastNavigation::Recast
	RecastNavigation::DebugUtils
)

add_executable(RecastServerNavSmoke EXCLUDE_FROM_ALL
	${CMAKE_CURRENT_SOURCE_DIR}/smoke_load_main.cpp
)

set_target_properties(RecastServerNavSmoke PROPERTIES
	CXX_STANDARD 20
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF
)

if(MSVC)
	target_compile_options(RecastServerNavSmoke PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
endif()

target_link_libraries(RecastServerNavSmoke PRIVATE RecastServerNav)

add_executable(RecastServerNavRebuildSmoke EXCLUDE_FROM_ALL
	${CMAKE_CURRENT_SOURCE_DIR}/smoke_rebuild_main.cpp
)

set_target_properties(RecastServerNavRebuildSmoke PROPERTIES
	CXX_STANDARD 20
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF
)

target_include_directories(RecastServerNavRebuildSmoke PRIVATE
	${CMAKE_SOURCE_DIR}/RecastBake/shim
	${CMAKE_SOURCE_DIR}/RecastDemo/Include
)

if(MSVC)
	target_compile_options(RecastServerNavRebuildSmoke PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
endif()

target_link_libraries(RecastServerNavRebuildSmoke PRIVATE RecastServerNav)

add_executable(RecastServerNavPermanentSmoke EXCLUDE_FROM_ALL
	${CMAKE_CURRENT_SOURCE_DIR}/smoke_permanent_main.cpp
)

set_target_properties(RecastServerNavPermanentSmoke PROPERTIES
	CXX_STANDARD 20
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF
)

if(MSVC)
	target_compile_options(RecastServerNavPermanentSmoke PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
endif()

target_link_libraries(RecastServerNavPermanentSmoke PRIVATE RecastServerNav)
