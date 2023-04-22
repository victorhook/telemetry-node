#include "vstp.h"
#include "credentials.h"

#include "string.h"


#define SERVER_NOT_CONNECTED 0

#define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)


// -- Helper functions -- //
static bool valid_length(const uint8_t length);
static bool valid_command(const uint8_t command);

static void mark_buffer_as_full(vstp_state_t* vstp_state, rx_buf_t* tx_buf);

static void transmit_upstream_data(vstp_state_t* vstp_state);

static uint8_t get_next_buf_number(const uint8_t current_buf_number);

/*
 * Returns the next tx buffer to transmit.
 * Returns NULL if there's no data to transmit, or if the tx_buf_number is
 * out of bounds (error).
*/
static rx_buf_t* get_next_tx_buf_to_fill(vstp_state_t* vstp_state);

static void handle_incoming_packet(vstp_state_t* vstp_state, const vstp_cmd_t cmd);


// -- Command handlers --- /
static void cmd_handler_log_start(vstp_state_t* vstp_state);
static void cmd_handler_log_stop(vstp_state_t* vstp_state);
static void cmd_handler_log_data(vstp_state_t* vstp_state);
static void cmd_handler_log_sd_start(vstp_state_t* vstp_state);
static void cmd_handler_log_sd_stop(vstp_state_t* vstp_state);


// -- Public functions -- //

void vstp_init(vstp_state_t* vstp_state, uart_read_one_byte uart_read)
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

    vstp_state->uart_read = uart_read;

    // Initialize WiFi
    vstp_state->server = (WiFiServer*) malloc(sizeof(WiFiServer));
    /*
    WiFi.setHostname(WIFI_HOST_NAME);
    if (VSTP_NETWORK_WIFI_MODE_STA == 1)
    {
        WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
    else
    {

    }
    */
}


void vstp_process_byte(vstp_state_t* vstp_state, const uint8_t byte)
{
    vstp_fsm_state_t next_state = vstp_state->fsm;
    bool validate_packet = false;

    switch (vstp_state->fsm)
    {
        case FSM_STATE_WAIT_FOR_CMD:
        {
            if (valid_command(byte))
            {
                vstp_state->rx_pkt.cmd = (vstp_cmd_t) byte;
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
            if (vstp_state->rx_pkt.len > 0)
            {
                next_state = FSM_STATE_READING_DATA;
                vstp_state->rx_pkt.crc = byte;
            }
            else
            {   // RX packet contains no data
                next_state = FSM_STATE_WAIT_FOR_CMD;
                validate_packet = true;
            }
            break;
        }
        case FSM_STATE_READING_DATA:
        {
            if (vstp_state->bytes_read >= vstp_state->rx_pkt.len)
            {
                next_state = FSM_STATE_WAIT_FOR_CMD;
                validate_packet = true;
            }
            else
            {   // Update RX buffer, CRC, reading counter
                vstp_state->rx_pkt.buf[vstp_state->bytes_read] = byte;
                vstp_state->rx_crc ^= byte;
                vstp_state->bytes_read++;
            }
            break;
        }
    }

    if (validate_packet)
    {
        DEBUG_PRINTF("Validate packet: \n");
        DEBUG_PRINTF("CMD: %d, CRC: %d, Len: %d\n",
            vstp_state->rx_pkt.cmd,
            vstp_state->rx_pkt.len,
            vstp_state->rx_pkt.crc
        );
        if (vstp_state->rx_crc == vstp_state->rx_pkt.crc)
        {   // Done reading packet, give it to packer handler
            handle_incoming_packet(vstp_state, vstp_state->rx_pkt.cmd);
        }
        else
        {   // Incorrect CRC
            vstp_state->parse_errors++;
        }
    }

    DEBUG_PRINTF("State: %d -> %d\n", vstp_state->fsm, next_state);
    vstp_state->fsm = next_state;
}


void vstp_update(vstp_state_t* vstp_state)
{
    // Read byte from RX UART and process in fsm.
    int next_byte = vstp_state->uart_read();
    if (next_byte != -1)
    {
        vstp_process_byte(vstp_state, next_byte);
    }

    return;



    bool send_data;
    rx_buf_t* next_tx_buf;

    next_tx_buf = &vstp_state->rx_bufs[vstp_state->tx_buf_number];

    send_data = next_tx_buf->size > 0;

    if (vstp_state->server->status() == SERVER_NOT_CONNECTED)
    {
        vstp_state->server->begin(VSTP_NETWORK_SERVER_PORT);
    }

    if (send_data)
    {
        if (vstp_state->is_logging_upstream)
        {
            // Copy data from RX buffer into TX buffer
            // TODO: Turn off interrupts?
            vstp_state->tx_buf.size = next_tx_buf->size;
            memcpy(vstp_state->tx_buf.data, next_tx_buf->data, next_tx_buf->size);
            transmit_upstream_data(vstp_state);
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
/*
 * Tries to transmit the data upstream to a connected client.
 * If no client is connected, we see if any pending connections are
 * waiting and if so, we accept them.
 * If still, no client is connected, we simply return, which means that
 * the data never reaches the client.
 */
static void transmit_upstream_data(vstp_state_t* vstp_state)
{
    if (!vstp_state->client.connected())
    {
        // No client connected, try to accept incoming connections
        vstp_state->server->available();
        if (!vstp_state->client.connected())
        {
            // Still no client connected? then we'll return
            return;
        }
    }

    vstp_state->client.write(vstp_state->tx_buf.data, vstp_state->tx_buf.size);
}

static bool valid_length(const uint8_t length)
{
    return length <= VSTP_PACKET_MAX_PAYLOAD_SIZE;
}
static bool valid_command(const uint8_t command)
{
    return true;
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

    DEBUG_PRINTF("Incoming: %d\n", cmd);

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

static void mark_buffer_as_full(vstp_state_t* vstp_state, rx_buf_t* tx_buf) {
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

    rx_buf_t* tx_buf = get_next_tx_buf_to_fill(vstp_state);

    if (tx_buf == NULL)
    {
        discard_packet = true;
    }
    else
    {
        new_tx_buf_size = tx_buf->size + nbr_of_bytes_to_copy;

        if (new_tx_buf_size > VSTP_TX_MAX_PDU_SIZE)
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


static rx_buf_t* get_next_tx_buf_to_fill(vstp_state_t* vstp_state) {
    rx_buf_t* tx_buf = &vstp_state->rx_bufs[vstp_state->tx_buf_number];
    if (tx_buf->full)
    {
        return NULL;
    }

    return tx_buf;
}

