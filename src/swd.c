/**
 * Serial wire debug interface
 */

/**
 * How this works:
 * Calling swd_init sets up the FTM to generate interrupts on timer overflow
 * and on channel 1 match. Channel 1 is set up to match on FTM_MOD/2. The bus
 * state variable is set to SWD_BUS_IDLE. The data is set to input and floats
 * high. The clock is set to output and held high
 *
 * During the overflow interrupt, if the bus state is not SWD_BUS_IDLE, the
 * clock will be brought high. In addition, the handle_queue function will be
 * called always.
 *
 * During the match interrupt, if the bus state is not SWD_BUS_IDLE, the clock
 * will be brought low.
 *
 * The handle_queue function
 */

#include "arm_cm4.h"
#include "swd.h"

#define SWD_INIT_KEY 0x79e7
#define SWD_INIT_STATE_RESET0 50
#define SWD_INIT_STATE_SWITCH (SWD_INIT_STATE_RESET0 + 16)
#define SWD_INIT_STATE_RESET1 (SWD_INIT_STATE_SWITCH + 50)

#define SWD_RESP_OK    0b001
#define SWD_RESP_WAIT  0b010
#define SWD_RESP_FAULT 0b100

#define SWD_READ_STATE_REQ    8
#define SWD_READ_STATE_TM0    (SWD_READ_STATE_REQ + 1)
#define SWD_READ_STATE_RESP   (SWD_READ_STATE_TM0 + 3)
#define SWD_READ_STATE_READ   (SWD_READ_STATE_RESP + 32)
#define SWD_READ_STATE_PARITY (SWD_READ_STATE_READ + 1)
#define SWD_READ_STATE_TM1    (SWD_READ_STATE_PARITY + 1)
#define SWD_READ_STATE_FINISH (SWD_READ_STATE_TM1 + 8)

#define SWD_WRITE_STATE_REQ    8
#define SWD_WRITE_STATE_TM0    (SWD_WRITE_STATE_REQ + 1)
#define SWD_WRITE_STATE_RESP   (SWD_WRITE_STATE_TM0 + 3)
#define SWD_WRITE_STATE_TM1    (SWD_WRITE_STATE_RESP + 1)
#define SWD_WRITE_STATE_DATA   (SWD_WRITE_STATE_TM1 + 32)
#define SWD_WRITE_STATE_PARITY (SWD_WRITE_STATE_DATA + 1)
#define SWD_WRITE_STATE_FINISH (SWD_WRITE_STATE_PARITY + 8)

#define NEXT(I) (I + 1)
#define PREV(I) (I - 1)
#define NEXT_INDEX(S, I) (I >= (S) ? 0 : NEXT(I))

typedef enum { SWD_INIT, SWD_READ, SWD_WRITE, SWD_IDLE } cmd_type_t;

typedef struct {
    cmd_type_t command;
    swd_result_t* result; //written with the result of the command
    uint8_t request;
    uint32_t data;
    uint32_t state; //written by interrupt when swd_busy = TRUE
} cmd_t;

//written only by the interface routines when swd_busy is FALSE
static cmd_t current_command;

static cmd_t cmd_queue[SWD_QUEUE_LENGTH];
static uint32_t cmd_in = 0;
static uint32_t cmd_out = 0;

static uint8_t swd_queue_empty(void);
static uint8_t swd_queue_full(void);
static uint8_t swd_queue_cmd(const cmd_t* cmd);
static uint8_t swd_dequeue_cmd(cmd_t* dest);

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

static void swd_handle_init(void);
static void swd_handle_read(void);
static void swd_handle_write(void);

void swd_init(void)
{
    //set up data and clock for GPIO
    SWD_CLK_MODE;
    SWD_DATA_MODE;

    //data is input for the moment
    SWD_CLK_OUT;
    SWD_DATA_IN;

    //set up ftm0 to generate 50% pwm at a relatively high frequency
    SIM_SCGC6 |= SIM_SCGC6_FTM0_MASK;//enable clock
    FTM0_QDCTRL = 0; //quaden = 0
    FTM0_SC = 0;
    FTM0_CNTIN = 0;
    FTM0_CNT = 0;
    FTM0_MOD = 1024;
    FTM0_C0SC = FTM_CnSC_MSB_MASK | FTM_CnSC_ELSB_MASK;
    FTM0_C0V = FTM0_MOD / 2; //50% duty cycle

    //enable the ftm0 interrupt on the falling edge (channel match) so we can switch the data line
    FTM0_C0SC |= FTM_CnSC_CHIE_MASK;
    enable_irq(IRQ(INT_FTM0));

    swd_idle_bus();
}

