/****************************************************************************
 * app/video_test/video_test_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal V4L2 continuous-capture acceptance test for the ESP32-P4X
 * SC2336 camera.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/videoio.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/crc32.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define VIDEO_TEST_PATH               "/dev/video0"
#define VIDEO_TEST_WIDTH               1024
#define VIDEO_TEST_HEIGHT              600
#define VIDEO_TEST_BYTES_PER_PIXEL     2
#define VIDEO_TEST_FRAME_BYTES         \
  (VIDEO_TEST_WIDTH * VIDEO_TEST_HEIGHT * VIDEO_TEST_BYTES_PER_PIXEL)
#define VIDEO_TEST_FRAME_INTERVAL_NUM  1
#define VIDEO_TEST_FRAME_INTERVAL_DEN  30
#define VIDEO_TEST_BUFFER_COUNT        3
#define VIDEO_TEST_BUFFER_ALIGNMENT    64
#define VIDEO_TEST_DEFAULT_FRAMES      100
#define VIDEO_TEST_MAX_FRAMES          1000
#define VIDEO_TEST_POLL_TIMEOUT_MS     1500
#define VIDEO_TEST_SAMPLE_BLOCKS       64
#define VIDEO_TEST_SAMPLE_BYTES        64
#define VIDEO_TEST_MIN_FPS_X100        2850
#define VIDEO_TEST_MAX_FPS_X100        3150

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct video_test_frame_stats_s
{
  uint32_t crc;
  uint8_t min;
  uint8_t max;
  bool nonzero;
};

struct video_test_result_s
{
  struct video_test_frame_stats_s stats;
  struct timeval timestamp;
  uint64_t dequeue_us;
  uint32_t sequence;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void video_test_usage(FAR const char *program)
{
  fprintf(stderr, "Usage: %s [frames: 1-%u] [--full]\n", program,
          VIDEO_TEST_MAX_FRAMES);
}

static int video_test_parse_frames(int argc, FAR char *argv[],
                                   FAR unsigned int *frames, FAR bool *full)
{
  FAR char *end;
  unsigned long value;

  *full = false;
  if (argc > 1 && strcmp(argv[argc - 1], "--full") == 0)
    {
      *full = true;
      argc--;
    }

  *frames = VIDEO_TEST_DEFAULT_FRAMES;
  if (argc == 1)
    {
      return OK;
    }

  if (argc != 2)
    {
      return -EINVAL;
    }

  value = strtoul(argv[1], &end, 10);
  if (*argv[1] == '\0' || *end != '\0' || value == 0 ||
      value > VIDEO_TEST_MAX_FRAMES)
    {
      return -EINVAL;
    }

  *frames = (unsigned int)value;
  return OK;
}

static int video_test_ioctl(int fd, int request, FAR void *arg,
                            FAR const char *step)
{
  if (ioctl(fd, request, (uintptr_t)arg) < 0)
    {
      fprintf(stderr, "video_test: FAIL step=%s errno=%d\n", step, errno);
      return -errno;
    }

  return OK;
}

static void video_test_frame_stats(
  FAR const uint8_t *buffer, size_t bytes, bool full,
  FAR struct video_test_frame_stats_s *stats)
{
  size_t blocks = full ? 1 : VIDEO_TEST_SAMPLE_BLOCKS;
  size_t length = full ? bytes : VIDEO_TEST_SAMPLE_BYTES;
  size_t block;
  size_t index;
  size_t offset;

  stats->min = UINT8_MAX;
  stats->max = 0;
  stats->nonzero = false;
  stats->crc = 0;

  /* Sample equally spaced cache-line-sized windows, including both ends.
   * Full inspection is intentionally separate from the throughput test.
   */

  for (block = 0; block < blocks; block++)
    {
      offset = full ? 0 : block * (bytes - length) / (blocks - 1);
      for (index = 0; index < length; index++)
        {
          uint8_t value = buffer[offset + index];

          if (value < stats->min)
            {
              stats->min = value;
            }

          if (value > stats->max)
            {
              stats->max = value;
            }
        }

      stats->crc = crc32part(buffer + offset, length, stats->crc);
    }

  stats->nonzero = stats->max != 0;
}

