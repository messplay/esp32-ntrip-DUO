idf_component_register(SRCS "main.c"
		"config.c"
		"core_dump.c"
		"log.c"
		"interface/ntrip_util.c"
		"retry.c"
		"status_led.c"
		"stream_stats.c"
		"uart.c"
		"util.c"
		"web_server.c"
		"wifi.c"
		"interface/ntrip_server.c"
                "interface/ntrip_server_2.c"

                "sd_logger.c"

		"protocol/nmea.c"
        INCLUDE_DIRS "include")

spiffs_create_partition_image(www ../www FLASH_IN_PROJECT)