int8_t swd_begin_init(swd_result_t* res)
{
    cmd_t command = {
        .command = SWD_INIT,
        .result = res
    };

    return swd_queue_cmd(&command);

    return SWD_OK;
}

int8_t swd_begin_write(uint8_t req, uint32_t data, swd_result_t* res)
{
    return SWD_OK;
}

int8_t swd_begin_read(uint8_t req, swd_result_t* res)
{
    return SWD_OK;
}

void FTM0_IRQHandler(void)
{
    if (FTM0_SC & FTM_SC_TOF_MASK)
    {
        //clock is now high
        SWD_CLK_HIGH;

        //clear the interrupt flag
        FTM0_SC &= ~FTM_SC_TOF_MASK;
    }
    else if (FTM0_C0SC & FTM_CnSC_CHF_MASK)
    {
        //clock is now low
        SWD_CLK_LOW;

        //handle the current command
        swd_handle_current_command();

        //clear the interrupt flag
        FTM0_C0SC &= ~FTM_CnSC_CHF_MASK;
    }
}

static uint8_t swd_queue_empty(void)
{
    return cmd_in == cmd_out;
}

static uint8_t swd_queue_full(void)
{
    return NEXT_INDEX(SWD_QUEUE_LENGTH - 1, cmd_in) == cmd_out;
}

static uint8_t swd_queue_cmd(const cmd_t* cmd)
{
    if (swd_queue_full())
        return SWD_ERR;

    cmd_queue[cmd_in] = *cmd;
    cmd_in = NEXT_INDEX(SWD_QUEUE_LENGTH - 1,cmd_in);

    return SWD_OK;
}

static uint8_t swd_dequeue_cmd(cmd_t* dest)
{
    if (swd_queue_empty())
        return SWD_ERR;

    *dest = cmd_queue[cmd_out];
    cmd_out = NEXT_INDEX(SWD_QUEUE_LENGTH - 1, cmd_out);

    return SWD_OK;
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

    //the clock is now high
    SWD_CLK_HIGH;
    FTM0_CNT = 0;
    //run the clock (system clock, prescaler 1)
    //enable the ftm0 overflow so we can reset the clock
    FTM0_SC = FTM_SC_TOIE_MASK | FTM_SC_CLKS(1) | FTM_SC_PS(0);
}

static void swd_idle_bus(void)
{
    //stop the clock
    FTM0_SC = 0;
    //set data to output, driven low
    SWD_DATA_OUT;
    SWD_DATA_LOW;
    //we are no longer busy
    swd_busy = FALSE;
}

static void swd_handle_current_command(void)
{
    switch (current_command.command)
    {
    case SWD_INIT:
        swd_handle_init();
        break;
    case SWD_READ:
        swd_handle_read();
        break;
    case SWD_WRITE:
        swd_handle_write();
        break;
    case SWD_IDLE:
        //since this executes when the clock is brought low, it is now safe to idle the bus
        swd_idle_bus();
        break;
    default:
        //invalid command? stop the bus
        last_response = SWD_ERR_BUS;
        current_command.command = SWD_IDLE;
        break;
    }
}

static void swd_handle_init(void)
{
    uint16_t mask;
    cmd_t next;

    if (current_command.state == 0)
    {
        //initial state
        swd_start_bus(1);
        current_command.state++;
    }
    else if (current_command.state <= SWD_INIT_STATE_RESET0)
    {
        //50 high pulses
        SWD_DATA_HIGH;
        current_command.state++;
    }
    else if (current_command.state <= SWD_INIT_STATE_SWITCH)
    {
        //we output the 16 bit switching code
        mask = 1 << (current_command.state - 50);
        if (SWD_INIT_KEY & mask)
        {
            SWD_DATA_HIGH;
        }
        else
        {
            SWD_DATA_LOW;
        }
        current_command.state++;
    }
    else if (current_command.state <= SWD_INIT_STATE_RESET1)
    {
        //50 more high pulses
        SWD_DATA_HIGH;
        current_command.state++;
    }
    else
    {
        //we are now done. initiate a read of the SW-DP IDCODE
        next.command = SWD_READ;
        next.request = SWD_DP_READ_IDCODE;
        next.state = 0;
        current_command = next;
    }
}

