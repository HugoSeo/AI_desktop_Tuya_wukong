#ifndef __COMMON_FUNCTION_H_
#define __COMMON_FUNCTION_H_

#include "tuya_cloud_types.h"

/**
 * @brief psram API:bk_psram_byte_write
 * @defgroup bk_api_psram PSRAM API group
 * @{
 */

/**
 * @brief     write one byte of data to psram
 *
 * @param addr the psram address to write to
 * @param value the value of one byte of data
 *
 * @return
 *    - NULL
 */
static inline void ty_psram_byte_write(unsigned char *addr, unsigned char value)
{
	uint32_t addr_align = (uint32_t)addr;
	uint32_t temp = 0;
	uint8_t index = 0;

	index = (addr_align & 0x3);
	addr_align &= 0xFFFFFFFC;
	temp = *((volatile uint32_t *)addr_align);
	switch (index)
	{
		case 0:
			temp &= ~(0xFF);
			temp |= value;
			break;
		case 1:
			temp &= ~(0xFF << 8);
			temp |= (value << 8);
			break;
		case 2:
			temp &= ~(0xFF << 16);
			temp |= (value << 16);
			break;
		case 3:
			temp &= ~(0xFF << 24);
			temp |= (value << 24);
		default:
			break;
	}

	*((volatile uint32_t *)addr_align) = temp;
}

/**
 * @brief     psram memory copy:bk_psram_word_memcpy
 *
 * @param dst_t the destination address to be copied to
 * @param src_t the source address to be copied
 * @param length length of the data copy
 *
 * @attation 1. length unit is byte
 *
 * @return
 *    - NULL
 */
static inline void ty_psram_word_memcpy(void *dst_t, void *src_t, unsigned int length)
{
	unsigned int *dst = (unsigned int*)dst_t, *src = (unsigned int*)src_t;
	int index = (unsigned int)dst & 0x03;
	int count = length >> 2, tail = length & 0x3;
	unsigned int tmp = 0;
	unsigned char *p, *pp = (unsigned char *)src;

	if (!index)
	{
		while (count--)
		{
			*dst++ = pp[0] | pp[1] << 8 | pp[2] << 16 | pp[3] << 24;
			pp += 4;
		}

		if (tail)
		{
			tmp = *dst;
			p = (unsigned char *)&tmp;

			while(tail--)
			{
				*p++ = *pp++;
			}
			*dst = tmp;
		}
	}
	else
	{
		unsigned int *pre_dst = (unsigned int*)((unsigned int)dst & 0xFFFFFFFC);
		unsigned int pre_count = 4 - index;
		tmp = *pre_dst;
		p = (unsigned char *)&tmp + index;

		if (pre_count > length) {
			pre_count = length;
		}

		while (pre_count--)
		{
			*p++ = *pp++;
			length--;
		}

		*pre_dst = tmp;

		if (length <= 0)
		{
			return;
		}

		dst = pre_dst + 1;
		count = length >> 2;
		tail = length & 0x3;

		while (count--)
		{
			*dst++ = pp[0] | pp[1] << 8 | pp[2] << 16 | pp[3] << 24;
			pp += 4;
		}

		if (tail)
		{
			tmp = *dst;
			p = (unsigned char *)&tmp;

			while(tail--)
			{
				*p++ = *pp++;
			}
			*dst = tmp;
		}
	}
}

/**
 * @brief 将文件读到psram，接口会依据文件大小分配psram空间，读到内容存在file_content里，
          外部使用完后，需调用psram_free释放file_content.
 * @param[in] file_path:文件路径 
 * @param[in] file_content:  
 * @return OPRT_OK 成功
 * @return < 0 失败
 */
OPERATE_RET cf_read_file_to_psram(char *file_path, char **file_content);

#endif
