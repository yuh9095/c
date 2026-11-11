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

add_library(ninit-init-core STATIC
    src/init/manager_process.c
    src/init/manager_supervisor.c
    src/init/pid1_runtime.c
    src/init/pid1_events.c
    src/init/pid1_setup.c
    src/init/environment.c
    src/init/mount_manager.c
    src/init/boot_coordinator.c
)
target_link_libraries(ninit-init-core PUBLIC ninit-common)
target_include_directories(ninit-init-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(ninit-init-core PRIVATE -Wall -Wextra -Wpedantic)

add_library(ninit-manager-core STATIC
    src/manager/unit_manager.c
    src/manager/unit_file.c
    src/manager/dependency_plan.c
    src/manager/job_manager.c
    src/manager/service_executor.c
    src/manager/service_runtime.c
    src/manager/child_reaper.c
)
target_link_libraries(ninit-manager-core PUBLIC ninit-common)
target_include_directories(ninit-manager-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(ninit-manager-core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(ninit-init src/init/main.c)
target_link_libraries(ninit-init PRIVATE ninit-init-core)
target_compile_options(ninit-init PRIVATE -Wall -Wextra -Wpedantic)

add_executable(ninit-manager src/manager/main.c)
target_link_libraries(ninit-manager PRIVATE ninit-common ninit-manager-core)
target_compile_options(ninit-manager PRIVATE -Wall -Wextra -Wpedantic)

add_executable(ninitctl src/ctl/main.c)
target_link_libraries(ninitctl PRIVATE ninit-common)
target_compile_options(ninitctl PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS ninit-init ninit-manager ninitctl
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
    target_link_libraries(test-pid1-runtime PRIVATE ninit-init-core)
    target_compile_options(test-pid1-runtime PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME pid1-runtime COMMAND test-pid1-runtime)

    add_executable(test-pid1-namespace-helper tests/pid1_namespace_helper.c)
    target_compile_options(test-pid1-namespace-helper PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME pid1-namespace-manager-crash
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pid1_namespace.sh
                $<TARGET_FILE:ninit-init>
                $<TARGET_FILE:ninit-manager>
                $<TARGET_FILE:test-pid1-namespace-helper>)
    set_tests_properties(pid1-namespace-manager-crash PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT 45)

    add_executable(test-environment tests/test_environment.c)
    target_link_libraries(test-environment PRIVATE ninit-init-core)
    target_compile_options(test-environment PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME environment-detection COMMAND test-environment)

    add_executable(test-mount-manager tests/test_mount_manager.c)
    target_link_libraries(test-mount-manager PRIVATE ninit-init-core)
    target_compile_options(test-mount-manager PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME mount-manager COMMAND test-mount-manager)

    add_executable(test-pid1-setup tests/test_pid1_setup.c)
    target_link_libraries(test-pid1-setup PRIVATE ninit-init-core)
    target_compile_options(test-pid1-setup PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME pid1-setup COMMAND test-pid1-setup)

    add_executable(test-pid1-events tests/test_pid1_events.c)
    target_link_libraries(test-pid1-events PRIVATE ninit-init-core)
    target_compile_options(test-pid1-events PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME pid1-events COMMAND test-pid1-events)

    add_executable(test-manager-spawn tests/test_manager_spawn.c)
    target_link_libraries(test-manager-spawn PRIVATE ninit-init-core)
    target_compile_options(test-manager-spawn PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME manager-spawn COMMAND test-manager-spawn $<TARGET_FILE:ninit-manager>)

    add_executable(test-manager-supervisor tests/test_manager_supervisor.c)
    target_link_libraries(test-manager-supervisor PRIVATE ninit-init-core)
    target_compile_options(test-manager-supervisor PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME manager-supervisor COMMAND test-manager-supervisor $<TARGET_FILE:ninit-manager>)

    add_executable(test-boot-coordinator tests/test_boot_coordinator.c)
    target_link_libraries(test-boot-coordinator PRIVATE ninit-init-core)
    target_compile_options(test-boot-coordinator PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME boot-coordinator COMMAND test-boot-coordinator)

    add_executable(test-unit-manager tests/test_unit_manager.c)
    target_link_libraries(test-unit-manager PRIVATE ninit-manager-core)
    target_compile_options(test-unit-manager PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME unit-manager COMMAND test-unit-manager)

    add_executable(test-unit-file tests/test_unit_file.c)
    target_link_libraries(test-unit-file PRIVATE ninit-manager-core)
    target_compile_options(test-unit-file PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME unit-file COMMAND test-unit-file)

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

    add_executable(test-service-runtime tests/test_service_runtime.c)
    target_link_libraries(test-service-runtime PRIVATE ninit-manager-core)
    target_compile_options(test-service-runtime PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME service-runtime COMMAND test-service-runtime)

    add_executable(test-manager-child-reaper tests/test_manager_child_reaper.c)
    target_link_libraries(test-manager-child-reaper PRIVATE ninit-manager-core)
    target_compile_options(test-manager-child-reaper PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME manager-child-reaper COMMAND test-manager-child-reaper)
endif()
