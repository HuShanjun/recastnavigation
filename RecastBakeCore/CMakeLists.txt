add_library(RecastBakeCore STATIC)

target_sources(RecastBakeCore PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/Source/TileCacheCompression.cpp
	${CMAKE_SOURCE_DIR}/RecastDemo/Contrib/fastlz/fastlz.c
)

target_include_directories(RecastBakeCore PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/Include
)

target_include_directories(RecastBakeCore PRIVATE
	${CMAKE_SOURCE_DIR}/RecastDemo/Contrib/fastlz
	${CMAKE_SOURCE_DIR}/RecastDemo/Include
)

set_target_properties(RecastBakeCore PROPERTIES
	CXX_STANDARD 20
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF
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

target_link_libraries(RecastBakeCore PUBLIC
	RecastNavigation::Recast
	RecastNavigation::Detour
	RecastNavigation::DetourTileCache
)
