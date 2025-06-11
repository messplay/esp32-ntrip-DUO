#ifndef ESP32_XBEE_SD_LOGGER_H
#define ESP32_XBEE_SD_LOGGER_H

#include <stddef.h>

void sd_logger_init();
void sd_logger_write(const void *data, size_t length);

#endif //ESP32_XBEE_SD_LOGGER_H
