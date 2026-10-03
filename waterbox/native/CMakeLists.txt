# The native reference: Vita3K's libraries under a headless driver. Built by
# Vita3K's own CMake with CHIMERA_HEADLESS (patch 0001), which adds this
# directory in place of the Vita3K program; CMAKE_SOURCE_DIR is extern/vita3k.

find_package(OpenGL REQUIRED COMPONENTS EGL)

add_executable(vita3k-run-native
	run-native.cpp
	headless_frame.cpp
	../driver/memfs.cpp
	${CMAKE_SOURCE_DIR}/vita3k/interface.cpp
	${CMAKE_SOURCE_DIR}/vita3k/performance.cpp
)
target_include_directories(vita3k-run-native PRIVATE ${CMAKE_SOURCE_DIR}/vita3k)
# the libraries upstream's program links, minus the GUI (which brought
# pugixml with it)
target_link_libraries(vita3k-run-native PRIVATE
	app audio compat config cppcommon ctrl dialog display ime lang gdbstub gxm io miniz
	modules motion packages patch renderer shader touch util psvpfsparser pugixml::pugixml
	SDL3::SDL3 OpenGL::EGL)

set_target_properties(vita3k-run-native PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
# the machine's filesystem defines the C library's file calls: exported, so
# the shared libraries (libstdc++'s file streams) reach them too
target_link_options(vita3k-run-native PRIVATE "-Wl,--dynamic-list=${CMAKE_CURRENT_SOURCE_DIR}/memfs.list")

# the static assets beside the program, where upstream's program has them
add_custom_command(TARGET vita3k-run-native POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data" "$<TARGET_FILE_DIR:vita3k-run-native>/data"
	COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/vita3k/shaders-builtin" "$<TARGET_FILE_DIR:vita3k-run-native>/shaders-builtin")
