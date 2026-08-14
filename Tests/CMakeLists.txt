if(MSVC)
	# Root CMake disables exceptions globally (/EHs-c-, _HAS_EXCEPTIONS=0).
	# RebuildQueue needs std::thread; restore like RecastServerNav.
	string(REPLACE "/EHs-c-" "/EHsc" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
	add_compile_definitions(_HAS_EXCEPTIONS=1)
endif()

add_executable(Tests)

set_target_properties(Tests PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF  # Disable compiler-specific extensions
)

# Remove warnings-as-errors
if(MSVC)
	target_compile_options(Tests PRIVATE /W3 /WX-)
else()
	target_compile_options(Tests PRIVATE -Wno-error)
endif()

target_include_directories(Tests PRIVATE
	./Contrib
	../RecastDemo/Include
	../RecastBake
	../RecastBake/thirdparty
	../RecastBakeCore/Include
	../RecastServerNav/Source
	../RecastServerNav/Include
)

target_compile_definitions(Tests PRIVATE TOML_EXCEPTIONS=0)

target_sources(Tests PRIVATE 
	Contrib/catch2/catch_amalgamated.cpp
	Detour/Tests_Detour.cpp
	DetourCrowd/Tests_DetourPathCorridor.cpp
	Recast/Tests_Alloc.cpp
	Recast/Tests_Recast.cpp
	Recast/Tests_RecastFilter.cpp
	Recast/Tests_RecastRasterization.cpp
	RecastDemo/Tests_PartitionedMesh.cpp
	../RecastDemo/Source/PartitionedMesh.cpp
	RecastBake/Tests_BakeConfig.cpp
	../RecastBake/BakeConfig.cpp
	RecastServerNav/Tests_RebuildQueue.cpp
	RecastServerNav/Tests_PermanentBoxes.cpp
	RecastBakeCore/Tests_TileRasterizer.cpp
	RecastBakeCore/Tests_TileCacheSetIO.cpp
)

target_link_libraries(Tests PRIVATE Recast Detour DetourCrowd RecastServerNav RecastBakeCore)

add_test(NAME Tests COMMAND Tests)
