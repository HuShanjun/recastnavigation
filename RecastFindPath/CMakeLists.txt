# Find dependencies
# list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

add_executable(RecastFindPath)
target_sources(RecastFindPath PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)
target_include_directories(RecastFindPath PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/Include
)

set_target_properties(RecastFindPath PROPERTIES
	CXX_STANDARD 20
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF
)

# Link libraries
target_link_libraries(RecastFindPath PRIVATE
	RecastNavigation::DebugUtils
	RecastNavigation::Detour
	RecastNavigation::DetourCrowd
	RecastNavigation::DetourTileCache
	RecastNavigation::Recast
)
