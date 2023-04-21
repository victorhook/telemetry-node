#include "vstp.h"

#include "string.h"



// -- Helper functions -- //
static bool valid_command(const uint8_t cmd);

static void mark_buffer_as_full(vstp_state_t* vstp_state, tx_buf_t* tx_buf);

static void transmit_upstream_data(const tx_upstream_packet_t* tx_upstream_pkt);

static uint8_t get_next_buf_number(const uint8_t current_buf_number);

/*
 * Returns the next tx buffer to transmit.
 * Returns NULL if there's no data to transmit, or if the tx_buf_number is
 * out of bounds (error).
*/
static tx_buf_t* get_next_tx_buf_to_fill(const vstp_state_t* vstp_state);

static void handle_incoming_packet(vstp_state_t* vstp_state, const vstp_cmd_t cmd);


// -- Command handlers --- /
static void cmd_handler_log_start(vstp_state_t* vstp_state);
static void cmd_handler_log_stop(vstp_state_t* vstp_state);
static void cmd_handler_log_data(vstp_state_t* vstp_state);
static void cmd_handler_log_sd_start(vstp_state_t* vstp_state);
static void cmd_handler_log_sd_stop(vstp_state_t* vstp_state);


// -- Public functions -- //

void vstp_init(vstp_state_t* vstp_state)
{
    // States
    vstp_state->fsm = FSM_STATE_WAIT_FOR_CMD;
    vstp_state->is_logging_upstream = false;
    vstp_state->is_logging_to_sd = false;
    vstp_state->is_logging_debug = false;

    // RX Parsing states
    vstp_state->parse_errors = 0;
    vstp_state->discarded_packets = 0;

    // TX buffer
    vstp_state->rx_buf_number = 0;
    vstp_state->tx_buf_number = 0;
}


void vstp_process_byte(vstp_state_t* vstp_state, const uint8_t byte)
{
    vstp_fsm_state_t next_state;

    switch (vstp_state->fsm)
    {
        case FSM_STATE_WAIT_FOR_CMD:
        {
            if (valid_command(byte))
            {
                vstp_state->rx_pkt.cmd = byte;
                vstp_state->rx_crc = byte;
                next_state = FSM_STATE_WAIT_FOR_LENGTH;
            }
            else
            {
                next_state = FSM_STATE_WAIT_FOR_CMD;
                vstp_state->parse_errors++;
            }
            break;
        }
        case FSM_STATE_WAIT_FOR_LENGTH:
        {
            if (valid_length(byte))
            {
                vstp_state->rx_pkt.len = byte;
                vstp_state->rx_crc ^= byte;
                next_state = FSM_STATE_WAIT_FOR_CRC;
            }
            else
            {
                vstp_state->parse_errors++;
                next_state = FSM_STATE_WAIT_FOR_CMD;
            }
            break;
        }
        case FSM_STATE_WAIT_FOR_CRC:
        {
            next_state = FSM_STATE_READING_DATA;
            vstp_state->rx_pkt.crc = byte;
            break;
        }
        case FSM_STATE_READING_DATA:
        {
            if (vstp_state->bytes_read >= vstp_state->rx_pkt.len)
            {
                if (vstp_state->rx_crc == vstp_state->rx_pkt.crc)
                {
                    // Done reading packet, give it to packer handler
                    handle_incoming_packet(vstp_state);
                }
                else
                {
                    // Incorrect CRC
                    vstp_state->parse_errors++;
                }
                next_state = FSM_STATE_WAIT_FOR_CMD;
            }
            else
            {
                // Update RX buffer, CRC, reading counter
                vstp_state->rx_pkt.buf[vstp_state->bytes_read] = byte;
                vstp_state->rx_crc ^= byte;
                vstp_state->bytes_read++;
            }
            break;
        }
    }
}


void vstp_update(vstp_state_t* vstp_state)
{
    bool send_data;
    tx_buf_t* next_tx_buf;

    next_tx_buf = &vstp_state->tx_bufs[vstp_state->tx_buf_number];

    send_data = next_tx_buf->size > 0;


    if (send_data)
    {
        if (vstp_state->is_logging_upstream)
        {
            // Prepare upstream data packet.
            // Header is 2 bytes for packet size
            // TODO: Turn off interrupts?
            vstp_state->tx_upstream_pkt.size = next_tx_buf->size;
            memcpy(vstp_state->tx_upstream_pkt.data, next_tx_buf->data, next_tx_buf->size);
            transmit_upstream_data(&vstp_state->tx_upstream_pkt);
        }
        if (vstp_state->is_logging_to_sd)
        {
            // TODO
        }
        if (vstp_state->is_logging_debug)
        {
            // TODO
        }

        next_tx_buf->size = 0;
        next_tx_buf->full = false;
        vstp_state->tx_buf_number = get_next_buf_number(vstp_state->tx_buf_number);

    }
    else
    {
        return;
    }

}


