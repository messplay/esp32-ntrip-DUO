/*
 * SD logger for RTCM data
 * This file is part of the ESP32-XBee distribution (https://github.com/nebkat/esp32-xbee).
 * Copyright (c) 2019 Nebojsa Cvetkovic.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include "sd_logger.h"
#include "config.h"

static const char *TAG = "SD_LOG";

static FILE *log_file = NULL;
static time_t day_start = 0;
static bool sd_enabled = false;
static sdmmc_card_t *card;

static void open_log_file() {
    time_t now;
    time(&now);
    struct tm tm_info;
    localtime_r(&now, &tm_info);

    struct tm day = tm_info;
    day.tm_hour = day.tm_min = day.tm_sec = 0;
    day_start = mktime(&day);

    char path[64];
    snprintf(path, sizeof(path), "/sdcard/%04d%02d%02d.rtcm",
             tm_info.tm_year + 1900, tm_info.tm_mon + 1, tm_info.tm_mday);

    log_file = fopen(path, "a");
    if (!log_file) {
        ESP_LOGE(TAG, "Could not open %s: %d %s", path, errno, strerror(errno));
        sd_enabled = false;
    }
}

void sd_logger_init() {
    if (!config_get_bool1(CONF_ITEM(KEY_CONFIG_SD_LOG_ACTIVE))) return;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    sdspi_slot_config_t slot_config = SDSPI_SLOT_CONFIG_DEFAULT();
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = false,
            .max_files = 3
    };

    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card: %s", esp_err_to_name(ret));
        return;
    }

    sd_enabled = true;
    open_log_file();
}

void sd_logger_write(const void *data, size_t length) {
    if (!sd_enabled || !log_file) return;

    time_t now;
    time(&now);
    if (now - day_start >= 24 * 60 * 60) {
        fclose(log_file);
        open_log_file();
        if (!sd_enabled) return;
    }

    size_t written = fwrite(data, 1, length, log_file);
    if (written != length) {
        ESP_LOGE(TAG, "Failed writing SD: %d %s", errno, strerror(errno));
    }
}
