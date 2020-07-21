include_directories(${PROJECT_SOURCE_DIR}/include/msgpack-c/include/)
include_directories(${PROJECT_SOURCE_DIR}/src/)

add_executable(hcs_test hcs_test.c)

target_link_libraries(hcs_test raid)
target_compile_options(hcs_test PRIVATE -Wall -pedantic -std=gnu99)
