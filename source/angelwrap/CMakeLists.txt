project (angelwrap)

include_directories(${ANGELSCRIPT_INCLUDE_DIR})

file(GLOB angelwrap_HEADERS
    "*.h"
    "addon/*.h"
	"../gameshared/q_*.h"
)

file(GLOB angelwrap_SOURCES
    "*.cpp"
    "*.c"
    "addon/*.cpp"
    "../gameshared/q_*.cpp"
)

if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	set(ANGELWRAP_PLATFORM_LIBRARIES "pthread")
else ()
        set(ANGELWRAP_PLATFORM_LIBRARIES "")
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(angelwrap STATIC ${angelwrap_SOURCES} ${angelwrap_HEADERS})
target_link_libraries(angelwrap PRIVATE angelscript ${ANGELWRAP_PLATFORM_LIBRARIES})
qf_set_output_dir(angelwrap "")
