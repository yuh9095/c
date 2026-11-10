cmake_minimum_required(VERSION 3.16)

project(Ninit
    VERSION 0.1.0
    DESCRIPTION "An independent Linux init and service manager"
    LANGUAGES C
)

include(CTest)
include(GNUInstallDirs)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(NINIT_BUILD_TESTS "Build Ninit tests" ON)

add_library(ninit-common STATIC
    src/common/log.c
)
target_include_directories(ninit-common
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_options(ninit-common PRIVATE -Wall -Wextra -Wpedantic)

add_executable(ninit-manager src/manager/main.c)
target_link_libraries(ninit-manager PRIVATE ninit-common)
target_compile_options(ninit-manager PRIVATE -Wall -Wextra -Wpedantic)

add_executable(ninitctl src/ctl/main.c)
target_link_libraries(ninitctl PRIVATE ninit-common)
target_compile_options(ninitctl PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS ninit-manager ninitctl
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ninit
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(BUILD_TESTING AND NINIT_BUILD_TESTS)
    add_executable(test-log tests/test_log.c)
    target_link_libraries(test-log PRIVATE ninit-common)
    target_compile_options(test-log PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME log-level-names COMMAND test-log)
endif()
