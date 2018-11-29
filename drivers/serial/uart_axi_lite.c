#include <kernel.h>
#include <arch/cpu.h>
#include <uart.h>
#include <sys_io.h>
#include <board.h>

#define DEV_CFG(dev)					\
	((const struct uart_device_config * const)	\
	 (dev)->config->config_info)

#define RX_FIFO_REGISTER_OFFSET 0x0
#define TX_FIFO_REGISTER_OFFSET 0x4
#define STATUS_REGISTER_OFFSET 0x8
#define CTRL_REGISTER_OFFSET 0xC

#define TX_FIFO_FULL (1u << 3)
#define RX_VALID (1u << 0)

#define TX_FIFO_RESET (1u << 0)
#define RX_FIFO_RESET (1u << 1)

static unsigned char uart_axi_lite_poll_out(struct device *dev,
					      unsigned char c)
{
	while(sys_read8(DEV_CFG(dev)->regs + STATUS_REGISTER_OFFSET) == TX_FIFO_FULL)
		;

	sys_write8(c, DEV_CFG(dev)->regs + TX_FIFO_REGISTER_OFFSET);
	return c;
}

static int uart_axi_lite_poll_in(struct device *dev, unsigned char *c)
{
	/* reading from an empy FIFO triggers BUS ERROR, so we need
	   to check if there is anything to read */
	if(!(sys_read8(DEV_CFG(dev)->regs + STATUS_REGISTER_OFFSET) & RX_VALID))
		return -EBUSY;

	*c = sys_read8(DEV_CFG(dev)->regs + RX_FIFO_REGISTER_OFFSET);
	return 0;
}

static int uart_axi_lite_init(struct device *dev)
{
	/* reset TX and RX FIFOs */
	sys_write8(TX_FIFO_RESET | RX_FIFO_RESET, DEV_CFG(dev)->regs + CTRL_REGISTER_OFFSET);
	return 0;
}


static const struct uart_driver_api uart_axi_lite_driver_api = {
	.poll_in = uart_axi_lite_poll_in,
	.poll_out = uart_axi_lite_poll_out,
	.err_check = NULL,
};

static const struct uart_device_config uart_axi_lite_dev_cfg_0 = {
	.regs = AXI_LITE_UART_BASE,
};

DEVICE_AND_API_INIT(uart_axi_lite_0, "uart0",
		    uart_axi_lite_init, NULL,
		    &uart_axi_lite_dev_cfg_0,
		    PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		    (void *)&uart_axi_lite_driver_api);

