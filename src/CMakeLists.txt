include_directories(${PROJECT_SOURCE_DIR}/include/msgpack-c/include/)

set(TARGET_NAME raid)

file(GLOB SOURCE_FILES *.c ${PROJECT_SOURCE_DIR}/include/msgpack-c/src/*.c)
file(GLOB HEADER_FILES *.h)

add_library(${TARGET_NAME} ${SOURCE_FILES} ${HEADER_FILES})

target_compile_options(${TARGET_NAME} PRIVATE -Wall -pedantic -std=gnu99)
