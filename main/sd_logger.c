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
#include <esp_event.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include "sd_logger.h"
#include "config.h"
#include "uart.h" // Required for UART_EVENT_READ

static const char *TAG = "SD_LOG";

static FILE *log_file = NULL;
static time_t day_start = 0;
static bool sd_card_mounted = false; // Renamed from sd_enabled to clarify it means card is usable
static bool sd_logging_dynamically_enabled = false; // For runtime toggle
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
        // sd_card_mounted remains true if card itself is fine, but file opening failed.
        // sd_logging_dynamically_enabled might still be true, but writes will fail.
    } else {
        ESP_LOGI(TAG, "Opened log file: %s", path);
    }
}

void sd_logger_set_active(bool active) {
    sd_logging_dynamically_enabled = active;
    ESP_LOGI(TAG, "SD logging %s", active ? "enabled" : "disabled");
}

bool sd_logger_is_active() {
    return sd_logging_dynamically_enabled && sd_card_mounted;
}

bool sd_logger_is_card_mounted() {
    return sd_card_mounted;
}

void sd_logger_event_handler(void* handler_arg, esp_event_base_t base, int32_t id, void* event_data) {
    if (base == UART_EVENT_READ && event_data != NULL) {
        // The event_data for UART_EVENT_READ is a pointer to the buffer.
        // The id for UART_EVENT_READ is the length of the data.
        // However, the uart_event_post in uart.c passes the buffer itself as event_data
        // and length as event_id. Let's assume event_data is `uint8_t**` and id is `size_t`.
        // No, checking uart.c: esp_event_post(UART_EVENT_READ, len, &buffer, len, portMAX_DELAY);
        // So, id is 'len', event_data is 'buffer'.
        // And the signature for esp_event_handler_t is (void* handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
        // So, event_id is len, and event_data is pointer to buffer.

        uint8_t *data = (uint8_t *) event_data; // Data buffer
        size_t length = (size_t) id;           // Data length

        if (sd_logger_is_active()) {
            sd_logger_write(data, length);
        }
    }
}

void sd_logger_init() {
    // Initialize sd_logging_dynamically_enabled from persistent config first
    sd_logging_dynamically_enabled = config_get_bool1(CONF_ITEM(KEY_CONFIG_SD_LOG_ACTIVE));
    ESP_LOGI(TAG, "Initial SD logging state from config: %s", sd_logging_dynamically_enabled ? "enabled" : "disabled");

    // Try to mount SD card regardless of the KEY_CONFIG_SD_LOG_ACTIVE,
    // so user can enable it later at runtime if card is present.
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    sdspi_slot_config_t slot_config = SDSPI_SLOT_CONFIG_DEFAULT();
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = false,
            .max_files = 3
    };

    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card: %s. SD logging will be unavailable.", esp_err_to_name(ret));
        sd_card_mounted = false;
        // sd_logging_dynamically_enabled might be true from config, but operations will fail.
    } else {
        ESP_LOGI(TAG, "SD card mounted successfully.");
        sd_card_mounted = true;
        if (sd_logging_dynamically_enabled) { // Only open file if enabled at init
            open_log_file();
        }
    }

    // Register event handler for UART data
    ESP_ERROR_CHECK(esp_event_handler_register(UART_EVENT_READ, ESP_EVENT_ANY_ID, &sd_logger_event_handler, NULL));
    ESP_LOGI(TAG, "Registered UART_EVENT_READ handler for SD logging.");
}

void sd_logger_write(const void *data, size_t length) {
    // This function is now primarily called by the event handler.
    // The main checks (sd_logger_is_active()) are done in the event handler before calling this.
    // We still need to check log_file here as open_log_file can fail or might not have been called.

    if (!sd_card_mounted) { // If card isn't there, definitely can't write.
        // ESP_LOGV(TAG, "Cannot write to SD: card not mounted."); // Too noisy for verbose
        return;
    }

    // sd_logging_dynamically_enabled is checked by the caller (event_handler via sd_logger_is_active)

    if (!log_file) {
        // Attempt to open log file if it's not open.
        // This could happen if logging was enabled dynamically after init,
        // or if a previous open_log_file failed.
        if (sd_logging_dynamically_enabled) { // Only try to open if we are supposed to be logging
            ESP_LOGI(TAG, "Log file not open. Attempting to open for write operation.");
            open_log_file();
            if (!log_file) { // If still not open, can't write.
                ESP_LOGW(TAG, "Cannot write to SD: Log file is not open even after attempt.");
                return;
            }
        } else {
            // Not supposed to be logging, and file isn't open. Nothing to do.
            return;
        }
    }

    time_t now;
    time(&now);

    // Check if day_start is initialized. If not, it means a file was never opened successfully.
    if (day_start == 0 && log_file) {
        // This case might occur if logging was off, then turned on, and open_log_file succeeded.
        // We need to initialize day_start correctly.
        struct tm tm_info;
        localtime_r(&now, &tm_info);
        struct tm day = tm_info;
        day.tm_hour = day.tm_min = day.tm_sec = 0;
        day_start = mktime(&day);
        ESP_LOGI(TAG, "day_start initialized during write operation.");
    }

    if (log_file && day_start != 0 && (now - day_start >= 24 * 60 * 60)) {
        ESP_LOGI(TAG, "Midnight passed. Rotating log file.");
        fclose(log_file);
        log_file = NULL;
        open_log_file(); // This will update day_start
        if (!log_file) {
            ESP_LOGE(TAG, "Failed to open new log file after midnight. SD Logging will be paused until file can be opened.");
            return;
        }
    }

    if (!log_file) { // Final check if log_file is valid before writing
        ESP_LOGW(TAG, "Cannot write to SD: Log file became null before fwrite.");
        return;
    }

    size_t written = fwrite(data, 1, length, log_file);
    if (written != length) {
        ESP_LOGE(TAG, "Failed writing SD: %d %s", errno, strerror(errno));
    }
}
