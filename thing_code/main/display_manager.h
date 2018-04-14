#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "driver/spi_master.h"
#include "soc/gpio_struct.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

#include "app_constants.h"

typedef enum {
    LEFT = 0,
    RIGHT
} dm_BANK_SELECT;

char *dm_getBankChars(dm_BANK_SELECT bank);
void dm_setBankChars(dm_BANK_SELECT bank, const char *bankChars);
char *dm_getBankSource(dm_BANK_SELECT bank);
void dm_setBankSource(dm_BANK_SELECT bank, const char *bankSource);

int8_t dm_getBrightness();
void dm_setBrightness(const int8_t brightness);

void dm_flash();

void dm_clear();
void dm_clear_bank(dm_BANK_SELECT bank);
void dm_init();

void _dm_spiInit();
void _dm_spiTransfer(volatile uint8_t opcode, volatile uint8_t data);
void _dm_setChar(int digit, const char value, bool dp);
void dm_display(const char *message, int startPosition);

void dm_led_test();
void dm_set_led(int digit, int ledArray);

#endif