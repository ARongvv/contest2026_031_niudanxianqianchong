/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stddef.h>

#include "../device/smart_home_device.h"

#define SMART_HOME_SCENE_CATALOG_VERSION 1u
#define SMART_HOME_SCENE_CATALOG_MAX_SCENES 8u
#define SMART_HOME_SCENE_CATALOG_MAX_ACTIONS 8u
#define SMART_HOME_SCENE_ID_SIZE 32u

typedef struct {
    smart_home_device_type_t device_type;
    char room[SMART_HOME_ROOM_NAME_SIZE];
    int on;
    int brightness;
    int ac_mode;
    int ac_fan_speed;
    int temperature;
} smart_home_scene_action_t;

typedef struct {
    char id[SMART_HOME_SCENE_ID_SIZE];
    smart_home_scene_action_t actions[SMART_HOME_SCENE_CATALOG_MAX_ACTIONS];
    size_t action_count;
} smart_home_scene_t;

typedef struct {
    unsigned int version;
    smart_home_scene_t scenes[SMART_HOME_SCENE_CATALOG_MAX_SCENES];
    size_t scene_count;
} smart_home_scene_catalog_t;

/* Load the machine-readable scene_catalog block from a Skill's Markdown body. */
int smart_home_scene_catalog_load(const char *skill_text,
                                  smart_home_scene_catalog_t *catalog);
