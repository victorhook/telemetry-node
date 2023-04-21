#ifndef VSTP_H
#define VSTP_H

#include "stdint.h"
#include "stdbool.h"

#define VSTP_PACKET_HEADER_SIZE      3
#define VSTP_PACKET_MAX_PAYLOAD_SIZE (0xFF - VSTP_PACKET_HEADER_SIZE)
#define VSTP_RX_TIMEOUT_MS           500
#define VSTP_TX_MAX_PDU_SIZE         1024
#define VSTP_TX_BUF_MAX_PDUS         10  // Max nbr of PDUs to hold
// For upstream packets, we include a 2-byte header for the length.
#define VSTP_MAX_UPSTREAM_PAYLOAD_SIZE (VSTP_TX_MAX_PDU_SIZE - 2)


typedef enum {
    VSTP_CMD_LOG_START,
    VSTP_CMD_LOG_STOP,
    VSTP_CMD_LOG_DATA,
    VSTP_CMD_LOG_SD_START,
    VSTP_CMD_LOG_SD_STOP
} vstp_cmd_t;


typedef struct {
    vstp_cmd_t cmd;
    uint8_t len;
    uint8_t crc;
    uint8_t buf[VSTP_PACKET_MAX_PAYLOAD_SIZE];
}__attribute__((packed)) vstp_pkt_t;

typedef enum
{
    FSM_STATE_WAIT_FOR_CMD,
    FSM_STATE_WAIT_FOR_LENGTH,
    FSM_STATE_WAIT_FOR_CRC,
    FSM_STATE_READING_DATA
} vstp_fsm_state_t;

typedef struct
{
    uint16_t size;
    bool     full;
    uint8_t  data[VSTP_MAX_UPSTREAM_PAYLOAD_SIZE];
} tx_buf_t;

typedef struct {
    uint16_t size;
    uint8_t  data[];
}__attribute__((packed)) tx_upstream_packet_t;

typedef struct
{
    // States
    vstp_fsm_state_t          fsm;
    bool                 is_logging_upstream;
    bool                 is_logging_to_sd;
    bool                 is_logging_debug;

    // RX Parsing states
    uint16_t             parse_errors;
    uint16_t             discarded_packets;
    uint8_t              bytes_read;
    uint8_t              rx_crc;
    vstp_pkt_t           rx_pkt;

    // TX buffer
    uint8_t              rx_buf_number;
    uint8_t              tx_buf_number;
    tx_buf_t             tx_bufs[VSTP_TX_BUF_MAX_PDUS];
    tx_upstream_packet_t tx_upstream_pkt;
} vstp_state_t;


/*
 * Initializes the vstp state
 */
void vstp_init(vstp_state_t* vstp_state);

/*
 * Process a single byte in the internal vstp state machine
 */
void vstp_process_byte(vstp_state_t* vstp_state, const uint8_t byte);

/*
 * Performs next transmission of data (if needed)
 */
void vstp_update(vstp_state_t* vstp_state);



#endif /* VSTP_H */