// -- Static functions -- //

static void transmit_upstream_data(const tx_upstream_packet_t* tx_upstream_pkt)
{

}

static uint8_t get_next_buf_number(const uint8_t current_buf_number)
{
    uint8_t next = current_buf_number + 1;
    if (next >= VSTP_TX_BUF_MAX_PDUS)
    {
        next = 0;
    }
    return next;
}

static void handle_incoming_packet(vstp_state_t* vstp_state, const vstp_cmd_t cmd)
{

    switch (cmd)
    {
        case VSTP_CMD_LOG_START:
        {
            cmd_handler_log_start(vstp_state);
            break;
        }
        case VSTP_CMD_LOG_STOP:
        {
            cmd_handler_log_stop(vstp_state);
            break;
        }
        case VSTP_CMD_LOG_DATA:
        {
            cmd_handler_log_data(vstp_state);
            break;
        }
        case VSTP_CMD_LOG_SD_START:
        {
            cmd_handler_log_sd_start(vstp_state);
            break;
        }
        case VSTP_CMD_LOG_SD_STOP:
        {
            cmd_handler_log_sd_stop(vstp_state);
            break;
        }
    }



}

static void mark_buffer_as_full(vstp_state_t* vstp_state, tx_buf_t* tx_buf) {
    tx_buf->full = true;
    vstp_state->tx_buf_number = get_next_buf_number(vstp_state->rx_buf_number);
}

// -- Command handlers -- //
static void cmd_handler_log_data(vstp_state_t* vstp_state)
{
    bool discard_packet;
    uint16_t new_tx_buf_size;
    // Data to copy is Data length + Header size
    uint16_t nbr_of_bytes_to_copy = vstp_state->rx_pkt.len + VSTP_PACKET_HEADER_SIZE;

    tx_buf_t* tx_buf = get_next_tx_buf_to_fill(vstp_state);

    if (tx_buf == NULL)
    {
        discard_packet = true;
    }
    else
    {
        new_tx_buf_size = tx_buf->size + nbr_of_bytes_to_copy;

        if (new_tx_buf_size > VSTP_MAX_UPSTREAM_PAYLOAD_SIZE)
        {
            // New tx buffer size is too big, so we'll try to find another available buffer
            mark_buffer_as_full(vstp_state, tx_buf);

            tx_buf = get_next_tx_buf_to_fill(vstp_state);

            if (tx_buf == NULL)
            {
                discard_packet = true;
            }
            else
            {
                // We don't have to check for size here, because there's never
                // two "not discovered full" packets in a row.
                new_tx_buf_size = tx_buf->size + nbr_of_bytes_to_copy;
            }
        }
    }

    if (discard_packet)
    {
        // No valid tx buffer found, probably means we're receiving
        // data faster than we can send.
        // We will discard the newly received data!
        vstp_state->discarded_packets++;
    }
    else
    {
        uint16_t old_tx_buf_size = tx_buf->size;
        // TODO: Disable interrupts?

        // Copy the data in the RX buffer into the TX buffer
        memcpy(&tx_buf->data[old_tx_buf_size], vstp_state->rx_pkt.buf, nbr_of_bytes_to_copy);
        // Change the size of the TX buffer
        tx_buf->size = new_tx_buf_size;
    }
}
static void cmd_handler_log_start(vstp_state_t* vstp_state)
{
    vstp_state->is_logging_upstream = true;
}
static void cmd_handler_log_stop(vstp_state_t* vstp_state)
{
    vstp_state->is_logging_upstream = false;
}
static void cmd_handler_log_sd_start(vstp_state_t* vstp_state)
{
    vstp_state->is_logging_to_sd = true;
}
static void cmd_handler_log_sd_stop(vstp_state_t* vstp_state)
{
    vstp_state->is_logging_to_sd = false;
}


static tx_buf_t* get_next_tx_buf_to_fill(const vstp_state_t* vstp_state) {
    tx_buf_t* tx_buf = &vstp_state->tx_bufs[vstp_state->tx_buf_number];
    if (tx_buf->full)
    {
        return NULL;
    }

    return tx_buf;
}

