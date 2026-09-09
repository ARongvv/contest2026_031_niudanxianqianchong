# Contest 2026 team 031 SC2336 V4L2 video capture test

if(CONFIG_LVX_USE_DEMO_CONTEST2026_031_VIDEO_TEST)
  nuttx_add_application(
    NAME video_test
    SRCS video_test_main.c
    STACKSIZE 4096)
endif()
