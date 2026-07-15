/* BASELINE SENDER (C) — naive on purpose. Rewrite it (C, C++, Go, or Rust).
 *
 * Ports (all 127.0.0.1):
 *   bind 47010  <- harness source delivers frame i here at t0 + i*20ms
 *                  (format: 4-byte big-endian seq + 160-byte payload)
 *   send 47001  -> relay uplink toward the receiver (YOUR wire format)
 *   bind 47004  <- feedback from your receiver, via the relay (optional)
 *
 * This baseline forwards each frame once, unchanged, and ignores feedback.
 * No redundancy, no retransmission. It cannot pass. That is the point.
 *
 * Env vars available if you want them: T0 (epoch seconds, float),
 * DURATION_S, DELAY_MS. The harness kills this process when the run ends,
 * so a forever-loop is fine.
 *
 * build: make        run: python3 run.py --delay_ms 60
 */
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdint.h>

typedef uint32_t u32;
typedef unsigned char byte;
// Step 1: Payload size from the harness.
#define PAYLOAD_LEN 160
// Step 1: Packet type for normal data packets.
#define TYPE_DATA 1
// Step 1: Our custom packet size.
#define PKT_LEN (1 + 4 + PAYLOAD_LEN)


// Step 1: Storing a 32-bit integer in network byte order.
static void put_u32(byte *p, u32 value)
{
    p[0] = (byte)(value>>24);
    p[1] = (byte)(value>>16);
    p[2] = (byte)(value>>8);
    p[3] = (byte)value;
}

// Step 1: Reading a 32-bit integer from network byte order.
static u32 get_u32(const byte *p)
{
    return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|(u32)p[3];
}

int main(void)
{
    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr = {0};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47010);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (bind(in_fd, (struct sockaddr *)&in_addr, sizeof in_addr) < 0) {
        perror("bind 47010");
        return 1;
    }

    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in relay = {0};
    relay.sin_family = AF_INET;
    relay.sin_port = htons(47001);
    relay.sin_addr.s_addr = inet_addr("127.0.0.1");

    byte in_buf[2048];
    /* Step 1: Outgoing packet in our own protocol */
    byte out_pkt[PKT_LEN];

    for (;;)
    {
        ssize_t n = recvfrom(in_fd,in_buf,sizeof(in_buf),0,NULL,NULL);
        if (n<(ssize_t)(4+PAYLOAD_LEN))continue;
        u32 seq = get_u32(in_buf);
        const byte *payload = in_buf + 4;
        out_pkt[0] = TYPE_DATA;
        put_u32(out_pkt+1,seq);
        memcpy(out_pkt+5,payload,PAYLOAD_LEN);
        sendto(out_fd,out_pkt,PKT_LEN,0,(struct sockaddr *)&relay,sizeof(relay));
    }
    return 0;
}
