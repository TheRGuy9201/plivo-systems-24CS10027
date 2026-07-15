/* BASELINE RECEIVER (C) — naive on purpose. Rewrite it (C, C++, Go, or Rust).
 *
 * Ports (all 127.0.0.1):
 *   bind 47002  <- media from your sender, via the hostile relay
 *   send 47020  -> harness player. MUST be: 4-byte big-endian seq +
 *                  160-byte payload. Frame i counts only if it arrives
 *                  BEFORE its deadline t0 + DELAY_MS + i*20ms.
 *   send 47003  -> feedback to your sender, via the relay (optional)
 *
 * This baseline forwards whatever arrives straight to the player: lost
 * frames stay lost, late frames stay late, duplicates are re-sent
 * harmlessly. All yours to fix — jitter buffer, reordering, recovery.
 *
 * Env vars available: T0, DURATION_S, DELAY_MS. Harness kills the process
 * at run end; a forever-loop is fine.
 */
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdint.h>
//Step 3
typedef uint32_t u32;
typedef unsigned char byte;
#define PAYLOAD_LEN 160
#define TYPE_DATA 1
#define TYPE_PARITY  2
#define GROUP_SIZE   2
#define PKT_LEN (1 + 4 + PAYLOAD_LEN)
#define GROUP_WINDOW 4096
static void put_u32(byte *p, u32 value)
{
    p[0] = (byte)(value>>24);
    p[1] = (byte)(value>>16);
    p[2] = (byte)(value>>8);
    p[3] = (byte)value;
}
static u32 get_u32(const byte *p)
{
    return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|(u32)p[3];
}
//Step 3: Store one FEC group.
typedef struct
{
    u32 group_id;
    int valid;
    byte received_mask;
    byte forwarded_mask;     // Step 4: Will track forwarded packets later.
    int has_parity;
    byte payload[GROUP_SIZE][PAYLOAD_LEN];
    byte parity[PAYLOAD_LEN];

}Group;

static Group groups[GROUP_WINDOW];

//Step 3: Get the storage corresponding to a group.
Group *get_group(u32 group_id)
{
    Group *g = &groups[group_id%GROUP_WINDOW];
    if (!g->valid||g->group_id!=group_id)
    {
        memset(g,0,sizeof(*g));
        g->group_id = group_id;
        g->valid = 1;
    }
    return g;
}

//Step 4: Count number of received packets in a group.
int popcount8(byte mask)
{
    int cnt = 0;
    while(mask)
    {
        cnt+=(mask&1);
        mask>>=1;
    }
    return cnt;
}
//Step 4: Recovering one missing packet using XOR parity.
void try_reconstruct(Group *g)
{
    if(!g->has_parity)return;
    // Need exactly one missing packet.
    if(popcount8(g->received_mask)!=GROUP_SIZE-1)return;
    int missing = -1;
    for(int i = 0; i < GROUP_SIZE; i++)
    {
        if(!(g->received_mask&(1<<i)))
        {
            missing = i;
            break;
        }
    }
    if(missing==-1)return;
    byte rebuilt[PAYLOAD_LEN];
    memcpy(rebuilt,g->parity,PAYLOAD_LEN);
    for(int i = 0; i<GROUP_SIZE; i++)
    {
        if(i==missing)continue;
        for(int j = 0; j<PAYLOAD_LEN; j++)
        {
            rebuilt[j]^=g->payload[i][j];
        }
    }
    memcpy(g->payload[missing],rebuilt,PAYLOAD_LEN);
    g->received_mask|=(1<<missing);
}

int main(void) {
    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr = {0};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47002);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (bind(in_fd, (struct sockaddr *)&in_addr, sizeof in_addr) < 0) {
        perror("bind 47002");
        return 1;
    }

    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in player = {0};
    player.sin_family = AF_INET;
    player.sin_port = htons(47020);
    player.sin_addr.s_addr = inet_addr("127.0.0.1");
    //Step 3: Receive packet in our protocol.
    byte buf[2048];
    byte out_pkt[4+PAYLOAD_LEN];
    for (;;) {
        ssize_t n = recvfrom(in_fd, buf, sizeof buf, 0, NULL, NULL);
        if(n!=PKT_LEN)continue;
        //Step 3: Decode our packet.
        byte type = buf[0];
        u32 field = get_u32(buf+1);
        const byte *payload = buf+5;
        Group *g;
        if(type==TYPE_DATA)
        {
            u32 seq = field;
            u32 group_id = seq/GROUP_SIZE;
            u32 pos = seq%GROUP_SIZE;
            g = get_group(group_id);
            if(!(g->received_mask&(1<<pos)))
            {
                memcpy(g->payload[pos],payload,PAYLOAD_LEN);
                g->received_mask |= (1<<pos);
            }
        }
        else if(type==TYPE_PARITY)
        {
            u32 group_id = field;
            g = get_group(group_id);
            if(!g->has_parity)
            {
                memcpy(g->parity,payload,PAYLOAD_LEN);
                g->has_parity = 1;
            }
        }
        else continue;
        try_reconstruct(g);//Step 4: recovering a missing packet
        //Step 5: Forward every packet that is available but has not yet been forwarded.
        for(int p = 0; p < GROUP_SIZE; p++)
        {
            if((g->received_mask&(1<<p))&&!(g->forwarded_mask&(1<<p)))
            {
                u32 seq = g->group_id*GROUP_SIZE+p;
                put_u32(out_pkt, seq);
                memcpy(out_pkt+4,g->payload[p],PAYLOAD_LEN);
                sendto(out_fd,out_pkt,sizeof(out_pkt),0,(struct sockaddr *)&player,sizeof(player));
                g->forwarded_mask|=(1<<p);
            }
        }
    }
    return 0;
}
