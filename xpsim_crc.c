// xpsim.c - XPLink sandbox: two-terminal link simulation + efficiency benchmark
// Build:  gcc -O2 -Wall -o xpsim xpsim.c XPLink.c
// Terminal 1:  ./xpsim flight
// Terminal 2:  ./xpsim ground
// Benchmark:   ./xpsim bench [num_packets] [baud]
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include "XPLink.h"

#define WIRE_LEN 12                    // 10 byte packet + 1 COBS code + 1 end byte
#define FIFO_PATH "/tmp/xplink_g2f"    // the "radio": ground writes, flight reads

static const char *TYPE_NAMES[] = {
    "CMD", "ACC", "TEMP1", "TEMP2", "TEMP3", "TEMP4",
    "PRESSURE1", "PRESSURE2", "PRESSURE3", "PRESSURE4", "PRESSURE5", "PRESSURE6",
    "PRESSURE7", "PRESSURE8", "PRESSURE9", "PRESSURE10", "PRESSURE11", "PRESSURE12",
    "THRUST", "SOLENOID", "XP_STATE", "SWITCHES"};
#define NTYPES ((int)(sizeof(TYPE_NAMES) / sizeof(TYPE_NAMES[0])))

// ---------- helpers ----------
static double now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e9 + t.tv_nsec;
}

static void print_hex(const char *label, const uint8_t *b, int n)
{
    printf("  %-8s", label);
    for (int i = 0; i < n; i++)
        printf("%02X ", b[i]);
    printf("\n");
}

// fewest bytes needed to hold the value (at least 1)
static int useful_bytes(uint64_t v)
{
    int n = 1;
    while (v >>= 8)
        n++;
    return n;
}

// flip `flips` distinct random bits somewhere in buf (simulated radio noise)
static void flip_bits(uint8_t *buf, int len, int flips)
{
    int total = len * 8, chosen[64];
    for (int f = 0; f < flips && f < 64; f++)
    {
        int bit, dup;
        do
        {
            bit = rand() % total;
            dup = 0;
            for (int k = 0; k < f; k++)
                if (chosen[k] == bit)
                    dup = 1;
        } while (dup);
        chosen[f] = bit;
        buf[bit / 8] ^= (uint8_t)(1u << (bit % 8));
    }
}

static int parse_type(const char *s)
{
    if (s[0] >= '0' && s[0] <= '9')
    {
        int v = atoi(s);
        return (v >= 0 && v < NTYPES) ? v : -1;
    }
    for (int i = 0; i < NTYPES; i++)
        if (strcasecmp(s, TYPE_NAMES[i]) == 0)
            return i;
    return -1;
}

static void open_fifo_exists(void)
{
    if (mkfifo(FIFO_PATH, 0666) != 0 && errno != EEXIST)
    {
        perror("mkfifo");
        exit(1);
    }
}

// ---------- FLIGHT COMPUTER (receiver) ----------
static int flight(void)
{
    open_fifo_exists();
    printf("[flight] waiting for ground to connect...\n");
    int fd = open(FIFO_PATH, O_RDONLY);
    if (fd < 0)
    {
        perror("open");
        return 1;
    }
    printf("[flight] link up. listening.\n\n");

    uint8_t b;
    xp_packet_t pk;
    long ok = 0, bad = 0, bytes = 0;
    int first = 1;
    double t_first = 0, t_last = 0;

    while (read(fd, &b, 1) == 1)
    {
        bytes++;
        t_last = now_ns();
        if (bytes == 1)
            t_first = t_last;

        // the first 0x00 is just the sync byte ground sends on connect
        if (first && b == END_BYTE)
        {
            XPLINK_UNPACK(&pk, b);
            first = 0;
            continue;
        }
        first = 0;

        if (XPLINK_UNPACK(&pk, b))
        {
            ok++;
            const char *name = (pk.type >= 0 && pk.type < NTYPES) ? TYPE_NAMES[pk.type] : "???";
            printf("[flight] ACK  #%ld  from=0x%02X  type=%s  value=%llu  checksum OK\n",
                   ok, pk.sender_id, name, (unsigned long long)pk.data);
        }
        else if (b == END_BYTE)
        {
            bad++;
            printf("[flight] DROP  frame ended but failed unpack/checksum (corrupt)\n");
        }
    }

    double secs = (t_last - t_first) / 1e9;
    printf("\n[flight] link closed. summary:\n");
    printf("  bytes received : %ld\n  packets ACKed  : %ld\n  packets dropped: %ld\n", bytes, ok, bad);
    if (secs > 0)
        printf("  first->last byte: %.3f s\n", secs);
    return 0;
}

