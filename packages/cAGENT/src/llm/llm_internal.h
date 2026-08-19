/* SPDX-License-Identifier: Apache-2.0 */
/**
 * LLM 内部辅助类型和函数声明。
 *
 * 私有于 src/llm，核心通过 agent_model_ops_t 抽象调用模型，
 * 不直接使用此文件中的类型。
 *
 * 命名约定：
 *   model_*.c  — provider 实现（model_openai.c、model_mock.c）
 *   llm_*.c    — LLM 子系统内部工具（llm_parse.c、llm_router.c）
 *
 * P1 parser 只支持 mock/internal 文本格式和一个极小 OpenAI-compatible 子集。
 * provider 可以直接返回 agent_model_response_t；parser 主要供 mock/test 使用。
 * TODO: 声明 OpenAI request builder 辅助函数
 *   - llm_openai_build_request()：构建 Chat Completions JSON body
 */

#pragma once

#include <stddef.h>

#include <agent.h>

#ifndef CAGENT_LLM_PARSE_MAX_TOOL_CALLS
#define CAGENT_LLM_PARSE_MAX_TOOL_CALLS 4u
#endif

typedef struct {
    const char *content;
    agent_tool_call_t tool_calls[CAGENT_LLM_PARSE_MAX_TOOL_CALLS];
    size_t tool_call_count;
} agent_llm_parse_result_t;

int llm_parse_response_text(const char *text, agent_llm_parse_result_t *result);
