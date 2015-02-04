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
 * The handle_queue function operates the bus state machine.
 */

#include "arm_cm4.h"
#include "swd.h"

#define SWD_RESP_OK    0b001
#define SWD_RESP_WAIT  0b010
#define SWD_RESP_FAULT 0b100

#define SWD_READ_STATE_REQ    8
#define SWD_READ_STATE_TM0    (SWD_READ_STATE_REQ + 1)
#define SWD_READ_STATE_RESP   (SWD_READ_STATE_TM0 + 3)
#define SWD_READ_STATE_READ   (SWD_READ_STATE_RESP + 32)
#define SWD_READ_STATE_PARITY (SWD_READ_STATE_READ + 1)
#define SWD_READ_STATE_TM1    (SWD_READ_STATE_PARITY + 1)

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

typedef enum { SWD_READ, SWD_WRITE } cmd_type_t;

/**
 * Bus state type
 * SWD_BUS_IDLE: The bus is idle, clock should be held high, data should be released
 * SWD_BUS_INIT: The bus is being initialized to SWD mode (>50 pulses, swd sequence, another 50 pulses, read idcode)
 * SWD_BUS_RUN: The bus is currently dequeing and executing commands
 * SWD_BUS_STOP: The bus is stopping by clocking at least 8 additional pulses before returning to SWD_BUS_IDLE
 */
typedef enum { SWD_BUS_IDLE, SWD_BUS_INIT, SWD_BUS_RUN, SWD_BUS_STOP } bus_state_t;

typedef struct {
    cmd_type_t command;
    swd_result_t* result; //written with the result of the command
    uint8_t request;
    uint32_t data;
    uint32_t state;
    uint32_t state_data;
} cmd_t;

static bus_state_t state = SWD_BUS_IDLE;

static cmd_t cmd_queue[SWD_QUEUE_LENGTH];
static uint32_t cmd_in = 0;
static uint32_t cmd_out = 0;

// bit sequence for initializing an SWD connection
// transmitted 0th index first, msb first
static const uint8_t swd_initseq[] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, //56 ones
    0x79, 0xe7, //swd switchover command
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, //56 ones again
};

// bit sequence for stopping an SWD connection
// transmitted 0th index first, msb first
static const uint8_t swd_stopseq[] = {
    0xff //we just need 8 ones
};

/**
 * Returns true if the queue is empty
 */
static uint8_t swd_queue_empty(void);
/**
 * Returns true if the queue is full
 */
static uint8_t swd_queue_full(void);
/**
 * Queues a command
 * @param cmd Command to queue (by copying)
 * @return TRUE if the operation succeeded
 */
static int8_t swd_queue_cmd(const cmd_t* cmd);
/**
 * Dequeues a command
 * @param dest Destination to dequeue the command into
 * @return TRUE if the operation succeeded
 */
static int8_t swd_dequeue_cmd(cmd_t* dest);

/**
 * Handles the bus state machine
 */
static void swd_do_bus(void);

/**
 * Handles the current command
 * @return SWD_DONE when the passed command is complete
 */
static uint8_t swd_handle_command(cmd_t* cmd);

/**
 * Handles a read command
 * @return SWD_DONE when the passed command is complete
 */
static uint8_t swd_handle_read(cmd_t* cmd);

/**
 * Handles a write command
 * @return SWD_DONE when the passed command is complete
 */
static uint8_t swd_handle_write(cmd_t* cmd);

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

    //start up the bus timer interrupts. The bus will remain "idle" until a command is queued
    //the clock is now high
    SWD_CLK_HIGH;
    FTM0_CNT = 0;
    //run the clock (system clock, prescaler 1)
    //enable the ftm0 overflow so we can reset the clock
    FTM0_SC = FTM_SC_TOIE_MASK | FTM_SC_CLKS(1) | FTM_SC_PS(0);
}

int8_t swd_begin_write(uint8_t req, uint32_t data, swd_result_t* res)
{
    cmd_t command = {
        .command = SWD_WRITE,
        .request = req,
        .data = data,
        .result = res
    };

    return swd_queue_cmd(&command);
}

int8_t swd_begin_read(uint8_t req, swd_result_t* res)
{
    cmd_t command = {
        .command = SWD_READ,
        .request = req,
        .result = res
    };

    return swd_queue_cmd(&command);
}