// ---------- GROUND COMPUTER (sender) ----------
static int noise_pct = 0, noise_flips = 1;

static void send_packet(int fd, int type, uint64_t value)
{
    xp_packet_t pk = {.type = (xp_msg_t)type, .data = value, .sender_id = SYS_ID};
    uint8_t wire[WIRE_LEN];

    if (value >> 56)
        printf("  warning: value needs more than 7 bytes, top byte will be cut off\n");

    double t0 = now_ns();
    XPLINK_PACK(wire, &pk);
    double dt = now_ns() - t0;

    int need = useful_bytes(value & 0x00FFFFFFFFFFFFFFULL);
    printf("  sending %s = %llu\n", TYPE_NAMES[type], (unsigned long long)value);
    print_hex("COBS'd:", wire, WIRE_LEN);
    printf("  pack time %.0f ns | wire %d B | data needed %d B | efficiency %.1f%%\n",
           dt, WIRE_LEN, need, 100.0 * need / WIRE_LEN);

    if (noise_pct > 0 && (rand() % 100) < noise_pct)
    {
        flip_bits(wire, WIRE_LEN, noise_flips);
        print_hex("NOISY:", wire, WIRE_LEN);
        printf("  (radio noise flipped %d bit(s))\n", noise_flips);
    }
    if (write(fd, wire, WIRE_LEN) != WIRE_LEN)
        perror("write");
}

static int ground(void)
{
    open_fifo_exists();
    printf("[ground] waiting for flight computer...\n");
    int fd = open(FIFO_PATH, O_WRONLY);
    if (fd < 0)
    {
        perror("open");
        return 1;
    }
    uint8_t sync = END_BYTE; // lets the receiver lock onto packet boundaries
    write(fd, &sync, 1);

    printf("[ground] link up.\n"
           "  send <TYPE|number> <value>   e.g. send TEMP1 1234\n"
           "  burst <n>                    send n random packets\n"
           "  noise <percent> [bits]       chance a packet gets bit flips (default 1 bit)\n"
           "  types                        list packet types\n"
           "  quit\n\n");

    char line[128];
    srand((unsigned)time(NULL));
    while (printf("ground> "), fflush(stdout), fgets(line, sizeof line, stdin))
    {
        char cmd[16] = "", a[32] = "", c[32] = "";
        int n = sscanf(line, "%15s %31s %31s", cmd, a, c);
        if (n < 1)
            continue;
        if (!strcasecmp(cmd, "quit") || !strcasecmp(cmd, "q"))
            break;
        else if (!strcasecmp(cmd, "types"))
            for (int i = 0; i < NTYPES; i++)
                printf("  %2d %s\n", i, TYPE_NAMES[i]);
        else if (!strcasecmp(cmd, "send") && n == 3)
        {
            int t = parse_type(a);
            if (t < 0)
                printf("  unknown type (try 'types')\n");
            else
                send_packet(fd, t, strtoull(c, NULL, 0));
        }
        else if (!strcasecmp(cmd, "burst") && n >= 2)
        {
            int k = atoi(a);
            for (int i = 0; i < k; i++)
                send_packet(fd, rand() % NTYPES, ((uint64_t)rand() << 16) ^ rand());
        }
        else if (!strcasecmp(cmd, "noise") && n >= 2)
        {
            noise_pct = atoi(a);
            noise_flips = (n == 3) ? atoi(c) : 1;
            printf("  noise: %d%% of packets get %d flipped bit(s)\n", noise_pct, noise_flips);
        }
        else
            printf("  ?\n");
    }
    close(fd);
    return 0;
}