static int video_test_check_capability(int fd)
{
  struct v4l2_capability capability;
  int ret;

  memset(&capability, 0, sizeof(capability));
  ret = video_test_ioctl(fd, VIDIOC_QUERYCAP, &capability,
                         "VIDIOC_QUERYCAP");
  if (ret < 0)
    {
      return ret;
    }

  printf("video_test: device=%s driver=%s capabilities=0x%08" PRIx32 "\n",
         VIDEO_TEST_PATH, (FAR char *)capability.driver,
         capability.capabilities);

  if ((capability.capabilities & (V4L2_CAP_VIDEO_CAPTURE |
                                  V4L2_CAP_STREAMING)) !=
      (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING))
    {
      fprintf(stderr, "video_test: FAIL step=capability\n");
      return -ENOTSUP;
    }

  return OK;
}

static int video_test_enumerate_format(int fd)
{
  struct v4l2_fmtdesc format;
  struct v4l2_frmsizeenum frame_size;
  struct v4l2_frmivalenum frame_interval;
  int ret;

  memset(&format, 0, sizeof(format));
  format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  ret = video_test_ioctl(fd, VIDIOC_ENUM_FMT, &format, "VIDIOC_ENUM_FMT");
  if (ret < 0)
    {
      return ret;
    }

  if (format.pixelformat != V4L2_PIX_FMT_RGB565)
    {
      fprintf(stderr, "video_test: FAIL step=format expected=RGB565\n");
      return -ENOTSUP;
    }

  memset(&frame_size, 0, sizeof(frame_size));
  frame_size.buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  frame_size.pixel_format = V4L2_PIX_FMT_RGB565;
  ret = video_test_ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frame_size,
                         "VIDIOC_ENUM_FRAMESIZES");
  if (ret < 0)
    {
      return ret;
    }

  if (frame_size.type != V4L2_FRMSIZE_TYPE_DISCRETE ||
      frame_size.discrete.width != VIDEO_TEST_WIDTH ||
      frame_size.discrete.height != VIDEO_TEST_HEIGHT)
    {
      fprintf(stderr, "video_test: FAIL step=frame_size_enum\n");
      return -ENOTSUP;
    }

  memset(&frame_interval, 0, sizeof(frame_interval));
  frame_interval.buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  frame_interval.pixel_format = V4L2_PIX_FMT_RGB565;
  frame_interval.width = VIDEO_TEST_WIDTH;
  frame_interval.height = VIDEO_TEST_HEIGHT;
  ret = video_test_ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &frame_interval,
                         "VIDIOC_ENUM_FRAMEINTERVALS");
  if (ret < 0)
    {
      return ret;
    }

  if (frame_interval.type != V4L2_FRMIVAL_TYPE_DISCRETE ||
      frame_interval.discrete.numerator != VIDEO_TEST_FRAME_INTERVAL_NUM ||
      frame_interval.discrete.denominator != VIDEO_TEST_FRAME_INTERVAL_DEN)
    {
      fprintf(stderr, "video_test: FAIL step=frame_interval_enum\n");
      return -ENOTSUP;
    }

  return OK;
}

static int video_test_set_format(int fd)
{
  struct v4l2_format format;
  struct v4l2_streamparm parameter;
  int ret;

  memset(&format, 0, sizeof(format));
  format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  ret = video_test_ioctl(fd, VIDIOC_G_FMT, &format, "VIDIOC_G_FMT");
  if (ret < 0)
    {
      return ret;
    }

  memset(&format, 0, sizeof(format));
  format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  format.fmt.pix.width = VIDEO_TEST_WIDTH;
  format.fmt.pix.height = VIDEO_TEST_HEIGHT;
  format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
  format.fmt.pix.field = V4L2_FIELD_ANY;
  format.fmt.pix.bytesperline =
    VIDEO_TEST_WIDTH * VIDEO_TEST_BYTES_PER_PIXEL;
  format.fmt.pix.sizeimage = VIDEO_TEST_FRAME_BYTES;
  ret = video_test_ioctl(fd, VIDIOC_S_FMT, &format, "VIDIOC_S_FMT");
  if (ret < 0)
    {
      return ret;
    }

  memset(&parameter, 0, sizeof(parameter));
  parameter.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  parameter.parm.capture.timeperframe.numerator =
    VIDEO_TEST_FRAME_INTERVAL_NUM;
  parameter.parm.capture.timeperframe.denominator =
    VIDEO_TEST_FRAME_INTERVAL_DEN;
  ret = video_test_ioctl(fd, VIDIOC_S_PARM, &parameter, "VIDIOC_S_PARM");
  if (ret < 0)
    {
      return ret;
    }

  printf("video_test: format=RGB565 %ux%u size=%u interval=%u/%u\n",
         VIDEO_TEST_WIDTH, VIDEO_TEST_HEIGHT, VIDEO_TEST_FRAME_BYTES,
         VIDEO_TEST_FRAME_INTERVAL_NUM, VIDEO_TEST_FRAME_INTERVAL_DEN);
  return OK;
}

