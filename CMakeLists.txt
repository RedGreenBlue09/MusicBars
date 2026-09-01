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

set(MUSICBARS_SSE_COMPILE_FLAG "")
set(MUSICBARS_AVX_COMPILE_FLAG "")
set(MUSICBARS_FMA3_COMPILE_FLAG "")
set(MUSICBARS_NEON_COMPILE_FLAG "")
set(MUSICBARS_NEON_FMA_COMPILE_FLAG "")
if (MSVC)
	set(MUSICBARS_AVX_COMPILE_FLAG "/arch:AVX")
	set(MUSICBARS_FMA3_COMPILE_FLAG "/arch:AVX2")
	set(MUSICBARS_NEON_FMA_COMPILE_FLAG "/arch:VFPv4")
elseif (CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
	set(MUSICBARS_SSE_COMPILE_FLAG "-msse")
	set(MUSICBARS_AVX_COMPILE_FLAG "-mavx")
	set(MUSICBARS_FMA3_COMPILE_FLAG "-mfma")
	set(MUSICBARS_NEON_COMPILE_FLAG "-mfpu=neon")
	set(MUSICBARS_NEON_FMA_COMPILE_FLAG "-mfpu=vfpv4")
endif()

option(MUSICBARS_ENABLE_SSE "Enable SSE vectorization" OFF)
option(MUSICBARS_ENABLE_AVX "Enable AVX vectorization" OFF)
option(MUSICBARS_ENABLE_FMA3 "Enable AVX FMA3 vectorization" OFF)
option(MUSICBARS_ENABLE_NEON "Enable NEON vectorization" OFF)
option(MUSICBARS_ENABLE_NEON_FMA "Enable NEON FMA vectorization" OFF)
if (MUSICBARS_ENABLE_FMA3)
	set(MUSICBARS_ENABLE_AVX ON)
	set(MUSICBARS_ENABLE_SSE ON)
endif()
if (MUSICBARS_ENABLE_AVX)
	set(MUSICBARS_ENABLE_SSE ON)
endif()
if (MUSICBARS_ENABLE_NEON_FMA)
	set(MUSICBARS_ENABLE_NEON ON)
endif()
message(MUSICBARS_ENABLE_SSE=${MUSICBARS_ENABLE_SSE})
message(MUSICBARS_ENABLE_AVX=${MUSICBARS_ENABLE_AVX})
message(MUSICBARS_ENABLE_FMA3=${MUSICBARS_ENABLE_FMA3})
message(MUSICBARS_ENABLE_NEON=${MUSICBARS_ENABLE_NEON})
message(MUSICBARS_ENABLE_NEON_FMA=${MUSICBARS_ENABLE_NEON_FMA})

if (NOT MUSICBARS_ENABLE_SSE)
	set(MUSICBARS_SSE_COMPILE_FLAG "")
endif()
if (NOT MUSICBARS_ENABLE_AVX)
	set(MUSICBARS_AVX_COMPILE_FLAG "")
endif()
if (NOT MUSICBARS_ENABLE_FMA3)
	set(MUSICBARS_FMA3_COMPILE_FLAG "")
endif()
if (NOT MUSICBARS_ENABLE_NEON)
	set(MUSICBARS_NEON_COMPILE_FLAG "")
endif()
if (NOT MUSICBARS_ENABLE_NEON_FMA)
	set(MUSICBARS_NEON_FMA_COMPILE_FLAG "")
endif()

option(MUSICBARS_ENABLE_MODERN_SHADERS "Build with modern shaders support" ON)

add_subdirectory("${MusicBars_SOURCE_DIR}/Library/miniaudio")
set_target_properties(miniaudio PROPERTIES
	INTERPROCEDURAL_OPTIMIZATION TRUE
)

set(SDL_STATIC ON)
set(SDL_SHARED OFF)
add_subdirectory("${MusicBars_SOURCE_DIR}/Library/SDL")
set_target_properties(SDL3-static PROPERTIES
	INTERPROCEDURAL_OPTIMIZATION TRUE
)

if (MUSICBARS_ENABLE_MODERN_SHADERS)
	set(SDLSHADERCROSS_STATIC ON)
	set(SDLSHADERCROSS_SHARED OFF)
	set(SDLSHADERCROSS_VENDORED ON)
	add_subdirectory("${MusicBars_SOURCE_DIR}/Library/SDL_shadercross")
	set_target_properties(SDL3_shadercross-static PROPERTIES
		INTERPROCEDURAL_OPTIMIZATION TRUE
	)
endif()

set(MusicBars_HEADER_FILES
	"${MusicBars_SOURCE_DIR}/Source/MatrixMult/CpuCommon.h"
	"${MusicBars_SOURCE_DIR}/Source/Utilty/Arena.h"
	"${MusicBars_SOURCE_DIR}/Source/Utilty/Atomic.h"
	"${MusicBars_SOURCE_DIR}/Source/Utilty/Common.h"
	"${MusicBars_SOURCE_DIR}/Source/Utilty/IntMath.h"
	"${MusicBars_SOURCE_DIR}/Source/Utilty/Machine.h"
	"${MusicBars_SOURCE_DIR}/Source/Utilty/Time.h"
)
set(MusicBars_SOURCE_FILES
	"${MusicBars_SOURCE_DIR}/Source/Main.c"
	"${MusicBars_SOURCE_DIR}/Source/MatrixMult/Cpu.c"
	"${MusicBars_SOURCE_DIR}/Source/MatrixMult/CpuAvx.c"
	"${MusicBars_SOURCE_DIR}/Source/MatrixMult/CpuDetection.c"
	"${MusicBars_SOURCE_DIR}/Source/MatrixMult/CpuFma3.c"
	"${MusicBars_SOURCE_DIR}/Source/MatrixMult/CpuSse.c"
	"${MusicBars_SOURCE_DIR}/Source/MatrixMult/CpuNeon.c"
	"${MusicBars_SOURCE_DIR}/Source/MatrixMult/CpuNeonFma.c"
	"${MusicBars_SOURCE_DIR}/Source/Render/Legacy.c"
	"${MusicBars_SOURCE_DIR}/Source/Render/Modern.c"
	"${MusicBars_SOURCE_DIR}/Source/Utilty/IntMath.c"
	"${MusicBars_SOURCE_DIR}/Source/Utilty/Time.c"
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

set(MUSICBARS_CPU_DETECTION_COMPILE_FLAG "")
if (MUSICBARS_ENABLE_FMA3)
	set(MUSICBARS_CPU_DETECTION_COMPILE_FLAG "${MUSICBARS_FMA3_COMPILE_FLAG}")
elseif (MUSICBARS_ENABLE_AVX)
	set(MUSICBARS_CPU_DETECTION_COMPILE_FLAG "${MUSICBARS_AVX_COMPILE_FLAG}")
elseif (MUSICBARS_ENABLE_SSE)
	set(MUSICBARS_CPU_DETECTION_COMPILE_FLAG "${MUSICBARS_SSE_COMPILE_FLAG}")
endif()
if (MUSICBARS_ENABLE_NEON_FMA)
	set(MUSICBARS_CPU_DETECTION_COMPILE_FLAG "${MUSICBARS_NEON_FMA_COMPILE_FLAG}")
elseif (MUSICBARS_ENABLE_NEON)
	set(MUSICBARS_CPU_DETECTION_COMPILE_FLAG "${MUSICBARS_NEON_COMPILE_FLAG}")
endif()

# Set per-file compiler flags for ISA-specific implementations
set_source_files_properties("${MusicBars_SOURCE_DIR}/Source/MatrixMult/CpuDetection.c"
	PROPERTIES COMPILE_OPTIONS "${MUSICBARS_CPU_DETECTION_COMPILE_FLAG}"
)
set_source_files_properties("${MusicBars_SOURCE_DIR}/Source/MatrixMult/CpuSse.c"
	PROPERTIES COMPILE_OPTIONS "${MUSICBARS_SSE_COMPILE_FLAG}"
)
set_source_files_properties("${MusicBars_SOURCE_DIR}/Source/MatrixMult/CpuAvx.c"
	PROPERTIES COMPILE_OPTIONS "${MUSICBARS_AVX_COMPILE_FLAG}"
)
set_source_files_properties("${MusicBars_SOURCE_DIR}/Source/MatrixMult/CpuFma3.c"
	PROPERTIES COMPILE_OPTIONS "${MUSICBARS_FMA3_COMPILE_FLAG}"
)
set_source_files_properties("${MusicBars_SOURCE_DIR}/Source/MatrixMult/CpuNeon.c"
	PROPERTIES COMPILE_OPTIONS "${MUSICBARS_NEON_COMPILE_FLAG}"
)
set_source_files_properties("${MusicBars_SOURCE_DIR}/Source/MatrixMult/CpuNeonFma.c"
	PROPERTIES COMPILE_OPTIONS "${MUSICBARS_NEON_FMA_COMPILE_FLAG}"
)

if (MUSICBARS_ENABLE_MODERN_SHADERS)
	set_source_files_properties("${MusicBars_SOURCE_DIR}/Source/Render/Modern.c"
		PROPERTIES COMPILE_DEFINITIONS "MUSICBARS_ENABLE_MODERN_SHADERS=1"
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
if (MUSICBARS_ENABLE_MODERN_SHADERS)
	target_link_libraries(MusicBars
		PRIVATE SDL3_shadercross-static
	)
endif()
if (WIN32)
	target_link_libraries(MusicBars
		PRIVATE ntdll
	)
endif()
set_target_properties(MusicBars PROPERTIES
	INTERPROCEDURAL_OPTIMIZATION TRUE
	C_STANDARD 11
)

if (MSVC)
    target_compile_options(MusicBars PRIVATE "/experimental:c11atomics")
endif()
if (MINGW)
	target_link_options(MusicBars PRIVATE -Wl,--pdb=)
endif()

set_build_dir_default(MusicBars)
