if(DEFINED NUTTX_DIR AND NOT COMMAND nuttx_add_library)
    return()
endif()

cmake_minimum_required(VERSION 3.13)

if(COMMAND nuttx_add_library)
    if(CONFIG_CAGENT)
        set(CAGENT_SOURCES
            src/core/agent_core.c
            src/core/agent_event.c
            src/core/agent_loop.c
            src/core/config_load.c
            src/core/context_builder.c
            src/llm/model.c
            src/llm/llm_parse.c
            src/llm/llm_router.c
            src/memory/memory_store.c
            src/memory/session_mgr.c
            src/runtime/runtime.c
            src/skills/skill_registry.c
            src/tools/tool_guard.c
            src/tools/tool_registry.c
            src/tools/tool_schema.c
        )

        if(CONFIG_CAGENT_MODEL_MOCK)
            list(APPEND CAGENT_SOURCES src/llm/model_mock.c)
        endif()

        if(CONFIG_CAGENT_MODEL_OPENAI)
            list(APPEND CAGENT_SOURCES src/llm/model_openai.c)
        endif()

        if(CONFIG_CAGENT_RUNTIME_OPENVELA)
            list(APPEND CAGENT_SOURCES src/runtime/runtime_openvela.c)
        endif()

        nuttx_add_library(cagent STATIC)
        target_sources(cagent PRIVATE ${CAGENT_SOURCES})
        target_include_directories(cagent
            PUBLIC
                ${CMAKE_CURRENT_LIST_DIR}/include
            PRIVATE
                ${CMAKE_CURRENT_LIST_DIR}/src
        )

        if(CONFIG_CAGENT_RUNTIME_OPENVELA)
            target_compile_definitions(cagent PUBLIC CAGENT_RUNTIME_OPENVELA=1)
            if(CONFIG_ARCH_CHIP_ESP32S3)
                target_include_directories(cagent PRIVATE
                    ${NUTTX_DIR}/arch/xtensa/src/esp32s3)
            endif()
        endif()

        if(CONFIG_CAGENT_RUNTIME_OPENVELA_TLS)
            set(MBEDTLS_INC ${NUTTX_APPS_DIR}/crypto/mbedtls/mbedtls/include)
            target_include_directories(cagent PRIVATE ${MBEDTLS_INC})
            target_compile_definitions(cagent PUBLIC CAGENT_RUNTIME_OPENVELA_TLS=1)
        endif()

        set(CAGENT_CONFIG_SYSTEM_CONTEXT_BUFFER_SIZE ${CONFIG_CAGENT_SYSTEM_CONTEXT_BUFFER_SIZE})
        set(CAGENT_CONFIG_TOOL_SCHEMA_BUFFER_SIZE ${CONFIG_CAGENT_TOOL_SCHEMA_BUFFER_SIZE})
        set(CAGENT_CONFIG_MESSAGES_BUFFER_SIZE ${CONFIG_CAGENT_MESSAGES_BUFFER_SIZE})
        set(CAGENT_CONFIG_HTTP_REQUEST_BUFFER_SIZE ${CONFIG_CAGENT_HTTP_REQUEST_BUFFER_SIZE})
        set(CAGENT_CONFIG_HTTP_RESPONSE_BUFFER_SIZE ${CONFIG_CAGENT_HTTP_RESPONSE_BUFFER_SIZE})
        set(CAGENT_CONFIG_REQUEST_ARENA_EXTRA_SIZE ${CONFIG_CAGENT_REQUEST_ARENA_EXTRA_SIZE})
        if(NOT CAGENT_CONFIG_SYSTEM_CONTEXT_BUFFER_SIZE)
            set(CAGENT_CONFIG_SYSTEM_CONTEXT_BUFFER_SIZE ${CONFIG_CAGENT_CONTEXT_BUFFER_SIZE})
        endif()
        if(NOT CAGENT_CONFIG_TOOL_SCHEMA_BUFFER_SIZE)
            set(CAGENT_CONFIG_TOOL_SCHEMA_BUFFER_SIZE ${CONFIG_CAGENT_CONTEXT_BUFFER_SIZE})
        endif()
        if(NOT CAGENT_CONFIG_MESSAGES_BUFFER_SIZE)
            set(CAGENT_CONFIG_MESSAGES_BUFFER_SIZE ${CONFIG_CAGENT_CONTEXT_BUFFER_SIZE})
        endif()
        if(NOT CAGENT_CONFIG_HTTP_REQUEST_BUFFER_SIZE)
            set(CAGENT_CONFIG_HTTP_REQUEST_BUFFER_SIZE ${CONFIG_CAGENT_CONTEXT_BUFFER_SIZE})
        endif()
        if(NOT CAGENT_CONFIG_HTTP_RESPONSE_BUFFER_SIZE)
            set(CAGENT_CONFIG_HTTP_RESPONSE_BUFFER_SIZE ${CONFIG_CAGENT_CONTEXT_BUFFER_SIZE})
        endif()
        if(NOT CAGENT_CONFIG_REQUEST_ARENA_EXTRA_SIZE)
            set(CAGENT_CONFIG_REQUEST_ARENA_EXTRA_SIZE 0)
        endif()

        target_compile_definitions(cagent
            PUBLIC
                CAGENT_MAX_TOOLS=${CONFIG_CAGENT_MAX_TOOLS}
                CAGENT_MAX_SKILLS=${CONFIG_CAGENT_MAX_SKILLS}
                CAGENT_MAX_CONTEXT_PROVIDERS=${CONFIG_CAGENT_MAX_CONTEXT_PROVIDERS}
                CAGENT_MAX_SESSIONS=${CONFIG_CAGENT_MAX_SESSIONS}
                CAGENT_MAX_SESSION_MESSAGES=${CONFIG_CAGENT_MAX_SESSION_MESSAGES}
                CAGENT_CONTEXT_BUFFER_SIZE=${CONFIG_CAGENT_CONTEXT_BUFFER_SIZE}
                CAGENT_SYSTEM_CONTEXT_BUFFER_SIZE=${CAGENT_CONFIG_SYSTEM_CONTEXT_BUFFER_SIZE}
                CAGENT_TOOL_SCHEMA_BUFFER_SIZE=${CAGENT_CONFIG_TOOL_SCHEMA_BUFFER_SIZE}
                CAGENT_MESSAGES_BUFFER_SIZE=${CAGENT_CONFIG_MESSAGES_BUFFER_SIZE}
                CAGENT_HTTP_REQUEST_BUFFER_SIZE=${CAGENT_CONFIG_HTTP_REQUEST_BUFFER_SIZE}
                CAGENT_HTTP_RESPONSE_BUFFER_SIZE=${CAGENT_CONFIG_HTTP_RESPONSE_BUFFER_SIZE}
                CAGENT_REQUEST_ARENA_EXTRA_SIZE=${CAGENT_CONFIG_REQUEST_ARENA_EXTRA_SIZE}
                CAGENT_SESSION_CONTENT_MAX_SIZE=${CONFIG_CAGENT_SESSION_CONTENT_MAX_SIZE}
                CAGENT_SESSION_TOOL_CALL_MAX_SIZE=${CONFIG_CAGENT_SESSION_TOOL_CALL_MAX_SIZE}
                CAGENT_SESSION_MAX_TOOL_CALLS=${CONFIG_CAGENT_SESSION_MAX_TOOL_CALLS}
                CAGENT_TOOL_ARGS_MAX_SIZE=${CONFIG_CAGENT_TOOL_ARGS_MAX_SIZE}
                CAGENT_TOOL_OUTPUT_MAX_SIZE=${CONFIG_CAGENT_TOOL_OUTPUT_MAX_SIZE}
                CAGENT_DEFAULT_MAX_STEPS=${CONFIG_CAGENT_DEFAULT_MAX_STEPS}
                CAGENT_DEFAULT_TIMEOUT_MS=${CONFIG_CAGENT_DEFAULT_TIMEOUT_MS}
                CAGENT_DEFAULT_MODEL_TIMEOUT_MS=${CONFIG_CAGENT_DEFAULT_MODEL_TIMEOUT_MS}
                CAGENT_DEFAULT_TOOL_TIMEOUT_MS=${CONFIG_CAGENT_DEFAULT_TOOL_TIMEOUT_MS}
                CAGENT_DEFAULT_MAX_OUTPUT_TOKENS=${CONFIG_CAGENT_DEFAULT_MAX_OUTPUT_TOKENS}
        )

        set_property(
            TARGET nuttx
            APPEND
            PROPERTY NUTTX_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_LIST_DIR}/include
        )
    endif()
    return()
