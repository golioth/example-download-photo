/*
 * Copyright (c) 2022-2023 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(example_download_photo, LOG_LEVEL_DBG);

#include <app_version.h>
#include <golioth/client.h>
#include <golioth/ota.h>
#include <golioth/settings.h>
#include <samples/common/net_connect.h>
#include <samples/common/sample_credentials.h>
#include <zephyr/kernel.h>

/* Current firmware version; update in VERSION file */
static const char *_current_version =
    STRINGIFY(APP_VERSION_MAJOR) "." STRINGIFY(APP_VERSION_MINOR) "." STRINGIFY(APP_PATCHLEVEL);

static struct golioth_client *client;
K_SEM_DEFINE(connected, 0, 1);

static k_tid_t _system_thread = 0;

static int32_t _loop_delay_s = 10;
#define LOOP_DELAY_S_MAX 43200
#define LOOP_DELAY_S_MIN 0


static void wake_system_thread(void)
{
    k_wakeup(_system_thread);
}

static void on_client_event(struct golioth_client *client,
                            enum golioth_client_event event,
                            void *arg)
{
    bool is_connected = (event == GOLIOTH_CLIENT_EVENT_CONNECTED);

    if (is_connected)
    {
        k_sem_give(&connected);
    }
    LOG_INF("Golioth client %s", is_connected ? "connected" : "disconnected");
}

static enum golioth_settings_status on_loop_delay_setting(int32_t new_value, void *arg)
{
    _loop_delay_s = new_value;
    LOG_INF("Set loop delay to %i seconds", new_value);
    wake_system_thread();
    return GOLIOTH_SETTINGS_SUCCESS;
}

static int app_settings_register(struct golioth_client *client)
{
    struct golioth_settings *settings = golioth_settings_init(client);

    int err = golioth_settings_register_int_with_range(settings,
                                                       "LOOP_DELAY_S",
                                                       LOOP_DELAY_S_MIN,
                                                       LOOP_DELAY_S_MAX,
                                                       on_loop_delay_setting,
                                                       NULL);

    if (err)
    {
        LOG_ERR("Failed to register settings callback: %d", err);
    }

    return err;
}

struct ota_observe_data
{
    struct golioth_ota_manifest manifest;
    struct k_sem manifest_received;
};

static void on_ota_manifest(struct golioth_client *client,
                            const struct golioth_response *response,
                            const char *path,
                            const uint8_t *payload,
                            size_t payload_size,
                            void *arg)
{
    struct ota_observe_data *data = arg;

    LOG_INF("Manifest received");

    if (response->status != GOLIOTH_OK)
    {
        return;
    }

    LOG_HEXDUMP_INF(payload, payload_size, "Received OTA manifest");

    enum golioth_ota_state state = golioth_ota_get_state();
    if (state == GOLIOTH_OTA_STATE_DOWNLOADING)
    {
        GLTH_LOGW(TAG, "Ignoring manifest while download in progress");
        return;
    }

    enum golioth_status status =
        golioth_ota_payload_as_manifest(payload, payload_size, &data->manifest);
    if (status != GOLIOTH_OK)
    {
        GLTH_LOGE(TAG, "Failed to parse manifest: %s", golioth_status_to_str(status));
        return;
    }

    if (data->manifest.num_components > 0) {
        k_sem_give(&data->manifest_received);
    }
}

int main(void)
{
    enum golioth_status status;
    struct ota_observe_data ota_observe_data = {};

    LOG_DBG("Start Golioth example_download_photo");
    LOG_INF("Firmware version: %s", _current_version);

    /* Get system thread id so loop delay change event can wake main */
    _system_thread = k_current_get();

    /* Start the network connection */
    net_connect();

    /* Get the client configuration from auto-loaded settings */
    const struct golioth_client_config *client_config = golioth_sample_credentials_get();

    /* Create and start a Golioth Client */
    client = golioth_client_create(client_config);

    /* Register Golioth event callback */
    golioth_client_register_event_callback(client, on_client_event, &ota_observe_data);

    /* Register Golioth Settings service */
    app_settings_register(client);

    /* Block until connected to Golioth */
    k_sem_take(&connected, K_FOREVER);

    status = golioth_ota_report_state_sync(client,
                                           GOLIOTH_OTA_STATE_IDLE,
                                           GOLIOTH_OTA_REASON_READY,
                                           "main",
                                           _current_version,
                                           NULL,
                                           GOLIOTH_SYS_WAIT_FOREVER);

    if (status != GOLIOTH_OK)
    {
        GLTH_LOGE(TAG, "Failed to report firmware state: %d", status);
    }

    status = GOLIOTH_ERR_NULL;
    uint32_t retry_delay_s = 5;

    k_sem_init(&ota_observe_data.manifest_received, 0, 1);

    LOG_INF("Registering manifest observation");

    while (status != GOLIOTH_OK)
    {
        status = golioth_ota_observe_manifest_async(client, on_ota_manifest, &ota_observe_data);
        if (status == GOLIOTH_OK)
        {
            break;
        }

        GLTH_LOGW(TAG,
                  "Failed to observe manifest, retry in %" PRIu32 "s: %d",
                  retry_delay_s,
                  status);

        golioth_sys_msleep(retry_delay_s * 1000);

        retry_delay_s = retry_delay_s * 2;

        if (retry_delay_s > CONFIG_GOLIOTH_OTA_OBSERVATION_RETRY_MAX_DELAY_S)
        {
            retry_delay_s = CONFIG_GOLIOTH_OTA_OBSERVATION_RETRY_MAX_DELAY_S;
        }
    }

    LOG_INF("Waiting for FW update");

    while (true)
    {
        k_sem_take(&ota_observe_data.manifest_received, K_FOREVER);

        LOG_INF("Received new manifest (num_components=%zu)", ota_observe_data.manifest.num_components);

        for (size_t i = 0; i < ota_observe_data.manifest.num_components; i++) {
            struct golioth_ota_component *component = &ota_observe_data.manifest.components[i];

            LOG_INF("component %zu: package=%s version=%s", i, component->package, component->version);
        }
    }
}
