#include "XPLink.h"
#include <stdio.h>
#include <string.h>

#define BUFFERSIZE 255

// vars for unpacking
static uint8_t BUFFER[BUFFERSIZE];
static uint8_t BUFF_HEAD = 0;
static uint8_t PREV_END = 0;

// CRC-8 (polynomial 0x07) over len bytes. Starts at 0xFF instead of 0 so an
// all-zero frame can never look valid.
static uint8_t crc8(const uint8_t *data, int len)
{
    uint8_t crc = 0xFF;
    for (int i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
    return crc;
}

int XPLINK_PACK(uint8_t *output, xp_packet_t *xppack)
{
    uint8_t packet[10];

    // create struct without cobs
    for (int i = 0; i < 10; i++)
    {
        packet[i] = 0;
    }

    // Sender ID
    packet[0] = SYS_ID;

    // Packet type
    packet[1] = xppack->type;

    // Pack 7 least significant bytes of the value (little-endian)
    for (int i = 0; i < 7; i++)
    {
        packet[2 + i] = (xppack->data >> (i * 8)) & 0xFF;
    }
    //switch statement for the different types of packets?

    // checksum (CRC-8 over the first 9 bytes)
    packet[9] = crc8(packet, 9);
    // what could be a better checksum?

    /*
    printf("INITIAL PACKET: \n");
    for (int i = 0; i < 10; i++)
    {
        printf("%x ", packet[i]);
    }
    */


    // THIS MIGHTVE BEEN LEFT FROM THE PRINT TEST
    /*printf(
        "\n\n");*/

    COBS_PACK(output, packet, 10, END_BYTE);

    return 1;
}

uint8_t XPLINK_UNPACK(xp_packet_t *output, uint8_t byte)
{

    // prevent buffer overflow
    if (BUFF_HEAD >= BUFFERSIZE)
    {
        BUFF_HEAD = 0;
        PREV_END = 0;
    }

    if (PREV_END == 0 && BUFF_HEAD == 0 && byte != END_BYTE) // return instantly if we havent yet received an end byte
    {
        return 0;
    }

    // check for start of new packet
    if (PREV_END)
    {
        PREV_END = 0;
        // reset buffer
        BUFF_HEAD = 0;
    }

    // add byte to buffer
    BUFFER[BUFF_HEAD] = byte;
    BUFF_HEAD++;

    // if end byte, collect buffer
    if (byte == END_BYTE)
    {
        PREV_END = 1;

        // a valid frame is always exactly 12 bytes (10 data + code byte + end byte)
        if (BUFF_HEAD != 12)
            return 0;

        // unpack the buffer
        uint8_t raw[11] = {0};

        COBS_UNPACK(raw, BUFFER, BUFF_HEAD, END_BYTE);

        // Checksum
        uint8_t calculated_checksum = crc8(raw, 9);

        if (calculated_checksum != raw[9])
        {
            // Checksum failed, packet is corrupt
            return 0;
        }

        output->sender_id = raw[0];
        output->type = raw[1];
        output->data = 0; // Clear old data
        for (int i = 0; i < 7; i++)
        {
            // get data
            output->data |= ((uint64_t)raw[2 + i] << (i * 8));
        }
        return 1;
    }

    return 0;
}

uint8_t *COBS_PACK(uint8_t *output, uint8_t *input, uint8_t size, uint8_t end_byte)
{

    uint8_t curr_idx = 0;

    for (int i = 0; i < size; i++)
    {

        if (input[i] == end_byte)
        {
            output[curr_idx] = (i - curr_idx) + 1;
            curr_idx = i + 1;
        }
        else
        {
            output[i + 1] = input[i];
        }

        if (i == size - 1)
        {
            output[curr_idx] = (i - curr_idx) + 2;
        }
    }

    output[size + 1] = end_byte;

    return output;
}

uint8_t *COBS_UNPACK(uint8_t *out, uint8_t *in, uint8_t insize, uint8_t end_byte)
{

    uint8_t read_head = 0;
    uint8_t write_head = 0;

    uint8_t read_step = in[read_head];

    while (read_step != end_byte)
    {
        // prevent crashing on garbled data
        if (read_step == 0 || (read_head + read_step) >= insize || (write_head + read_step) > 11)
            break;

        memcpy(&out[write_head], &in[read_head + 1], read_step - 1);
        // increment
        read_head += read_step;
        write_head += read_step;
        read_step = in[read_head];

        // add additional zero
        out[write_head - 1] = 0;
    }

    return out;
}