endif()

project(cAGENT VERSION 0.1.0 LANGUAGES C)

option(CAGENT_BUILD_SHARED "Build cagent as a shared library" OFF)
option(CAGENT_BUILD_EXAMPLES "Build cagent examples" OFF)
option(CAGENT_MODEL_MOCK "Compile the mock model provider" ON)
option(CAGENT_MODEL_OPENAI "Compile the OpenAI-compatible model provider" ON)
option(CAGENT_MODEL_OPENAI_COMPAT "Alias for CAGENT_MODEL_OPENAI" ON)
option(CAGENT_RUNTIME_OPENVELA "Compile the openvela runtime adapter stub" OFF)
option(CAGENT_RUNTIME_OPENVELA_TLS "Enable TLS support in the openvela runtime adapter" OFF)

set(CAGENT_MAX_TOOLS 12 CACHE STRING "Maximum registered tools")
set(CAGENT_MAX_SKILLS 8 CACHE STRING "Maximum registered skills")
set(CAGENT_MAX_CONTEXT_PROVIDERS 8 CACHE STRING "Maximum context providers")
set(CAGENT_MAX_SESSIONS 4 CACHE STRING "Maximum sessions")
set(CAGENT_MAX_SESSION_MESSAGES 24 CACHE STRING "Maximum messages per session")
set(CAGENT_CONTEXT_BUFFER_SIZE 4096 CACHE STRING "Default context buffer size")
set(CAGENT_SYSTEM_CONTEXT_BUFFER_SIZE ${CAGENT_CONTEXT_BUFFER_SIZE} CACHE STRING "System/context buffer size")
set(CAGENT_TOOL_SCHEMA_BUFFER_SIZE ${CAGENT_CONTEXT_BUFFER_SIZE} CACHE STRING "Tool schema buffer size")
set(CAGENT_MESSAGES_BUFFER_SIZE ${CAGENT_CONTEXT_BUFFER_SIZE} CACHE STRING "Session messages buffer size")
set(CAGENT_HTTP_REQUEST_BUFFER_SIZE ${CAGENT_CONTEXT_BUFFER_SIZE} CACHE STRING "OpenAI-compatible HTTP request buffer size")
set(CAGENT_HTTP_RESPONSE_BUFFER_SIZE ${CAGENT_CONTEXT_BUFFER_SIZE} CACHE STRING "OpenAI-compatible HTTP response buffer size")
set(CAGENT_REQUEST_ARENA_EXTRA_SIZE 0 CACHE STRING "Extra bytes added to the computed request arena")
set(CAGENT_SESSION_CONTENT_MAX_SIZE 512 CACHE STRING "Maximum bytes per stored session message")
set(CAGENT_SESSION_TOOL_CALL_MAX_SIZE 256 CACHE STRING "Maximum bytes per stored session tool call arguments")
set(CAGENT_SESSION_MAX_TOOL_CALLS 4 CACHE STRING "Maximum tool calls stored in one assistant message")
set(CAGENT_TOOL_ARGS_MAX_SIZE 1024 CACHE STRING "Maximum tool input JSON size")
set(CAGENT_TOOL_OUTPUT_MAX_SIZE 1024 CACHE STRING "Maximum tool output JSON size")
set(CAGENT_DEFAULT_MAX_STEPS 8 CACHE STRING "Default max ReAct steps")
set(CAGENT_DEFAULT_TIMEOUT_MS 30000 CACHE STRING "Default run timeout")
set(CAGENT_DEFAULT_MODEL_TIMEOUT_MS 15000 CACHE STRING "Default per-model timeout")
set(CAGENT_DEFAULT_TOOL_TIMEOUT_MS 3000 CACHE STRING "Default per-tool timeout")
set(CAGENT_DEFAULT_MAX_OUTPUT_TOKENS 512 CACHE STRING "Default model output token budget")

