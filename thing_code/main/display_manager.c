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

#include "display_manager.h"

#define SPI_MOSI_PIN 21
#define SPI_CLK_PIN 16
#define SPI_CS_PIN 17

static const char *TAG = "display_manager";
spi_device_handle_t m_handle;
spi_host_device_t m_host = HSPI_HOST;
uint8_t maxDevices = 1;
#define DISPLAY_LENGTH 8

//the opcodes for the MAX7221 and MAX7219
#define OP_NOOP 0x00
#define OP_DIGIT0 0x01
#define OP_DIGIT1 0x02
#define OP_DIGIT2 0x03
#define OP_DIGIT3 0x04
#define OP_DIGIT4 0x05
#define OP_DIGIT5 0x06
#define OP_DIGIT6 0x07
#define OP_DIGIT7 0x08
#define OP_DECODEMODE 0x09
#define OP_INTENSITY 0x0a
#define OP_SCANLIMIT 0x0b
#define OP_SHUTDOWN 0x0c
#define OP_DISPLAYTEST 0x0f

char _leftBankChars[9] = "";
char _rightBankChars[9] = "";
uint8_t _brightness = 10;

/*
 * Segments to be switched on for characters and digits on
 * 7-Segment Displays
 */
const static uint8_t charTable[] = {
    0b01111110, 0b00110000, 0b01101101,
    0b01111001, 0b00110011, 0b01011011, 0b01011111, 0b01110000, 0b01111111,
    0b01111011, 0b01110111, 0b00011111, 0b00001101, 0b00111101, 0b01001111,
    0b01000111, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,
    0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,
    0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,
    0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,
    0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b10000000,
    0b00000001, 0b10000000, 0b00000000, 0b01111110, 0b00110000, 0b01101101,
    0b01111001, 0b00110011, 0b01011011, 0b01011111, 0b01110000, 0b01111111,
    0b01111011, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,
    0b00000000, 0b00000000, 0b01110111, 0b00011111, 0b00001101, 0b00111101,
    0b01001111, 0b01000111, 0b00000000, 0b00110111, 0b00000000, 0b00000000,
    0b00000000, 0b00001110, 0b00000000, 0b00000000, 0b00000000, 0b01100111,
    0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,
    0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,
    0b00000000, 0b00000000, 0b00001000, 0b00000000, 0b01110111, 0b00011111,
    0b00001101, 0b00111101, 0b01001111, 0b01000111, 0b00000000, 0b00110111,
    0b00000000, 0b00000000, 0b00000000, 0b00001110, 0b00000000, 0b00010101,
    0b00011101, 0b01100111, 0b00000000, 0b00000000, 0b00000000, 0b00000000,
    0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,
    0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000};

char *dm_getLeftBank() { return _leftBankChars; };
void dm_setLeftBank(const char *leftBank)
{
    strcpy(_leftBankChars, leftBank);
    ESP_LOGI(TAG, "Displaying %s on the left back.", _leftBankChars);
    _dm_display(_leftBankChars, 1);
};

char *dm_getRightBank() { return _rightBankChars; };
void dm_setRightBank(const char *rightBank)
{
    strcpy(_rightBankChars, rightBank);
    ESP_LOGI(TAG, "Displaying %s on the right back.", _rightBankChars);
    _dm_display(_rightBankChars, 5);
};

uint8_t dm_getBrightness() { return _brightness; };
void dm_setBrightness(uint8_t brightness)
{
    _brightness = brightness;

    if (_brightness > 15)
        _brightness = 16;
    ESP_LOGI(TAG, "Setting brightness to %d", _brightness);
    _dm_spiTransfer(OP_INTENSITY, _brightness);
};

void dm_flash();

void dm_clear()
{
    ESP_LOGI(TAG, "Clearing the entire display");
    _dm_display("        ", 1);
};

void dm_init()
{
    _dm_spiInit();
    _dm_spiTransfer(OP_DISPLAYTEST, 0);
    //scanlimit is set to max on startup
    _dm_spiTransfer(OP_SCANLIMIT, 7);
    //decode is done in source
    _dm_spiTransfer(OP_DECODEMODE, 0);
    _dm_spiTransfer(OP_SHUTDOWN, 1);
};