static void swd_handle_read(void)
{
    static uint32_t data;
    uint32_t mask;

    if (current_command.state == 0)
    {
        //initial state
        swd_start_bus(current_command.request & 0x1);
        current_command.state++;
    }
    else if (current_command.state <= SWD_READ_STATE_REQ)
    {
        mask = 1 << current_command.state;
        current_command.state++;
        if (current_command.request & mask)
        {
            SWD_DATA_HIGH;
        }
        else
        {
            SWD_DATA_LOW;
        }
        current_command.state++;
    }
    else if (current_command.state <= SWD_READ_STATE_TM0)
    {
        //turnaround: release the data line
        SWD_DATA_IN;
        current_command.state++;
    }
    else if (current_command.state <= SWD_READ_STATE_RESP)
    {
        if (current_command.state == SWD_READ_STATE_TM0 + 1)
            data = 0;

        data |= SWD_DATA_VALUE <<  (current_command.state - SWD_READ_STATE_TM0);

        if (current_command.state == SWD_READ_STATE_RESP)
        {
            switch (data)
            {
            case SWD_RESP_OK:
                last_response = SWD_OK;
                break;
            case SWD_RESP_WAIT:
                last_response = SWD_ERR_WAIT;
                swd_idle_bus();
                break;
            case SWD_RESP_FAULT:
                last_response = SWD_ERR_FAULT;
                swd_idle_bus();
                break;
            default:
                last_response = SWD_ERR_BUS;
                swd_idle_bus();
                break;
            }
        }

        current_command.state++;
    }
    else if (current_command.state <= SWD_READ_STATE_READ)
    {
        //begin reading the thing
        if (current_command.state == SWD_READ_STATE_RESP + 1)
            data = 0;

        data |= SWD_DATA_VALUE << (current_command.state - SWD_READ_STATE_RESP);

        current_command.state++;
    }
    else if (current_command.state <= SWD_READ_STATE_PARITY)
    {
        //TODO: Use the parity bit
        last_read_data = data;
        current_command.state++;
    }
    else if (current_command.state <= SWD_READ_STATE_TM1)
    {
        //turnaround: grab the data line and set to zero
        SWD_DATA_OUT;
        SWD_DATA_LOW;
        current_command.state++;
    }
    else if (current_command.state <= SWD_READ_STATE_FINISH)
    {
        //continue clocking low
        SWD_DATA_LOW;
    }
    else
    {
        //command is finished
        swd_idle_bus();
    }
}

static void swd_handle_write(void)
{
    static uint32_t data;
    uint32_t mask, temp;

    if (current_command.state == 0)
    {
        //initial state
        swd_start_bus(current_command.request & 0x1);
        current_command.state++;
    }
    else if (current_command.state <= SWD_WRITE_STATE_REQ)
    {
        mask = 1 << current_command.state;
        current_command.state++;
        if (current_command.request & mask)
        {
            SWD_DATA_HIGH;
        }
        else
        {
            SWD_DATA_LOW;
        }
        current_command.state++;
    }
    else if (current_command.state <= SWD_WRITE_STATE_TM0)
    {
        //turnaround: release the data line
        SWD_DATA_IN;
        current_command.state++;
    }
    else if (current_command.state <= SWD_WRITE_STATE_RESP)
    {
        if (current_command.state == SWD_WRITE_STATE_TM0 + 1)
            data = 0;

        data |= SWD_DATA_VALUE <<  (current_command.state - SWD_WRITE_STATE_TM0);

        if (current_command.state == SWD_WRITE_STATE_RESP)
        {
            switch (data)
            {
            case SWD_RESP_OK:
                last_response = SWD_OK;
                break;
            case SWD_RESP_WAIT:
                last_response = SWD_ERR_WAIT;
                swd_idle_bus();
                break;
            case SWD_RESP_FAULT:
                last_response = SWD_ERR_FAULT;
                swd_idle_bus();
                break;
            default:
                last_response = SWD_ERR_BUS;
                swd_idle_bus();
                break;
            }
        }

        current_command.state++;
    }
    else if (current_command.state <= SWD_WRITE_STATE_TM1)
    {
        //turnaroud: grab data line and set it to zero
        SWD_DATA_OUT;
        SWD_DATA_LOW;

        current_command.state++;
    }
    else if (current_command.state <= SWD_WRITE_STATE_DATA)
    {
        mask = 1 << (current_command.state - SWD_WRITE_STATE_TM1);
        if (current_command.data & mask)
        {
            SWD_DATA_HIGH;
        }
        else
        {
            SWD_DATA_LOW;
        }

        current_command.state++;
    }
    else if (current_command.state <= SWD_WRITE_STATE_PARITY)
    {
        //parallel parity bit calculation: http://www.graphics.stanford.edu/~seander/bithacks.html#ParityParallel
        temp = current_command.data;
        temp ^= temp >> 16;
        temp ^= temp >> 8;
        temp ^= temp >> 4;
        temp &= 0xf;
        if ((0x6996 >> temp) & 1)
        {
            SWD_DATA_HIGH;
        }
        else
        {
            SWD_DATA_LOW;
        }
    }
    else if (current_command.state <= SWD_WRITE_STATE_FINISH)
    {
        //clock low
        SWD_DATA_LOW;
    }
    else
    {
        //command is finished
        swd_idle_bus();
    }
}