static int video_test_request_buffers(int fd)
{
  struct v4l2_requestbuffers request;
  int ret;

  memset(&request, 0, sizeof(request));
  request.count = VIDEO_TEST_BUFFER_COUNT;
  request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  request.memory = V4L2_MEMORY_USERPTR;
  request.mode = V4L2_BUF_MODE_RING;
  ret = video_test_ioctl(fd, VIDIOC_REQBUFS, &request, "VIDIOC_REQBUFS");
  if (ret < 0)
    {
      return ret;
    }

  if (request.count != VIDEO_TEST_BUFFER_COUNT)
    {
      fprintf(stderr, "video_test: FAIL step=buffer_count actual=%" PRIu32
              " expected=%u\n", request.count, VIDEO_TEST_BUFFER_COUNT);
      return -ENOMEM;
    }

  return OK;
}

static void video_test_free_buffers(FAR uint8_t *buffers[])
{
  unsigned int index;

  for (index = 0; index < VIDEO_TEST_BUFFER_COUNT; index++)
    {
      free(buffers[index]);
      buffers[index] = NULL;
    }
}

static int video_test_queue_buffer(int fd, unsigned int index,
                                   FAR uint8_t *buffer)
{
  struct v4l2_buffer v4l2_buffer;

  memset(&v4l2_buffer, 0, sizeof(v4l2_buffer));
  v4l2_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  v4l2_buffer.memory = V4L2_MEMORY_USERPTR;
  v4l2_buffer.index = index;
  v4l2_buffer.m.userptr = (unsigned long)buffer;
  v4l2_buffer.length = VIDEO_TEST_FRAME_BYTES;
  return video_test_ioctl(fd, VIDIOC_QBUF, &v4l2_buffer, "VIDIOC_QBUF");
}

static int video_test_queue_all_buffers(int fd, FAR uint8_t *buffers[])
{
  unsigned int index;
  int ret;

  for (index = 0; index < VIDEO_TEST_BUFFER_COUNT; index++)
    {
      buffers[index] = memalign(VIDEO_TEST_BUFFER_ALIGNMENT,
                                VIDEO_TEST_FRAME_BYTES);
      if (buffers[index] == NULL)
        {
          fprintf(stderr, "video_test: FAIL step=frame_buffer index=%u "
                  "bytes=%u\n", index, VIDEO_TEST_FRAME_BYTES);
          return -ENOMEM;
        }

      ret = video_test_queue_buffer(fd, index, buffers[index]);
      if (ret < 0)
        {
          return ret;
        }
    }

  return OK;
}