// ---------- BENCHMARK ----------
typedef struct { long ok, dropped, wrong; } Res;

static Res run_trials(int n, int flips)
{
    Res r = {0, 0, 0};
    uint8_t wire[WIRE_LEN];
    xp_packet_t in, out;
    XPLINK_UNPACK(&out, END_BYTE); // sync
    for (int i = 0; i < n; i++)
    {
        in.type = (xp_msg_t)(rand() % NTYPES);
        in.sender_id = SYS_ID;
        in.data = 0;
        int sz = 1 + rand() % 7;
        for (int k = 0; k < sz; k++)
            in.data |= (uint64_t)(rand() & 0xFF) << (8 * k);

        XPLINK_PACK(wire, &in);
        if (flips > 0)
            flip_bits(wire, WIRE_LEN, flips);

        int got = 0;
        for (int j = 0; j < WIRE_LEN; j++)
            if (XPLINK_UNPACK(&out, wire[j]))
                got = 1;
        if (!got)
            r.dropped++;
        else if (out.type == in.type && out.data == in.data)
            r.ok++;
        else
            r.wrong++;
    }
    return r;
}

static int bench(int n, int baud)
{
    srand(12345);
    printf("XPLink benchmark: %d packets, %d baud (8N1 = 10 bits/byte)\n\n", n, baud);

    // 1. size / airtime
    printf("1) Size efficiency (every packet is %d bytes on the wire)\n", WIRE_LEN);
    printf("   data size | efficiency | overhead | airtime  | max pkts/s | useful B/s\n");
    int sizes[] = {1, 2, 4, 7};
    for (int s = 0; s < 4; s++)
    {
        double air = WIRE_LEN * 10.0 / baud;
        printf("   %5d B   |  %5.1f%%    |  %2d B    | %6.2f ms | %8.1f   | %8.1f\n",
               sizes[s], 100.0 * sizes[s] / WIRE_LEN, WIRE_LEN - sizes[s],
               air * 1000, 1.0 / air, sizes[s] / air);
    }

    // 2. CPU speed (clean link)
    xp_packet_t p = {.type = TEMP1, .data = 0x1234, .sender_id = SYS_ID}, out;
    uint8_t wire[WIRE_LEN];
    double t0 = now_ns();
    for (int i = 0; i < n; i++)
    {
        p.data = i;
        XPLINK_PACK(wire, &p);
    }
    double t_pack = (now_ns() - t0) / n;
    XPLINK_PACK(wire, &p);
    t0 = now_ns();
    for (int i = 0; i < n; i++)
        for (int j = 0; j < WIRE_LEN; j++)
            XPLINK_UNPACK(&out, wire[j]);
    double t_unpack = (now_ns() - t0) / n;
    printf("\n2) CPU cost: pack %.0f ns/packet, unpack %.0f ns/packet (on this PC, not the MCU)\n",
           t_pack, t_unpack);

    // 3. error handling
    printf("\n3) Corruption test (random bit flips per packet)\n");
    printf("   flips | delivered ok | dropped | ACCEPTED BUT WRONG\n");
    int fl[] = {0, 1, 2, 3, 8};
    for (int k = 0; k < 5; k++)
    {
        Res r = run_trials(n, fl[k]);
        printf("   %4d  | %9.2f%%   | %6.2f%% | %7.3f%%  (%ld pkts)\n", fl[k],
               100.0 * r.ok / n, 100.0 * r.dropped / n, 100.0 * r.wrong / n, r.wrong);
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "flight"))
        return flight();
    if (argc >= 2 && !strcmp(argv[1], "ground"))
        return ground();
    if (argc >= 2 && !strcmp(argv[1], "bench"))
        return bench(argc > 2 ? atoi(argv[2]) : 100000, argc > 3 ? atoi(argv[3]) : 9600);
    printf("usage: %s flight | ground | bench [n] [baud]\n", argv[0]);
    return 1;
}
