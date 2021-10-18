cmake_minimum_required(VERSION 3.18...3.21 FATAL_ERROR)

include(/opt/devkitpro/cmake/devkitPPC.cmake)
if(NOT devkitPPC)
	message(FATAL_ERROR "Missing toolchain file! Use '-DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/devkitPPC.cmake' option.")
endif()
# environment
set(MACHDEP "-DGEKKO -mrvl -mcpu=750 -meabi -mhard-float")
set(CMAKE_C_FLAGS_INIT "${MACHDEP}")
set(CMAKE_ASM_FLAGS_INIT "${MACHDEP}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${MACHDEP}")
include_directories(SYSTEM /opt/devkitpro/libogc/include)
link_directories(BEFORE /opt/devkitpro/libogc/lib/wii)

project(EasyRPG-Player_Wii-Forwarder VERSION 0.1.0 LANGUAGES C ASM)

# generate gfx assets
find_program(GXTEXCONV NAMES gxtexconv HINTS "${DEVKITPRO}/tools/bin" REQUIRED)
foreach(SCF assets/images.scf)
	get_filename_component(basename "${SCF}" NAME)
	string(REPLACE "." "_" cleanname "${basename}")
	add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/assets/${cleanname}.h
		                      ${CMAKE_CURRENT_BINARY_DIR}/assets/${cleanname}.tpl
		COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/assets
		COMMAND ${GXTEXCONV} -s ${SCF} -o ${CMAKE_CURRENT_BINARY_DIR}/assets/${cleanname}.tpl
		DEPENDS ${SCF}
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		COMMENT "Converting gfx assets ${basename} → ${cleanname}.tpl"
	)
	list(APPEND FORWARDER_IMMEDIATES ${CMAKE_CURRENT_BINARY_DIR}/assets/${cleanname}.tpl)
	list(APPEND FORWARDER_ASSETS ${CMAKE_CURRENT_BINARY_DIR}/assets/${cleanname}.h)
endforeach()

# embedd tpl and other data
find_program(BIN2S NAMES bin2s HINTS "${DEVKITPRO}/tools/bin" REQUIRED)
foreach(immediate ${FORWARDER_IMMEDIATES})
	get_filename_component(basename "${immediate}" NAME)
	string(REPLACE "." "_" cleanname "${basename}")
	add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/assets/${cleanname}.h
		                      ${CMAKE_CURRENT_BINARY_DIR}/assets/${cleanname}.s
		COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/assets
		COMMAND ${BIN2S} -a 32 -H ${CMAKE_CURRENT_BINARY_DIR}/assets/${cleanname}.h ${immediate} > ${CMAKE_CURRENT_BINARY_DIR}/assets/${cleanname}.s
		DEPENDS ${immediate}
		COMMENT "Embedding binary ${basename}"
	)
	list(APPEND FORWARDER_ASSETS ${CMAKE_CURRENT_BINARY_DIR}/assets/${cleanname}.s ${CMAKE_CURRENT_BINARY_DIR}/assets/${cleanname}.h)
endforeach()

# main sources
add_executable(forwarder
	src/dolloader.h
	src/dolloader.c
	src/elf_abi.h
	src/elfloader.h
	src/elfloader.c
	src/video.h
	src/video.c
	src/usbgecko.c
	src/usbgecko.h
	src/main.c
	${FORWARDER_ASSETS})

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	target_compile_options(forwarder PRIVATE -DDEBUG -Wall -Wextra)
	set(CMAKE_VERBOSE_MAKEFILE TRUE)
endif()

target_include_directories(forwarder PUBLIC
	src
	${CMAKE_CURRENT_BINARY_DIR}/assets)

target_link_libraries(forwarder
	-lfat
	-logc
	-lm
	# generate map file
	-Wl,-Map,forwarder.map
	# move loader to end of mem1 to make space for loaded executable. See
	# https://wiibrew.org/wiki/Memory_map (1mb space to not overlap with hbc)
	-Wl,--section-start,.init=0x81230000)

find_program(ELF2DOL elf2dol HINTS "${DEVKITPRO}/tools/bin" REQUIRED)
add_custom_command(TARGET forwarder POST_BUILD
	BYPRODUCTS boot.dol
	COMMAND ${ELF2DOL} forwarder boot.dol
	COMMENT "Prepare for distribution")