void _dm_display(const char *message, int startPosition)
{
    int currentPosition = startPosition;
    bool setDot = false;
    // 0.123
    for (int i = 0; i < strlen(message); i++)
    {
        if (message[i] == '\0')
            return;
        if ((currentPosition < 0) || (currentPosition > DISPLAY_LENGTH) || (message[i] == 'z'))
        {
            currentPosition++;
            continue;
        }

        if ((i != strlen(message)) && (message[i + 1] == '.'))
        {
            setDot = true;
        }

        _dm_setChar(DISPLAY_LENGTH - currentPosition, message[i], setDot);
        if (setDot && message[i] != '.')
        {
            i++;
        }
        setDot = false;
        currentPosition++;
    }
}

void _dm_spiTransfer(volatile uint8_t opcode, volatile uint8_t data)
{
    //Create an array with the data to shift out
    int offset = 0 * 2;
    int maxbytes = 2;

    /* The array for shifting the data to the devices */
    uint8_t spidata[16];

    for (int i = 0; i < maxbytes; i++)
    {
        spidata[i] = (uint8_t)0;
    }
    //put our device data into the array
    spidata[offset] = opcode;
    spidata[offset + 1] = data;
    //spi->transfer(spidata, maxbytes);

    spi_transaction_t trans_desc;
    trans_desc.flags = 0;
    trans_desc.length = maxbytes * 8;
    trans_desc.rxlength = 0;
    trans_desc.tx_buffer = spidata;
    trans_desc.rx_buffer = spidata;

    //ESP_LOGI(tag, "... Transferring");
    esp_err_t rc = spi_device_transmit(m_handle, &trans_desc);
    if (rc != ESP_OK)
    {
        ESP_LOGE(TAG, "transfer:spi_device_transmit: %d", rc);
    }
}

void _dm_setChar(int digit, const char value, bool dp)
{
    int addr = 0;
    uint8_t index, v;

    if (addr < 0 || addr >= maxDevices)
    {
        return;
    }
    if (digit < 0 || digit > 7)
    {
        return;
    }
    index = (uint8_t)value;
    if (index > 127)
    {
        //no defined beyond index 127, so we use the space char
        index = 32;
    }
    v = charTable[index];
    if (dp)
    {
        v |= 0b10000000;
    }

    _dm_spiTransfer(digit + 1, v);
}

void _dm_spiInit()
{
    //Initialize non-SPI GPIOs
    gpio_set_direction(SPI_MOSI_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(SPI_CLK_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(SPI_CS_PIN, GPIO_MODE_OUTPUT);

    ESP_LOGD(TAG, "init: mosi=%d, miso=%d, clk=%d, cs=%d", SPI_MOSI_PIN, -1, SPI_CLK_PIN, SPI_CS_PIN);

    spi_bus_config_t bus_config;
    bus_config.sclk_io_num = SPI_CLK_PIN;  // CLK
    bus_config.mosi_io_num = SPI_MOSI_PIN; // MOSI
    bus_config.miso_io_num = -1;           // MISO
    bus_config.quadwp_io_num = -1;         // Not used
    bus_config.quadhd_io_num = -1;         // Not used
    bus_config.max_transfer_sz = 0;        // 0 means use default.

    ESP_LOGI(TAG, "... Initializing bus; host=%d", m_host);

    esp_err_t errRc = spi_bus_initialize(
        m_host,
        &bus_config,
        1 // DMA Channel
        );

    if (errRc != ESP_OK)
    {
        ESP_LOGE(TAG, "spi_bus_initialize(): rc=%d", errRc);
        abort();
    }

    spi_device_interface_config_t dev_config;
    dev_config.address_bits = 0;
    dev_config.command_bits = 0;
    dev_config.dummy_bits = 0;
    dev_config.mode = 0;
    dev_config.duty_cycle_pos = 0;
    dev_config.cs_ena_posttrans = 0;
    dev_config.cs_ena_pretrans = 0;
    dev_config.clock_speed_hz = 100000;
    dev_config.spics_io_num = SPI_CS_PIN;
    dev_config.flags = 0;
    dev_config.queue_size = 1;
    dev_config.pre_cb = NULL;
    dev_config.post_cb = NULL;

    ESP_LOGI(TAG, "... Adding device bus.");
    errRc = spi_bus_add_device(m_host, &dev_config, &m_handle);
    if (errRc != ESP_OK)
    {
        ESP_LOGE(TAG, "spi_bus_add_device(): rc=%d", errRc);
        abort();
    }
}
