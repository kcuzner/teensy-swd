/**
 * Serial Wire Debug interface
 *
 * This operates asynchronously via one of the FlexTimer modules.
 *
 * The program can queue an operation to be performed by calling one of the
 * swd_begin_* methods. The operation is queued into the internal queue to be
 * executed at the next possible opportunity. If the queue is full, an
 * SWD_ERR_BUSY will be returned.
 *
 * Each swd_begin_* command takes in a pointer to a swd_result_t struct. This
 * struct will be written by the SWD module to indicate the individual command
 * completion status and result.
 */

#ifndef _SWD_H_
#define _SWD_H_

#include "arm_cm4.h"

#define SWD_CLK_MODE PORTD_PCR7=PORT_PCR_MUX(1) | PORT_PCR_DSE_MASK
#define SWD_DIO_MODE PORTD_PCR3=(PORT_PCR_MUX(1) | PORT_PCR_PE_MASK | PORT_PCR_PS_MASK)
#define SWD_GPIO    PTD_BASE_PTR
#define SWD_CLK_PIN 7 //pin 5
#define SWD_DIO_PIN 3 //pin 8

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

#define SWD_QUEUE_LENGTH 64

#define SWD_OK        0  //request/response ok
#define SWD_ERR       -1
#define SWD_ERR_BUSY  -2 //bus busy
#define SWD_ERR_WAIT  -3 //client WAIT response
#define SWD_ERR_FAULT -4 //client FAULT response
#define SWD_ERR_BUS   -5 //internal error while running

#define SWD_DONE 1

/**
 * Holds information about the result of a command
 */
typedef struct {
    /*
     * Written by the SWD module to SWD_DONE once the command is completed
     */
    uint8_t done;
    /*
     * Written by the SWD module to the result of the command once it is completed, before done is written
     */
    int8_t result;
    /*
     * Written by the SWD module to the read data for this command once it is completed, before done is written
     */
    uint32_t data;
} swd_result_t;

/**
 * Initializes the Serial Wire Debug driver using FTM0
 */
void swd_init(void);

/**
 * Begins a write sequence
 * @param req Request byte
 * @param data Data to send
 * @return SWD_OK or an error code
 */
int8_t swd_begin_write(uint8_t req, uint32_t data, swd_result_t* res);

/**
 * Begins a read sequence
 * @param req Request byte
 * @return SWD_OK or an error code
 */
int8_t swd_begin_read(uint8_t req, swd_result_t* res);

#endif // _SWD_H_
