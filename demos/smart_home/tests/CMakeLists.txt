# smart_home host 测试。
# 构建（在 openvela_smarthome 根或独立）：
#   cmake -S demos/smart_home/tests -B build/tests
#   cmake --build build/tests && ctest --test-dir build/tests
#
# 路径宏通过 target_compile_definitions 传入，对所有编译单元生效。

cmake_minimum_required(VERSION 3.16)
project(smart_home_tests C)

set(CMAKE_C_STANDARD 99)

# smart_home 源
set(SMART_HOME_SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/../src)
get_filename_component(SMART_HOME_PROJECT_ROOT
    ${CMAKE_CURRENT_LIST_DIR}/../../.. ABSOLUTE)

# 比赛项目既可能直接位于 openvela 根目录，也可能位于 linked worktree。
# 在两种布局中向上寻找同时包含 apps/ 和 packages/ 的 openvela 根目录。
set(OPENVELA_ROOT "")
foreach(candidate
    ${SMART_HOME_PROJECT_ROOT}/..
    ${SMART_HOME_PROJECT_ROOT}/../..)
  if(EXISTS ${candidate}/apps/netutils/cjson/cJSON/cJSON.c
     AND EXISTS ${candidate}/packages/cAGENT/include)
    get_filename_component(OPENVELA_ROOT ${candidate} ABSOLUTE)
    break()
  endif()
endforeach()

if(NOT OPENVELA_ROOT)
  message(FATAL_ERROR "Unable to locate the openvela root containing apps/ and packages/")
endif()

# cJSON 源
set(CJSON_SRC ${OPENVELA_ROOT}/apps/netutils/cjson/cJSON/cJSON.c)
set(CJSON_INC ${OPENVELA_ROOT}/apps/netutils/cjson/cJSON)
set(CAGENT_INC ${OPENVELA_ROOT}/packages/cAGENT/include)
set(SMART_HOME_FEATURE_MODULES_DIR "" CACHE PATH
    "Optional system.smarthome Feature modules directory")
if(NOT SMART_HOME_FEATURE_MODULES_DIR)
  set(SMART_HOME_FEATURE_MODULES_DIR
      ${OPENVELA_ROOT}/frameworks/runtimes/feature/modules)
endif()

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

add_executable(test_quickapp_provider
    test_quickapp_provider.c
    ${SMART_HOME_SRC_DIR}/quickapp/smart_home_quickapp_provider.c
    ${SMART_HOME_SRC_DIR}/device/smart_home_device.c
    ${SMART_HOME_SRC_DIR}/device/smart_home_device_service.c
    ${SMART_HOME_SRC_DIR}/skills/smart_home_scene_catalog.c
    ${CJSON_SRC}
)

target_compile_definitions(test_quickapp_provider PRIVATE
    FAR=
    _POSIX_C_SOURCE=200809L
)

target_include_directories(test_quickapp_provider PRIVATE
    ${SMART_HOME_SRC_DIR}/quickapp
    ${SMART_HOME_SRC_DIR}/config
    ${SMART_HOME_SRC_DIR}/device
    ${SMART_HOME_SRC_DIR}/skills
    ${CJSON_INC}
    ${CAGENT_INC}
)

target_link_libraries(test_quickapp_provider PRIVATE m Threads::Threads)

if(EXISTS ${SMART_HOME_FEATURE_MODULES_DIR}/smart_home_quickapp_bridge.h)
  add_executable(test_quickapp_feature_bridge
      test_quickapp_feature_bridge.c
      ${SMART_HOME_SRC_DIR}/quickapp/smart_home_quickapp_feature_bridge.c
      ${SMART_HOME_SRC_DIR}/quickapp/smart_home_quickapp_provider.c
      ${SMART_HOME_SRC_DIR}/device/smart_home_device.c
      ${SMART_HOME_SRC_DIR}/device/smart_home_device_service.c
      ${SMART_HOME_SRC_DIR}/skills/smart_home_scene_catalog.c
      ${CJSON_SRC}
  )

  target_compile_definitions(test_quickapp_feature_bridge PRIVATE
      FAR=
      _POSIX_C_SOURCE=200809L
  )

  target_include_directories(test_quickapp_feature_bridge PRIVATE
      ${SMART_HOME_SRC_DIR}/quickapp
      ${SMART_HOME_SRC_DIR}/config
      ${SMART_HOME_SRC_DIR}/device
      ${SMART_HOME_SRC_DIR}/skills
      ${SMART_HOME_FEATURE_MODULES_DIR}
      ${CJSON_INC}
      ${CAGENT_INC}
  )

  target_link_libraries(test_quickapp_feature_bridge PRIVATE m Threads::Threads)
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME config_store COMMAND test_config_store)
    add_test(NAME secrets COMMAND test_secrets)
  add_test(NAME device_service COMMAND test_device_service)
  add_test(NAME quickapp_provider COMMAND test_quickapp_provider)
  if(TARGET test_quickapp_feature_bridge)
    add_test(NAME quickapp_feature_bridge COMMAND test_quickapp_feature_bridge)
  endif()
endif()