void FTM0_IRQHandler(void)
{
    if (FTM0_SC & FTM_SC_TOF_MASK)
    {
        if (state != SWD_BUS_IDLE)
        {
            //clock is now high
            SWD_CLK_HIGH;
        }

        //do our bus things while the target isn't listening
        swd_do_bus();

        //clear the interrupt flag
        FTM0_SC &= ~FTM_SC_TOF_MASK;
    }
    else if (FTM0_C0SC & FTM_CnSC_CHF_MASK)
    {
        if (state != SWD_BUS_IDLE)
        {
            //clock is now low
            SWD_CLK_LOW;
        }

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

static int8_t swd_queue_cmd(const cmd_t* cmd)
{
    if (swd_queue_full())
        return SWD_ERR;

    DisableInterrupts;
    cmd_queue[cmd_in] = *cmd;
    cmd_in = NEXT_INDEX(SWD_QUEUE_LENGTH - 1,cmd_in);
    EnableInterrupts;

    return SWD_OK;
}

static int8_t swd_dequeue_cmd(cmd_t* dest)
{
    if (swd_queue_empty())
        return SWD_ERR;

    DisableInterrupts;
    *dest = cmd_queue[cmd_out];
    cmd_out = NEXT_INDEX(SWD_QUEUE_LENGTH - 1, cmd_out);
    EnableInterrupts;

    return SWD_OK;
}

static void swd_do_bus(void)
{
    static uint32_t counter = 0; //generic counter for the state
    static cmd_t current_command;

    uint8_t t;

    //state actions
    switch (state)
    {
    case SWD_BUS_IDLE:
        SWD_DATA_IN; //let the data float high
        break;
    case SWD_BUS_INIT:
        SWD_DATA_OUT;
        t = 0x8 >> (counter & 0x7); //this is the mask for the bit, transmitted MSB first
        if (swd_initseq[counter >> 3] & t)
        {
            SWD_DATA_HIGH;
        }
        else
        {
            SWD_DATA_LOW;
        }
        counter++;
        break;
    case SWD_BUS_STOP:
        SWD_DATA_OUT;
        t = 0x8 >> (counter & 0x7); //this is the mask for the bit, transmitted MSB first
        if (swd_stopseq[counter >> 3] & t)
        {
            SWD_DATA_HIGH;
        }
        else
        {
            SWD_DATA_LOW;
        }
        counter++;
        break;
    default:
        break;
    }

    //state transitions
    switch (state)
    {
    case SWD_BUS_IDLE:
        if (!swd_queue_empty())
        {
            counter = 0;
            state = SWD_BUS_INIT;
        }
        break;
    case SWD_BUS_INIT:
        if (counter >= sizeof(swd_initseq) * 8)
        {
            if (swd_dequeue_cmd(&current_command) == SWD_OK)
            {
                //if we have finished the init sequence and dequeued a command, initiate run mode
                state = SWD_BUS_RUN;
            }
            else
            {
                //a failure to dequeue: stop the bus
                counter = 0;
                state = SWD_BUS_STOP;
            }
        }
        break;
    case SWD_BUS_RUN:
        if (swd_handle_command(&current_command) == SWD_DONE)
        {
            if (swd_queue_empty() || swd_dequeue_cmd(&current_command) != SWD_OK)
            {
                //we either have an empty queue or failed to dequeue a new command
                //we move into the stop state
                counter = 0;
                state = SWD_BUS_STOP;
            }
        }
        break;
    case SWD_BUS_STOP:
        if (counter >= sizeof(swd_stopseq) * 8)
        {
            //we may now idle the bus
            state = SWD_BUS_IDLE;
        }
        break;
    }
}

static uint8_t swd_handle_command(cmd_t* cmd)
{
    switch (cmd->command)
    {
    case SWD_READ:
        return swd_handle_read(cmd);
    case SWD_WRITE:
        return swd_handle_write(cmd);
    default:
        //invalid command? we are done with it
        cmd->result->done = 1;
        cmd->result->result = SWD_ERR_BUS;
        return SWD_DONE;
    }
}

static uint8_t swd_handle_read(cmd_t* cmd)
{
    uint32_t mask;

    if (cmd->state < SWD_READ_STATE_REQ)
    {
        mask = 1 << cmd->state;
        SWD_DATA_OUT;
        if (cmd->request & mask)
        {
            SWD_DATA_HIGH;
        }
        else
        {
            SWD_DATA_LOW;
        }
        cmd->state++;
    }
    else if (cmd->state < SWD_READ_STATE_TM0)
    {
        //turnaround
        SWD_DATA_IN;
        cmd->state_data = 0; //prepare to read response
        cmd->state++;
    }
    else if (cmd->state < SWD_READ_STATE_RESP)
    {
        cmd->state_data |= SWD_DATA_VALUE << (cmd->state - SWD_READ_STATE_TM0);
        if (cmd->state == 11)
        {
            //determine response
            switch (cmd->state_data)
            {
            case SWD_RESP_OK:
                cmd->data = 0;
                cmd->state++;
                break;
            case SWD_RESP_FAULT:
                //fault error
                cmd->result->result = SWD_ERR_FAULT;
                cmd->result->done = 1;
                return SWD_DONE;
            case SWD_RESP_WAIT:
                //the SWD slave is busy
                cmd->result->result = SWD_ERR_BUSY;
                cmd->result->done = 1;
                return SWD_DONE;
            default:
                //unknown error
                cmd->result->result = SWD_ERR_BUS;
                cmd->result->done = 1;
                return SWD_DONE;
            }
        }
        else
        {
            cmd->state++;
        }
    }
    else if (cmd->state < SWD_READ_STATE_READ)
    {
        cmd->data |= SWD_DATA_VALUE << (cmd->state - SWD_READ_STATE_RESP);
        cmd->state++;
    }
    else if (cmd->state < SWD_READ_STATE_PARITY)
    {
        //TODO: Use the parity bit
        cmd->result->data = cmd->data;
        cmd->state++;
    }
    else if (cmd->state < SWD_READ_STATE_TM1)
    {
        //turnaround
        SWD_DATA_OUT;
        cmd->result->result = SWD_OK;
        cmd->result->done = 1;
        //we are now done
        return SWD_DONE;
    }
    else
    {
        //wat?
        cmd->result->result = SWD_ERR_BUS;
        cmd->result->done = 1;
        return SWD_DONE;
    }

    //if we make it this far, we assume the state machine is not finished
    return !SWD_DONE;
}

static uint8_t swd_handle_write(cmd_t* cmd)
{
    uint32_t mask, temp;

    if (cmd->state < SWD_WRITE_STATE_REQ)
    {
        mask = 1 << cmd->state;
        SWD_DATA_OUT;
        if (cmd->request & mask)
        {
            SWD_DATA_HIGH;
        }
        else
        {
            SWD_DATA_LOW;
        }
        cmd->state++;
    }
    else if (cmd->state < SWD_WRITE_STATE_TM0)
    {
        //turnaround
        SWD_DATA_IN;
        cmd->state_data = 0; //prepare to read response
        cmd->state++;
    }
    else if (cmd->state < SWD_WRITE_STATE_RESP)
    {
        cmd->state_data |= SWD_DATA_VALUE << (cmd->state - SWD_READ_STATE_TM0);
        cmd->state++;
    }
    else if (cmd->state < SWD_WRITE_STATE_TM1)
    {
        //turnaround
        SWD_DATA_OUT;
        switch (cmd->state_data)
        {
        case SWD_RESP_OK:
            cmd->data = 0;
            cmd->state++;
            break;
        case SWD_RESP_FAULT:
            //fault error
            cmd->result->result = SWD_ERR_FAULT;
            cmd->result->done = 1;
            return SWD_DONE;
        case SWD_RESP_WAIT:
            //the SWD slave is busy
            cmd->result->result = SWD_ERR_BUSY;
            cmd->result->done = 1;
            return SWD_DONE;
        default:
            //unknown error
            cmd->result->result = SWD_ERR_BUS;
            cmd->result->done = 1;
            return SWD_DONE;
        }
    }
    else if (cmd->state < SWD_WRITE_STATE_DATA)
    {
        //lsb first
        mask = 1 << (cmd->state - SWD_WRITE_STATE_TM1);
        if (cmd->data & mask)
        {
            SWD_DATA_HIGH;
        }
        else
        {
            SWD_DATA_LOW;
        }
        cmd->state++;
    }
    else if (cmd->state < SWD_WRITE_STATE_PARITY)
    {
        //parallel parity bit calculation: http://www.graphics.stanford.edu/~seander/bithacks.html#ParityParallel
        temp = cmd->data;
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
        cmd->result->result = SWD_OK;
        cmd->result->done = 1;
        return SWD_DONE;
    }
    else
    {
        //wat?
        cmd->result->result = SWD_ERR_BUS;
        cmd->result->done = 1;
        return SWD_DONE;
    }

    //if we get this far, we assume that the state machine needs to continue
    return !SWD_DONE;
}
