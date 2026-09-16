/****************************************************************************
 * demos/smart_home/src/audio/audio_smoke_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2026 The NuttX Contributors
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <mqueue.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <nuttx/audio/audio.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define AUDIO_SMOKE_OUTPUT_DEVICE  "/dev/audio/pcm0"
#define AUDIO_SMOKE_INPUT_DEVICE   "/dev/audio/pcm_in0"
#define AUDIO_SMOKE_DEFAULT_PATH   "/data/es8311_16k_mono.pcm"
#define AUDIO_SMOKE_SAMPLE_RATE    16000
#define AUDIO_SMOKE_BYTES_PER_SEC  32000
#define AUDIO_SMOKE_DEFAULT_PLAY_S 3
#define AUDIO_SMOKE_DEFAULT_REC_S  10
#define AUDIO_SMOKE_MAX_SECONDS    30
#define AUDIO_SMOKE_MQ_NAME_SIZE   32

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct audio_smoke_state_s
{
  FAR struct ap_buffer_s **buffers;
  struct ap_buffer_info_s buffer_info;
  mqd_t mq;
  char mq_name[AUDIO_SMOKE_MQ_NAME_SIZE];
  int audio_fd;
  int output_fd;
  unsigned int outstanding;
  uint32_t remaining;
  uint32_t sample_index;
  bool started;
  bool stopping;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void audio_smoke_usage(FAR const char *progname)
{
  printf("Usage:\\n"
         "  %s play [seconds]\\n"
         "  %s record [seconds] [pcm_path]\\n"
         "Defaults: play=%d s, record=%d s, path=%s\\n",
         progname, progname, AUDIO_SMOKE_DEFAULT_PLAY_S,
         AUDIO_SMOKE_DEFAULT_REC_S, AUDIO_SMOKE_DEFAULT_PATH);
}

static int audio_smoke_parse_seconds(FAR const char *text,
                                     unsigned int default_seconds,
                                     FAR unsigned int *seconds)
{
  FAR char *end;
  unsigned long value;

  if (text == NULL)
    {
      *seconds = default_seconds;
      return OK;
    }

  errno = 0;
  value = strtoul(text, &end, 10);
  if (errno != 0 || *text == '\0' || *end != '\0' || value == 0 ||
      value > AUDIO_SMOKE_MAX_SECONDS)
    {
      return -EINVAL;
    }

  *seconds = (unsigned int)value;
  return OK;
}

static int audio_smoke_configure(int fd, uint8_t type)
{
  struct audio_caps_desc_s caps;

  memset(&caps, 0, sizeof(caps));
  caps.caps.ac_len = sizeof(caps.caps);
  caps.caps.ac_type = type;
  caps.caps.ac_subtype = AUDIO_FMT_PCM;
  caps.caps.ac_channels = 1;
  caps.caps.ac_controls.hw[0] = AUDIO_SMOKE_SAMPLE_RATE;
  caps.caps.ac_controls.b[2] = 16;

  if (ioctl(fd, AUDIOIOC_CONFIGURE, (unsigned long)(uintptr_t)&caps) < 0)
    {
      return -errno;
    }

  return OK;
}

static int audio_smoke_allocate_buffers(FAR struct audio_smoke_state_s *state)
{
  struct audio_buf_desc_s desc;
  unsigned int index;

  state->buffers = calloc(state->buffer_info.nbuffers,
                          sizeof(FAR struct ap_buffer_s *));
  if (state->buffers == NULL)
    {
      return -ENOMEM;
    }

  for (index = 0; index < state->buffer_info.nbuffers; index++)
    {
      memset(&desc, 0, sizeof(desc));
      desc.numbytes = state->buffer_info.buffer_size;
      desc.u.pbuffer = &state->buffers[index];
      if (ioctl(state->audio_fd, AUDIOIOC_ALLOCBUFFER,
                (unsigned long)(uintptr_t)&desc) < 0)
        {
          return -errno;
        }
    }

  return OK;
}

static void audio_smoke_free_buffers(FAR struct audio_smoke_state_s *state)
{
  struct audio_buf_desc_s desc;
  unsigned int index;

  if (state->buffers == NULL)
    {
      return;
    }

  for (index = 0; index < state->buffer_info.nbuffers; index++)
    {
      if (state->buffers[index] != NULL)
        {
          memset(&desc, 0, sizeof(desc));
          desc.u.buffer = state->buffers[index];
          ioctl(state->audio_fd, AUDIOIOC_FREEBUFFER,
                (unsigned long)(uintptr_t)&desc);
        }
    }

  free(state->buffers);
  state->buffers = NULL;
}

static void audio_smoke_cleanup(FAR struct audio_smoke_state_s *state)
{
  if (state->audio_fd >= 0)
    {
      if (state->started && !state->stopping)
        {
          ioctl(state->audio_fd, AUDIOIOC_STOP, 0);
        }

      audio_smoke_free_buffers(state);
      ioctl(state->audio_fd, AUDIOIOC_UNREGISTERMQ,
            (unsigned long)(uintptr_t)state->mq);
      ioctl(state->audio_fd, AUDIOIOC_RELEASE, 0);
      close(state->audio_fd);
      state->audio_fd = -1;
    }

  if (state->output_fd >= 0)
    {
      close(state->output_fd);
      state->output_fd = -1;
    }

  if (state->mq >= 0)
    {
      mq_close(state->mq);
      mq_unlink(state->mq_name);
      state->mq = (mqd_t)-1;
    }
}

static int audio_smoke_prepare(FAR struct audio_smoke_state_s *state,
                               FAR const char *device, uint8_t type)
{
  struct mq_attr attr;
  int ret;

  state->audio_fd = open(device, O_RDWR | O_CLOEXEC);
  if (state->audio_fd < 0)
    {
      return -errno;
    }

  if (ioctl(state->audio_fd, AUDIOIOC_RESERVE, 0) < 0)
    {
      return -errno;
    }

  ret = audio_smoke_configure(state->audio_fd, type);
  if (ret < 0)
    {
      return ret;
    }

  if (ioctl(state->audio_fd, AUDIOIOC_GETBUFFERINFO,
            (unsigned long)(uintptr_t)&state->buffer_info) < 0)
    {
      /* ES8311 only reports its preferred values when
       * CONFIG_AUDIO_DRIVER_SPECIFIC_BUFFERS is enabled.  The generic
       * audio defaults are valid otherwise.
       */

      state->buffer_info.buffer_size = CONFIG_AUDIO_BUFFER_NUMBYTES;
      state->buffer_info.nbuffers = CONFIG_AUDIO_NUM_BUFFERS;
    }

  if (state->buffer_info.nbuffers == 0 || state->buffer_info.buffer_size == 0)
    {
      return -EINVAL;
    }

  memset(&attr, 0, sizeof(attr));
  attr.mq_maxmsg = state->buffer_info.nbuffers + 8;
  attr.mq_msgsize = sizeof(struct audio_msg_s);
  snprintf(state->mq_name, sizeof(state->mq_name), "/audio_smoke_%ld",
           (long)getpid());
  state->mq = mq_open(state->mq_name, O_RDWR | O_CREAT, 0644, &attr);
  if (state->mq < 0)
    {
      return -errno;
    }

  if (ioctl(state->audio_fd, AUDIOIOC_REGISTERMQ,
            (unsigned long)(uintptr_t)state->mq) < 0)
    {
      return -errno;
    }

  return audio_smoke_allocate_buffers(state);
}

