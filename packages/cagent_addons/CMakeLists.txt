############################################################################
# packages/cagent_addons/CMakeLists.txt
#
# cAGENT addon package: OpenClaw Node protocol and MCP bridge.
# NuttX in-tree build + standalone host build (used by host-side tests).
############################################################################

if(DEFINED NUTTX_DIR AND NOT COMMAND nuttx_add_library)
    return()
endif()

cmake_minimum_required(VERSION 3.13)

if(COMMAND nuttx_add_library)
    if(CONFIG_CAGENT_ADDONS)
        set(CADDONS_SOURCES src/common/errors.c)

        target_compile_definitions(nuttx INTERFACE
            CAGENT_ADDONS_MAX_PUBLIC_NAME_SIZE=${CONFIG_CAGENT_ADDONS_MAX_PUBLIC_NAME_SIZE}
            CAGENT_ADDONS_MAX_DESCRIPTION_SIZE=${CONFIG_CAGENT_ADDONS_MAX_DESCRIPTION_SIZE}
            CAGENT_ADDONS_MAX_SCHEMA_SIZE=${CONFIG_CAGENT_ADDONS_MAX_SCHEMA_SIZE}
            CAGENT_ADDONS_MAX_RESULT_SIZE=${CONFIG_CAGENT_ADDONS_MAX_RESULT_SIZE}
        )

        # Protocol layer: no CAGENT dependency
        if(CONFIG_CAGENT_ADDONS_NODE)
            list(APPEND CADDONS_SOURCES
                src/transport/ws_transport.c
                src/node/ws_frame.c
                src/node/node_proto.c
            )
            target_compile_definitions(nuttx INTERFACE
                CAGENT_ADDONS_MAX_NODE_COMMANDS=${CONFIG_CAGENT_ADDONS_MAX_NODE_COMMANDS}
                CAGENT_ADDONS_RECV_BUF_SIZE=${CONFIG_CAGENT_ADDONS_RECV_BUF_SIZE}
                CAGENT_ADDONS_WS_CONNECT_TIMEOUT_MS=${CONFIG_CAGENT_ADDONS_WS_CONNECT_TIMEOUT_MS}
                CAGENT_ADDONS_WS_IO_TIMEOUT_MS=${CONFIG_CAGENT_ADDONS_WS_IO_TIMEOUT_MS}
                CAGENT_ADDONS_TRANSPORT_STACK_SIZE=${CONFIG_CAGENT_ADDONS_TRANSPORT_STACK_SIZE}
            )
        endif()

        if(CONFIG_CAGENT_ADDONS_NODE_CLIENT)
            list(APPEND CADDONS_SOURCES src/node/node_client.c)
            target_compile_definitions(nuttx INTERFACE
                CAGENT_ADDONS_NODE_CLIENT_STACK_SIZE=${CONFIG_CAGENT_ADDONS_NODE_CLIENT_STACK_SIZE}
                CAGENT_ADDONS_NODE_QUEUE_SIZE=${CONFIG_CAGENT_ADDONS_NODE_QUEUE_SIZE}
            )
        endif()

        # Glue layer: requires CAGENT.  MCP and Node gateway share the
        # remote-tool catalog, but either module may be enabled on its own.
        if(CONFIG_CAGENT_ADDONS_NODE_GATEWAY OR CONFIG_CAGENT_ADDONS_MCP)
            list(APPEND CADDONS_SOURCES src/glue/remote_tool.c)
            target_compile_definitions(nuttx INTERFACE
                CAGENT_ADDONS_MAX_MCP_TOOLS=${CONFIG_CAGENT_ADDONS_MAX_MCP_TOOLS}
                CAGENT_ADDONS_MAX_REMOTE_TOOLS=${CONFIG_CAGENT_ADDONS_MAX_REMOTE_TOOLS}
            )
        endif()

        if(CONFIG_CAGENT_ADDONS_NODE_GATEWAY)
            list(APPEND CADDONS_SOURCES
                src/node/node_gateway.c
            )
            target_compile_definitions(nuttx INTERFACE
                CAGENT_ADDONS_MAX_NODES=${CONFIG_CAGENT_ADDONS_MAX_NODES}
                CAGENT_ADDONS_MAX_PENDING=${CONFIG_CAGENT_ADDONS_MAX_PENDING}
                CAGENT_ADDONS_INVOKE_TIMEOUT_MS=${CONFIG_CAGENT_ADDONS_INVOKE_TIMEOUT_MS}
                CAGENT_ADDONS_GATEWAY_STACK_SIZE=${CONFIG_CAGENT_ADDONS_GATEWAY_STACK_SIZE}
                CAGENT_ADDONS_MAX_NODE_TOOLS=${CONFIG_CAGENT_ADDONS_MAX_NODE_TOOLS}
            )
        endif()

        if(CONFIG_CAGENT_ADDONS_MCP)
            list(APPEND CADDONS_SOURCES src/mcp/mcp_bridge.c)
            target_compile_definitions(nuttx INTERFACE
                CAGENT_ADDONS_MCP_RESPONSE_BUF_SIZE=${CONFIG_CAGENT_ADDONS_MCP_RESPONSE_BUF_SIZE}
                CAGENT_ADDONS_MCP_PROTOCOL_VERSION=\"${CONFIG_CAGENT_ADDONS_MCP_PROTOCOL_VERSION}\"
            )
        endif()

        nuttx_add_library(cagent_addons STATIC)
        target_sources(cagent_addons PRIVATE ${CADDONS_SOURCES})
        target_include_directories(cagent_addons
            PUBLIC
                ${CMAKE_CURRENT_LIST_DIR}/include
        )

        set_property(
            TARGET nuttx
            APPEND
            PROPERTY NUTTX_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_LIST_DIR}/include
        )
    endif()
    return()
