project(raid)

option(RAID_BUILD_TESTS "Build Tests" OFF)

add_subdirectory(src)

if (RAID_BUILD_TESTS)
  add_subdirectory(test)
endif (RAID_BUILD_TEST)

