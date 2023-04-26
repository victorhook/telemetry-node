#include "vstp.h"
#include "credentials.h"

#include "string.h"


#define SERVER_NOT_CONNECTED 0

//#define DO_DEBUG

#ifdef DO_DEBUG
    #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
    #define DEBUG_PRINTF(...)
#endif


// -- Helper functions -- //
static bool valid_length(const uint8_t length);
static bool valid_command(const uint8_t command);

static void transmit_upstream_data(vstp_state_t* vstp_state);

/* Returns NULL if there is no receive buffer available */
static pkt_buf_t* get_next_rx_buf(vstp_state_t* vstp_state);

/* Adds the incoming RX packet to the RX buffer.
 * Returns true if successful and false if the RX buffer is full.
 */
static bool add_rx_buf(vstp_state_t* vstp_state);

/* "Consumes" the current rx buffer, so the internal ring buffer index
 * is incremented.
 */
static void consume_rx_buf(vstp_state_t* vstp_state, pkt_buf_t* buf);

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
    vstp_state->rx_buf_index = 0;
    vstp_state->rx_buf_size = 0;

    vstp_state->last_upstream_tx = 0;

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
            vstp_state->rx_pkt.crc = byte;
            if (vstp_state->rx_pkt.len > 0)
            {
                next_state = FSM_STATE_READING_DATA;
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
            // Update RX buffer, CRC, reading counter
            vstp_state->rx_pkt.buf[vstp_state->bytes_read] = byte;
            vstp_state->rx_crc ^= byte;
            vstp_state->bytes_read++;
            DEBUG_PRINTF("DATA: %d/%d\n", vstp_state->bytes_read, vstp_state->rx_pkt.len);

            if (vstp_state->bytes_read >= vstp_state->rx_pkt.len)
            {
                next_state = FSM_STATE_WAIT_FOR_CMD;
                validate_packet = true;
            }
            break;
        }
    }

    if (validate_packet)
    {
        DEBUG_PRINTF("Validate packet: ");
        DEBUG_PRINTF("CMD: %d, Len: %d, CRC: %d, Calculated CRC: %d\n",
            vstp_state->rx_pkt.cmd,
            vstp_state->rx_pkt.len,
            vstp_state->rx_pkt.crc,
            vstp_state->rx_crc
        );
        if (vstp_state->rx_crc == vstp_state->rx_pkt.crc)
        {   // Done reading packet, give it to packer handler
            handle_incoming_packet(vstp_state, vstp_state->rx_pkt.cmd);
        }
        else
        {   // Incorrect CRC
            vstp_state->parse_errors++;
        }

        // Reset packet states
        vstp_state->bytes_read = 0;
    }

    DEBUG_PRINTF("Errs: %d. State: %d -> %d\n", vstp_state->parse_errors, vstp_state->fsm, next_state);
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

    if (vstp_state->server->status() == SERVER_NOT_CONNECTED)
    {
        vstp_state->server->begin(VSTP_NETWORK_SERVER_PORT);
    }

    static uint32_t t0_debug_msg = 0;
    uint32_t now = millis();
    pkt_buf_t* next_rx_buf = get_next_rx_buf(vstp_state);

    bool consume_data = vstp_state->is_logging_upstream | vstp_state->is_logging_to_sd;

    if ((now - t0_debug_msg) > 1000)
    {
        DEBUG_PRINTF("buf size: %d, ", vstp_state->rx_buf_size);
        DEBUG_PRINTF("rx_index: %d, ", vstp_state->rx_buf_size);
        DEBUG_PRINTF("consume: %d, ", consume_data);
        DEBUG_PRINTF("log_upstream: %d, ", vstp_state->is_logging_upstream);
        DEBUG_PRINTF("log_sd: %d, ", vstp_state->is_logging_to_sd);
        DEBUG_PRINTF("log_debug: %d", vstp_state->is_logging_debug);
        DEBUG_PRINTF("\n");
        t0_debug_msg = now;
    }

    if (next_rx_buf == NULL)
    {
        return;
    }

    memcpy(vstp_state->tx_buf.data, next_rx_buf->data, next_rx_buf->size);
    vstp_state->tx_buf.size = next_rx_buf->size;

    if (vstp_state->is_logging_upstream)
    {
        // Copy data from RX buffer into TX buffer
        // TODO: Turn off interrupts?
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

    if (consume_data)
    {
        consume_rx_buf(vstp_state, next_rx_buf);
    }

    vstp_state->last_upstream_tx = now;
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
        vstp_state->client = vstp_state->server->available();
        if (!vstp_state->client.connected())
        {
            // Still no client connected? then we'll return
            return;
        }
    }

    DEBUG_PRINTF("Upstream: %d bytes\n", vstp_state->tx_buf.size);
    vstp_state->client.write(vstp_state->tx_buf.data, vstp_state->tx_buf.size);
}

static bool valid_length(const uint8_t length)
{
    return length <= VSTP_PACKET_MAX_PAYLOAD_SIZE;
}
static bool valid_command(const uint8_t command)
{
    return command <= VSTP_NBR_OF_CMDS;
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

// -- Command handlers -- //
static void cmd_handler_log_data(vstp_state_t* vstp_state)
{
    if (!add_rx_buf(vstp_state))
    {
        DEBUG_PRINTF("Discarding packet\n");
        vstp_state->discarded_packets++;
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



static pkt_buf_t* get_next_rx_buf(vstp_state_t* vstp_state)
{
    if (vstp_state->rx_buf_size == 0)
    {
        return NULL;
    }
    // Example:
    // index = 2
    // size = 4
    // MAX_PKTS = 10
    // index = ( (2-4) + 10 ) % 10 = 8
    size_t buf_index = ( (vstp_state->rx_buf_index - vstp_state->rx_buf_size) + VSTP_RX_BUF_NBR_OF_PKTS ) % VSTP_RX_BUF_NBR_OF_PKTS;
    return &vstp_state->rx_bufs[buf_index];
}

static bool add_rx_buf(vstp_state_t* vstp_state)
{
    if (vstp_state->rx_buf_size >= VSTP_RX_BUF_NBR_OF_PKTS)
    {
        return false;
    }

    pkt_buf_t* buf = &vstp_state->rx_bufs[vstp_state->rx_buf_index];
    memcpy(buf->data, vstp_state->rx_pkt.buf, vstp_state->rx_pkt.len);
    buf->size = vstp_state->rx_pkt.len;

    vstp_state->rx_buf_size += 1;
    vstp_state->rx_buf_index = (vstp_state->rx_buf_index + 1) % VSTP_RX_BUF_NBR_OF_PKTS;

    return true;
}

static void consume_rx_buf(vstp_state_t* vstp_state, pkt_buf_t* buf)
{
    if (vstp_state->rx_buf_size == 0)
    {
        // Should never happen!
        DEBUG_PRINTF("Tried to consume buffer when size 0!");
        return;
    }

    vstp_state->rx_buf_index = ((vstp_state->rx_buf_index - 1) + VSTP_RX_BUF_NBR_OF_PKTS) % VSTP_RX_BUF_NBR_OF_PKTS;
    vstp_state->rx_buf_size--;
}