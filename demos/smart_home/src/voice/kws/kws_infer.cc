/****************************************************************************
 * demos/smart_home/src/voice/kws/kws_infer.cc
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2026 The NuttX Contributors
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "kws_infer.h"

#include <errno.h>
#include <stdint.h>
#include <new>

#include "model.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static tflite::MicroInterpreter *g_interpreter = nullptr;
static TfLiteTensor *g_input = nullptr;
static TfLiteTensor *g_output = nullptr;
static size_t g_arena_used = 0;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int tensor_elements(const TfLiteTensor *tensor)
{
  int count = 1;
  int index;

  if (tensor == nullptr || tensor->dims == nullptr)
    {
      return 0;
    }

  for (index = 0; index < tensor->dims->size; index++)
    {
      count *= tensor->dims->data[index];
    }

  return count;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

extern "C" int kws_infer_init(void *arena, size_t arena_size)
{
  tflite::MicroMutableOpResolver<9> resolver;
  const tflite::Model *model;
  uintptr_t base;
  uintptr_t aligned;
  size_t usable;

  if (arena == nullptr || arena_size < 32)
    {
      return -EINVAL;
    }

  if (g_interpreter != nullptr)
    {
      return 0;  /* 幂等 */
    }

  /* 内部 16 字节对齐：bulk 堆不保证对齐，MicroInterpreter 亦不会
   * 自己补齐，未对齐 arena 会静默损失容量或崩溃。 */
  base = (uintptr_t)arena;
  aligned = (base + 15u) & ~(uintptr_t)15u;
  usable = arena_size - (size_t)(aligned - base);

  model = tflite::GetModel(g_s3_model);
  if (model == nullptr || model->version() != TFLITE_SCHEMA_VERSION)
    {
      return -EINVAL;
    }

  if (resolver.AddShape() != kTfLiteOk ||
      resolver.AddStridedSlice() != kTfLiteOk ||
      resolver.AddPack() != kTfLiteOk ||
      resolver.AddReshape() != kTfLiteOk ||
      resolver.AddConv2D() != kTfLiteOk ||
      resolver.AddDepthwiseConv2D() != kTfLiteOk ||
      resolver.AddMean() != kTfLiteOk ||
      resolver.AddFullyConnected() != kTfLiteOk ||
      resolver.AddSoftmax() != kTfLiteOk)
    {
      return -EINVAL;
    }

  /* placement new 到静态存储：避免函数局部动态初始化的 guard 依赖。 */
  static alignas(8) uint8_t
      interpreter_storage[sizeof(tflite::MicroInterpreter)];
  g_interpreter = new (interpreter_storage)
      tflite::MicroInterpreter(model, resolver, (void *)aligned, usable);

  if (g_interpreter->AllocateTensors() != kTfLiteOk)
    {
      g_interpreter = nullptr;
      return -ENOMEM;
    }

  g_arena_used = g_interpreter->arena_used_bytes();

  g_input = g_interpreter->input(0);
  g_output = g_interpreter->output(0);
  if (g_input == nullptr || g_output == nullptr ||
      g_input->type != kTfLiteInt8 || g_output->type != kTfLiteInt8 ||
      tensor_elements(g_input) != KWS_INFER_FEATURE_SIZE ||
      tensor_elements(g_output) != KWS_INFER_CLASS_COUNT)
    {
      g_interpreter = nullptr;
      return -EINVAL;
    }

  return 0;
}

extern "C" size_t kws_infer_arena_used(void)
{
  return g_arena_used;
}

extern "C" void kws_infer_input_params(float *scale, int *zero_point)
{
  if (scale != nullptr)
    {
      *scale = g_input != nullptr ? g_input->params.scale : 1.0f;
    }

  if (zero_point != nullptr)
    {
      *zero_point = g_input != nullptr ? g_input->params.zero_point : 0;
    }
}

extern "C" int kws_infer_run(const int8_t *features, float *scores)
{
  int index;

  if (g_interpreter == nullptr || features == nullptr || scores == nullptr)
    {
      return -EINVAL;
    }

  memcpy(g_input->data.int8, features, KWS_INFER_FEATURE_SIZE);
  if (g_interpreter->Invoke() != kTfLiteOk)
    {
      return -EIO;
    }

  for (index = 0; index < KWS_INFER_CLASS_COUNT; index++)
    {
      scores[index] = (g_output->data.int8[index] -
                       g_output->params.zero_point) *
                      g_output->params.scale;
    }

  return 0;
}
