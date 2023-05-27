#ifndef VSTP_H
#define VSTP_H

#include <ESP8266WiFi.h>

#include "stdint.h"
#include "stdbool.h"


#define VSTP_PACKET_HEADER_SIZE      3
#define VSTP_PACKET_MAX_PAYLOAD_SIZE (0xFF - VSTP_PACKET_HEADER_SIZE)
#define VSTP_RX_TIMEOUT_MS           500
#define VSTP_RX_BUF_NBR_OF_PKTS      50
#define VSTP_PKT_BUF_SIZE            VSTP_PACKET_MAX_PAYLOAD_SIZE

// How long to wait between transmission to force-send the current TX buffer,
// even if it's not full
#define VSTP_UPSTREAM_TX_MAX_DELAY_MS 100000

// Choose between STA (Station) and AP (Access Point)
#define VSTP_NETWORK_WIFI_MODE_STA 1
#define VSTP_NETWORK_SERVER_PORT 80

typedef enum {
    VSTP_CMD_LOG_START    = 1,
    VSTP_CMD_LOG_STOP     = 2,
    VSTP_CMD_LOG_DATA     = 3,
    VSTP_CMD_LOG_SD_START = 4,
    VSTP_CMD_LOG_SD_STOP  = 5,
    VSTP_CMD_RESET        = 6
} vstp_cmd_t;

// This is used for validating commands, please update accordingly
#define VSTP_LOWEST_CMD_VALUE 1
#define VSTP_NBR_OF_CMDS      6

typedef enum
{
    FSM_STATE_WAIT_FOR_CMD,
    FSM_STATE_WAIT_FOR_LENGTH,
    FSM_STATE_WAIT_FOR_CRC,
    FSM_STATE_READING_DATA
} vstp_fsm_state_t;

typedef enum
{
    VSTP_WIFI_STA,
    VSTP_WIFI_AP,
} vstp_wifi_mode_t;

typedef struct {
    vstp_cmd_t cmd;
    uint8_t    len;
    uint8_t    crc;
    uint8_t    buf[VSTP_PACKET_MAX_PAYLOAD_SIZE];
}__attribute__((packed)) vstp_pkt_t;

typedef struct
{
    uint16_t size;
    uint8_t  data[VSTP_PKT_BUF_SIZE];
} pkt_buf_t;

/*
 * Returns the next byte available in UART RX buffer.
 * Returns -1 if no data is available.
 */
typedef int(*uart_read_one_byte)();


typedef struct
{
    // States
    vstp_fsm_state_t     fsm;
    bool                 is_logging_upstream;
    bool                 is_logging_to_sd;
    bool                 is_logging_debug;

    // RX Parsing states
    uint16_t             parse_errors;
    uint16_t             discarded_packets;
    uint8_t              bytes_read;
    uint8_t              rx_crc;
    vstp_pkt_t           rx_pkt;

    // RX and TX buffers
    pkt_buf_t             rx_bufs[VSTP_RX_BUF_NBR_OF_PKTS];
    size_t                rx_buf_index;
    size_t                rx_buf_size;
    pkt_buf_t             tx_buf;

    uint32_t             last_upstream_tx;

    // Network
    WiFiServer*          server;
    WiFiClient           client;

    // Functions
    uart_read_one_byte   uart_read;
} vstp_state_t;


/*
 * Initializes the vstp state
 */
void vstp_init(vstp_state_t* vstp_state, uart_read_one_byte uart_read);

/*
 * Process a single byte in the internal vstp state machine
 */
void vstp_process_byte(vstp_state_t* vstp_state, const uint8_t byte);

/*
 * Performs next transmission of data (if needed)
 */
void vstp_update(vstp_state_t* vstp_state);



#endif /* VSTP_H */
