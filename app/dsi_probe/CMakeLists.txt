# ##############################################################################
# Contest 2026 team 031 MIPI-DSI Host probe
# ##############################################################################

if(CONFIG_LVX_USE_DEMO_CONTEST2026_031_DSI_PROBE)
  nuttx_add_application(
    NAME dsi_probe
    SRCS dsi_probe_main.c
    STACKSIZE 4096)
endif()
