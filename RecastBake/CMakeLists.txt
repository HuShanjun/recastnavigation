add_executable(RecastBake)

target_sources(RecastBake PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

target_include_directories(RecastBake PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
)

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
