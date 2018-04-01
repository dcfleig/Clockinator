deps_config := \
	/home/david/Github/esp-idf/components/app_trace/Kconfig \
	/home/david/Github/esp-idf/components/aws_iot/Kconfig \
	/home/david/Github/esp-idf/components/bt/Kconfig \
	/home/david/Github/esp-idf/components/driver/Kconfig \
	/home/david/Github/esp-idf/components/esp32/Kconfig \
	/home/david/Github/esp-idf/components/esp_adc_cal/Kconfig \
	/home/david/Github/esp-idf/components/ethernet/Kconfig \
	/home/david/Github/esp-idf/components/fatfs/Kconfig \
	/home/david/Github/esp-idf/components/freertos/Kconfig \
	/home/david/Github/esp-idf/components/heap/Kconfig \
	/home/david/Github/esp-idf/components/libsodium/Kconfig \
	/home/david/Github/esp-idf/components/log/Kconfig \
	/home/david/Github/esp-idf/components/lwip/Kconfig \
	/home/david/Github/esp-idf/components/mbedtls/Kconfig \
	/home/david/Github/esp-idf/components/openssl/Kconfig \
	/home/david/Github/esp-idf/components/pthread/Kconfig \
	/home/david/Github/esp-idf/components/spi_flash/Kconfig \
	/home/david/Github/esp-idf/components/spiffs/Kconfig \
	/home/david/Github/esp-idf/components/tcpip_adapter/Kconfig \
	/home/david/Github/esp-idf/components/wear_levelling/Kconfig \
	/home/david/Github/esp-idf/components/bootloader/Kconfig.projbuild \
	/home/david/Github/esp-idf/components/esptool_py/Kconfig.projbuild \
	/c/Users/david/Github/Clockinator/thing_code/main/Kconfig.projbuild \
	/home/david/Github/esp-idf/components/partition_table/Kconfig.projbuild \
	/home/david/Github/esp-idf/Kconfig

include/config/auto.conf: \
	$(deps_config)


$(deps_config): ;
