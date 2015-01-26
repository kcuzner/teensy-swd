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

#define SWD_CLK_MODE PORTA_PCR12=PORT_PCR_MUX(1)
#define SWD_CLK_HIGH GPIOA_PSOR=(1<<12)
#define SWD_CLK_LOW  GPIOA_PCOR=(1<<12)
#define SWD_CLK_OUT GPIOA_PDDR|=(1<<12)
#define SWD_DATA_MODE PORTA_PCR13=PORT_PCR_MUX(1)
#define SWD_DATA_IN  GPIOA_PDDR&=~(1<<13)
#define SWD_DATA_OUT GPIOA_PDDR|=(1<<13)
#define SWD_DATA_HIGH GPIOA_PSOR=(1<<13)
#define SWD_DATA_LOW  GPIOA_PCOR=(1<<13)
#define SWD_DATA_VALUE ((GPIOA_PDIR >> 13) & 0x1)

#define SWD_START_MASK  0x80
#define SWD_APnDP_MASK  0x40
#define SWD_RnW_MASK    0x20
#define SWD_ADDR_SHIFT  3
#define SWD_ADDR_MASK   (0x3 << SWD_ADDR_SHIFT)
#define SWD_ADDR(N)     ((N << SWD_ADDR_SHIFT) & SWD_ADDR_MASK)
#define SWD_PARITY_MASK 0x04
#define SWD_STOP_MASK   0x02
#define SWD_PARK_MASK   0x01

#define SWD_DP_READ_IDCODE (SWD_START_MASK | SWD_RnW_MASK | SWD_ADDR(0) | SWD_PARK_MASK)

#define SWD_OK        0  //request/response ok
#define SWD_ERR_BUSY  -1 //bus busy
#define SWD_ERR_WAIT  -2 //client WAIT response
#define SWD_ERR_FAULT -3 //client FAULT response
#define SWD_ERR_BUS   -4 //internal error while running

/**
 * Initializes the Serial Wire Debug driver using FTM0
 */
void swd_init(void);

/**
 * Returns TRUE if the swd interface is busy
 * @return SWD_OK or an error code
 */
int8_t swd_is_busy(void);

/**
 * Begins a bus init sequence, including mode switch from JTAG
 */
int8_t swd_begin_init(void);

/**
 * Begins a write sequence
 * @param req Request byte
 * @param data Data to send
 * @return SWD_OK or an error code
 */
int8_t swd_begin_write(uint8_t req, uint32_t data);

/**
 * Begins a read sequence
 * @param req Request byte
 * @return SWD_OK or an error code
 */
int8_t swd_begin_read(uint8_t req);

/**
 * Returns the slave response from the last command
 * @param dest Location to write the slave response (SWD_OK or an error code)
 * @return SWD_OK or an error code
 */
int8_t swd_get_last_response(int8_t* dest);

/**
 * Returns the last read data
 * @param dest Location to write the data
 * @return SWD_OK or an error code
 */
int8_t swd_get_last_read(uint32_t* dest);

#endif // _SWD_H_
