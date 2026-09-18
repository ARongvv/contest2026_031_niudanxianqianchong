#include "../agent/smart_home_agent.h"
#include "../net/smart_home_network.h"
#include "../config/smart_home_secrets.h"
#include "../smart_home_cpu_debug.h"
#include "../ui/smart_home_ui.h"
#include <cagent/runtime_openvela.h>
#ifdef CONFIG_SMART_HOME_APP_BRIDGE
#include "../addons/smart_home_app_bridge.h"
#endif
#ifdef CONFIG_SMART_HOME_DEMO_UI_LVGL
#include "../ui/lvgl/smart_home_lvgl.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    char stack_marker;
    smart_home_agent_app_t app;
    smart_home_network_status_t network_status;
#ifndef CONFIG_SMART_HOME_DEMO_OFFLINE_UI
    int network_ret;
#endif
    int ret;

    printf("=== Smart Home Agent Demo ===\n");
    if (argc > 1) {
        printf("input: %s\n\n", argv[1]);
    } else {
#if defined(CONFIG_SMART_HOME_DEMO_UI_LVGL)
        printf("starting LVGL UI\n\n");
#elif defined(CONFIG_SMART_HOME_DEMO_UI_QUICKAPP)
        printf("starting QuickApp native service\n\n");
#else
        printf("type 'quit' to exit\n\n");
#endif
    }

    /* Initialize the platform network before initializing the agent.
     * ESP32-S3 uses Wi-Fi (wlan0), while goldfish uses Ethernet (eth0).
     * Continue on failure so the UI can still show diagnostics.
     */

#ifdef CONFIG_SMART_HOME_DEMO_OFFLINE_UI
    smart_home_network_status_init(&network_status);
    network_status.init_status = SMART_HOME_NETWORK_STATUS_NA;
    network_status.ip_status = SMART_HOME_NETWORK_STATUS_NA;
    network_status.dns_status = SMART_HOME_NETWORK_STATUS_NA;
    printf("[smart_home_net] offline UI profile: network initialization skipped\n");
#else
#ifdef CONFIG_ESP32P4_FUNCTION_EV_BOARD_ESP_HOSTED
    {
        char ssid[33] = "";
        char password[65] = "";

        /* Runtime credentials take precedence over optional local board
         * Kconfig values.  Do not log either string. */
        if (smart_home_secrets_get_wifi_credentials(ssid, sizeof(ssid),
                                                    password,
                                                    sizeof(password)) == AGENT_OK) {
            network_ret = smart_home_network_connect_credentials(
                &network_status, ssid, password);
            memset(ssid, 0, sizeof(ssid));
            memset(password, 0, sizeof(password));
        } else {
            network_ret = smart_home_network_prepare_setup(&network_status);
        }
    }
#else
    network_ret = smart_home_network_init(&network_status);
#endif
    if (network_ret < 0) {
        syslog(LOG_WARNING, "Network init failed: %d. "
               "Continuing with existing network.\n", network_ret);
        fprintf(stderr,
                "Network init failed: %d (continuing)\n",
                network_ret);
    }
#endif
    smart_home_cpu_debug_log("main-network-ready");
    ov_mem_region_log("main-stack", &stack_marker);
    ov_mem_region_log("smart-home-app", &app);

    ret = smart_home_agent_app_init(&app);
    if (ret != AGENT_OK) {
        fprintf(stderr, "smart_home_agent_app_init failed: %d\n", ret);
        return EXIT_FAILURE;
    }
    smart_home_agent_app_set_network_status(&app, &network_status);
#ifdef CONFIG_SMART_HOME_APP_BRIDGE
    ret = smart_home_app_bridge_start(&app.app_bridge, &app);
    if (ret != AGENT_OK) {
        fprintf(stderr, "smart_home app bridge failed: %d\n", ret);
        smart_home_agent_app_deinit(&app);
        return EXIT_FAILURE;
    }
#endif

#if defined(CONFIG_SMART_HOME_DEMO_UI_QUICKAPP)
    if (argc > 1) {
        fprintf(stderr, "QuickApp mode ignores console input\n");
    }
    printf("SmartHome IPC service is ready; start the RPK with vapp separately.\n");
    for (;;) {
        pause();
    }
#elif defined(CONFIG_SMART_HOME_DEMO_UI_LVGL)
    smart_home_cpu_debug_log("main-before-ui");
    if (argc > 1) {
        ret = smart_home_ui_run_once(&app, argv[1]);
    } else {
        ret = smart_home_lvgl_run(&app, NULL);
    }
#else
    if (argc > 1) {
        ret = smart_home_ui_run_once(&app, argv[1]);
    } else {
        ret = smart_home_ui_run(&app);
    }
#endif

#ifdef CONFIG_SMART_HOME_APP_BRIDGE
    smart_home_app_bridge_stop(&app.app_bridge);
#endif
    smart_home_agent_app_deinit(&app);
    return ret == AGENT_OK ? EXIT_SUCCESS : EXIT_FAILURE;
}
