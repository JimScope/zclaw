#ifndef BOARD_SPI_H
#define BOARD_SPI_H

#include "driver/spi_common.h"
#include "esp_err.h"

#define BOARD_SPI_HOST SPI2_HOST
#define BOARD_SPI_MAX_TRANSFER (240 * 16 * 2 + 64)

// Initialise the SPI2 bus shared by the display and the SD card. Idempotent.
esp_err_t board_spi_init(void);

#endif // BOARD_SPI_H
