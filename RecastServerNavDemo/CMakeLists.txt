if(MSVC)
	# Match RecastServerNav: restore exceptions for STL / linking consistency.
	string(REPLACE "/EHs-c-" "/EHsc" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
	add_compile_definitions(_HAS_EXCEPTIONS=1)
endif()

add_executable(RecastServerNavDemo)
target_sources(RecastServerNavDemo PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

set_target_properties(RecastServerNavDemo PROPERTIES
	CXX_STANDARD 20
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF
)

if(MSVC)
	target_compile_options(RecastServerNavDemo PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
endif()

target_link_libraries(RecastServerNavDemo PRIVATE RecastServerNav)
