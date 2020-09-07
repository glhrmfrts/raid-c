include_directories(${PROJECT_SOURCE_DIR}/include/msgpack-c/include/)
include_directories(${PROJECT_SOURCE_DIR}/src/)

add_executable(raid_test raid_test.c)

target_link_libraries(raid_test raid pthread)
target_compile_options(raid_test PRIVATE -g -Wall -pedantic -std=gnu99)
