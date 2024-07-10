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
#include <zephyr/fs/fs.h>

#include <zephyr/drivers/display.h>
#include <lvgl.h>

#include <mbedtls/sha256.h>

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

enum golioth_status write_block(const struct golioth_ota_component *component,
                                uint32_t block_idx,
                                uint8_t *block_buffer,
                                size_t block_size,
                                bool is_last,
                                void *arg)
{
    const char *filename = component->package;
    struct fs_file_t fp = {};
    fs_mode_t flags = FS_O_CREATE | FS_O_WRITE;
    char path[32];
    int err;
    ssize_t ret;

    if (block_idx == 0) {
        flags |= FS_O_TRUNC;
    }

    sprintf(path, "/storage/%s", filename);

    err = fs_open(&fp, path, flags);
    if (err) {
        LOG_ERR("Failed to open %s: %d", filename, err);

        return err;
    }

    err = fs_seek(&fp, block_idx * CONFIG_GOLIOTH_BLOCKWISE_DOWNLOAD_BUFFER_SIZE, FS_SEEK_SET);
    if (err) {
        goto fp_close;
    }

    ret = fs_write(&fp, block_buffer, block_size);
    if (ret < 0) {
        goto fp_close;
    }

fp_close:
    fs_close(&fp);

    return GOLIOTH_OK;
}

static int greeting_show(void)
{
    mbedtls_sha256_context ctx;
    char hash[32];
    struct fs_file_t greeting_fp = {};
    int err;
    ssize_t ret;

    mbedtls_sha256_init(&ctx);

    err = mbedtls_sha256_starts(&ctx, 0);
    if (err) {
        return -EFAULT;
    }

    err = fs_open(&greeting_fp, "/storage/greeting", FS_O_READ);
    if (err) {
        LOG_ERR("Failed to open greeting: %d", err);

        return err;
    }

    while (true) {
        uint8_t buffer[16];

        ret = fs_read(&greeting_fp, buffer, sizeof(buffer));
        if (ret < 0) {
            err = ret;
            LOG_ERR("Failed to read: %d", err);
            goto greeting_close;
        }
        if (ret == 0) {
            break;
        }

        LOG_HEXDUMP_INF(buffer, ret, "greeting");

        err = mbedtls_sha256_update(&ctx, buffer, ret);
        if (err) {
            LOG_ERR("Failed to get update sha256: %d", err);
            goto greeting_close;
        }
    }

    err = mbedtls_sha256_finish(&ctx, hash);
    if (err) {
        LOG_ERR("Failed to finish sha256 hash: %d", err);
        return -EFAULT;
    }

    LOG_HEXDUMP_INF(hash, sizeof(hash), "hash");

greeting_close:
    fs_close(&greeting_fp);

    if (err) {
        return GOLIOTH_ERR_FAIL;
    }

    return 0;
}

static lv_img_dsc_t img_background;

static int background_show(void)
{
    char hash[32] = {};
    struct fs_dirent dirent;
    struct fs_file_t background_fp = {};
    lv_img_header_t *img_header;
    uint8_t *buffer;
    int err;
    ssize_t ret;

    err = fs_stat("/storage/background", &dirent);
    if (err) {
        if (err == -ENOENT) {
            LOG_WRN("No background image found on FS");
        } else {
            LOG_ERR("Failed to check/stat background image: %d", err);
        }

        return err;
    }

    LOG_INF("Background image file size: %zu", dirent.size);

    buffer = malloc(dirent.size);
    if (!buffer) {
        LOG_ERR("Failed to allocate memory");
        return -ENOMEM;
    }

    err = fs_open(&background_fp, "/storage/background", FS_O_READ);
    if (err) {
        LOG_WRN("Failed to load background: %d", err);
        goto buffer_free;
    }

    ret = fs_read(&background_fp, buffer, dirent.size);
    if (ret < 0) {
        LOG_ERR("Failed to read: %zd", ret);
        err = ret;
        goto background_close;
    }

    if (ret != dirent.size) {
        LOG_ERR("ret (%d) != dirent.size (%d)", (int) ret, (int) dirent.size);
        err = -EIO;
        goto background_close;
    }

    /* LOG_HEXDUMP_INF(buffer, ret, "background"); */

    err = mbedtls_sha256(buffer, dirent.size, hash, 0);
    if (err) {
        LOG_ERR("Failed to get update sha256: %d", err);
        goto background_close;
    }

    LOG_HEXDUMP_INF(hash, sizeof(hash), "hash");

    img_header = (void *)buffer;
    img_background.header = *img_header;
    img_background.data_size = dirent.size - sizeof(*img_header);
    img_background.data = &buffer[sizeof(*img_header)];

    lv_obj_t * background = lv_img_create(lv_scr_act());
    lv_img_set_src(background, &img_background);
    lv_obj_align(background, LV_ALIGN_CENTER, 0, 0);

background_close:
    fs_close(&background_fp);

buffer_free:
    free(buffer);

    return err;
}

int main(void)
{
    enum golioth_status status;
    struct ota_observe_data ota_observe_data = {};
    int err;

    LOG_DBG("Start Golioth example_download_photo");
    LOG_INF("Firmware version: %s", _current_version);

    greeting_show();
    background_show();

    char count_str[11] = {0};
    const struct device *display_dev;
    lv_obj_t *count_label;
    uint32_t count = 0;

    display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display_dev)) {
        LOG_ERR("Device not ready, aborting test");
        return 0;
    }

    count_label = lv_label_create(lv_scr_act());
    lv_obj_align(count_label, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_label_set_text(count_label, "0");

    lv_task_handler();
    display_blanking_off(display_dev);

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
        uint32_t count_cur = k_uptime_get() / 1000;
        if (count_cur != count) {
            count = count_cur;
            sprintf(count_str, "%d", (int) count);
            lv_label_set_text(count_label, count_str);
        }

        lv_task_handler();

        err = k_sem_take(&ota_observe_data.manifest_received, K_MSEC(10));
        if (!err) {
            LOG_INF("Received new manifest (num_components=%zu)", ota_observe_data.manifest.num_components);

            for (size_t i = 0; i < ota_observe_data.manifest.num_components; i++) {
                struct golioth_ota_component *component = &ota_observe_data.manifest.components[i];

                LOG_INF("component %zu: package=%s version=%s uri=%s hash=%s", i, component->package, component->version, component->uri, component->hash);

                status = golioth_ota_download_component(client, component, write_block, NULL);
            }
        }
    }
}
