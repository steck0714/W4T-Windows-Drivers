/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include "../src/t214/emc_bpmp.h"
struct fixture {
    uint8_t mail[128];
    uint32_t token, writes, rings, frees, barriers;
    uint64_t now;
    int timeout, response_error;
};
static uint32_t token(void *p) { return ((struct fixture *)p)->token; }
static void clear(void *p, uint32_t bits)
{
    struct fixture *f = p;
    assert(bits == (3u<<8) || bits == (1u<<8));
    if (bits == (3u<<8)) assert(f->barriers);
    else { assert(((f->token>>8)&3u) == 3u); ++f->frees; }
    f->token &= ~bits;
}
static void read_mail(void *p,uint32_t off,void *out,uint32_t n)
{
    assert(off + n <= 128);
    memcpy(out,((struct fixture *)p)->mail+off,n);
}
static void write_mail(void *p,uint32_t off,const void *in,uint32_t n)
{
    struct fixture *f=p;
    assert(off + n <= 128 && ((f->token>>8)&3u)==2u);
    memcpy(f->mail+off,in,n); ++f->writes;
}
static void barrier(void *p) { ++((struct fixture *)p)->barriers; }
static void ring(void *p)
{
    struct fixture *f=p;uint32_t mrq,flags;
    memcpy(&mrq,f->mail,4);memcpy(&flags,f->mail+4,4);
    assert(((f->token>>8)&3u)==0 && mrq==22 && flags==1);
    ++f->rings;
}
static uint64_t now(void *p) { return ((struct fixture *)p)->now; }
static void wait_one(void *p)
{
    struct fixture *f=p;int32_t result=f->response_error;
    uint64_t hz=2133000000ULL;
    f->now+=1000;
    if (!f->timeout) {
        memcpy(f->mail,&result,4);memcpy(f->mail+8,&hz,8);
        f->token|=3u<<8;
    }
}
static const struct sw_emc_mail_ops ops={token,clear,read_mail,write_mail,barrier,ring,now,wait_one};
int main(void)
{
    struct fixture f={0};struct sw_emc_mailbox m;
    uint32_t command=1u<<24,writes;uint64_t output=0;
    f.token=2u<<8;
    assert(sw_emc_mailbox_init(&m,ops,&f,0)==SwBpmpInvalid);
    assert(!sw_emc_mailbox_init(&m,ops,&f,4));
    assert(!sw_emc_mailbox_message(&m,22,&command,4,&output,8));
    assert(output==2133000000ULL && f.frees==1 && ((f.token>>8)&3u)==2u);
    f.response_error=-22;output=123;
    assert(sw_emc_mailbox_message(&m,22,&command,4,&output,8)==-22);
    assert(output==123 && f.frees==2);
    f.response_error=0;f.timeout=1;
    assert(sw_emc_mailbox_message(&m,22,&command,4,&output,8)==SwBpmpTimeout);
    assert(m.poisoned && f.now>=500000 && f.frees==2);
    writes=f.writes;
    assert(sw_emc_mailbox_message(&m,22,&command,4,&output,8)==SwBpmpPoisoned);
    assert(f.writes==writes && f.frees==2);
    f.token=3u<<8;
    assert(sw_emc_mailbox_init(&m,ops,&f,4)==SwBpmpBusy);
    assert(f.frees==2); /* do not steal an already-acked channel */
    puts("PASS: threaded BPMP tokens, payload ordering, firmware errors and timeout ownership (model only)");
    return 0;
}