static int video_test_capture_frames(int fd, unsigned int frames,
                                     FAR uint8_t *buffers[],
                                     FAR bool *streaming, bool full,
                                     FAR struct video_test_result_s *results)
{
  struct pollfd pollfd;
  struct v4l2_buffer v4l2_buffer;
  struct video_test_frame_stats_s stats;
  struct timespec now;
  uint32_t previous_sequence = 0;
  bool have_previous_sequence = false;
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  unsigned int frame;
  int ret;

  ret = video_test_ioctl(fd, VIDIOC_STREAMON, &type, "VIDIOC_STREAMON");
  if (ret < 0)
    {
      return ret;
    }

  *streaming = true;

  pollfd.fd = fd;
  pollfd.events = POLLIN;
  for (frame = 0; frame < frames; frame++)
    {
      pollfd.revents = 0;
      ret = poll(&pollfd, 1, VIDEO_TEST_POLL_TIMEOUT_MS);
      if (ret == 0)
        {
          fprintf(stderr, "video_test: FAIL step=poll frame=%u errno=%d\n",
                  frame, ETIMEDOUT);
          return -ETIMEDOUT;
        }

      if (ret < 0)
        {
          fprintf(stderr, "video_test: FAIL step=poll frame=%u errno=%d\n",
                  frame, errno);
          return -errno;
        }

      if ((pollfd.revents & POLLIN) == 0)
        {
          fprintf(stderr, "video_test: FAIL step=poll_event frame=%u "
                  "revents=0x%04x\n", frame,
                  (unsigned int)pollfd.revents);
          return -EIO;
        }

      memset(&v4l2_buffer, 0, sizeof(v4l2_buffer));
      v4l2_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      v4l2_buffer.memory = V4L2_MEMORY_USERPTR;
      ret = video_test_ioctl(fd, VIDIOC_DQBUF, &v4l2_buffer, "VIDIOC_DQBUF");
      if (ret < 0)
        {
          return ret;
        }

      if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
        {
          return -errno;
        }

      results[frame].dequeue_us = (uint64_t)now.tv_sec * 1000000 +
                                  now.tv_nsec / 1000;
      results[frame].sequence = v4l2_buffer.sequence;
      results[frame].timestamp = v4l2_buffer.timestamp;

      if (v4l2_buffer.index >= VIDEO_TEST_BUFFER_COUNT ||
          v4l2_buffer.m.userptr != (unsigned long)buffers[v4l2_buffer.index])
        {
          fprintf(stderr, "video_test: FAIL step=buffer_identity frame=%u "
                  "index=%" PRIu32 "\n", frame, v4l2_buffer.index);
          return -EIO;
        }

      if ((v4l2_buffer.flags & V4L2_BUF_FLAG_ERROR) != 0)
        {
          fprintf(stderr, "video_test: FAIL step=frame_error frame=%u "
                  "flags=0x%08" PRIx32 "\n", frame, v4l2_buffer.flags);
          return -EIO;
        }

      if (v4l2_buffer.bytesused != VIDEO_TEST_FRAME_BYTES)
        {
          fprintf(stderr, "video_test: FAIL step=frame_size frame=%u "
                  "actual=%" PRIu32 " expected=%u\n", frame,
                  v4l2_buffer.bytesused, VIDEO_TEST_FRAME_BYTES);
          return -EIO;
        }

      if (have_previous_sequence &&
          (int32_t)(v4l2_buffer.sequence - previous_sequence) <= 0)
        {
          fprintf(stderr, "video_test: FAIL step=sequence frame=%u "
                  "previous=%" PRIu32 " actual=%" PRIu32 "\n", frame,
                  previous_sequence, v4l2_buffer.sequence);
          return -EIO;
        }

      video_test_frame_stats(buffers[v4l2_buffer.index],
                             v4l2_buffer.bytesused, full, &stats);
      if (!stats.nonzero || stats.min == stats.max)
        {
          fprintf(stderr, "video_test: FAIL step=frame_content frame=%u "
                  "min=0x%02x max=0x%02x\n", frame, stats.min, stats.max);
          return -EIO;
        }

      results[frame].stats = stats;

      /* Return ownership before any logging.  Never inspect pixels after
       * QBUF: the capture engine may overwrite them immediately.
       */

      previous_sequence = v4l2_buffer.sequence;
      have_previous_sequence = true;
      ret = video_test_queue_buffer(fd, v4l2_buffer.index,
                                    buffers[v4l2_buffer.index]);
      if (ret < 0)
        {
          return ret;
        }
    }

  return OK;
}

