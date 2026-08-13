add_library(RecastServerNav STATIC)

target_sources(RecastServerNav PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/Source/ServerNav.cpp
	${CMAKE_SOURCE_DIR}/RecastDemo/Contrib/fastlz/fastlz.c
)

target_include_directories(RecastServerNav PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/Include
)

target_include_directories(RecastServerNav PRIVATE
	${CMAKE_SOURCE_DIR}/RecastDemo/Contrib/fastlz
)

set_target_properties(RecastServerNav PROPERTIES
	CXX_STANDARD 20
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF
)

if(MSVC)
	target_compile_options(RecastServerNav PRIVATE /EHsc)
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
)
