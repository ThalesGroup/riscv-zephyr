/*
 * Copyright (c) 2017 Antmicro <www.antmicro.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <misc/printk.h>
#include <spi.h>
#include <soc.h>
#include <device.h>
#include "spi_fe310.h"


#define DEVICE_PM_ACTIVE_STATE 1
#define DEVICE_PM_LOW_POWER_STATE 2
#define DEVICE_PM_OFF_STATE 4
#define DEVICE_PM_SET_POWER_STATE 1
#define DEVICE_PM_GET_POWER_STATE 2
/* VAR MASKING */
#define OP_MODE(X)	(X & 1)
#define MODE(X)		((X >> 1) & 7)
#define TRANSFER(X)	((X >> 4) & 1)
#define WORD_SIZE(X)	((X >> 5) & 63)
#define LINES(X)	((X >> 11) & 3)
#define CS_HOLD(X)	((X >> 13) & 1)
#define LOCK_ON(X)	((X >> 14) & 1)
#define EEPROM(X)	((X >> 15) & 1)

struct spi_fe310_runtime {
	u32_t power_state;
};

struct spi_fe310_config {
};

static struct spi_fe310_runtime spi_fe310_mst_0_runtime;

static struct spi_fe310_config spi_fe310_mst_0_config;

void spi_fe310_configure(struct spi_config *config)
{
	u16_t oper = config->operation;

	SPI1_REG(SPI_REG_CSMODE) = SPI_CSMODE_AUTO;
	/* system clock is defined as in boards/hifive1/clock.c
	 *(16MHz) max bus frequency = 8MHz
	 */
	SPI1_REG(SPI_REG_SCKDIV) =
		(((u32_t)(8000000UL/config->frequency) - 1) & 4095);
	SPI1_REG(SPI_REG_SCKMODE) =
		MODE(oper) & 3;
	/* Mask only polarity and phase; no loop mode in FE310 */
	SPI1_REG(SPI_REG_CSID) = config->slave;
	SPI1_REG(SPI_REG_FMT) |= SPI_FMT_PROTO(LINES(oper))
				| SPI_FMT_ENDIAN(TRANSFER(oper))
				| SPI_FMT_LEN(WORD_SIZE(oper));
	if (CS_HOLD(oper)) {
		SPI1_REG(SPI_REG_CSMODE) = SPI_CSMODE_HOLD;
	} else {
		SPI1_REG(SPI_REG_CSMODE) = SPI_CSMODE_AUTO;
	}
}


void transfer(u8_t data)
{
	while (SPI1_REG(SPI_REG_TXFIFO) & SPI_TXFIFO_FULL) {
	}

	SPI1_REG(SPI_REG_TXFIFO) = data;
	/* Wait for TX watermark */
	while (!(SPI1_REG(SPI_REG_IP) & SPI_IP_TXWM)) {
	}
}

u8_t transfer_r(u8_t data)
{
	transfer(data);
	/* Wait for RX watermark */
	while (!(SPI1_REG(SPI_REG_IP) & SPI_IP_RXWM)) {
	}
	u8_t received = SPI1_REG(SPI_REG_RXFIFO) & 0xFF;
	return received;
}


static int spi_fe310_transceive(struct spi_config *config,
				 const struct spi_buf *tx_bufs,
				 size_t tx_count,
				 struct spi_buf *rx_bufs,
				 size_t rx_count)
{
	spi_fe310_configure(config);
	SPI1_REG(SPI_REG_CSMODE) = SPI_CSMODE_HOLD;
	/* Hold down CS pin during transmission */
	size_t rx_packet_index = 0;
	size_t tx_packet_index = 0;

	while (rx_packet_index < rx_count || tx_packet_index < tx_count) {
		size_t tx_buf_len;
		size_t rx_buf_len;
		u8_t *tx_buf = NULL;
		u8_t *rx_buf = NULL;

		if (tx_bufs != NULL) {
			tx_buf = (u8_t *)tx_bufs[tx_packet_index].buf;
			tx_buf_len = tx_bufs[tx_packet_index].len;
		} else {
			tx_buf_len = 0;
		}

		if (rx_bufs != NULL) {
			rx_buf = (u8_t *)rx_bufs[rx_packet_index].buf;
			rx_buf_len = rx_bufs[rx_packet_index].len;
		} else {
			rx_buf_len = 0;
		}

		size_t tx_index = 0;
		size_t rx_index = 0;

		if (tx_buf_len <= rx_buf_len) {
			for (int i = 0; i < tx_buf_len; i++) {
				rx_buf[rx_index] = transfer_r(tx_buf[tx_index]);
				tx_index++;
				rx_index++;
			}
			size_t difference = rx_buf_len - tx_buf_len;

			for (int i = 0; i < difference; i++) {
				rx_buf[rx_index] = transfer_r(0xFF);
				/* If receive buffer is longer
				 *than transmit buffer, send dummy bytes
				 */
				rx_index++;
			}
		} else {
			for (int i = 0; i < rx_buf_len; i++) {
				rx_buf[rx_index] = transfer_r(tx_buf[tx_index]);
				tx_index++;
				rx_index++;
			}
			size_t difference = tx_buf_len - rx_buf_len;

			for (int i = 0; i < difference; i++) {
				transfer_r(tx_buf[tx_index]);
				/* If transmit buffer is longer
				 * than receive buffer, drop bytes
				 */
				tx_index++;
			}
		}

		tx_packet_index++;
		rx_packet_index++;

	}

	SPI1_REG(SPI_REG_CSMODE) = SPI_CSMODE_AUTO;
	return 0;

}
static int spi_fe310_release(struct spi_config *config)
{
	SPI1_REG(SPI_REG_CSMODE) = SPI_CSMODE_AUTO; /* Remove chip hold */

	return 0;
}