set(CAGENT_SOURCES
    src/core/agent_core.c
    src/core/agent_event.c
    src/core/agent_loop.c
    src/core/config_load.c
    src/core/context_builder.c
    src/llm/model.c
    src/llm/llm_parse.c
    src/llm/llm_router.c
    src/memory/memory_store.c
    src/memory/session_mgr.c
    src/runtime/runtime.c
    src/skills/skill_registry.c
    src/tools/tool_guard.c
    src/tools/tool_registry.c
    src/tools/tool_schema.c
)

if(CAGENT_MODEL_MOCK)
    list(APPEND CAGENT_SOURCES src/llm/model_mock.c)
endif()

if(CAGENT_MODEL_OPENAI OR CAGENT_MODEL_OPENAI_COMPAT)
    list(APPEND CAGENT_SOURCES src/llm/model_openai.c)
endif()

if(CAGENT_RUNTIME_OPENVELA)
    list(APPEND CAGENT_SOURCES src/runtime/runtime_openvela.c)
endif()

if(CAGENT_BUILD_SHARED)
    add_library(cagent SHARED ${CAGENT_SOURCES})
else()
    add_library(cagent STATIC ${CAGENT_SOURCES})
endif()

target_include_directories(cagent
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(cagent PUBLIC c_std_99)

target_compile_definitions(cagent
    PUBLIC
        CAGENT_MAX_TOOLS=${CAGENT_MAX_TOOLS}
        CAGENT_MAX_SKILLS=${CAGENT_MAX_SKILLS}
        CAGENT_MAX_CONTEXT_PROVIDERS=${CAGENT_MAX_CONTEXT_PROVIDERS}
        CAGENT_MAX_SESSIONS=${CAGENT_MAX_SESSIONS}
        CAGENT_MAX_SESSION_MESSAGES=${CAGENT_MAX_SESSION_MESSAGES}
        CAGENT_CONTEXT_BUFFER_SIZE=${CAGENT_CONTEXT_BUFFER_SIZE}
        CAGENT_SYSTEM_CONTEXT_BUFFER_SIZE=${CAGENT_SYSTEM_CONTEXT_BUFFER_SIZE}
        CAGENT_TOOL_SCHEMA_BUFFER_SIZE=${CAGENT_TOOL_SCHEMA_BUFFER_SIZE}
        CAGENT_MESSAGES_BUFFER_SIZE=${CAGENT_MESSAGES_BUFFER_SIZE}
        CAGENT_HTTP_REQUEST_BUFFER_SIZE=${CAGENT_HTTP_REQUEST_BUFFER_SIZE}
        CAGENT_HTTP_RESPONSE_BUFFER_SIZE=${CAGENT_HTTP_RESPONSE_BUFFER_SIZE}
        CAGENT_REQUEST_ARENA_EXTRA_SIZE=${CAGENT_REQUEST_ARENA_EXTRA_SIZE}
        CAGENT_SESSION_CONTENT_MAX_SIZE=${CAGENT_SESSION_CONTENT_MAX_SIZE}
        CAGENT_SESSION_TOOL_CALL_MAX_SIZE=${CAGENT_SESSION_TOOL_CALL_MAX_SIZE}
        CAGENT_SESSION_MAX_TOOL_CALLS=${CAGENT_SESSION_MAX_TOOL_CALLS}
        CAGENT_TOOL_ARGS_MAX_SIZE=${CAGENT_TOOL_ARGS_MAX_SIZE}
        CAGENT_TOOL_OUTPUT_MAX_SIZE=${CAGENT_TOOL_OUTPUT_MAX_SIZE}
        CAGENT_DEFAULT_MAX_STEPS=${CAGENT_DEFAULT_MAX_STEPS}
        CAGENT_DEFAULT_TIMEOUT_MS=${CAGENT_DEFAULT_TIMEOUT_MS}
        CAGENT_DEFAULT_MODEL_TIMEOUT_MS=${CAGENT_DEFAULT_MODEL_TIMEOUT_MS}
        CAGENT_DEFAULT_TOOL_TIMEOUT_MS=${CAGENT_DEFAULT_TOOL_TIMEOUT_MS}
        CAGENT_DEFAULT_MAX_OUTPUT_TOKENS=${CAGENT_DEFAULT_MAX_OUTPUT_TOKENS}
)

if(CAGENT_RUNTIME_OPENVELA)
    target_compile_definitions(cagent PUBLIC CAGENT_RUNTIME_OPENVELA=1)
endif()

if(CAGENT_RUNTIME_OPENVELA_TLS)
    target_compile_definitions(cagent PUBLIC CAGENT_RUNTIME_OPENVELA_TLS=1)
endif()

set_target_properties(cagent PROPERTIES
    OUTPUT_NAME cagent
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

if(CAGENT_BUILD_EXAMPLES)
    if(CAGENT_MODEL_MOCK)
        add_subdirectory(examples/mock_react)
        add_subdirectory(examples/cagent_demo)
    else()
        message(WARNING "CAGENT_BUILD_EXAMPLES requires CAGENT_MODEL_MOCK for mock_react")
    endif()
endif()
