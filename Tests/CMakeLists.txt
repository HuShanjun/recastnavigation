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
	../RecastServerNav/Source
)

target_compile_definitions(Tests PRIVATE TOML_EXCEPTIONS=0)

# RebuildQueue uses std::thread; restore exceptions for Tests (root CMake disables them).
if(MSVC)
	target_compile_options(Tests PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
	target_compile_definitions(Tests PRIVATE _HAS_EXCEPTIONS=1)
endif()

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
	../RecastServerNav/Source/RebuildQueue.cpp
)

target_link_libraries(Tests PRIVATE Recast Detour DetourCrowd)

add_test(NAME Tests COMMAND Tests)
