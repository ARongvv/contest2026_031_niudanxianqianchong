# smart_home host 测试。
# 构建（在 openvela_smarthome 根或独立）：
#   cmake -S demos/smart_home/tests -B build/tests
#   cmake --build build/tests && ctest --test-dir build/tests
#
# 路径宏通过 target_compile_definitions 传入，对所有编译单元生效。

cmake_minimum_required(VERSION 3.16)
project(smart_home_tests C)

set(CMAKE_C_STANDARD 99)

# cJSON 源
set(CJSON_SRC ${CMAKE_CURRENT_LIST_DIR}/../../../../apps/netutils/cjson/cJSON/cJSON.c)
set(CJSON_INC ${CMAKE_CURRENT_LIST_DIR}/../../../../apps/netutils/cjson/cJSON)

# smart_home 源
set(SMART_HOME_SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/../src)
set(CAGENT_INC ${CMAKE_CURRENT_LIST_DIR}/../../../packages/cAGENT/include)

set(TEST_DIR /tmp/smh_cfg_test_path)

add_executable(test_config_store
    test_config_store.c
    ${SMART_HOME_SRC_DIR}/config/smart_home_config_store.c
    ${SMART_HOME_SRC_DIR}/config/smart_home_backends.c
    ${SMART_HOME_SRC_DIR}/device/smart_home_device.c
    ${CJSON_SRC}
)

target_compile_definitions(test_config_store PRIVATE
    FAR=
    SMART_HOME_CONFIG_BACKENDS_PATH="${TEST_DIR}/backends.json"
    SMART_HOME_CONFIG_SETTINGS_PATH="${TEST_DIR}/settings.json"
    SMART_HOME_CONFIG_STATE_PATH="${TEST_DIR}/state.json"
)

target_include_directories(test_config_store PRIVATE
    ${SMART_HOME_SRC_DIR}/config
    ${SMART_HOME_SRC_DIR}/agent
    ${SMART_HOME_SRC_DIR}/device
    ${SMART_HOME_SRC_DIR}/net
    ${SMART_HOME_SRC_DIR}/skills
    ${CJSON_INC}
    ${CAGENT_INC}
)

target_link_libraries(test_config_store PRIVATE m)

add_executable(test_secrets
    test_secrets.c
    ${SMART_HOME_SRC_DIR}/config/smart_home_secrets.c
    ${CJSON_SRC}
)

target_compile_definitions(test_secrets PRIVATE
    FAR=
    CONFIG_SMART_HOME_MODEL_SECRETS_PATH="${TEST_DIR}/secrets.json"
    CONFIG_SMART_HOME_MODEL_SECRETS_MAX_FILE_SIZE=4096
)

target_include_directories(test_secrets PRIVATE
    ${SMART_HOME_SRC_DIR}/config
    ${CJSON_INC}
    ${CAGENT_INC}
)

target_link_libraries(test_secrets PRIVATE m)

add_executable(test_device_service
    test_device_service.c
    ${SMART_HOME_SRC_DIR}/device/smart_home_device.c
    ${SMART_HOME_SRC_DIR}/device/smart_home_device_service.c
    ${SMART_HOME_SRC_DIR}/skills/smart_home_scene_catalog.c
    ${CJSON_SRC}
)

target_compile_definitions(test_device_service PRIVATE
    FAR=
    _POSIX_C_SOURCE=200809L
    SMART_HOME_SCENES_SKILL_PATH="${CMAKE_CURRENT_LIST_DIR}/../res/skills/smart_home_scenes.md"
)

target_include_directories(test_device_service PRIVATE
    ${SMART_HOME_SRC_DIR}/config
    ${SMART_HOME_SRC_DIR}/device
    ${SMART_HOME_SRC_DIR}/skills
    ${CJSON_INC}
    ${CAGENT_INC}
)

find_package(Threads REQUIRED)
target_link_libraries(test_device_service PRIVATE m Threads::Threads)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME config_store COMMAND test_config_store)
    add_test(NAME secrets COMMAND test_secrets)
    add_test(NAME device_service COMMAND test_device_service)
endif()