static size_t audio_smoke_fill_tone(FAR struct audio_smoke_state_s *state,
                                    FAR struct ap_buffer_s *apb)
{
  static const int16_t wave[16] =
  {
       0,  4592,  8485, 11086, 12000, 11086,  8485,  4592,
       0, -4592, -8485,-11086,-12000,-11086, -8485, -4592
  };
  size_t bytes = apb->nmaxbytes;
  size_t index;
  int16_t sample;

  if (bytes > state->remaining)
    {
      bytes = state->remaining;
    }

  bytes &= ~(size_t)1;
  for (index = 0; index < bytes; index += sizeof(int16_t))
    {
      sample = wave[state->sample_index++ & 15];
      apb->samp[index] = (uint8_t)sample;
      apb->samp[index + 1] = (uint8_t)(sample >> 8);
    }

  apb->nbytes = bytes;
  apb->curbyte = 0;
  apb->flags = 0;
  state->remaining -= bytes;
  return bytes;
}

static int audio_smoke_enqueue(FAR struct audio_smoke_state_s *state,
                               FAR struct ap_buffer_s *apb, bool playback)
{
  struct audio_buf_desc_s desc;

  if (playback)
    {
      if (audio_smoke_fill_tone(state, apb) == 0)
        {
          return -ENODATA;
        }
    }
  else
    {
      apb->nbytes = apb->nmaxbytes;
      apb->curbyte = 0;
      apb->flags = 0;
    }

  memset(&desc, 0, sizeof(desc));
  desc.numbytes = apb->nbytes;
  desc.u.buffer = apb;
  if (ioctl(state->audio_fd, AUDIOIOC_ENQUEUEBUFFER,
            (unsigned long)(uintptr_t)&desc) < 0)
    {
      return -errno;
    }

  state->outstanding++;
  return OK;
}

