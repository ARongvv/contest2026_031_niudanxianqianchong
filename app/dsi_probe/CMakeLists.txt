# ##############################################################################
# Contest 2026 team 031 MIPI-DSI Host probe
# ##############################################################################

if(CONFIG_LVX_USE_DEMO_CONTEST2026_031_DSI_PROBE)
  set(EK79007_PANEL_DRIVER_DIR
      "${CMAKE_CURRENT_LIST_DIR}/../../drivers/nuttx/drivers/lcd")

  nuttx_add_application(
    NAME dsi_probe
    SRCS dsi_probe_main.c ${EK79007_PANEL_DRIVER_DIR}/ek79007.c
    INCLUDE_DIRECTORIES ${EK79007_PANEL_DRIVER_DIR}
    STACKSIZE 4096)
endif()
