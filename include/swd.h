/**
 * Serial Wire Debug interface
 *
 * This operates asynchronously via one of the FlexTimer modules.
 *
 * The program can queue an operation to be performed by calling one of the
 * swd_begin_* methods. Once an operation is in progress, the swd_is_busy
 * function will return TRUE. Once the operation completes, the swd_is_busy
 * function returns FALSE. If an operation is in progress when a call to
 * one of the swd_begin_* functions occurs, the function will return the
 * SWD_ERR_BUSY value. Otherwise, SWD_OK should be returned from any functions.
 *
 * For reads specifically, once the read completes, the swd_get_last_read
 * function can be used to read the last returned value. If a read has not been
 * performed, the dest value may be filled with garbage.
 */

#ifndef _SWD_H_
#define _SWD_H_

#include "arm_cm4.h"

#define SWD_CLK_HIGH GPIOA_PSOR=(1<<12)
#define SWD_CLK_LOW  GPIOA_PCOR=(1<<12)
#define SWD_CLK_OUT GPIOA_PDDR|=(1<<12)
#define SWD_DATA_IN  GPIOA_PDDR&=~(1<<13)
#define SWD_DATA_OUT GPIOA_PDDR|=(1<<13)
#define SWD_DATA_HIGH GPIOA_PSOR=(1<<13)
#define SWD_DATA_LOW  GPIOA_PCOR=(1<<13)

#define SWD_OK        0
#define SWD_ERR_BUSY  -1

void swd_init(void);

int8_t swd_is_busy(void);

int8_t swd_begin_init(void);

int8_t swd_begin_write(uint8_t req, uint32_t data);

int8_t swd_begin_read(uint8_t req);

int8_t swd_get_last_read(uint32_t* dest);

#endif // _SWD_H_