static int audio_smoke_write_all(int fd, FAR const uint8_t *data,
                                 size_t bytes)
{
  ssize_t written;

  while (bytes > 0)
    {
      written = write(fd, data, bytes);
      if (written < 0)
        {
          return -errno;
        }

      if (written == 0)
        {
          return -EIO;
        }

      data += written;
      bytes -= written;
    }

  return OK;
}

static int audio_smoke_stop(FAR struct audio_smoke_state_s *state)
{
  if (!state->stopping)
    {
      if (ioctl(state->audio_fd, AUDIOIOC_STOP, 0) < 0)
        {
          return -errno;
        }

      state->stopping = true;
    }

  return OK;
}

static int audio_smoke_run(FAR struct audio_smoke_state_s *state,
                           bool playback)
{
  struct audio_msg_s message;
  unsigned int priority;
  unsigned int index;
  ssize_t size;
  size_t bytes;
  FAR struct ap_buffer_s *apb;
  int ret = OK;

  if (ioctl(state->audio_fd, AUDIOIOC_START, 0) < 0)
    {
      return -errno;
    }

  state->started = true;

  /* Start the codec worker before it owns any application buffer.  This
   * leaves the error path before AUDIOIOC_START free of queued DMA buffers.
   */

  for (index = 0; index < state->buffer_info.nbuffers &&
                  (playback ? state->remaining > 0 : true); index++)
    {
      ret = audio_smoke_enqueue(state, state->buffers[index], playback);
      if (ret < 0)
        {
          return ret;
        }
    }

  if (state->outstanding == 0)
    {
      return -ENODATA;
    }

  while (true)
    {
      size = mq_receive(state->mq, (FAR char *)&message, sizeof(message),
                        &priority);
      if (size != sizeof(message))
        {
          if (errno == EINTR)
            {
              continue;
            }

          return -errno;
        }

      if (message.msg_id == AUDIO_MSG_COMPLETE)
        {
          return ret;
        }

      if (message.msg_id == AUDIO_MSG_IOERR ||
          message.msg_id == AUDIO_MSG_UNDERRUN)
        {
          ret = message.msg_id == AUDIO_MSG_IOERR && message.u.data != 0 ?
                -(int)message.u.data : -EIO;
          audio_smoke_stop(state);
          return ret;
        }

      if (message.msg_id != AUDIO_MSG_DEQUEUE)
        {
          continue;
        }

      if (state->outstanding > 0)
        {
          state->outstanding--;
        }

      apb = (FAR struct ap_buffer_s *)message.u.ptr;
      if (ret == OK && state->remaining > 0)
        {
          if (playback)
            {
              ret = audio_smoke_enqueue(state, apb, true);
            }
          else
            {
              bytes = apb->nbytes;
              if (bytes == 0)
                {
                  ret = -EIO;
                }
              else
                {
                  if (bytes > state->remaining)
                    {
                      bytes = state->remaining;
                    }

                  ret = audio_smoke_write_all(state->output_fd,
                                               apb->samp, bytes);
                  if (ret == OK)
                    {
                      state->remaining -= bytes;
                      if (state->remaining > 0)
                        {
                          ret = audio_smoke_enqueue(state, apb, false);
                        }
                    }
                }
            }
        }

      if ((ret != OK || state->remaining == 0) && state->outstanding == 0)
        {
          int stop_ret = audio_smoke_stop(state);
          if (ret == OK)
            {
              ret = stop_ret;
            }
        }
    }
}

