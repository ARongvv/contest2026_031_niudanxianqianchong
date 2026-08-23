############################################################################
# Contest 2026 team 031 MIPI-DSI Host probe
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = dsi_probe
PRIORITY  = SCHED_PRIORITY_DEFAULT
STACKSIZE = 4096
MODULE    = $(CONFIG_LVX_USE_DEMO_CONTEST2026_031_DSI_PROBE)

MAINSRC = dsi_probe_main.c

# Keep the EK79007 panel-control implementation canonical in the contest
# driver tree.  The probe links it as a private component; it is not yet a
# globally registered NuttX LCD or framebuffer driver.

PANEL_DRIVER_DIR = $(abspath $(CURDIR)/../../drivers/nuttx/drivers/lcd)
VPATH += :$(PANEL_DRIVER_DIR)
CFLAGS += -I$(PANEL_DRIVER_DIR)
CSRCS += ek79007.c

include $(APPDIR)/Application.mk
