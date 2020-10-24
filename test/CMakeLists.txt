include_directories(${PROJECT_SOURCE_DIR}/include/msgpack-c/include/)
include_directories(${PROJECT_SOURCE_DIR}/src/)

set(TARGET_NAME raid_test)

add_executable(${TARGET_NAME} raid_test.c)

if (WIN32)
    include_directories(${PROJECT_SOURCE_DIR}/include/pthreads4w/)
    target_link_directories(${TARGET_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/libs/win32/)
    target_link_libraries(${TARGET_NAME} PRIVATE raid pthreads4w)
    target_compile_options(${TARGET_NAME} PRIVATE -g -Wall -pedantic /std:gnu99 -fPIC)
endif (WIN32)

if (UNIX)
    target_link_libraries(${TARGET_NAME} raid pthread)
    target_compile_options(${TARGET_NAME} PRIVATE -g -Wall -pedantic -std=gnu99)
endif (UNIX)
