/* SPDX-License-Identifier: Apache-2.0 */

#include <cagent/types.h>
#include <smart_home_device.h>
#include <smart_home_device_service.h>
#include <smart_home_quickapp_feature_bridge.h>
#include <smart_home_quickapp_provider.h>
#include <smart_home_quickapp_bridge.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_bridge_exposes_provider_state(void)
{
    smart_home_state_t state;
    smart_home_device_service_t service;
    smart_home_quickapp_provider_t provider;
    struct smart_home_quickapp_bridge_capability_s capability;
    struct smart_home_quickapp_bridge_snapshot_s before;
    struct smart_home_quickapp_bridge_snapshot_s after;

    smart_home_device_init(&state);
    assert(smart_home_device_service_init(&service, &state) == AGENT_OK);
    assert(smart_home_quickapp_provider_init(&provider, &service) == AGENT_OK);

    smart_home_quickapp_feature_bridge_register(&provider);
    assert(smart_home_quickapp_bridge_get_capabilities(&capability) ==
           SMART_HOME_QUICKAPP_BRIDGE_OK);
    assert(capability.api_version == SMART_HOME_QUICKAPP_API_VERSION);
    assert(strcmp(capability.device_id, SMART_HOME_QUICKAPP_DEVICE_ID) == 0);
    assert(strcmp(capability.type, "light") == 0);
    assert(strcmp(capability.command,
                  SMART_HOME_QUICKAPP_COMMAND_SET_POWER) == 0);

    assert(smart_home_quickapp_bridge_get_snapshot(
               SMART_HOME_QUICKAPP_DEVICE_ID, &before) ==
           SMART_HOME_QUICKAPP_BRIDGE_OK);
    assert(before.revision == 1u);
    assert(!before.power);

    assert(smart_home_quickapp_bridge_control(
               "bridge-request-1", SMART_HOME_QUICKAPP_DEVICE_ID,
               SMART_HOME_QUICKAPP_COMMAND_SET_POWER, true, before.revision,
               &after) == SMART_HOME_QUICKAPP_BRIDGE_OK);
    assert(after.revision == before.revision + 1u);
    assert(after.power);
    assert(after.brightness == SMART_HOME_QUICKAPP_DEFAULT_BRIGHTNESS);

    assert(smart_home_quickapp_bridge_control(
               "bridge-request-2", SMART_HOME_QUICKAPP_DEVICE_ID,
               SMART_HOME_QUICKAPP_COMMAND_SET_POWER, false, before.revision,
               &after) == SMART_HOME_QUICKAPP_BRIDGE_ERROR_STALE);

    smart_home_quickapp_feature_bridge_unregister(&provider);
    assert(smart_home_quickapp_bridge_get_snapshot(
               SMART_HOME_QUICKAPP_DEVICE_ID, &after) ==
           SMART_HOME_QUICKAPP_BRIDGE_ERROR_UNAVAILABLE);

    smart_home_quickapp_provider_deinit(&provider);
    smart_home_device_service_deinit(&service);
}

int main(void)
{
    test_bridge_exposes_provider_state();
    printf("all quickapp feature bridge tests passed\n");
    return 0;
}

/* device.c registers a cAGENT context provider; host tests only need a stub. */
int agent_register_context_provider(agent_t *agent,
                                    const agent_context_provider_t *provider)
{
    (void)agent;
    (void)provider;
    return AGENT_OK;
}
