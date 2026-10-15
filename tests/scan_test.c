/* SPDX-License-Identifier: MIT */
#include "ar_scan.h"
#include <assert.h>
#include <stdio.h>
static uint32_t effects[16][2];
static unsigned count;
static void write(uint32_t a,uint32_t v,void *context)
{
 (void)context; assert(count<16); effects[count][0]=a;effects[count++][1]=v;
}
int main(void)
{
 uint32_t rows[]={0x11580000,0x10590000,0x115a0000}; uint16_t channel=0;
 assert(!ar_scan_advance(rows,3,&channel,0,0) && channel==2);
 assert(!ar_scan_advance(rows,3,&channel,0,0) && channel==0);
 channel=255;assert(!ar_scan_advance(rows,3,&channel,0,0) && channel==0);
 channel=0x102;assert(!ar_scan_advance(rows,3,&channel,1,4) && channel==0x202);
 channel=0x402;assert(!ar_scan_advance(rows,3,&channel,1,4) && channel==0x102);
 assert(!ar_scan_advance(rows,3,&channel,0,4) && channel==0x102);
 channel=2;rows[0]&=~0x1000000;rows[2]&=~0x1000000;
 assert(!ar_scan_advance(rows,3,&channel,0,0) && channel==2);
 assert(ar_scan_advance(rows,256,&channel,0,0)==-22 && channel==2);
 assert(!ar_scan_advance(NULL,0,&channel,0,0) && channel==2);
 assert(!ar_scan_tune(5770000,0x123456789a,0,write,write,NULL));
 assert(count==8);
 for(unsigned i=0;i<5;i++){assert(effects[i][0]==0x90a+i && effects[i][1]==0x12+i*0x22);}
 assert(effects[5][0]==0x9fc1903 && effects[5][1]==4);
 assert(effects[6][0]==0x9fc1601 && effects[6][1]==1);
 assert(effects[7][0]==0x9fc1201 && effects[7][1]==1);
 assert(ar_scan_tune(5770000,1ULL<<40,0,write,write,NULL)==-22 && count==8);
 assert(ar_scan_tune(5770000,0,2,write,write,NULL)==-22 && count==8);
 puts("measurement scan advancement, tune fields and validation passed");
}
