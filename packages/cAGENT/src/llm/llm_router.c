/* SPDX-License-Identifier: Apache-2.0 */
/**
 * 可选模型路由器。
 *
 * 选择多个 model provider 之一执行推理。不应成为 core 必需依赖。
 * 核心在无此文件时仍可正常工作（单 mock 或 OpenAI provider）。
 *
 * 可能的路由策略：
 *   - 按任务复杂度选择模型
 *   - 按成本/延迟选择
 *   - 健康检查 + failover
 *   - 成功/失败指标上报
 *
 * 边界：
 *   - 路由内部逻辑不泄漏到 agent_loop.c
 *   - 持久化配置/UI/CLI 属于应用层
 *
 * TODO: 实现 agent_model_ops_t（对外表现为单个 provider）
 * TODO: 实现后端列表管理和选择策略
 * TODO: 实现 failover 逻辑
 */
