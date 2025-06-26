#ifndef ESP32_XBEE_SD_LOGGER_H
#define ESP32_XBEE_SD_LOGGER_H

#include <stddef.h>
#include <stdbool.h>

void sd_logger_init();
void sd_logger_write(const void *data, size_t length);
void sd_logger_set_active(bool active);
bool sd_logger_is_active();
bool sd_logger_is_card_mounted(); // New getter
void sd_logger_event_handler(void* handler_arg, esp_event_base_t base, int32_t id, void* event_data);

#endif //ESP32_XBEE_SD_LOGGER_H
