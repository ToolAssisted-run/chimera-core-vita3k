# The native reference: the core's exports (driver/) over Vita3K's libraries,
# the GPU bridge's host half in the same process. Built by Vita3K's own CMake
# with CHIMERA_HEADLESS (patch 0001), which adds this directory in place of
# the Vita3K program; CMAKE_SOURCE_DIR is extern/vita3k.

find_package(OpenGL REQUIRED COMPONENTS EGL)

set(W ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(GEN ${W}/../build/generated-gl)
set(MB "$ENV{HOME}/chimera/extern/chimera-common-minibox" CACHE PATH "miniBox checkout")

# The bridge's host half and its own glad (glad 2) as ONE object whose only
# global symbols are chimera_gl_host_*: Vita3K's renderer has glad 1, and the
# two define the same glad_gl* pointers.
add_library(glhost-parts OBJECT ${W}/gl-host.c ${W}/glad/src/gl.c)
target_compile_definitions(glhost-parts PRIVATE CHIMERA_GL_BRIDGE)
target_include_directories(glhost-parts PRIVATE ${W}/glad/include ${GEN} ${MB}/source/gl ${MB}/source/host)
set(GLHOST ${CMAKE_CURRENT_BINARY_DIR}/glhost.o)
add_custom_command(OUTPUT ${GLHOST}
	COMMAND ld -r -o ${GLHOST}.tmp $<TARGET_OBJECTS:glhost-parts>
	COMMAND objcopy --wildcard --keep-global-symbol=chimera_gl_host_* ${GLHOST}.tmp ${GLHOST}
	DEPENDS glhost-parts $<TARGET_OBJECTS:glhost-parts>
	COMMAND_EXPAND_LISTS)

add_executable(vita3k-run-native
	run-native.cpp
	${CMAKE_SOURCE_DIR}/vita3k/interface.cpp
	${CMAKE_SOURCE_DIR}/vita3k/performance.cpp
	${W}/driver/vita3k_driver.cpp
	${W}/driver/bridge_frame.cpp
	${W}/driver/wbx-entry.cpp
	${W}/driver/memfs.cpp
	${W}/driver/savedata.cpp
	${W}/../build/generated-assets.cpp
	${GEN}/gl-bridge-guest.cpp
	${GLHOST}
)
set_source_files_properties(${GLHOST} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
target_include_directories(vita3k-run-native PRIVATE ${CMAKE_SOURCE_DIR}/vita3k ${W}/driver ${MB}/source/gl ${GEN})
# the generated wrappers want glad 2's types: built apart from Vita3K's glad
set_source_files_properties(${GEN}/gl-bridge-guest.cpp PROPERTIES INCLUDE_DIRECTORIES "${W}/glad/include;${GEN};${MB}/source/gl")
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
