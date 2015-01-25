/**
 * Serial wire debug interface
 */

#include "arm_cm4.h"
#include "swd.h"

typedef enum { SWD_INIT, SWD_READ, SWD_WRITE } cmd_type_t;

typedef struct {
    cmd_type_t command;
    uint8_t request;
    uint32_t data;
    uint32_t state; //written by interrupt when swd_busy = TRUE
} cmd_t;

//written only by the interrupt routine
static uint8_t swd_busy = FALSE;

//written only by the interface routines when swd_busy is FALSE
static cmd_t current_command;

/**
 * Starts the SWD bus
 */
static void swd_start_bus(uint8_t data_state);

/**
 * Changes the bus into an idle state, stopping the clock, setting data to input,
 * and resetting the busy state to FALSE
 */
static void swd_idle_bus(void);

/**
 * Handles the current command
 */
static void swd_handle_current_command(void);

void swd_init(void)
{
    //data is input for the moment
    SWD_DATA_IN;

    //set up ftm0 to generate 50% pwm at a relatively high frequency
    SIM_SCGC6 |= SIM_SCGC6_FTM0_MASK;//enable clock
    FTM0_QDCTRL = 0; //quaden = 0
    FTM0_SC = 0;
    FTM0_CNTIN = 0;
    FTM0_CNT = 0;
    FTM0_MOD = 32767;
    FTM0_C0SC = FTM_CnSC_MSB_MASK | FTM_CnSC_ELSB_MASK;
    FTM0_C0V = FTM0_MOD / 2; //50% duty cycle

    //set up PTC1 to ALT4, FTM0_CH0
    PORTC_PCR1 = PORT_PCR_MUX(4);

    //enable the ftm0 interrupt on the falling edge (channel match) so we can switch the data line
    FTM0_C0SC |= FTM_CnSC_CHIE_MASK;
    enable_irq(IRQ(INT_FTM0));

    swd_idle_bus();
}

int8_t swd_is_busy(void)
{
    return swd_busy;
}

int8_t swd_begin_init(void)
{
    if (swd_is_busy())
        return SWD_ERR_BUSY;

    cmd_t command = {
        .command = SWD_INIT
    };

    current_command = command;

    //start the command
    swd_handle_current_command();
    swd_start_clock();

    return SWD_OK;
}

int8_t swd_begin_write(uint8_t req, uint32_t data)
{
    return SWD_OK;
}

int8_t swd_begin_read(uint8_t req)
{
    return SWD_OK;
}

int8_t swd_get_last_read(uint32_t* dest)
{
    *dest = 0;
    return SWD_OK;
}

void FTM0_IRQHandler(void)
{
    if (FTM0_C0SC & FTM_CnSC_CHF_MASK)
    {
        //handle the current command
        swd_handle_current_command();

        //clear the interrupt flag
        FTM0_C0SC &= ~FTM_CnSC_CHF_MASK;
    }
}

static void swd_start_bus(uint8_t data_state)
{
    //set data to output
    SWD_DATA_OUT;
    if (data_state)
    {
        SWD_DATA_HIGH;
    }
    else
    {
        SWD_DATA_LOW;
    }

    FTM0_CNT = 0;
    //run the clock (system clock, prescaler 1)
    FTM0_SC = FTM_SC_CLKS(1) | FTM_SC_PS(0);
}

static void swd_idle_bus(void)
{
    //stop the clock
    FTM0_SC = 0;
    //set data to input
    SWD_DATA_IN;
    //we are no longer busy
    swd_busy = FALSE;
}

static void swd_handle_current_command(void)
{
    switch (current_command.command)
    {
    case SWD_INIT:
        break;
    default:
        //invalid command? stop the bus
        swd_idle_bus();
        break;
    }
}