static const struct spi_driver_api fe310_spi_api = {
	.transceive = spi_fe310_transceive,
	.release = spi_fe310_release,
};

static int spi_fe310_init(struct device *dev)
{

	struct spi_fe310_runtime *data =
				(struct spi_fe310_runtime *) dev->driver_data;

	/* Set up SPI controller
	 * SPI clock divider: determines the speed of SPI
	 * transfers. This cannot exceed 1.2Mhz for the SC18IS600.
	 * CPUfreq is set to 16Mhz in this demo.
	 * The formula is CPU_FREQ/2*(1+SPI_SCKDIV)
	 */
	SPI1_REG(SPI_REG_SCKDIV)    = 0;
	SPI1_REG(SPI_REG_TXCTRL)    = 0x01;
	SPI1_REG(SPI_REG_RXCTRL)    = 0x00;
	SPI1_REG(SPI_REG_IE)        = 0x03;
	SPI1_REG(SPI_REG_CSDEF) = 0xffff;
	/* CS is active-low */
	SPI1_REG(SPI_REG_CSMODE) = SPI_CSMODE_HOLD;
	/* hold CS where possible */
	SPI1_REG(SPI_REG_FMT) = SPI_FMT_PROTO(SPI_PROTO_S)
			| SPI_FMT_ENDIAN(SPI_ENDIAN_MSB)
			| SPI_FMT_DIR(SPI_DIR_RX)
			| SPI_FMT_LEN(8);


	data->power_state = DEVICE_PM_ACTIVE_STATE;

	return 0;
}

void spi_fe310_remove_pinmux(void)
{
	GPIO_REG(GPIO_IOF_EN) &= ~((1 << 2) | (1 << 3)
			| (1 << 4) | (1 << 5)
			| (1 << 9) | (1 << 10));
	GPIO_REG(GPIO_INPUT_EN) &= ~((1 << 2) | (1 << 3)
			| (1 << 4) | (1 << 5)
			| (1 << 9) | (1 << 10));
	GPIO_REG(GPIO_OUTPUT_EN) &= ~((1 << 2) | (1 << 3)
			| (1 << 4) | (1 << 5)
			| (1 << 9) | (1 << 10));
}

int spi_fe310_pm_control(struct device *dev, u32_t ctrl_command, void *context)
{
	struct spi_fe310_runtime *data =
			(struct spi_fe310_runtime *) dev->driver_data;

	u32_t target_power_state = *((u32_t *)context);

	if (ctrl_command == DEVICE_PM_SET_POWER_STATE) {
		if (data->power_state == target_power_state) {
		/* If called power state is equal
		 * to current state, do nothing
		 */
			return 0;
		}
		switch (target_power_state) {
		/* Driver implement only off and active states */
		case DEVICE_PM_OFF_STATE:
			spi_fe310_remove_pinmux();
			printk("Changed to off state\n");
			data->power_state = target_power_state;
			break;
		case DEVICE_PM_ACTIVE_STATE:
			printk("Changed to active state\n");
			data->power_state = target_power_state;
			break;
		}

		return 0;
	} else if (ctrl_command == DEVICE_PM_GET_POWER_STATE) {
		*((u32_t *)context) = data->power_state;
		return 0;
	} else {
		return -1;
	}
	return 0;
}

DEVICE_DEFINE(spi_master_0, CONFIG_SPI_0_NAME, spi_fe310_init,
		spi_fe310_pm_control, &spi_fe310_mst_0_runtime,
		&spi_fe310_mst_0_config, POST_KERNEL,
		CONFIG_SPI_INIT_PRIORITY, &fe310_spi_api);

