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
    src/common/ipc_protocol.c
    src/common/ipc_channel.c
    src/common/ipc_handshake.c
)
target_include_directories(ninit-common
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_options(ninit-common PRIVATE -Wall -Wextra -Wpedantic)

add_library(ninit-init STATIC
    src/init/manager_process.c
    src/init/manager_supervisor.c
    src/init/pid1_runtime.c
    src/init/pid1_events.c
    src/init/boot_coordinator.c
)
target_link_libraries(ninit-init PUBLIC ninit-common)
target_include_directories(ninit-init PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(ninit-init PRIVATE -Wall -Wextra -Wpedantic)

add_library(ninit-manager-core STATIC
    src/manager/unit_manager.c
    src/manager/dependency_plan.c
    src/manager/job_manager.c
    src/manager/service_executor.c
)
target_link_libraries(ninit-manager-core PUBLIC ninit-common)
target_include_directories(ninit-manager-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(ninit-manager-core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(ninit-manager src/manager/main.c)
target_link_libraries(ninit-manager PRIVATE ninit-common ninit-manager-core)
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

    add_executable(test-ipc-protocol tests/test_ipc_protocol.c)
    target_link_libraries(test-ipc-protocol PRIVATE ninit-common)
    target_compile_options(test-ipc-protocol PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ipc-protocol COMMAND test-ipc-protocol)

    add_executable(test-ipc-channel tests/test_ipc_channel.c)
    target_link_libraries(test-ipc-channel PRIVATE ninit-common)
    target_compile_options(test-ipc-channel PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ipc-channel COMMAND test-ipc-channel)

    add_executable(test-pid1-runtime tests/test_pid1_runtime.c)
    target_link_libraries(test-pid1-runtime PRIVATE ninit-init)
    target_compile_options(test-pid1-runtime PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME pid1-runtime COMMAND test-pid1-runtime)

    add_executable(test-pid1-events tests/test_pid1_events.c)
    target_link_libraries(test-pid1-events PRIVATE ninit-init)
    target_compile_options(test-pid1-events PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME pid1-events COMMAND test-pid1-events)

    add_executable(test-manager-spawn tests/test_manager_spawn.c)
    target_link_libraries(test-manager-spawn PRIVATE ninit-init)
    target_compile_options(test-manager-spawn PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME manager-spawn COMMAND test-manager-spawn $<TARGET_FILE:ninit-manager>)

    add_executable(test-manager-supervisor tests/test_manager_supervisor.c)
    target_link_libraries(test-manager-supervisor PRIVATE ninit-init)
    target_compile_options(test-manager-supervisor PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME manager-supervisor COMMAND test-manager-supervisor $<TARGET_FILE:ninit-manager>)

    add_executable(test-boot-coordinator tests/test_boot_coordinator.c)
    target_link_libraries(test-boot-coordinator PRIVATE ninit-init)
    target_compile_options(test-boot-coordinator PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME boot-coordinator COMMAND test-boot-coordinator)

    add_executable(test-unit-manager tests/test_unit_manager.c)
    target_link_libraries(test-unit-manager PRIVATE ninit-manager-core)
    target_compile_options(test-unit-manager PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME unit-manager COMMAND test-unit-manager)

    add_executable(test-dependency-plan tests/test_dependency_plan.c)
    target_link_libraries(test-dependency-plan PRIVATE ninit-manager-core)
    target_compile_options(test-dependency-plan PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME dependency-plan COMMAND test-dependency-plan)

    add_executable(test-job-manager tests/test_job_manager.c)
    target_link_libraries(test-job-manager PRIVATE ninit-manager-core)
    target_compile_options(test-job-manager PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME job-manager COMMAND test-job-manager)

    add_executable(test-service-executor tests/test_service_executor.c)
    target_link_libraries(test-service-executor PRIVATE ninit-manager-core)
    target_compile_options(test-service-executor PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME service-executor COMMAND test-service-executor)
endif()
