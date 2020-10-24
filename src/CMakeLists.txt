include_directories(${PROJECT_SOURCE_DIR}/include/msgpack-c/include/)

set(TARGET_NAME raid)

file(GLOB SOURCE_FILES *.c ${PROJECT_SOURCE_DIR}/include/msgpack-c/src/*.c)
file(GLOB HEADER_FILES *.h)

add_library(${TARGET_NAME} ${SOURCE_FILES} ${HEADER_FILES})

option(RAID_DEBUG_MEM "Debug memory" OFF)
if (RAID_DEBUG_MEM)
  add_compile_definitions(RAID_DEBUG_MEM)
endif (RAID_DEBUG_MEM)

if (WIN32)
  include_directories(${PROJECT_SOURCE_DIR}/include/pthreads4w/)
  target_link_directories(${TARGET_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/libs/win32/)
  target_link_libraries(${TARGET_NAME} PRIVATE pthreads4w)
  target_compile_options(${TARGET_NAME} PRIVATE -g -Wall -pedantic /std:gnu99 -fPIC)
endif (WIN32)

if (UNIX)
  target_compile_options(${TARGET_NAME} PRIVATE -g -Wall -Wextra -pedantic -std=gnu99 -fPIC)
endif (UNIX)