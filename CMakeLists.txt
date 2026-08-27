cmake_minimum_required(VERSION 3.21)
cmake_policy(SET CMP0069 NEW) 
set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

macro(set_build_dir_default TARGET_NAME_)
	set_target_properties(${TARGET_NAME_}
		PROPERTIES
		ARCHIVE_OUTPUT_DIRECTORY "Binary"
		LIBRARY_OUTPUT_DIRECTORY "Library"
		RUNTIME_OUTPUT_DIRECTORY "Binary"
	)
endmacro()

project(MusicBars LANGUAGES C)

add_subdirectory("${MusicBars_SOURCE_DIR}/Library/miniaudio")
set(SDL_STATIC ON)
set(SDL_SHARED OFF)
set_target_properties(miniaudio PROPERTIES
	INTERPROCEDURAL_OPTIMIZATION TRUE
)
add_subdirectory("${MusicBars_SOURCE_DIR}/Library/SDL")
set_target_properties(SDL3-static PROPERTIES
	INTERPROCEDURAL_OPTIMIZATION TRUE
)

set(MusicBars_HEADER_FILES
	"${MusicBars_SOURCE_DIR}/Source/Atomic.h"
	"${MusicBars_SOURCE_DIR}/Source/Common.h"
	"${MusicBars_SOURCE_DIR}/Source/Machine.h"
)
set(MusicBars_SOURCE_FILES
	"${MusicBars_SOURCE_DIR}/Source/MatrixMult/Cpu.c"
	"${MusicBars_SOURCE_DIR}/Source/Main.c"
	"${MusicBars_SOURCE_DIR}/Source/Render/Cpu.c"
)
set(MusicBars_INCLUDE_DIRECTORIES
	"${MusicBars_SOURCE_DIR}/Source"
)
if (MINGW)
	list(APPEND MusicBars_HEADER_FILES
		"${MusicBars_SOURCE_DIR}/Source/CRT/threads.h"
	)
	list(APPEND MusicBars_SOURCE_FILES
		"${MusicBars_SOURCE_DIR}/Source/CRT/c11threads_win32.c"
	)
	list(APPEND MusicBars_INCLUDE_DIRECTORIES
		"${MusicBars_SOURCE_DIR}/Source/CRT"
	)
endif()

add_executable(MusicBars
	${MusicBars_HEADER_FILES}
	${MusicBars_SOURCE_FILES}
)
target_include_directories(MusicBars PRIVATE
	${MusicBars_INCLUDE_DIRECTORIES}
)
target_link_libraries(MusicBars
	PRIVATE miniaudio
	PRIVATE SDL3-static
)
set_target_properties(MusicBars PROPERTIES
	INTERPROCEDURAL_OPTIMIZATION TRUE
	C_STANDARD 11
)

if (MSVC)
    target_compile_options(MusicBars PRIVATE "/experimental:c11atomics")
endif()

set_build_dir_default(MusicBars)
