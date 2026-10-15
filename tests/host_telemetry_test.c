/* SPDX-License-Identifier: MIT */
#include "ar_host_telemetry.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct ar_host_telemetry s;
static void reset(void) { ar_host_telemetry_init(&s); ar_host_telemetry_link(&s, 1); }
static uint32_t crc32(const uint8_t *p, size_t n)
{
 uint32_t c = 0;
 for (size_t i = 0; i < n; i++) {
  c ^= p[i]; for (int j = 0; j < 8; j++) { c = c & 1 ? (c >> 1) ^ 0x82f63b78u : c >> 1; }
 }
 return c;
}
static uint8_t crc8(const uint8_t *p, size_t n)
{
 uint8_t c = 0;
 for (size_t i = 0; i < n; i++) {
  c ^= p[i]; for (int j = 0; j < 8; j++) { c = c & 128 ? (c << 1) ^ 0xd5 : c << 1; }
 }
 return c;
}
static void put16(uint8_t *p, unsigned x) { p[0] = x; p[1] = x >> 8; }
static size_t outer(uint8_t *out, const uint8_t *p, size_t n, unsigned flags)
{
 out[0] = 0xfe; out[1] = 0xa5; out[3] = 17; put16(out + 4, (n << 4) | flags);
 out[2] = out[3] + out[4] + out[5]; memcpy(out + 6, p, n); out[n+6] = 9;
 uint32_t c = crc32(out, n+7); put16(out+n+7, c); put16(out+n+9, c >> 16); return n+11;
}
static size_t msp(uint8_t *out, unsigned command, const uint8_t *p, size_t n, int version)
{
 memcpy(out, version == 1 ? "$M>" : "$X>", 3);
 if (version == 1) {
  out[3] = n; out[4] = command; memcpy(out+5,p,n); uint8_t c = 0;
  for (size_t i=3;i<n+5;i++) { c ^= out[i]; } out[n+5] = c; return n+6;
 }
 out[3]=0; put16(out+4,command);put16(out+6,n);memcpy(out+8,p,n);out[n+8]=crc8(out+3,n+5);return n+9;
}
static void feed(const uint8_t *p, size_t n, uint32_t now) { ar_host_telemetry_feed(&s,p,n,now); }
int main(void)
{
 uint8_t app[4095]={5}, wire[8200], nested[1024], body[512]={0};
 app[1]=250;put16(app+11,12345);
 size_t n=outer(wire,app,39,0);
 for (size_t split=0;split<=n;split++) {
  reset();feed(wire,split,100);feed(wire+split,n-split,101);
  assert(s.info.seen==1 && s.info.records[0].length==39 && s.info.records[0].count==1);
  assert(!memcmp(s.info.records[0].data,app,39));
 }
 /* Largest legal envelope, including a 64-byte record prefix and zero tail. */
 n=outer(wire,app,sizeof app,0);reset();
 for(size_t i=0;i<n;i++) { feed(wire+i,1,200); }
 assert(s.info.frames==1 && s.info.records[0].length==64 && s.used==0);
 n=outer(wire,app,15,0);feed(wire,n,201);assert(s.info.records[0].data[15]==0);
 /* Every split of an MSP reply across TWO outer application records. */
 memcpy(body,"TEST",4);
 for(int version=1;version<=2;version++) {
  size_t m=msp(nested,2,body,4,version);
  for(size_t split=0;split<=m;split++) {
   reset();app[0]=2;memcpy(app+1,nested,split);n=outer(wire,app,split+1,0);feed(wire,n,300);
   memcpy(app+1,nested+split,m-split);n=outer(wire,app,m-split+1,0);feed(wire,n,301);
   assert(s.info.seen==2 && s.info.msp_frames==1 && !memcmp(s.info.records[1].data,"TEST",4));
  }
 }
 /* Several messages in one application record, including MSPv2-in-v1. */
 size_t m=msp(nested,3,body,3,2);
 size_t wrapped=msp(nested+100,255,nested+3,m-3,1);
 app[0]=2;memcpy(app+1,nested+100,wrapped);
 size_t a=msp(app+1+wrapped,101,body,27,1);
 size_t b=msp(app+1+wrapped+a,110,body,9,2);
 n=outer(wire,app,1+wrapped+a+b,0);reset();feed(wire,n,400);
 assert(s.info.seen==28 && s.info.msp_frames==3);
 /* ACKs, other channels, zero payload, short known messages, unknown MSP. */
 reset();app[0]=5;
 for(unsigned f=1;f<16;f++) { n=outer(wire,app,39,f);feed(wire,n,500); }
 n=outer(wire,app,0,0);feed(wire,n,500);assert(s.info.seen==0 && s.info.ignored==16);
 n=outer(wire,app,14,0);feed(wire,n,500);assert(s.info.short_records==1);
 app[0]=2;m=msp(app+1,110,body,6,1);n=outer(wire,app,m+1,0);feed(wire,n,500);
 assert(s.info.short_records==2 && s.info.seen==0);
 m=msp(app+1,182,body,18,2);n=outer(wire,app,m+1,0);feed(wire,n,500);
 assert(s.info.msp_frames==2 && s.info.seen==0);
 /* Both nested checksums matter, outer corruption cannot update records. */
 reset();app[0]=2;m=msp(app+1,110,body,9,2);app[m]^=1;n=outer(wire,app,m+1,0);feed(wire,n,600);
 assert(s.info.msp_bad==1 && s.info.seen==0);
 app[m]^=1;n=outer(wire,app,m+1,0);wire[n-1]^=1;feed(wire,n,600);
 assert(s.info.bad_crc==1 && s.info.seen==0);
 wire[n-1]^=1;feed(wire,n,601);assert(s.info.seen==16 && s.info.records[4].count==1);
 /* Oversize nested frames, max supported payload, resynchronization. */
 reset();app[0]=2;memcpy(app+1,"$X>\0\x02\0\xff\xff",8);
 m=msp(app+9,2,body,4,1);n=outer(wire,app,m+9,0);feed(wire,n,700);
 assert(s.info.msp_oversize==1 && s.info.seen==2);
 m=msp(app+1,101,body,512,2);n=outer(wire,app,m+1,0);feed(wire,n,701);
 assert(s.info.records[3].length==64 && s.msp_used==0);
 /* Idle partials expire (including the millisecond counter wrap). */
 reset();app[0]=5;n=outer(wire,app,15,0);feed(wire,8,UINT32_MAX-10);
 feed(wire,n,2500);assert(s.info.partial_resets==1 && s.info.seen==1);
 reset();app[0]=2;m=msp(nested,2,body,4,1);memcpy(app+1,nested,5);
 n=outer(wire,app,6,0);feed(wire,n,0);memcpy(app+1,nested,m);
 n=outer(wire,app,m+1,0);feed(wire,n,2001);assert(s.info.partial_resets==1 && s.info.seen==2);
 /* Link transitions clear partials and old values, preserve diagnostics. */
 feed(wire,7,2002);ar_host_telemetry_link(&s,0);
 assert(s.used==0 && s.msp_used==0 && s.info.seen==0 && !s.info.records[1].count);
 uint32_t frames=s.info.frames;feed(wire,n,2003);assert(s.info.frames==frames);
 ar_host_telemetry_link(&s,1);feed(wire,n,2004);assert(s.info.sessions==2 && s.info.seen==2);
 /* Deterministic noisy input exercises bounded resync under sanitizers. */
 reset();uint32_t x=29;
 for(unsigned i=0;i<100000;i++) { x=x*1664525u+1013904223u;uint8_t c=x>>24;feed(&c,1,i); }
 ar_host_telemetry_link(&s,0);ar_host_telemetry_link(&s,1);feed(wire,n,100001);
 assert(s.info.seen==2);
 _Static_assert(sizeof(struct ar_host_telemetry_info)==444,"wire snapshot");
 puts("host telemetry: fragmented envelopes, nested MSP, corruption, bounds and resets passed");
}