endif()

# ── Standalone host build (protocol layer + unit tests) ─────────────────

project(cagent_addons VERSION 0.1.0 LANGUAGES C)

option(CADDONS_NODE "Build the OpenClaw Node protocol layer" ON)
option(CADDONS_NODE_CLIENT "Build the node-role client" ON)
option(CADDONS_GATEWAY "Build the Node gateway and cAGENT glue" OFF)
option(CADDONS_MCP "Build the standard MCP client and cAGENT glue" OFF)
option(CADDONS_BUILD_TESTS "Build host-side unit tests" OFF)

set(CADDONS_MAX_NODE_COMMANDS 8 CACHE STRING "Maximum commands per node")
set(CADDONS_RECV_BUF_SIZE 4096 CACHE STRING "Single frame receive buffer size")
set(CADDONS_CJSON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../apps/netutils/cjson/cJSON"
    CACHE PATH "Directory containing cJSON.c and cJSON.h")
set(CADDONS_CAGENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../cAGENT"
    CACHE PATH "Path to the cAGENT source directory")

set(CADDONS_SOURCES src/common/errors.c)

if(CADDONS_NODE)
    list(APPEND CADDONS_SOURCES
        src/transport/ws_transport.c
        src/node/ws_frame.c
        src/node/node_proto.c
    )
endif()

if(CADDONS_NODE_CLIENT)
    if(NOT CADDONS_NODE)
        message(FATAL_ERROR "CADDONS_NODE_CLIENT requires CADDONS_NODE")
    endif()
    list(APPEND CADDONS_SOURCES src/node/node_client.c)
endif()

if(CADDONS_GATEWAY OR CADDONS_MCP)
    if(NOT EXISTS "${CADDONS_CAGENT_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "cAGENT not found; set CADDONS_CAGENT_DIR")
    endif()
    set(CAGENT_MAX_TOOLS 24 CACHE STRING "Maximum registered tools" FORCE)
    set(CAGENT_TOOL_SCHEMA_BUFFER_SIZE 8192 CACHE STRING "Tool schema buffer" FORCE)
    set(CAGENT_TOOL_OUTPUT_MAX_SIZE 4096 CACHE STRING "Tool output buffer" FORCE)
    set(CAGENT_MODEL_OPENAI OFF CACHE BOOL "OpenAI model is not needed by addon tests" FORCE)
    set(CAGENT_MODEL_OPENAI_COMPAT OFF CACHE BOOL "OpenAI alias is not needed" FORCE)
    add_subdirectory("${CADDONS_CAGENT_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/cagent")
    list(APPEND CADDONS_SOURCES src/glue/remote_tool.c)
endif()

if(CADDONS_GATEWAY)
    if(NOT CADDONS_NODE)
        message(FATAL_ERROR "CADDONS_GATEWAY requires CADDONS_NODE")
    endif()
    list(APPEND CADDONS_SOURCES src/node/node_gateway.c)
endif()

if(CADDONS_MCP)
    list(APPEND CADDONS_SOURCES src/mcp/mcp_bridge.c)
endif()

add_library(cagent_addons STATIC ${CADDONS_SOURCES})

if(CADDONS_NODE OR CADDONS_GATEWAY OR CADDONS_MCP)
    if(NOT EXISTS "${CADDONS_CJSON_DIR}/cJSON.c" OR
       NOT EXISTS "${CADDONS_CJSON_DIR}/cJSON.h")
        message(FATAL_ERROR "cJSON not found; set CADDONS_CJSON_DIR")
    endif()
    target_sources(cagent_addons PRIVATE "${CADDONS_CJSON_DIR}/cJSON.c")
    target_include_directories(cagent_addons PRIVATE "${CADDONS_CJSON_DIR}")
    target_link_libraries(cagent_addons PUBLIC m)
endif()

target_include_directories(cagent_addons
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(cagent_addons PUBLIC c_std_99)

target_compile_definitions(cagent_addons
    PUBLIC
        CAGENT_ADDONS_MAX_NODE_COMMANDS=${CADDONS_MAX_NODE_COMMANDS}
        CAGENT_ADDONS_RECV_BUF_SIZE=${CADDONS_RECV_BUF_SIZE}
)

if(CADDONS_NODE OR CADDONS_GATEWAY OR CADDONS_MCP)
    find_package(Threads REQUIRED)
    target_link_libraries(cagent_addons PUBLIC Threads::Threads)
endif()

if(CADDONS_GATEWAY OR CADDONS_MCP)
    target_link_libraries(cagent_addons PUBLIC cagent)
endif()

set_target_properties(cagent_addons PROPERTIES
    OUTPUT_NAME cagent_addons
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

if(CADDONS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