static int audio_smoke_start(bool playback, unsigned int seconds,
                             FAR const char *path)
{
  struct audio_smoke_state_s state;
  uint32_t total_bytes;
  int ret;

  memset(&state, 0, sizeof(state));
  state.audio_fd = -1;
  state.output_fd = -1;
  state.mq = (mqd_t)-1;
  total_bytes = seconds * AUDIO_SMOKE_BYTES_PER_SEC;
  state.remaining = total_bytes;

  ret = audio_smoke_prepare(&state,
                            playback ? AUDIO_SMOKE_OUTPUT_DEVICE :
                                       AUDIO_SMOKE_INPUT_DEVICE,
                            playback ? AUDIO_TYPE_OUTPUT : AUDIO_TYPE_INPUT);
  if (ret < 0)
    {
      goto out;
    }

  if (!playback)
    {
      state.output_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
                             0644);
      if (state.output_fd < 0)
        {
          ret = -errno;
          goto out;
        }
    }

  printf("audio_smoke: %s %u s, 16 kHz mono PCM16\\n",
         playback ? "play" : "record", seconds);
  ret = audio_smoke_run(&state, playback);
  if (ret == OK)
    {
      printf("audio_smoke: %s complete, bytes=%" PRIu32 "\\n",
             playback ? "play" : "record", total_bytes);
    }

out:
  if (ret < 0)
    {
      fprintf(stderr, "audio_smoke: %s failed: %d (%s)\\n",
              playback ? "play" : "record", ret, strerror(-ret));
    }

  audio_smoke_cleanup(&state);
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  unsigned int seconds;
  int ret;

  if (argc < 2 || argc > 4)
    {
      audio_smoke_usage(argv[0]);
      return EXIT_FAILURE;
    }

  if (strcmp(argv[1], "play") == 0)
    {
      if (argc > 3 ||
          audio_smoke_parse_seconds(argc == 3 ? argv[2] : NULL,
                                    AUDIO_SMOKE_DEFAULT_PLAY_S,
                                    &seconds) < 0)
        {
          audio_smoke_usage(argv[0]);
          return EXIT_FAILURE;
        }

      ret = audio_smoke_start(true, seconds, NULL);
    }
  else if (strcmp(argv[1], "record") == 0)
    {
      if (audio_smoke_parse_seconds(argc >= 3 ? argv[2] : NULL,
                                    AUDIO_SMOKE_DEFAULT_REC_S,
                                    &seconds) < 0)
        {
          audio_smoke_usage(argv[0]);
          return EXIT_FAILURE;
        }

      ret = audio_smoke_start(false, seconds,
                              argc == 4 ? argv[3] : AUDIO_SMOKE_DEFAULT_PATH);
    }
  else
    {
      audio_smoke_usage(argv[0]);
      return EXIT_FAILURE;
    }

  return ret == OK ? EXIT_SUCCESS : EXIT_FAILURE;
}
