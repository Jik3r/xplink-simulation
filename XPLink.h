#ifndef XPLINK_H
#define XPLINK_H
#include <stdint.h>

#define SYS_ID 0x33
#define END_BYTE 0x00

typedef enum xp_msg_t
{
    CMD,
    ACC,
    TEMP1,
    TEMP2,
    TEMP3,
    TEMP4,
    PRESSURE1,
    PRESSURE2,
    PRESSURE3,
    PRESSURE4,
    PRESSURE5,
    PRESSURE6,
    PRESSURE7,
    PRESSURE8,
    PRESSURE9,
    PRESSURE10,
    PRESSURE11,
    PRESSURE12,
    THRUST,
    SOLENOID,
    XP_STATE,
    SWITCHES,
} xp_msg_t;

typedef struct xp_packet_t
{
    xp_msg_t type;
    uint64_t data;
    uint8_t sender_id;
    //uint8_t packet[12]

} xp_packet_t;

/// @brief Pack an XPLink packet
/// @param  uint8_t* Packet -> currenlty 12 long
/// @param  xp_type specify command type
/// @param  uint_64_t specify value
/// @return int
int XPLINK_PACK(uint8_t *, xp_packet_t *);

/// @brief UNpack an XPLink packet
/// @param  xp_packet_t* Output packet
/// @param  uint8_t* input byte
/// @return 1 or 0 -> success of unpack
uint8_t XPLINK_UNPACK(xp_packet_t *, uint8_t);

/// @brief Pack a byte array using cobs with the specified end bit
/// @param  uint8_t* Return Packet
/// @param  uint8_t* Packet to pack
/// @param  uint8_t Packet size
/// @param  uint8_t End Byte
/// @return uint8_t byte array
uint8_t *COBS_PACK(uint8_t *, uint8_t *, uint8_t, uint8_t);

/// @brief Pack a byte array using cobs with the specified end bit
/// @param  uint8_t Return Packet
/// @param  uint8_t Packet to unpack
/// @param  uint8_t input Packet size
/// @param  uint8_t End Byte
/// @return uint8_t byte array
uint8_t *COBS_UNPACK(uint8_t *, uint8_t *, uint8_t, uint8_t);

#endif