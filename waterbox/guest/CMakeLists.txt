# The guest half: Vita3K's libraries plus the boot code upstream's program
# has, as one archive set; build-core.sh links core.wbx from them. Built by
# Vita3K's own CMake with CHIMERA_HEADLESS (patch 0001), in place of the
# Vita3K program; CMAKE_SOURCE_DIR is extern/vita3k.
add_library(vita3k-guest-core STATIC
	${CMAKE_SOURCE_DIR}/vita3k/interface.cpp
	${CMAKE_SOURCE_DIR}/vita3k/performance.cpp
)
target_include_directories(vita3k-guest-core PRIVATE ${CMAKE_SOURCE_DIR}/vita3k)
target_link_libraries(vita3k-guest-core PUBLIC
	app audio compat config cppcommon ctrl dialog display ime lang gdbstub gxm io miniz
	modules motion packages patch renderer shader touch util psvpfsparser pugixml::pugixml)
