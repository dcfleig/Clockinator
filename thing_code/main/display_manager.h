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

enum dm_BANK_SELECT
{
    LEFT,
    RIGHT
};

char *
dm_getLeftBank();
void dm_setLeftBank(const char *leftBank);

char *dm_getRightBank();
void dm_setRightBank(const char *leftBank);

uint8_t dm_getBrightness();
void dm_setBrightness(uint8_t brightness);

void dm_flash();

void dm_clear();
void dm_init();

void _dm_spiInit();
void _dm_spiTransfer(volatile uint8_t opcode, volatile uint8_t data);
void _dm_setChar(int digit, const char value, bool dp);
void _dm_display(const char *message, int startPosition);

#endif