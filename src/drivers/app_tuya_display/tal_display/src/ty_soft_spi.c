#include "ty_soft_spi.h"
#include "tkl_system.h"
#include "tkl_gpio.h"

#define SOFT_SPI_DELAY 2

static void SPI_SendData(soft_spi_gpio_cfg_t *gpio_cfg, UINT8_T data)
{
	UINT8_T n;

	//in while loop, to avoid disable IRQ too much time, release it if finish one byte.
	TKL_ENTER_CRITICAL();
	for (n = 0; n < 8; n++) {
		if (data & 0x80) {
			tkl_gpio_write(gpio_cfg->spi_sda, TUYA_GPIO_LEVEL_HIGH);
		}else {
			tkl_gpio_write(gpio_cfg->spi_sda, TUYA_GPIO_LEVEL_LOW);
		}

		tkl_system_sleep_us(SOFT_SPI_DELAY);
		data <<= 1;

		tkl_gpio_write(gpio_cfg->spi_clk, TUYA_GPIO_LEVEL_LOW);
		tkl_system_sleep_us(SOFT_SPI_DELAY);
		tkl_gpio_write(gpio_cfg->spi_clk, TUYA_GPIO_LEVEL_HIGH);
		tkl_system_sleep_us(SOFT_SPI_DELAY);

	}
	TKL_EXIT_CRITICAL();
}

OPERATE_RET ty_soft_spi_init(soft_spi_gpio_cfg_t *gpio_cfg)
{
    TUYA_GPIO_BASE_CFG_T cfg = {.direct = TUYA_GPIO_OUTPUT, .level = TUYA_GPIO_LEVEL_HIGH};
    OPERATE_RET ret = tkl_gpio_init(gpio_cfg->spi_clk, &cfg);
    ret |= tkl_gpio_init(gpio_cfg->spi_sda, &cfg);
    ret |= tkl_gpio_init(gpio_cfg->spi_csx, &cfg);

    return ret;
}

void ty_soft_spi_write_cmd(soft_spi_gpio_cfg_t *gpio_cfg, UINT8_T cmd)
{
	tkl_gpio_write(gpio_cfg->spi_csx, TUYA_GPIO_LEVEL_LOW);
	tkl_system_sleep_us(SOFT_SPI_DELAY);
	tkl_gpio_write(gpio_cfg->spi_sda, TUYA_GPIO_LEVEL_LOW);
	tkl_system_sleep_us(SOFT_SPI_DELAY);

	tkl_gpio_write(gpio_cfg->spi_clk, TUYA_GPIO_LEVEL_LOW);
	tkl_system_sleep_us(SOFT_SPI_DELAY);
	tkl_gpio_write(gpio_cfg->spi_clk, TUYA_GPIO_LEVEL_HIGH);
	tkl_system_sleep_us(SOFT_SPI_DELAY);

	SPI_SendData(gpio_cfg, cmd);

	tkl_gpio_write(gpio_cfg->spi_csx, TUYA_GPIO_LEVEL_HIGH);
	tkl_system_sleep_us(SOFT_SPI_DELAY);
}

void ty_soft_spi_write_data(soft_spi_gpio_cfg_t *gpio_cfg, UINT8_T data)
{
	tkl_gpio_write(gpio_cfg->spi_csx, TUYA_GPIO_LEVEL_LOW);
	tkl_system_sleep_us(SOFT_SPI_DELAY);
	tkl_gpio_write(gpio_cfg->spi_sda, TUYA_GPIO_LEVEL_HIGH);
	tkl_system_sleep_us(SOFT_SPI_DELAY);

	tkl_gpio_write(gpio_cfg->spi_clk, TUYA_GPIO_LEVEL_LOW);
	tkl_system_sleep_us(SOFT_SPI_DELAY);
	tkl_gpio_write(gpio_cfg->spi_clk, TUYA_GPIO_LEVEL_HIGH);
	tkl_system_sleep_us(SOFT_SPI_DELAY);

	SPI_SendData(gpio_cfg, data);

	tkl_gpio_write(gpio_cfg->spi_csx, TUYA_GPIO_LEVEL_HIGH);
	tkl_system_sleep_us(SOFT_SPI_DELAY);
}