static int video_test_report(FAR const struct video_test_result_s *results,
                             unsigned int frames, bool full)
{
  uint64_t elapsed;
  uint64_t fps_x100;
  uint32_t gaps = 0;
  unsigned int frame;

  for (frame = 0; frame < frames; frame++)
    {
      if (frame > 0)
        {
          gaps += results[frame].sequence - results[frame - 1].sequence - 1;
        }

      printf("video_test: frame=%u sequence=%" PRIu32
             " timestamp=%ld.%06ld %s_crc32=0x%08" PRIx32 "\n",
             frame, results[frame].sequence,
             (long)results[frame].timestamp.tv_sec,
             (long)results[frame].timestamp.tv_usec,
             full ? "full" : "sample", results[frame].stats.crc);
    }

  if (frames < 30)
    {
      printf("video_test: content-only frames=%u sequence_gaps=%" PRIu32
             " (30fps acceptance requires >=30 frames)\n", frames, gaps);
      return OK;
    }

  /* Use dequeue wall time, excluding startup and all serial output.  V4L2
   * sequence gaps detect ring overwrites, not losses before delivery.
   */

  elapsed = results[frames - 1].dequeue_us - results[0].dequeue_us;
  fps_x100 = elapsed == 0 ? 0 : (uint64_t)(frames - 1) * 100000000 / elapsed;
  printf("video_test: app_fps=%" PRIu64 ".%02" PRIu64
         " sequence_gaps=%" PRIu32 " mode=%s\n",
         fps_x100 / 100, fps_x100 % 100, gaps,
         full ? "full-content" : "throughput");

  if (!full && (gaps != 0 || fps_x100 < VIDEO_TEST_MIN_FPS_X100 ||
                fps_x100 > VIDEO_TEST_MAX_FPS_X100))
    {
      fprintf(stderr, "video_test: FAIL step=30fps\n");
      return -EIO;
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  FAR uint8_t *buffers[VIDEO_TEST_BUFFER_COUNT] =
  {
    NULL
  };

  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  FAR struct video_test_result_s *results;
  unsigned int frames;
  bool full;
  bool streaming = false;
  int fd;
  int ret;

  ret = video_test_parse_frames(argc, argv, &frames, &full);
  if (ret < 0)
    {
      video_test_usage(argv[0]);
      return EXIT_FAILURE;
    }

  fd = open(VIDEO_TEST_PATH, O_RDWR);
  if (fd < 0)
    {
      fprintf(stderr, "video_test: FAIL step=open path=%s errno=%d\n",
              VIDEO_TEST_PATH, errno);
      return EXIT_FAILURE;
    }

  results = calloc(frames, sizeof(*results));
  if (results == NULL)
    {
      close(fd);
      return EXIT_FAILURE;
    }

  printf("=== ESP32-P4X SC2336 V4L2 Capture Test ===\n");
  printf("request: frames=%u timeout=%dms\n", frames,
         VIDEO_TEST_POLL_TIMEOUT_MS);

  ret = video_test_check_capability(fd);
  if (ret >= 0)
    {
      ret = video_test_enumerate_format(fd);
    }

  if (ret >= 0)
    {
      ret = video_test_set_format(fd);
    }

  if (ret >= 0)
    {
      ret = video_test_request_buffers(fd);
    }

  if (ret >= 0)
    {
      ret = video_test_queue_all_buffers(fd, buffers);
    }

  if (ret >= 0)
    {
      ret = video_test_capture_frames(fd, frames, buffers, &streaming,
                                       full, results);
    }

  if (streaming)
    {
      int stop_ret = video_test_ioctl(fd, VIDIOC_STREAMOFF, &type,
                                      "VIDIOC_STREAMOFF");
      if (ret >= 0 && stop_ret < 0)
        {
          ret = stop_ret;
        }
    }

  /* Close drains the lower-half frame worker.  A copy already in progress
   * at STREAMOFF must finish before its USERPTR destination is freed.
   */

  close(fd);
  video_test_free_buffers(buffers);

  if (ret >= 0)
    {
      ret = video_test_report(results, frames, full);
    }

  free(results);

  if (ret < 0)
    {
      return EXIT_FAILURE;
    }

  printf("video_test: PASS frames=%u acceptance=%s\n", frames,
         full || frames < 30 ? "content" : "30fps-app");
  return EXIT_SUCCESS;
}
