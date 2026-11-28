#!/usr/bin/env python3
"""Exercise actual dump callbacks with synthetic card identities and SDHC I/O.

These are host fault-injection tests, not evidence of a Windows crash dump.
No private card header, device access or installation is required.
"""
from pathlib import Path
import ast
import os
import re
import struct
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/nvtsdhc.c').read_text()
board = (root / 'src/t214_sdmmc.h').read_text()
dump = (root / 'src/t214_crashdump.h').read_text()
tree = ast.parse((root / 'tests/test-t214-data.py').read_text())
extract = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'function')
exec(compile(ast.Module(body=[extract], type_ignores=[]), '<extract-c>', 'exec'))


def target_header(visible):
    """Use the real target filter with public, deliberately synthetic data."""
    tail = bytearray(70)
    struct.pack_into('<I', tail, 0, 0x12345678)
    tail[10] = 0x0c
    struct.pack_into('<II', tail, 14, 2048, 8192)
    tail[26] = 0xee
    struct.pack_into('<II', tail, 30, 1, 2047)
    tail[58] = 7
    struct.pack_into('<II', tail, 62, 32768, 167772160)
    values = {
        'WINARM_FIRST': '32768', 'WINARM_COUNT': '167772160',
        'FAT32_EXTENT': '#define FAT32_FIRST 2048u\n#define FAT32_COUNT 8192u' if visible else '',
        'WINARM_SERIAL': '0x123456789abcdef0', 'LITE_CARD_BYTES': str(256 * 1024**3),
        'WINARM_MBR_SIGNATURE': '0x12345678',
        'DISK_GUID': ','.join(str(i + 1) for i in range(16)),
        'TYPE_AND_GUID': ','.join(str(i + 1) for i in range(32)),
        'MBR_TAIL': ','.join(str(v) for v in tail),
    }
    text = (root / 'winarm/winarm-target.h.in').read_text()
    for key, value in values.items():
        text = text.replace(f'@{key}@', value)
    assert '@' not in text
    return text, tail


prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
typedef uint32_t ULONG, *PULONG; typedef int32_t LONG;
typedef int64_t LONG64, LONGLONG; typedef uint64_t ULONGLONG;
typedef union { struct { ULONG LowPart; LONG HighPart; }; LONGLONG QuadPart; } LARGE_INTEGER;
typedef uintptr_t ULONG_PTR;
typedef uint16_t USHORT; typedef uint8_t UCHAR, BOOLEAN, KIRQL, *PUCHAR, *PBOOLEAN;
typedef USHORT *PUSHORT;
typedef size_t SIZE_T; typedef int NTSTATUS, KSPIN_LOCK, SDPORT_BUS_SPEED, SDPORT_BUS_VOLTAGE; typedef void *HANDLE, *PVOID;
enum { SdBusVoltageUndefined=0, SdBusVoltage33=1, SdBusVoltage30=2, SdBusVoltage18=3, SdBusVoltageOff=4 };
#define VOID void
#define _In_
#define _Out_
#define _Out_opt_
#define _Out_writes_all_(n)
#define _Inout_
#define _In_reads_(n)
#define _IRQL_requires_max_(x)
#define __forceinline static inline
#define NT_ASSERT assert
#define NT_ASSERTMSG(m,e) assert(e)
#define TRUE 1
#define FALSE 0
#define MAXULONG UINT32_MAX
#define PASSIVE_LEVEL 0
#define DISPATCH_LEVEL 2
#define HIGH_LEVEL 15
#define NT_SUCCESS(s) ((s)>=0)
#define RtlCopyMemory memcpy
#define RtlZeroMemory(p,n) memset((p),0,(n))
#define KeMemoryBarrier() ((void)0)
#define MAX(a,b) ((a)>(b)?(a):(b))
#define min(a,b) ((a)<(b)?(a):(b))
#define NV_T210_LITE_WINARM_WRITE 1
#define NV_T210_LITE_SLOT 1
#define NV_T214_SDR104 1
#define NV_PIO_POLL 1
#define NV_PIO_POLL_US 200
#define NV_PIO_POLL_BLOCKS 32
#define NV_PIO_DUMP_POLL_US 500000
#define SdPortWait(us) KeStallExecutionProcessor(us)
#ifndef NV_T214_CRASHDUMP
#define NV_T214_CRASHDUMP 1
#endif
enum { STATUS_SUCCESS=0, STATUS_INSUFFICIENT_RESOURCES=-1, STATUS_NOT_SUPPORTED=-2,
 STATUS_DEVICE_BUSY=-3, STATUS_IO_TIMEOUT=-4, STATUS_IO_DEVICE_ERROR=-5,
 STATUS_MEDIA_WRITE_PROTECTED=-6, STATUS_DEVICE_POWER_FAILURE=-7,
 STATUS_PENDING=1, STATUS_MORE_PROCESSING_REQUIRED=-8,
 STATUS_DEVICE_CONFIGURATION_ERROR=-9, STATUS_INVALID_PARAMETER=-10,
 STATUS_DEVICE_NOT_READY=-11 };
enum {SdResetTypeAll,SdResetTypeCmd,SdResetTypeDat};
enum {SdhcSpeedModeNormal=0,SdhcSpeedModeHigh=1,SdhcSpeedModeSDR104=4};
enum {SdBusSpeedNormal=0,SdBusSpeedHigh=1,SdBusSpeedSDR12=2,SdBusSpeedSDR25=3,
 SdBusSpeedSDR50=4,SdBusSpeedDDR50=5,SdBusSpeedSDR104=6,SdBusSpeedHS200=7,SdBusSpeedHS400=8};
enum { SdCommandTypeUndefined=0, SdCommandTypeSuspend=1, SdCommandTypeResume=2, SdCommandTypeAbort=3 };
enum { SdTransferTypeUndefined=0, SdTransferTypeNone=1, SdTransferTypeSingleBlock=2, SdTransferTypeMultiBlock=3, SdTransferTypeMultiBlockNoStop=4 };
enum { SdCommandClassStandard=1, SdCommandClassApp=2 };
enum { SdTransferDirectionUndefined=0, SdTransferDirectionRead=1, SdTransferDirectionWrite=2 };
enum { SdTransferMethodUndefined=0, SdTransferMethodPio=1, SdTransferMethodSgDma=2 };
enum { SdRequestTypeCommandNoTransfer=1, SdRequestTypeCommandWithTransfer=2, SdRequestTypeStartTransfer=3 };
enum { SdResponseTypeUndefined=0, SdResponseTypeNone=1, SdResponseTypeR1=2, SdResponseTypeR1B=3,
 SdResponseTypeR2=4, SdResponseTypeR3=5, SdResponseTypeR4=6, SdResponseTypeR5=7, SdResponseTypeR5B=8, SdResponseTypeR6=9 };
enum { LiteSdxc=1, LiteGpt=2, LitePartition=4, LiteNtfs=8, LiteMbr=16, LiteFatBoot=32,
 LiteWriteFacts=LiteSdxc|LiteMbr|LiteNtfs, LiteFatWriteFacts=LiteSdxc|LiteMbr|LiteFatBoot };
typedef struct { ULONG Index,Type,Class,ResponseType,TransferDirection,TransferMethod,TransferType,Argument,Length;
 BOOLEAN UseAutoCmd12,UseAutoCmd23; USHORT BlockSize,BlockCount; PUCHAR DataBuffer; } SDPORT_COMMAND,*PSDPORT_COMMAND;
typedef struct { ULONG Type,RequiredEvents; NTSTATUS Status; SDPORT_COMMAND Command; } SDPORT_REQUEST,*PSDPORT_REQUEST;
typedef struct {
 LONG TargetFacts; ULONG NextReadLba,IssuedWriteLba; USHORT IssuedWriteCount;
 UCHAR IssuedWriteIndex; BOOLEAN AppPrefixAccepted,CrashdumpMode;
 BOOLEAN T214DmaActive,T214DmaComplete,T214DmaFault,T214CardOn,T214Voltage18,T214Tuned;
 BOOLEAN T214BorrowedDumpMaps,T214DumpFreshPower,Removable;
 BOOLEAN T214DdrActive,T214DdrFatal,T214DdrRecoverNeeded,T214DdrAttempted;
 NTSTATUS T214DdrStatus;
 ULONG T214CardInfoValid,T214Rca,SpeedMode,T214CalFallbacks,T214DmaErrors;
 UCHAR T214Cid[16];
 PUCHAR T214Bounce,T214DmaBuffer; struct { ULONG LowPart; } T214BouncePa;
 ULONG T214DmaLength,T214DmaBlocks,T214DmaCapacity,T214DmaRequests,T214DmaMaxBlocks,T214PioFallbacks;
 struct { ULONG MaximumOutstandingRequests,MaximumBlockCount,MaximumBlockSize,BaseClockFrequencyKhz;
  struct {BOOLEAN ScatterGatherDma,Address64Bit;} Supported; } Capabilities;
 void *OutstandingRequests[1];
 ULONGLONG T214DumpDeadline;
 BOOLEAN T214DumpPioStarted;
 UCHAR T214DumpWriteFailures;
 PUCHAR BaseAddress,T214Car,T214Misc,T214Pmc,T214I2c,T214Gpio;
 HANDLE T214Gate,T214GateHandle;
 UCHAR regs[512];
} SDHC_EXTENSION,*PSDHC_EXTENSION;
static KIRQL current_irql;
static KIRQL KeGetCurrentIrql(void){return current_irql;}
static void KeAcquireSpinLock(KSPIN_LOCK *l,KIRQL *old){(void)l;*old=current_irql;if(current_irql<DISPATCH_LEVEL)current_irql=DISPATCH_LEVEL;}
static void KeReleaseSpinLock(KSPIN_LOCK *l,KIRQL old){(void)l;current_irql=old;}
static LONG InterlockedIncrement(volatile LONG *p){return ++(*p);}
static LONG64 InterlockedIncrement64(volatile LONG64 *p){return ++(*p);}
static LONG64 InterlockedCompareExchange64(volatile LONG64 *p,LONG64 v,LONG64 cmp){LONG64 old=*p;if(old==cmp)*p=v;return old;}
static LONG InterlockedOr(LONG *p,LONG n){return *p|=n;}
static LONG InterlockedAnd(LONG *p,LONG n){return *p&=n;}
static ULONG raw32(PUCHAR p,ULONG off){ULONG v;memcpy(&v,p+off,4);return v;}
static void put32(PUCHAR p,ULONG off,ULONG v){memcpy(p+off,&v,4);}
static unsigned mode,fifo,sector_reads,active_lba,error_resets,elapsed_us,mmio_writes;
static ULONGLONG interrupt_time;
static ULONGLONG KeQueryInterruptTimePrecise(ULONGLONG *counter){
 *counter=interrupt_time;return interrupt_time;
}
static bool reset_stuck;
static struct {
 ULONG Flags,Interrupts,ErrorInterrupts,BlocksRead,BlocksWritten;
 UCHAR LastCommand,LastErrorCommand,LastErrorKind;
 ULONG LastErrors,Commands,CommandErrors,CommandsOk,App42Done;
 ULONG DeniedCount,LastDeniedCommand,LastDeniedArgument,DeniedShape;
 PVOID DataRequest; LONGLONG DataStart,DataEnd;
 ULONG DataStartBlocks,DataCommands,DataBlocks,DataUs,GapUs;
 ULONG ClockKhzRequested,ClockKhzActual,RegClockControl;
} NvDiag;
static unsigned completed_calls;
static PSDPORT_REQUEST last_completed_req;
static NTSTATUS last_completed_status;
static UCHAR written[65536];
static unsigned written_bytes,ready_blocks,stall_after_batch_us,batch_size_blocks;
static unsigned inhibit_clear_after_us;
static PUCHAR inhibit_regs;
static bool sim_card_active,sim_card_app_cmd,sim_card_s18a,sim_card_18v;
static ULONG sim_card_last_arg,sim_card_pio_len,sim_card_pio_pos;
static USHORT sim_card_last_cmd,sim_card_last_tm;
static UCHAR sim_card_pio_buf[64];
static UCHAR mbr[512],ntfs[512],maps[5][16384];
static unsigned map_calls,unmap_calls,map_fail,gate_acquires,gate_releases,free_calls,close_calls;
static unsigned i2c_resets,pmic_fail_once,receipt_calls;
static ULONG last_receipt_action,last_receipt_ver,last_receipt_refs,last_receipt_ready;
static NTSTATUS last_receipt_status;
static bool gate_busy,bus_clear_fail,i2c_stuck_busy;
static ULONG pmic_value,sim_i2c_bc_pending;
static PUCHAR checked_host;
static bool gate_fail,reset_fail,source_fail,pll_fail,i2c_gate_fail,i2c_reset_fail;
static unsigned card_power_writes, off_started, last_off_us, isolated_us;
static bool pads_discharging, discharge_seen, e4_low, block_e4_rise, block_e4_fall, block_pad_mux;
static void KeStallExecutionProcessor(ULONG us){
 elapsed_us+=us;
 interrupt_time+=(ULONGLONG)us*10;
 if (e4_low && (raw32(maps[2],0x444)&(1u<<12)) && (raw32(maps[2],0x4e4)&(1u<<12)) &&
     (raw32(maps[1],0x3000)&0xc)==4 && !(raw32(maps[4],0x300)&0x3f) &&
     !(raw32(maps[4],0x310)&0x3f) && !(raw32(maps[4],0x320)&0x3f)) isolated_us+=us;
 if(stall_after_batch_us&&!ready_blocks&&elapsed_us>=stall_after_batch_us){
  ready_blocks=batch_size_blocks;
  stall_after_batch_us+=5000;
 }
 if(inhibit_clear_after_us&&inhibit_regs&&elapsed_us>=inhibit_clear_after_us){
  put32(inhibit_regs,0x24,0);
  inhibit_clear_after_us=0;
 }
}
static PUCHAR T214Map(ULONG address,ULONG length){
 static const ULONG addresses[]={0x60006000,0x70000000,0x7000e000,0x7000d000,0x6000d000};
 static const ULONG lengths[]={0x1000,0x4000,0x1000,0x1000,0x1000};
 assert(current_irql<=DISPATCH_LEVEL);
 unsigned i=map_calls++;assert(i<5&&address==addresses[i]&&length==lengths[i]);
 return map_fail==i+1?NULL:maps[i];
}
static void MmUnmapIoSpace(PUCHAR p,ULONG length){assert(p&&length&&current_irql<=DISPATCH_LEVEL);unmap_calls++;}
static void MmFreeContiguousMemory(PUCHAR p){assert(p);free_calls++;}
static void ZwClose(HANDLE h){assert(h);close_calls++;}
static NTSTATUS TegraI2c5Acquire(HANDLE h){assert(h);gate_acquires++;return gate_busy?STATUS_IO_TIMEOUT:0;}
static void TegraI2c5Release(HANDLE h){assert(h);gate_releases++;}
static NTSTATUS T214PmicMessage(PSDHC_EXTENSION s,BOOLEAN read,ULONG *word,ULONG bytes){
 (void)s;
 if(pmic_fail_once){pmic_fail_once--;return STATUS_IO_DEVICE_ERROR;}
 if(read)*word=pmic_value;
 else if(bytes==2)pmic_value=(*word>>8)&255;
 else assert(*word==0x27);
 return 0;
}
static void accessible(void){if(checked_host)assert((raw32(maps[0],0x10)&(1u<<14))&&!(raw32(maps[0],4)&(1u<<14)));}
static ULONG T214Read(PUCHAR p,ULONG off){
 if(p==checked_host)accessible();
 if(p==maps[3]&&checked_host){
  assert((raw32(maps[0],0x14)&(1u<<15))&&!(raw32(maps[0],8)&(1u<<15)));
 }
 if(p==maps[0]&&off==0x150)return raw32(p,off);
 return raw32(p,off);
}
static void T214Write(PUCHAR p,ULONG off,ULONG v){
 mmio_writes++;
 if(p==maps[0]){
  switch(off){
  case 0x300:assert(v==(1u<<14));put32(p,4,raw32(p,4)|v);return;
  case 0x304:assert(v==(1u<<14));if(!reset_fail){put32(p,4,raw32(p,4)&~v);if(checked_host)memset(checked_host,0,512);}return;
  case 0x320:assert(v==(1u<<14));if(!gate_fail)put32(p,0x10,raw32(p,0x10)|v);return;
  case 0x308:assert(v==(1u<<15));put32(p,8,raw32(p,8)|v);return;
  case 0x30c:assert(v==(1u<<15));if(!i2c_reset_fail){put32(p,8,raw32(p,8)&~v);i2c_resets++;sim_i2c_bc_pending=0;if(!i2c_stuck_busy)put32(maps[3],0x1c,0);}return;
  case 0x328:assert(v==(1u<<15));if(!i2c_gate_fail)put32(p,0x14,raw32(p,0x14)|v);return;
  case 0x150:
   if(checked_host)assert(raw32(p,4)&(1u<<14)); /* old DMA must be reset before retiming */
   if(source_fail)return;break;
  case 0x5a4:if((v&(1u<<30))&&!pll_fail)v|=1u<<27;break;
  }
 }
 if(p==checked_host)accessible();
 if (p==maps[4] && (off==0x380 || off==0x390 || off==0x3a0)) {
  ULONG mask=(v>>8)&0xff, reg=off-0x80;
  assert(mask==0x3f); /* PM6/7 belong to other pads. */
  if (!(off==0x380 && block_pad_mux)) put32(p,reg,(raw32(p,reg)&~mask)|(v&mask));
  pads_discharging=(raw32(p,0x300)&0x3f)==0x3f && (raw32(p,0x310)&0x3f)==0x3f &&
                   (raw32(p,0x320)&0x3f)==0x3f && (raw32(maps[1],0x3000)&0xc)==0;
  return;
 }
 if (p==maps[4] && off==0x1a0) {
  bool high=(v&0x10)!=0;
  assert((v>>8)==0x10);
  card_power_writes++;
  if ((high && block_e4_rise) || (!high && block_e4_fall)) return;
  put32(p,0x120,(raw32(p,0x120)&~0x10u)|(v&0x10));
  if (!high) {
   off_started=elapsed_us;isolated_us=0;
   e4_low=true;
   discharge_seen=pads_discharging && !(pmic_value&0xc0);
  } else if (e4_low) {
   last_off_us=elapsed_us-off_started;
   e4_low=false;
   if (last_off_us>=239000 && isolated_us>=229000 && discharge_seen) {
    sim_card_app_cmd=false; sim_card_s18a=false; sim_card_18v=false;
    sim_card_pio_len=sim_card_pio_pos=0;
   }
  }
  return;
 }
 if(p==maps[3]){
  if(checked_host)assert((raw32(maps[0],0x14)&(1u<<15))&&!(raw32(maps[0],8)&(1u<<15)));
  if(off==0x8c){
   if((v&1u)&&sim_i2c_bc_pending){
    sim_i2c_bc_pending=0;
    put32(p,0x68,raw32(p,0x68)|0x800u);
    put32(p,0x88,bus_clear_fail?0u:1u);
    if(!i2c_stuck_busy)put32(p,0x1c,0);
   }
   v=0; /* PMIC config load completes. */
  }
  if(off==0x5c)v&=~3u; /* FIFO flush completes. */
  if(off==0x68){put32(p,off,raw32(p,off)&~v);return;}
  if(off==0x84)sim_i2c_bc_pending=v&1u;
 }
 if(p!=maps[3]&&off==0x30)v=raw32(p,off)&~v;
 put32(p,off,v);
}
static ULONG SdhcReadRegisterUlong(PSDHC_EXTENSION s,ULONG off){
 if(s->regs==checked_host)accessible();
 if(off==0x24&&sim_card_active&&sim_card_pio_pos<sim_card_pio_len)return 0x00000800u; /* SDHC_PS_BUFFER_READ_ENABLE */
 if(off==0x24&&ready_blocks)return 0x00000400u; /* SDHC_PS_BUFFER_WRITE_ENABLE */
 if(off==0x20){
  assert(fifo<128);ULONG v=raw32(active_lba?ntfs:mbr,fifo++*4);
  if(fifo==128)put32(s->regs,0x30,raw32(s->regs,0x30)|2|(mode==3?1:0));
  return v;
 }
 return raw32(s->regs,off);
}
static UCHAR SdhcReadRegisterUchar(PSDHC_EXTENSION s,ULONG off){if(s->regs==checked_host)accessible();return s->regs[off];}
static USHORT SdhcReadRegisterUshort(PSDHC_EXTENSION s,ULONG off){USHORT v;if(s->regs==checked_host)accessible();memcpy(&v,s->regs+off,2);return v;}
static void SdhcWriteRegisterUlong(PSDHC_EXTENSION s,ULONG off,ULONG v){
 if(s->regs==checked_host)accessible();
 if(sim_card_active&&off==0x08)assert(!(raw32(s->regs,0x24)&1u));
 mmio_writes++;put32(s->regs,off,v);
}
static void SdhcWriteRegisterUchar(PSDHC_EXTENSION s,ULONG off,UCHAR v){
 if(s->regs==checked_host)accessible();
 mmio_writes++;
 if(off==0x2f){error_resets++;if(!reset_stuck){if(v&1)memset(s->regs,0,0x40);v=0;}}
 s->regs[off]=v;
}
static void SdhcWriteRegisterUshort(PSDHC_EXTENSION s,ULONG off,USHORT v){
 if(s->regs==checked_host)accessible();
 if(sim_card_active&&(off==0x04||off==0x06||off==0x0c))assert(!(raw32(s->regs,0x24)&3u));
 mmio_writes++;
 if(off==0x32)v=SdhcReadRegisterUshort(s,off)&~v;
 if(off==0x30)v=SdhcReadRegisterUshort(s,off)&~v;
 if(off==0x2c&&(v&1))v|=2; /* internal clock stabilizes */
 memcpy(s->regs+off,&v,2);
 if(off==0x0e){
  if(sim_card_active){
   ULONG idx=(v>>8)&0x3f,arg=raw32(s->regs,0x08);
   USHORT en=SdhcReadRegisterUshort(s,0x34);
   sim_card_last_cmd=v;sim_card_last_arg=arg;sim_card_last_tm=SdhcReadRegisterUshort(s,0x0c);
   assert(!(raw32(s->regs,0x24)&1u));
   if((v&0x20)||((v&3)==3&&(v&0xc0)!=0xc0))assert(!(raw32(s->regs,0x24)&2u));
   memset(s->regs+0x10,0,16);sim_card_pio_len=sim_card_pio_pos=0;
   /* Per SD Spec 4.2.3.1: once UHS-I card returns S18A=1 at 3.3V, any command
      other than CMD11 or CMD0 is illegal and ignored (CMD_TIMEOUT). */
   if (sim_card_18v && !s->T214Voltage18 && idx==8) {
    put32(s->regs,0x30,0x00018000u);
    return;
   }
   if(sim_card_s18a&&!sim_card_18v&&idx!=0&&idx!=11){
    sim_card_app_cmd=false;
    put32(s->regs,0x30,0x00018000u); /* SDHC_ES_CMD_TIMEOUT | SDHC_IS_ERROR_INTERRUPT */
    return;
   }
   USHORT st=1; /* SDHC_IS_CMD_COMPLETE */
   if(idx==55){
    sim_card_app_cmd=true;put32(s->regs,0x10,0x920);
   }else if(sim_card_app_cmd){
    sim_card_app_cmd=false;
    if(idx==41){
     bool s18=(arg&0x01000000u)!=0&&!sim_card_18v;
     if(arg!=0&&s18)sim_card_s18a=true;
     ULONG ocr=arg==0?0x00ff8000u:(0xc0ff8000u|(s18?0x01000000u:0));
     put32(s->regs,0x10,ocr);
    }else if(idx==51){
     static const UCHAR scr[8]={0x02,0x35,0x80,0x03,0,0,0,0};
     memcpy(sim_card_pio_buf,scr,8);sim_card_pio_len=8;put32(s->regs,0x10,0x920);
     st|=0x20;
    }else{
     put32(s->regs,0x10,0x920);
    }
   }else if(idx==0){
    sim_card_s18a=false;
   }else if(idx==8){
    put32(s->regs,0x10,arg&0xfffu);
   }else if(idx==11){
    assert(sim_card_s18a);sim_card_18v=true;put32(s->regs,0x10,0x900);
   }else if(idx==2){
    put32(s->regs,0x10,0x11223344u);put32(s->regs,0x14,0x55667788u);
   }else if(idx==3){
    put32(s->regs,0x10,0xaaaa0500u);
   }else if(idx==7||idx==12){
    put32(s->regs,0x10,0x700);st|=2; /* R1b -> TRANSFER_COMPLETE */
   }else if(idx==6&&(v&0x20)){
    memset(sim_card_pio_buf,0,64);sim_card_pio_buf[0]=0x00;sim_card_pio_buf[1]=0x64;sim_card_pio_buf[16]=0x01;
    sim_card_pio_len=64;put32(s->regs,0x10,0x900);st|=0x20;
   }else{
    put32(s->regs,0x10,0x900);
    if(v&0x20)st|=(sim_card_last_tm&0x10)?0x20:0x10;
   }
   put32(s->regs,0x30,st&en);
   return;
  }
  assert(v==0x113a);assert(raw32(s->regs,0x38)==0); /* no IRQ signals for private I/O */
  assert(SdhcReadRegisterUshort(s,0x0c)==18); /* never DMA */
  fifo=0;sector_reads++;active_lba=raw32(s->regs,8);
  assert(active_lba==0||active_lba==32768);
  put32(s->regs,0x30,mode==1?0x108000:mode==2?0:mode==3?0x22:0x21);
 }
}
static LONG InterlockedExchange(LONG *p,LONG v){LONG old=*p;*p=v;return old;}
static NTSTATUS T214DdrRecover(PSDHC_EXTENSION s){(void)s;assert(!"dump must not invoke normal DDR recovery");return STATUS_NOT_SUPPORTED;}
static NTSTATUS T214DdrClock(PSDHC_EXTENSION s,ULONG f){(void)s;(void)f;assert(!"dump must not invoke DDR clock");return STATUS_NOT_SUPPORTED;}
static void T214DmaDone(PSDHC_EXTENSION s,PSDPORT_REQUEST r){(void)s;(void)r;assert(!"dump must not complete DMA");}
static void T214ObserveCardInfo(PSDHC_EXTENSION s,PSDPORT_REQUEST r){(void)s;(void)r;}
static NTSTATUS SdhcConvertErrorToStatus(USHORT e){return e?STATUS_IO_DEVICE_ERROR:0;}
static UCHAR NvDiagErrorKind(ULONG e){return (UCHAR)(e?1:0);}
static LARGE_INTEGER KeQueryPerformanceCounter(LARGE_INTEGER *f){if(f)f->QuadPart=19200000;LARGE_INTEGER v={.QuadPart=1000};return v;}
static void SdPortCompleteRequest(PSDPORT_REQUEST r,NTSTATUS st){completed_calls++;last_completed_req=r;last_completed_status=st;}
static NTSTATUS SdhcBuildAdmaTransfer(PSDHC_EXTENSION s,PSDPORT_REQUEST r){(void)s;(void)r;assert(!"unexpected ADMA build");return STATUS_NOT_SUPPORTED;}
static NTSTATUS SdhcStartAdmaTransfer(PSDHC_EXTENSION s,PSDPORT_REQUEST r){(void)s;(void)r;assert(!"unexpected ADMA start");return STATUS_NOT_SUPPORTED;}
static void SdhcReadRegisterBufferUlong(PSDHC_EXTENSION s,ULONG off,PULONG p,ULONG count){
 assert(sim_card_active&&off==0x20&&count==1&&sim_card_pio_pos+4<=sim_card_pio_len);
 memcpy(p,sim_card_pio_buf+sim_card_pio_pos,4);sim_card_pio_pos+=4;
 if(sim_card_pio_pos==sim_card_pio_len&&(SdhcReadRegisterUshort(s,0x34)&2))
  put32(s->regs,0x30,raw32(s->regs,0x30)|2);
}
static void SdhcReadRegisterBufferUshort(PSDHC_EXTENSION s,ULONG off,PUSHORT p,ULONG count){
 (void)s;(void)off;(void)p;(void)count;assert(!"unexpected short read");
}
static void SdhcReadRegisterBufferUchar(PSDHC_EXTENSION s,ULONG off,PUCHAR p,ULONG count){
 (void)s;(void)off;(void)p;(void)count;assert(!"unexpected byte read");
}
static void SdhcWriteRegisterBufferUlong(PSDHC_EXTENSION s,ULONG off,PULONG p,ULONG count){
 assert(off==0x20&&count==1&&ready_blocks&&written_bytes+4<=sizeof(written));
 memcpy(written+written_bytes,p,4);written_bytes+=4;
 if(written_bytes%512==0){
  if(!--ready_blocks)put32(s->regs,0x24,0);
  /* Transfer complete only latches if enabled in SDHC_INTERRUPT_STATUS_ENABLE (0x34). */
  if((SdhcReadRegisterUshort(s,0x34)&2)&&written_bytes==65536)
   put32(s->regs,0x30,raw32(s->regs,0x30)|2);
 }
}
static void SdhcWriteRegisterBufferUshort(PSDHC_EXTENSION s,ULONG off,PUSHORT p,ULONG count){
 (void)s;(void)off;(void)p;(void)count;assert(!"unexpected short write");
}
static void SdhcWriteRegisterBufferUchar(PSDHC_EXTENSION s,ULONG off,PUCHAR p,ULONG count){
 (void)s;(void)off;(void)p;(void)count;assert(!"unexpected byte write");
}
'''
# Use the production register constants, avoiding a second SDHCI definition.
header = (root / 'src/nvtsdhc.h').read_text().replace('\\\n', ' ')
prefix += '\n'.join(re.findall(r'^#define[ \t]+SDHC_\w+[ \t]+[^\n]+', header, re.M)) + '\n'
prefix += re.search(r'^#define T214_IO_BIT[^\n]+', board, re.M).group(0) + '\n'
prefix += board[board.index('#define T214_PE_CNF'):board.index('static BOOLEAN T214CardPower')]
prefix += board[board.index('#define T214_SDMMC1_CLK_PAD'):board.index('static NTSTATUS T214PadCalibrate')]
prefix += function(header[header.index('SdhcGetHwUhsMode(\n'):], 'SdhcGetHwUhsMode', 'static USHORT')
prefix += function(header[header.index('SdhcGetResponseLength(\n'):], 'SdhcGetResponseLength', 'static UCHAR')
prefix += r'''
struct _NV_DUMP_PREP_RECEIPT;
static void T214HostDumpPrepReceipt(const void *raw);
static NTSTATUS T214PrepareDumpWrite(PSDHC_EXTENSION s, PSDPORT_REQUEST r);
static void T214ArmDumpWait(PSDHC_EXTENSION s, ULONG required);
static USHORT T214DumpTimeoutError(PSDHC_EXTENSION s, PSDPORT_REQUEST request, ULONG events);
'''

body = ''
for name, result in [('LiteForgetCard', 'void'), ('LiteObserveRead', 'void'), ('LiteBlocksInExtent', 'BOOLEAN'), ('LiteAllowRequest', 'BOOLEAN'),
                     ('SdhcEnableInterrupt', 'void'), ('SdhcDisableInterrupt', 'void'),
                     ('SdhcGetInterruptStatus', 'USHORT'), ('SdhcGetErrorStatus', 'USHORT'),
                     ('SdhcAcknowledgeInterrupts', 'void'), ('SdhcSlotInterrupt', 'BOOLEAN'),
                     ('SdhcSetBusWidth', 'NTSTATUS'),
                     ('SdhcSetHighSpeed', 'NTSTATUS'), ('SdhcSetUhsMode', 'NTSTATUS'), ('SdhcSetSpeed', 'NTSTATUS'),
                     ('SdhcCalcClockFrequency', 'USHORT'), ('SdhcSetClock', 'NTSTATUS'),
                     ('SdhcReadDataPort', 'void'), ('SdhcWriteDataPort', 'void'), ('SdhcStartPioTransfer', 'NTSTATUS'),
                     ('SdhcSetTransferMode', 'NTSTATUS')]:
    definition = source[source.index('\n' + name + '(\n'):] if name.startswith('Sdhc') else source
    body += function(definition, name, result)
for name, result in [('T214WaitClear', 'BOOLEAN'), ('T214InheritedClockKhz', 'ULONG'),
                     ('T214Release', 'void'), ('T214ResetI2c5ForDump', 'NTSTATUS'),
                     ('T214Ldo2', 'NTSTATUS'), ('T214ParentClock', 'NTSTATUS'),
                     ('T214CardPower', 'BOOLEAN'), ('T214PadCalibrate', 'NTSTATUS'),
                     ('T214VendorReset', 'void'), ('T214Power', 'NTSTATUS'),
                     ('T214SetBusVoltage', 'NTSTATUS'), ('T214BuildDma', 'BOOLEAN')]:
    definition = board[re.search(r'static (?:VOID|NTSTATUS|BOOLEAN|ULONG) ' + name + r'\([^;]+?\)\s*\{', board).start():]
    body += function(definition, name, result)
body += dump
body += r'''
static void T214HostDumpPrepReceipt(const void *raw){
 const NV_DUMP_PREP_RECEIPT *r=(const NV_DUMP_PREP_RECEIPT *)raw;
 receipt_calls++;
 last_receipt_ver=r->DriverVer;last_receipt_action=r->Action;
 last_receipt_status=r->Status;last_receipt_refs=r->Refs;last_receipt_ready=r->Ready;
}
'''
for name, result in [('SdhcRequestDpc', 'void'), ('SdhcResetHost', 'NTSTATUS'),
                     ('SdhcBuildPioTransfer', 'NTSTATUS'), ('SdhcBuildTransfer', 'NTSTATUS'),
                     ('SdhcStartTransfer', 'NTSTATUS'), ('SdhcSendCommand', 'NTSTATUS'),
                     ('SdhcGetResponse', 'NTSTATUS'), ('SdhcSlotGetResponse', 'void'),
                     ('SdhcSlotIssueRequest', 'NTSTATUS')]:
    body += function(source[source.index('\n' + name + '(\n'):], name, result)

checks = r'''
static void u64(PUCHAR p,uint64_t v){memcpy(p,&v,8);}
static void slot(SDHC_EXTENSION *s){memset(s,0,sizeof(*s));s->BaseAddress=s->regs;s->CrashdumpMode=1;s->TargetFacts=LiteSdxc;s->NextReadLba=99;
 s->Capabilities.MaximumOutstandingRequests=1;
 put32(s->regs,0x34,0x11112222);put32(s->regs,0x38,0);s->regs[0x2e]=7;
 put32(maps[4],0x100,0x10);put32(maps[4],0x110,0x10);
 put32(maps[0],0x14,1u<<15);put32(maps[0],8,0);}
static void restored(SDHC_EXTENSION *s){assert(raw32(s->regs,0x34)==0x11112222&&raw32(s->regs,0x38)==0&&s->NextReadLba==99&&s->regs[0x2e]==7);}
static void poll_done(SDHC_EXTENSION *ext,SDPORT_REQUEST *req){
 ULONG ev2=0,err2=0;BOOLEAN cc2=0,sdio2=0,tun2=0;
 assert(SdhcSlotInterrupt(ext,&ev2,&err2,&cc2,&sdio2,&tun2)&&err2==0);
 SdhcRequestDpc(ext,req,ev2,err2);
 assert(last_completed_status==STATUS_SUCCESS&&!ext->OutstandingRequests[0]);
}
int main(void){
 SDHC_EXTENSION s;
 assert(T214InheritedClockKhz(15)==48000&&T214InheritedClockKhz(3u<<29)==199680);
 assert(T214InheritedClockKhz(7u<<29|3)==(NV_T214_DDR200?399360u:0));
 assert(T214InheritedClockKhz(2u<<29)==0);
 /* Registration owns the maps, including rollback on every partial failure. */
 for(map_fail=0;map_fail<=5;map_fail++){
  slot(&s);map_calls=unmap_calls=0;mmio_writes=0;put32(maps[1],0x804,0x2120);
  assert(DumpPreInitialize()==(map_fail?STATUS_INSUFFICIENT_RESOURCES:0));
  assert(map_calls==5&&mmio_writes==0&&gate_acquires==0&&!s.T214Bounce&&!s.T214Gate);
  if(!map_fail)DumpUninitialize();
  assert(unmap_calls==(map_fail?4:5)&&!g_DumpPrepState);
 }
 slot(&s);map_calls=unmap_calls=0;map_fail=0;put32(maps[1],0x804,0);
 assert(DumpPreInitialize()==STATUS_NOT_SUPPORTED&&unmap_calls==5);
 assert(free_calls==0&&close_calls==0);

 /* Exported DumpPreInitialize / DumpUninitialize + HIGH_LEVEL takeover without MmMapIoSpaceEx. */
 current_irql=HIGH_LEVEL;map_calls=unmap_calls=receipt_calls=0;
 assert(DumpPreInitialize()==STATUS_DEVICE_NOT_READY);
 slot(&s);assert(T214InitializeDump(&s)==STATUS_DEVICE_NOT_READY&&map_calls==0);
 assert(!T214DumpEventsAccessible(&s));
 current_irql=PASSIVE_LEVEL;put32(maps[1],0x804,0x2120);put32(maps[0],0x150,0xe0000003);
 assert(DumpPreInitialize()==0&&map_calls==5&&unmap_calls==0&&receipt_calls==1);
 assert(last_receipt_ver==0x0005010bu&&last_receipt_action==1&&last_receipt_status==0&&last_receipt_refs==1&&last_receipt_ready==1);
 assert(DumpPreInitialize()==0&&map_calls==5&&receipt_calls==2&&last_receipt_refs==2&&last_receipt_ready==1);
 current_irql=HIGH_LEVEL;slot(&s);
 assert(DumpPreInitialize()==STATUS_DEVICE_NOT_READY&&(ULONG)g_DumpPrepState==2);
 assert(T214InitializeDump(&s)==0&&s.T214BorrowedDumpMaps&&map_calls==5&&unmap_calls==0);
 T214Release(&s);assert(!s.T214BorrowedDumpMaps&&!s.T214Car&&unmap_calls==0);
 DumpUninitialize(); /* Ignored at HIGH_LEVEL. */
 assert((ULONG)g_DumpPrepState==2&&unmap_calls==0);
 current_irql=PASSIVE_LEVEL;
 DumpUninitialize();assert(unmap_calls==0&&last_receipt_action==2&&last_receipt_refs==1&&last_receipt_ready==1);
 DumpUninitialize();assert(unmap_calls==5&&last_receipt_action==2&&last_receipt_refs==0&&last_receipt_ready==0&&g_DumpPrepState==0);
 map_calls=0; assert(DumpPreInitialize()==STATUS_SUCCESS);


 /* Prepare while SDR104 is active, then let the live card switch to DDR200
    before takeover, including when I2C5 was clock-gated in CAR by runtime SPB! */
 for(unsigned ddr=0;ddr<2;ddr++)for(unsigned loss=0;loss<4;loss++){
  slot(&s);map_calls=0;put32(maps[1],0x804,0x2120);
  put32(maps[0],0x150,3u<<29);assert(T214InitializeDump(&s)==0);
  put32(maps[0],0x150,ddr?0xe0000003:0x60000000);
  put32(maps[0],0x5a4,0x48006804);put32(maps[0],0x10,0x90000002|((loss&1)?0:1u<<14));
  put32(maps[0],4,0x10000004|((loss&2)?1u<<14:0));
  /* Simulate runtime Tegra210I2c leaving I2C5 gated/reset or in packet mode. */
  put32(maps[0],0x14,(loss&1)?0:(1u<<15));put32(maps[0],8,(loss&2)?(1u<<15):0);
  put32(maps[3],0,0x400);
  s.regs[0x2c]=7;s.regs[0x2d]=ddr?1:0;s.regs[0x3e]=ddr?0x8c:0x8b;
  s.T214CardOn=1;s.T214Voltage18=1;checked_host=s.regs;card_power_writes=0;
  assert(SdhcResetHost(&s,SdResetTypeAll)==0);
  assert(raw32(maps[0],0x150)==0x60000000&&s.Capabilities.BaseClockFrequencyKhz==199680);
  assert(!(s.regs[0x2c]&4)&&!(s.regs[0x3e]&15)&&s.regs[0x29]==15);
  assert(s.T214CardOn&&!s.T214Voltage18&&!s.T214DdrActive&&card_power_writes==2&&s.TargetFacts==0);
  assert(raw32(maps[0],0x10)==(0x90000002|(1u<<14))&&raw32(maps[0],4)==0x10000004);
  assert((raw32(maps[0],0x14)&(1u<<15))&&!(raw32(maps[0],8)&(1u<<15))&&raw32(maps[3],0)==0x2800);
#if NV_T214_DDR200
  assert(s.T214DdrAttempted&&!s.T214DdrFatal&&!s.T214DdrRecoverNeeded);
#endif
  checked_host=NULL;T214Release(&s);
 }
 for(unsigned fault=0;fault<4;fault++){
  slot(&s);map_calls=0;put32(maps[1],0x804,0x2120);assert(T214InitializeDump(&s)==0);
  put32(maps[0],0x150,0xe0000003);put32(maps[0],0x5a4,fault==3?0x40006804:0x48006804);
  put32(maps[0],0x10,0);put32(maps[0],4,1u<<14);
  gate_fail=fault==0;reset_fail=fault==1;source_fail=fault==2;pll_fail=fault==3;
  checked_host=s.regs;card_power_writes=0;elapsed_us=0;
  assert(SdhcResetHost(&s,SdResetTypeAll)<0&&card_power_writes==0&&elapsed_us<=1004);
  checked_host=NULL;T214Release(&s);gate_fail=reset_fail=source_fail=pll_fail=false;
 }
 /* I2C5 gate bypass, CAR/bus-clear recovery, transient PMIC retry and stuck-busy timeout. */
 slot(&s);s.T214Car=maps[0];s.T214I2c=maps[3];s.T214Gate=&s;gate_busy=true;
 put32(maps[3],0,0x2800);put32(maps[3],0x64,0x5678);
 assert(T214Ldo2(&s,3300,1)==0&&gate_acquires==0&&gate_releases==0&&raw32(maps[3],0x64)==0);
 pmic_fail_once=1;i2c_resets=0;
 assert(T214Ldo2(&s,3300,1)==0&&i2c_resets==1&&pmic_fail_once==0);
 s.CrashdumpMode=0;put32(maps[3],0,0x1234);put32(maps[3],0x64,0x5678);
 assert(T214Ldo2(&s,3300,1)==STATUS_IO_TIMEOUT&&gate_acquires==1&&gate_releases==0);
 gate_busy=false;assert(T214Ldo2(&s,3300,1)==0&&gate_acquires==2&&gate_releases==1);
 assert(raw32(maps[3],0)==(0x1234&~0x200)&&raw32(maps[3],0x64)==0x5678);
 s.CrashdumpMode=1;put32(maps[3],0x1c,0x100);i2c_stuck_busy=true;elapsed_us=0;
 assert(T214Ldo2(&s,3300,1)==STATUS_IO_TIMEOUT&&elapsed_us>=20000);
 i2c_stuck_busy=false;put32(maps[3],0x1c,0);
 bus_clear_fail=true;put32(maps[3],0,0x400);i2c_resets=0;
 /* Idle packet-mode I2C5 switches cleanly to normal mode without CAR reset or BusClear! */
 assert(T214Ldo2(&s,3300,1)==0&&i2c_resets==0&&raw32(maps[3],0)==0x2800);
 /* Stuck bus or failed PMIC transaction invokes BusClear (committed via I2C_CONFIG_LOAD). */
 pmic_fail_once=1;assert(T214Ldo2(&s,3300,1)==STATUS_IO_DEVICE_ERROR&&i2c_resets==1);
 bus_clear_fail=false;pmic_fail_once=0;put32(maps[3],0,0x2800);

 /* 1.8V HighSpeed in dumpsdport must program HOST_CONTROL2 to SDR25, not leave SDR12. */
 slot(&s);s.T214Voltage18=1;put32(s.regs,0x3e,SDHC_HC2_1_8V_SIGNALING);
 assert(SdhcSetSpeed(&s,SdBusSpeedHigh)==0&&(SdhcReadRegisterUshort(&s,0x3e)&7)==SDHC_HC2_SDR25);
 assert(SdhcSetSpeed(&s,SdBusSpeedNormal)==0&&(SdhcReadRegisterUshort(&s,0x3e)&7)==SDHC_HC2_SDR12);

 /* CrashdumpMode SdhcSetClock caps cloned HighSpeed 50 MHz requests at 25 MHz and waits >=10 ms. */
 slot(&s);s.Capabilities.BaseClockFrequencyKhz=199680;put32(s.regs,0xfe,2);
 s.regs[0x2c]=SDHC_CC_CLOCK_STABLE;elapsed_us=0;
 assert(SdhcSetClock(&s,50000)==0&&NvDiag.ClockKhzRequested==25000&&NvDiag.ClockKhzActual<=25000&&elapsed_us>=10000);
 elapsed_us=0;assert(SdhcSetClock(&s,400)==0&&NvDiag.ClockKhzRequested==400&&elapsed_us>=10000);

 /* Standard non-transfer CMD23 is permitted; app-prefixed or data-bearing CMD23 is rejected. */
 {
  SDPORT_REQUEST c23={0};c23.Type=SdRequestTypeCommandNoTransfer;
  c23.Command.Index=23;c23.Command.Class=SdCommandClassStandard;c23.Command.Argument=128;
  slot(&s);assert(LiteAllowRequest(&s,&c23));
  s.AppPrefixAccepted=1;assert(!LiteAllowRequest(&s,&c23));s.AppPrefixAccepted=0;
  c23.Type=SdRequestTypeCommandWithTransfer;assert(!LiteAllowRequest(&s,&c23));
 }

 /* Reproduce dumpsdport!Dump_WritePending stamping STATUS_MORE_PROCESSING_REQUIRED across Action=1->2:
  * 1) CommandWithTransfer (CMD25) stamped before CMD_COMPLETE must complete with STATUS_SUCCESS.
  * 2) StartTransfer with BlockCount>0 after partial PIO stall must preserve STATUS_MORE_PROCESSING_REQUIRED.
  * 3) StartTransfer with BlockCount==0 awaiting TRANSFER_COMPLETE must complete with STATUS_SUCCESS,
  *    never re-entering StartTransfer with BlockCount==0. */
 {
  SDPORT_REQUEST wr={0};
  slot(&s);
  wr.Type=SdRequestTypeCommandWithTransfer;wr.Command.Index=25;wr.Command.TransferMethod=SdTransferMethodPio;
  wr.Command.BlockCount=128;wr.RequiredEvents=SDHC_IS_CMD_COMPLETE|SDHC_IS_BUFFER_WRITE_READY;
  wr.Status=STATUS_MORE_PROCESSING_REQUIRED; /* stamped by dumpsdport 0x1c00027ec */
  s.OutstandingRequests[0]=&wr;completed_calls=0;last_completed_status=-999;
  SdhcRequestDpc(&s,&wr,SDHC_IS_CMD_COMPLETE|SDHC_IS_BUFFER_WRITE_READY,0);
  assert(completed_calls==1&&last_completed_status==STATUS_SUCCESS&&wr.Status==STATUS_SUCCESS&&!s.OutstandingRequests[0]);

  wr.Type=SdRequestTypeStartTransfer;wr.Command.BlockCount=24;
  wr.RequiredEvents=SDHC_IS_BUFFER_WRITE_READY;wr.Status=STATUS_MORE_PROCESSING_REQUIRED;
  s.OutstandingRequests[0]=&wr;completed_calls=0;
  SdhcRequestDpc(&s,&wr,SDHC_IS_BUFFER_WRITE_READY,0);
  assert(completed_calls==1&&last_completed_status==STATUS_MORE_PROCESSING_REQUIRED&&!s.OutstandingRequests[0]);

  wr.Type=SdRequestTypeStartTransfer;wr.Command.BlockCount=0;
  wr.RequiredEvents=SDHC_IS_TRANSFER_COMPLETE;
  wr.Status=STATUS_MORE_PROCESSING_REQUIRED; /* stamped by dumpsdport 0x1c00027ec on Action=1 yield */
  s.OutstandingRequests[0]=&wr;completed_calls=0;
  SdhcRequestDpc(&s,&wr,SDHC_IS_TRANSFER_COMPLETE,0);
  assert(completed_calls==1&&last_completed_status==STATUS_SUCCESS&&wr.Status==STATUS_SUCCESS&&!s.OutstandingRequests[0]);
 }

 /* Polling must retain all status enables while keeping IRQ delivery off. */
 {
  SDPORT_REQUEST r={0};
  ULONG events=0,errors=0;BOOLEAN cc=0,sdio=0,tune=0;
  slot(&s);current_irql=HIGH_LEVEL;
  r.Type=SdRequestTypeCommandNoTransfer;r.Command.Index=8;r.Command.ResponseType=SdResponseTypeR6;
  r.Command.TransferType=SdTransferTypeNone;
  sim_card_active=true;
  assert(SdhcSlotIssueRequest(&s,&r)==STATUS_PENDING);
  sim_card_active=false;put32(s.regs,0x30,0);
  interrupt_time=s.T214DumpDeadline-1;
  assert(!SdhcSlotInterrupt(&s,&events,&errors,&cc,&sdio,&tune));
  ++interrupt_time;
  assert(SdhcSlotInterrupt(&s,&events,&errors,&cc,&sdio,&tune));
  assert(events==SDHC_IS_ERROR_INTERRUPT&&errors==SDHC_ES_CMD_TIMEOUT&&raw32(s.regs,0x30)==0);
  SdhcRequestDpc(&s,&r,events,errors);assert(last_completed_status<0&&!s.OutstandingRequests[0]);
  assert(!SdhcSlotInterrupt(&s,&events,&errors,&cc,&sdio,&tune));
  r.Type=SdRequestTypeCommandWithTransfer;r.RequiredEvents=SDHC_IS_CMD_COMPLETE|SDHC_IS_BUFFER_WRITE_READY;
  s.OutstandingRequests[0]=&r;SdhcRequestDpc(&s,&r,SDHC_IS_CMD_COMPLETE,0);
  assert(s.T214DumpDeadline==interrupt_time+T214_DUMP_DATA_WAIT_100NS);
  r.Type=SdRequestTypeStartTransfer;r.Command.BlockCount=1;interrupt_time=s.T214DumpDeadline;
  put32(s.regs,0x30,SDHC_IS_BUFFER_WRITE_READY);put32(s.regs,0x24,0);
  assert(SdhcSlotInterrupt(&s,&events,&errors,&cc,&sdio,&tune)&&errors==SDHC_ES_DATA_TIMEOUT);
  put32(s.regs,0x24,SDHC_PS_BUFFER_WRITE_ENABLE);
  assert(SdhcSlotInterrupt(&s,&events,&errors,&cc,&sdio,&tune)&&!errors);
  r.Command.BlockCount=0;r.RequiredEvents=SDHC_IS_TRANSFER_COMPLETE;put32(s.regs,0x24,0);
  assert(SdhcSlotInterrupt(&s,&events,&errors,&cc,&sdio,&tune)&&errors==SDHC_ES_DATA_TIMEOUT);
  put32(s.regs,0x30,SDHC_IS_TRANSFER_COMPLETE);
  assert(SdhcSlotInterrupt(&s,&events,&errors,&cc,&sdio,&tune)&&!errors);
  put32(s.regs,0x30,(SDHC_ES_DATA_CRC_ERROR<<16)|SDHC_IS_ERROR_INTERRUPT);
  assert(SdhcSlotInterrupt(&s,&events,&errors,&cc,&sdio,&tune)&&errors==SDHC_ES_DATA_CRC_ERROR);
  s.CrashdumpMode=0;put32(s.regs,0x30,0);
  assert(!SdhcSlotInterrupt(&s,&events,&errors,&cc,&sdio,&tune));
  s.CrashdumpMode=1;r.Command.Index=25;r.Command.BlockCount=0;
  for(unsigned attempt=1;attempt<=T214_DUMP_WRITE_ATTEMPTS;attempt++){
   s.OutstandingRequests[0]=&r;r.RequiredEvents=SDHC_IS_TRANSFER_COMPLETE;
   SdhcRequestDpc(&s,&r,SDHC_IS_ERROR_INTERRUPT,SDHC_ES_DATA_TIMEOUT);
   assert(s.T214DumpWriteFailures==attempt&&last_completed_status<0);
  }
  assert(T214DumpTakeoverClock(&s)==STATUS_IO_TIMEOUT);
  /* Finishing the command phase alone cannot acknowledge the write. */
  s.OutstandingRequests[0]=&r;r.Type=SdRequestTypeCommandWithTransfer;
  r.RequiredEvents=SDHC_IS_CMD_COMPLETE;SdhcRequestDpc(&s,&r,SDHC_IS_CMD_COMPLETE,0);
  assert(s.T214DumpWriteFailures==T214_DUMP_WRITE_ATTEMPTS);
  s.OutstandingRequests[0]=&r;r.Type=SdRequestTypeStartTransfer;
  r.RequiredEvents=SDHC_IS_TRANSFER_COMPLETE;SdhcRequestDpc(&s,&r,SDHC_IS_TRANSFER_COMPLETE,0);
  assert(s.T214DumpWriteFailures==0);
  current_irql=PASSIVE_LEVEL;
 }

 slot(&s);put32(s.regs,0x34,0);put32(s.regs,0x38,0xffffffff);
 SdhcEnableInterrupt(&s,1);SdhcEnableInterrupt(&s,32);
 assert(SdhcReadRegisterUshort(&s,0x34)==33&&raw32(s.regs,0x38)==0);
 SdhcDisableInterrupt(&s,32);assert(SdhcReadRegisterUshort(&s,0x34)==1&&raw32(s.regs,0x38)==0);
 assert(SdhcReadRegisterUshort(&s,0x36)==0xffff);
 SdhcDisableInterrupt(&s,1);assert(raw32(s.regs,0x34)==0&&raw32(s.regs,0x38)==0);
 s.CrashdumpMode=0;SdhcEnableInterrupt(&s,33);SdhcDisableInterrupt(&s,32);
 assert(SdhcReadRegisterUshort(&s,0x34)==1&&SdhcReadRegisterUshort(&s,0x38)==1);

 /* Real target predicates, including the filtered MBR, gate dump writes. */
 memcpy(mbr+440,physical_tail,70);mbr[510]=0x55;mbr[511]=0xaa;
 memcpy(ntfs+3,"NTFS    ",8);ntfs[12]=2;ntfs[13]=8;ntfs[510]=0x55;ntfs[511]=0xaa;
 put32(ntfs,28,WINARM_FIRST);u64(ntfs+40,WINARM_COUNT-1);u64(ntfs+72,WINARM_SERIAL);
 SDPORT_REQUEST r={0};r.Type=SdRequestTypeCommandWithTransfer;
 static _Alignas(16) UCHAR payload[65536];
 for(unsigned i=0;i<sizeof(payload);i++)payload[i]=(UCHAR)(i*37+i/512);
 r.Command.DataBuffer=payload;r.Command.Length=16384;
 r.Command.Index=25;r.Command.BlockSize=512;r.Command.BlockCount=32;r.Command.Argument=WINARM_FIRST+32;
 r.Command.TransferDirection=SdTransferDirectionWrite;r.Command.TransferMethod=SdTransferMethodPio;
 r.Command.TransferType=SdTransferTypeMultiBlock;
 slot(&s);assert(T214PrepareDumpWrite(&s,&r)==0&&sector_reads==2);
 restored(&s);assert(s.TargetFacts==LiteWriteFacts&&LiteAllowRequest(&s,&r)==NV_T214_CRASHDUMP);
 assert(T214PrepareDumpWrite(&s,&r)==0&&sector_reads==2); /* no re-read on each write */
 assert(!memcmp(mbr+440,physical_tail,70)); /* physical MBR not rewritten */
 _Alignas(16) UCHAR bounce[20480];s.T214Bounce=bounce;s.T214DmaCapacity=16384;
 unsigned before=mmio_writes;assert(!T214BuildDma(&s,&r)&&mmio_writes==before);

 /* Exercise SdhcSlotInterrupt level-ready synthesis in CrashdumpMode. */
 ULONG ev=0,err=0;BOOLEAN cc=0,sdio=0,tun=0;
 s.OutstandingRequests[0]=&r;
 r.RequiredEvents=SDHC_IS_CMD_COMPLETE|SDHC_IS_BUFFER_WRITE_READY;
 ready_blocks=8;put32(s.regs,0x30,0);
 /* Must NOT synthesize BUFFER_WRITE_READY before CMD_COMPLETE! */
 assert(!SdhcSlotInterrupt(&s,&ev,&err,&cc,&sdio,&tun)&&ev==0);
 put32(s.regs,0x30,SDHC_IS_CMD_COMPLETE);
 /* When CMD_COMPLETE arrives and PRESENT_STATE has BUFFER_WRITE_ENABLE, both report! */
 assert(SdhcSlotInterrupt(&s,&ev,&err,&cc,&sdio,&tun)&&ev==(SDHC_IS_CMD_COMPLETE|SDHC_IS_BUFFER_WRITE_READY));
 /* Even when 0x30 is 0, waiting on BUFFER_WRITE_READY alone synthesizes from PRESENT_STATE! */
 r.RequiredEvents=SDHC_IS_BUFFER_WRITE_READY;ev=0;
 assert(SdhcSlotInterrupt(&s,&ev,&err,&cc,&sdio,&tun)&&ev==SDHC_IS_BUFFER_WRITE_READY);
 s.OutstandingRequests[0]=NULL;

 /* Exercise partial stall exceeding 500 ms and resumption. */
 ready_blocks=8;elapsed_us=0;written_bytes=0;r.RequiredEvents=0;
 assert(SdhcStartPioTransfer(&s,&r)==STATUS_PENDING&&r.Command.BlockCount==24);
 assert(r.Command.DataBuffer==payload+4096&&r.Status==STATUS_MORE_PROCESSING_REQUIRED);
 assert(r.RequiredEvents==SDHC_IS_BUFFER_WRITE_READY&&elapsed_us==NV_PIO_DUMP_POLL_US);
 ready_blocks=24;r.RequiredEvents=0;
 assert(SdhcStartPioTransfer(&s,&r)==STATUS_PENDING&&r.Command.BlockCount==0);
 assert(r.RequiredEvents==SDHC_IS_TRANSFER_COMPLETE&&r.Status==STATUS_SUCCESS);
 assert(written_bytes==16384&&!memcmp(written,payload,16384)&&NvDiag.BlocksWritten==32);

 /* Exercise full 128-block (64 KiB) dumpsdport CMD25 write with 5 ms flash stalls every 16 blocks
    AND status-enable cleared to 0 by ToggleEvents(FALSE) before StartTransfer! */
 memset(written,0,sizeof(written));written_bytes=0;elapsed_us=0;
 r.Command.DataBuffer=payload;r.Command.Length=65536;r.Command.BlockCount=128;r.RequiredEvents=0;
 put32(s.regs,0x34,0);put32(s.regs,0x30,0);
 batch_size_blocks=16;ready_blocks=16;stall_after_batch_us=5000;
 assert(SdhcStartPioTransfer(&s,&r)==STATUS_PENDING&&r.Command.BlockCount==0);
 stall_after_batch_us=0;
 assert(r.RequiredEvents==SDHC_IS_TRANSFER_COMPLETE&&r.Status==STATUS_SUCCESS);
 assert(written_bytes==65536&&!memcmp(written,payload,65536));
 /* Because StartTransfer re-armed 0x34 before pushing data, hardware latched TRANSFER_COMPLETE! */
 assert(SdhcReadRegisterUshort(&s,0x30)&SDHC_IS_TRANSFER_COMPLETE);

 r.Command.BlockCount=32;r.Command.DataBuffer=payload;
 r.Type=SdRequestTypeStartTransfer;s.IssuedWriteLba=r.Command.Argument;s.IssuedWriteIndex=25;s.IssuedWriteCount=32;
 assert(LiteAllowRequest(&s,&r)==NV_T214_CRASHDUMP);s.IssuedWriteLba++;
 assert(!LiteAllowRequest(&s,&r));r.Type=SdRequestTypeCommandWithTransfer;
 r.Command.Argument=WINARM_FIRST+WINARM_COUNT-31;assert(!LiteAllowRequest(&s,&r));
 r.Command.Argument=UINT32_MAX-4;assert(!LiteAllowRequest(&s,&r));
 r.Command.Argument=0;assert(!LiteAllowRequest(&s,&r));
#ifdef FAT32_FIRST
 s.TargetFacts|=LiteFatBoot;r.Command.Argument=FAT32_FIRST+32;assert(!LiteAllowRequest(&s,&r));
 s.CrashdumpMode=0;assert(LiteAllowRequest(&s,&r));s.CrashdumpMode=1;
#endif
 r.Command.Argument=WINARM_FIRST;r.Command.Index=38;assert(!LiteAllowRequest(&s,&r));r.Command.Index=25;
 slot(&s);ntfs[72]^=1;assert(T214PrepareDumpWrite(&s,&r)==STATUS_MEDIA_WRITE_PROTECTED);ntfs[72]^=1;
 assert(!(s.TargetFacts&LiteWriteFacts));restored(&s);
 slot(&s);mbr[440]^=1;assert(T214PrepareDumpWrite(&s,&r)==STATUS_MEDIA_WRITE_PROTECTED);mbr[440]^=1;
 assert(!(s.TargetFacts&LiteWriteFacts));restored(&s);
 for(mode=1;mode<=2;mode++){
  slot(&s);elapsed_us=0;
  assert(T214PrepareDumpWrite(&s,&r)==(mode==1?STATUS_IO_DEVICE_ERROR:STATUS_IO_TIMEOUT));
  assert(!(s.TargetFacts&LiteWriteFacts)&&elapsed_us<=1001000);restored(&s);
 }
 mode=3;slot(&s);assert(T214PrepareDumpWrite(&s,&r)==0);restored(&s); /* command completion arrives last */
 mode=0;slot(&s);s.AppPrefixAccepted=1;before=sector_reads;
 assert(T214PrepareDumpWrite(&s,&r)==STATUS_MEDIA_WRITE_PROTECTED&&sector_reads==before);
 slot(&s);s.TargetFacts=0;assert(T214PrepareDumpWrite(&s,&r)==STATUS_MEDIA_WRITE_PROTECTED&&sector_reads==before);
 slot(&s);s.OutstandingRequests[0]=&r;assert(T214PrepareDumpWrite(&s,&r)==STATUS_DEVICE_BUSY&&sector_reads==before);
 slot(&s);ready_blocks=0;put32(s.regs,0x24,3);elapsed_us=0;
 assert(T214PrepareDumpWrite(&s,&r)==STATUS_DEVICE_BUSY&&sector_reads==before&&elapsed_us==100000);restored(&s);
 slot(&s);mode=1;reset_stuck=1;elapsed_us=0;
 assert(T214PrepareDumpWrite(&s,&r)==STATUS_IO_TIMEOUT&&elapsed_us==1000);restored(&s);
 reset_stuck=0;mode=0;

 /* A pending error wins over the FIFO-ready level; spurious unwired SDHCI
  * card-detect bits on Switch Lite do NOT discard verified dump card identity. */
 {
  SDPORT_REQUEST write = {0};
  slot(&s);
  write.Type=SdRequestTypeCommandWithTransfer;
  write.Command.Index=25;write.Command.Argument=WINARM_FIRST+1;
  write.Command.BlockCount=1;write.Command.BlockSize=512;
  write.Command.TransferDirection=SdTransferDirectionWrite;
  write.Command.DataBuffer=payload;
  s.TargetFacts=LiteWriteFacts;
  put32(s.regs,SDHC_INTERRUPT_STATUS,SDHC_IS_CARD_DETECT);
  assert(T214PrepareDumpWrite(&s,&write)==0 && s.TargetFacts==LiteWriteFacts);
  ev=err=0xff;cc=sdio=tun=TRUE;
  assert(!SdhcSlotInterrupt(&s,&ev,&err,&cc,&sdio,&tun) && !ev && !cc && s.TargetFacts==LiteWriteFacts);
  put32(s.regs,SDHC_INTERRUPT_STATUS,(SDHC_ES_DATA_TIMEOUT<<16)|SDHC_IS_ERROR_INTERRUPT);
  ready_blocks=1;written_bytes=0;
  assert(SdhcStartPioTransfer(&s,&write)==STATUS_IO_DEVICE_ERROR);
  assert(written_bytes==0 && write.Command.BlockCount==1);
  write.Command.BlockCount=0;
  assert(SdhcStartPioTransfer(&s,&write)==STATUS_INVALID_PARAMETER && !written_bytes);
  write.Command.BlockCount=1;
  put32(s.regs,SDHC_INTERRUPT_STATUS,SDHC_IS_CMD_COMPLETE|SDHC_IS_CARD_DETECT);
  ev=err=0xff;cc=sdio=tun=TRUE;
  assert(SdhcSlotInterrupt(&s,&ev,&err,&cc,&sdio,&tun));
  assert(ev==SDHC_IS_CMD_COMPLETE && !err && !cc && !sdio && !tun && s.TargetFacts==LiteWriteFacts);
  SDPORT_REQUEST busy={0};
  s.OutstandingRequests[0]=&busy;unsigned writes_before=mmio_writes;
  assert(SdhcSlotIssueRequest(&s,&write)==STATUS_DEVICE_BUSY && writes_before==mmio_writes);
  assert(s.OutstandingRequests[0]==&busy);
  ready_blocks=0;
 }
 /* Full dumpsdport!Dump_Open re-init sequence on a cold-power-cycled 3.3V UHS-I SDXC card:
  * dumpsdport clears SignalingVoltage18V and skips CMD11, yet still sends ACMD41(0x51ff8000)
  * with S18R=1! Verify SdhcSendCommand clears S18R on the wire in CrashdumpMode (so S18A=0 and
  * CMD2..CMD16 + ACMD51/CMD6 PIO reads succeed), while preserving S18R=1 in normal runtime! */
 {
  sim_card_active=true;
  sim_card_18v=true;
  put32(maps[1],0x3000,0x2480);
  put32(maps[4],0x300,0xc0);put32(maps[4],0x310,0x94);put32(maps[4],0x320,0x85);
  slot(&s);map_calls=0;put32(maps[1],0x804,0x2120);
  put32(maps[0],0x150,0x60000000);put32(maps[0],0x5a4,0x48006804);
  put32(maps[0],0x10,1u<<14);put32(maps[0],4,0);
  put32(maps[0],0x14,1u<<15);put32(maps[0],8,0);put32(maps[3],0,0x400);
  assert(T214InitializeDump(&s)==0);
  s.T214CardOn=1;s.T214Voltage18=1;put32(s.regs,0xfe,2);card_power_writes=0;i2c_resets=0;
  assert(SdhcResetHost(&s,SdResetTypeAll)==0&&card_power_writes==2&&i2c_resets==0&&s.T214DumpFreshPower);
  assert(last_off_us>=239000 && isolated_us>=229000 && discharge_seen && !sim_card_18v);
  assert(raw32(maps[1],0x3000)==0x2480 && raw32(maps[4],0x300)==0xc0);
  assert(raw32(maps[4],0x310)==0x94 && raw32(maps[4],0x320)==0x85);
  SdhcEnableInterrupt(&s,0x80fb);
  assert(!(SdhcReadRegisterUshort(&s,0x34)&SDHC_IS_CARD_DETECT));
  assert(T214SetBusVoltage(&s,1)==0&&card_power_writes==2&&!s.T214DumpFreshPower); /* SdBusVoltage33 reuses fresh reset-all cycle */
  assert(SdhcSetClock(&s,400)==0);

  SDPORT_REQUEST q={0};ULONG resp[4]={0};
  /* CMD0 */
  q.Type=SdRequestTypeCommandNoTransfer;q.Command.Index=0;q.Command.Class=SdCommandClassStandard;
  q.Command.ResponseType=SdResponseTypeNone;q.Command.TransferType=SdTransferTypeNone;
  assert(SdhcSlotIssueRequest(&s,&q)==STATUS_PENDING);poll_done(&s,&q);
  /* CMD8(0x1aa) */
  memset(&q,0,sizeof(q));q.Type=SdRequestTypeCommandNoTransfer;q.Command.Index=8;
  q.Command.Class=SdCommandClassStandard;q.Command.ResponseType=SdResponseTypeR6;
  q.Command.TransferType=SdTransferTypeNone;q.Command.Argument=0x1aa;
  assert(SdhcSlotIssueRequest(&s,&q)==STATUS_PENDING);poll_done(&s,&q);
  SdhcSlotGetResponse(&s,&q.Command,resp);assert((resp[0]&0xfff)==0x1aa);
  /* CMD55(0) + ACMD41(0) inquiry */
  memset(&q,0,sizeof(q));q.Type=SdRequestTypeCommandNoTransfer;q.Command.Index=55;
  q.Command.Class=SdCommandClassApp;q.Command.ResponseType=SdResponseTypeR1;q.Command.TransferType=SdTransferTypeNone;
  assert(SdhcSlotIssueRequest(&s,&q)==STATUS_PENDING);poll_done(&s,&q);assert(s.AppPrefixAccepted);
  memset(&q,0,sizeof(q));q.Type=SdRequestTypeCommandNoTransfer;q.Command.Index=41;
  q.Command.Class=SdCommandClassApp;q.Command.ResponseType=SdResponseTypeR3;q.Command.TransferType=SdTransferTypeNone;
  assert(SdhcSlotIssueRequest(&s,&q)==STATUS_PENDING);poll_done(&s,&q);
  SdhcSlotGetResponse(&s,&q.Command,resp);assert(resp[0]==0x00ff8000u&&!(s.TargetFacts&LiteSdxc));
  /* CMD55(0) + ACMD41(0x51ff8000): dumpsdport sets S18R=1 and skips CMD11! */
  memset(&q,0,sizeof(q));q.Type=SdRequestTypeCommandNoTransfer;q.Command.Index=55;
  q.Command.Class=SdCommandClassApp;q.Command.ResponseType=SdResponseTypeR1;q.Command.TransferType=SdTransferTypeNone;
  assert(SdhcSlotIssueRequest(&s,&q)==STATUS_PENDING);poll_done(&s,&q);
  memset(&q,0,sizeof(q));q.Type=SdRequestTypeCommandNoTransfer;q.Command.Index=41;
  q.Command.Class=SdCommandClassApp;q.Command.ResponseType=SdResponseTypeR3;
  q.Command.TransferType=SdTransferTypeNone;q.Command.Argument=0x51ff8000u;
  assert(SdhcSlotIssueRequest(&s,&q)==STATUS_PENDING);
  assert(sim_card_last_arg==0x50ff8000u&&q.Command.Argument==0x51ff8000u&&!sim_card_s18a);
  poll_done(&s,&q);
  SdhcSlotGetResponse(&s,&q.Command,resp);
  assert(resp[0]==0xc0ff8000u&&(s.TargetFacts&LiteSdxc));
  /* CMD2 (without CMD11!) -> succeeds because S18A==0! */
  memset(&q,0,sizeof(q));q.Type=SdRequestTypeCommandNoTransfer;q.Command.Index=2;
  q.Command.Class=SdCommandClassStandard;q.Command.ResponseType=SdResponseTypeR2;q.Command.TransferType=SdTransferTypeNone;
  assert(SdhcSlotIssueRequest(&s,&q)==STATUS_PENDING);poll_done(&s,&q);
  SdhcSlotGetResponse(&s,&q.Command,resp);assert(resp[0]==0x11223344u&&resp[1]==0x55667788u);
  /* CMD3 -> RCA 0xaaaa0000 */
  memset(&q,0,sizeof(q));q.Type=SdRequestTypeCommandNoTransfer;q.Command.Index=3;
  q.Command.Class=SdCommandClassStandard;q.Command.ResponseType=SdResponseTypeR6;q.Command.TransferType=SdTransferTypeNone;
  assert(SdhcSlotIssueRequest(&s,&q)==STATUS_PENDING);poll_done(&s,&q);
  SdhcSlotGetResponse(&s,&q.Command,resp);ULONG rca=resp[0]&0xffff0000u;assert(rca==0xaaaa0000u);
  /* CMD7 (R1b) with transient DAT_INHIBIT clearing after 25 us */
  put32(s.regs,0x24,SDHC_PS_DAT_INHIBIT);inhibit_regs=s.regs;inhibit_clear_after_us=25;elapsed_us=0;
  memset(&q,0,sizeof(q));q.Type=SdRequestTypeCommandNoTransfer;q.Command.Index=7;
  q.Command.Class=SdCommandClassStandard;q.Command.ResponseType=SdResponseTypeR1B;
  q.Command.TransferType=SdTransferTypeNone;q.Command.Argument=rca;
  assert(SdhcSlotIssueRequest(&s,&q)==STATUS_PENDING&&elapsed_us==25);poll_done(&s,&q);
  /* CMD55 + ACMD6(2), CMD55 + ACMD42(0), SdhcSetBusWidth(4) */
  for(unsigned step=0;step<2;step++){
   memset(&q,0,sizeof(q));q.Type=SdRequestTypeCommandNoTransfer;q.Command.Index=55;
   q.Command.Class=SdCommandClassApp;q.Command.ResponseType=SdResponseTypeR1;
   q.Command.TransferType=SdTransferTypeNone;q.Command.Argument=rca;
   assert(SdhcSlotIssueRequest(&s,&q)==STATUS_PENDING);poll_done(&s,&q);
   memset(&q,0,sizeof(q));q.Type=SdRequestTypeCommandNoTransfer;q.Command.Index=step?42:6;
   q.Command.Class=SdCommandClassApp;q.Command.ResponseType=SdResponseTypeR1;
   q.Command.TransferType=SdTransferTypeNone;q.Command.Argument=step?0:2;
   assert(SdhcSlotIssueRequest(&s,&q)==STATUS_PENDING);poll_done(&s,&q);
  }
  assert(SdhcSetBusWidth(&s,4)==0&&(s.regs[0x28]&2)!=0);
  /* CMD55 + ACMD51 (8B PIO read) and CMD6 (64B PIO read) */
  UCHAR pio_in[64]={0};
  memset(&q,0,sizeof(q));q.Type=SdRequestTypeCommandNoTransfer;q.Command.Index=55;
  q.Command.Class=SdCommandClassApp;q.Command.ResponseType=SdResponseTypeR1;
  q.Command.TransferType=SdTransferTypeNone;q.Command.Argument=rca;
  assert(SdhcSlotIssueRequest(&s,&q)==STATUS_PENDING);poll_done(&s,&q);
  memset(&q,0,sizeof(q));q.Type=SdRequestTypeCommandWithTransfer;q.Command.Index=51;
  q.Command.Class=SdCommandClassApp;q.Command.ResponseType=SdResponseTypeR1;
  q.Command.TransferType=SdTransferTypeSingleBlock;q.Command.TransferDirection=SdTransferDirectionRead;
  q.Command.TransferMethod=SdTransferMethodPio;q.Command.BlockSize=8;q.Command.BlockCount=1;
  q.Command.Length=8;q.Command.DataBuffer=pio_in;
  assert(SdhcSlotIssueRequest(&s,&q)==STATUS_PENDING);poll_done(&s,&q);
  q.Type=SdRequestTypeStartTransfer;
  assert(SdhcSlotIssueRequest(&s,&q)==STATUS_PENDING&&q.Command.BlockCount==0);poll_done(&s,&q);
  assert(pio_in[0]==0x02&&pio_in[1]==0x35&&pio_in[3]==0x03);
  memset(pio_in,0,sizeof(pio_in));
  memset(&q,0,sizeof(q));q.Type=SdRequestTypeCommandWithTransfer;q.Command.Index=6;
  q.Command.Class=SdCommandClassStandard;q.Command.ResponseType=SdResponseTypeR1;
  q.Command.TransferType=SdTransferTypeSingleBlock;q.Command.TransferDirection=SdTransferDirectionRead;
  q.Command.TransferMethod=SdTransferMethodPio;q.Command.BlockSize=64;q.Command.BlockCount=1;
  q.Command.Length=64;q.Command.DataBuffer=pio_in;q.Command.Argument=0x80fffff1u;
  assert(SdhcSlotIssueRequest(&s,&q)==STATUS_PENDING);poll_done(&s,&q);
  q.Type=SdRequestTypeStartTransfer;
  assert(SdhcSlotIssueRequest(&s,&q)==STATUS_PENDING&&q.Command.BlockCount==0);poll_done(&s,&q);
  assert(pio_in[1]==0x64&&pio_in[16]==0x01);
  assert(SdhcSetSpeed(&s,SdBusSpeedHigh)==0&&SdhcSetClock(&s,50000)==0&&NvDiag.ClockKhzActual<=25000);
  /* CMD16(512) with stale ERROR_INTERRUPT bit set beforehand -> cleared cleanly in CrashdumpMode */
  put32(s.regs,0x30,0x00018001u);
  memset(&q,0,sizeof(q));q.Type=SdRequestTypeCommandNoTransfer;q.Command.Index=16;
  q.Command.Class=SdCommandClassStandard;q.Command.ResponseType=SdResponseTypeR1;
  q.Command.TransferType=SdTransferTypeNone;q.Command.Argument=512;
  assert(SdhcSlotIssueRequest(&s,&q)==STATUS_PENDING);poll_done(&s,&q);

  /* SdCommandTypeAbort (CMD12) ignores DAT_INHIBIT when CMD_INHIBIT is clear,
   * ordinary no-data R1 commands can also use CMD while DAT is busy. */
  put32(s.regs,0x24,SDHC_PS_DAT_INHIBIT);elapsed_us=0;
  memset(&q,0,sizeof(q));q.Type=SdRequestTypeCommandNoTransfer;q.Command.Index=12;
  q.Command.Type=SdCommandTypeAbort;q.Command.Class=SdCommandClassStandard;
  q.Command.ResponseType=SdResponseTypeR1B;q.Command.TransferType=SdTransferTypeNone;
  assert(SdhcSlotIssueRequest(&s,&q)==STATUS_PENDING&&elapsed_us==0);poll_done(&s,&q);

  put32(s.regs,0x24,SDHC_PS_DAT_INHIBIT);inhibit_regs=s.regs;inhibit_clear_after_us=0;elapsed_us=0;
  memset(&q,0,sizeof(q));q.Type=SdRequestTypeCommandNoTransfer;q.Command.Index=55;
  q.Command.Class=SdCommandClassApp;q.Command.ResponseType=SdResponseTypeR1;
  q.Command.TransferType=SdTransferTypeNone;q.Command.Argument=rca;
  assert(SdhcSlotIssueRequest(&s,&q)==STATUS_PENDING&&elapsed_us==0);poll_done(&s,&q);
  assert(s.AppPrefixAccepted);
  /* Stuck CMD_INHIBIT after CMD55 -> times out with STATUS_DEVICE_BUSY, clears slot & AppPrefixAccepted! */
  put32(s.regs,0x24,SDHC_PS_CMD_INHIBIT);elapsed_us=0;
  memset(&q,0,sizeof(q));q.Type=SdRequestTypeCommandNoTransfer;q.Command.Index=42;
  q.Command.Class=SdCommandClassApp;q.Command.ResponseType=SdResponseTypeR1;
  q.Command.TransferType=SdTransferTypeNone;q.Command.Argument=0;
  assert(SdhcSlotIssueRequest(&s,&q)==STATUS_DEVICE_BUSY&&elapsed_us==NV_PIO_DUMP_POLL_US);
  assert(!s.OutstandingRequests[0]&&!s.AppPrefixAccepted);
  put32(s.regs,0x24,0);

  /* Normal runtime (CrashdumpMode==0) preserves S18R=1 on ACMD41 so SDR104/DDR200 CMD11 works! */
  s.CrashdumpMode=0;sim_card_s18a=false;sim_card_18v=false;
  memset(&q,0,sizeof(q));q.Type=SdRequestTypeCommandNoTransfer;q.Command.Index=55;
  q.Command.Class=SdCommandClassApp;q.Command.ResponseType=SdResponseTypeR1;q.Command.TransferType=SdTransferTypeNone;
  assert(SdhcSlotIssueRequest(&s,&q)==STATUS_PENDING);poll_done(&s,&q);
  memset(&q,0,sizeof(q));q.Type=SdRequestTypeCommandNoTransfer;q.Command.Index=41;
  q.Command.Class=SdCommandClassApp;q.Command.ResponseType=SdResponseTypeR3;
  q.Command.TransferType=SdTransferTypeNone;q.Command.Argument=0x51ff8000u;
  assert(SdhcSlotIssueRequest(&s,&q)==STATUS_PENDING);
  assert(sim_card_last_arg==0x51ff8000u&&sim_card_s18a);
  poll_done(&s,&q);

  /* MultiBlock sets AUTO_CMD12; MultiBlockNoStop (CMD23+CMD25) omits AUTO_CMD12. */
  s.CrashdumpMode=1;s.TargetFacts=LiteWriteFacts;
  memset(&q,0,sizeof(q));q.Type=SdRequestTypeCommandWithTransfer;q.Command.Index=25;
  q.Command.Class=SdCommandClassStandard;q.Command.ResponseType=SdResponseTypeR1;
  q.Command.TransferDirection=SdTransferDirectionWrite;q.Command.TransferMethod=SdTransferMethodPio;
  q.Command.BlockSize=512;q.Command.BlockCount=8;q.Command.Length=4096;
  q.Command.DataBuffer=payload;q.Command.Argument=WINARM_FIRST+64;
  q.Command.TransferType=SdTransferTypeMultiBlock;
  s.CrashdumpMode=1; q.Command.UseAutoCmd12=TRUE;
  assert(SdhcSetTransferMode(&s,&q)==0&&(SdhcReadRegisterUshort(&s,0x0c)&SDHC_TM_AUTO_CMD12_ENABLE)!=0);
  q.Command.UseAutoCmd12=FALSE;
  assert(SdhcSetTransferMode(&s,&q)==0&&!(SdhcReadRegisterUshort(&s,0x0c)&SDHC_TM_AUTO_CMD12_ENABLE));
  q.Command.UseAutoCmd23=TRUE;
  assert(SdhcSetTransferMode(&s,&q)==STATUS_NOT_SUPPORTED);
  q.Command.UseAutoCmd23=FALSE; q.Command.UseAutoCmd12=TRUE;
  q.Command.TransferType=SdTransferTypeMultiBlockNoStop;
  assert(SdhcSetTransferMode(&s,&q)==0&&(SdhcReadRegisterUshort(&s,0x0c)&SDHC_TM_AUTO_CMD12_ENABLE)==0);

  T214Release(&s);sim_card_active=false;inhibit_regs=NULL;
  DumpUninitialize();assert(!g_DumpPrepState);
 }
 /* Failure after and before E4 rises must retire FreshPower, stop SDCLK,
  * keep the pads isolated and restore unrelated bits and all six SD latches. */
 slot(&s); s.T214Gpio=maps[4];s.T214Misc=maps[1];s.T214Pmc=maps[2];
 s.T214I2c=maps[3];s.T214Car=maps[0];checked_host=NULL;
 for (unsigned failure=0;failure<5;failure++) {
  put32(maps[4],0x110,failure==0?0:0x10);
  put32(maps[4],0x120,0x10);
  block_e4_rise=failure==1;block_pad_mux=failure==2;block_e4_fall=failure==3;
  pmic_fail_once=failure==4?2:0;
  unsigned old_writes=card_power_writes;
  s.T214DumpFreshPower=TRUE;
  NTSTATUS failed=T214Power(&s,TRUE);
  assert(failed==(failure==4?STATUS_IO_DEVICE_ERROR:STATUS_DEVICE_POWER_FAILURE));
  if (failure==0) assert(card_power_writes==old_writes);
  assert(!s.T214DumpFreshPower && !s.T214CardOn && !(s.regs[0x2c]&4));
  assert(raw32(maps[2],0x444)&T214_IO_BIT);
  assert(raw32(maps[1],0x3000)==0x2480 && raw32(maps[4],0x300)==0xc0);
  assert(raw32(maps[4],0x310)==0x94 && raw32(maps[4],0x320)==0x85);
 }
 block_e4_rise=false;block_e4_fall=false;block_pad_mux=false;put32(maps[4],0x110,0x10);
 s.CrashdumpMode=FALSE;s.T214Gate=(HANDLE)1;gate_busy=false;
 assert(T214Power(&s,TRUE)==0 && last_off_us==10000 && !discharge_seen);
 puts("PASS: DumpPreInitialize/HIGH_LEVEL takeover, Dump_Open ACMD41 S18R=0 & inhibit poll, 239 ms 1.8V physical-card model, pad restore on failure, I2C5 CAR/bus-clear recovery, 128-block CMD25 PIO, level-ready synthesis, 1.8V SDR25, identity and WINARM bounds");
}
'''

with tempfile.TemporaryDirectory(prefix='lite-crashdump-') as temp:
    path = Path(temp)
    for visible in (False, True):
        target, tail = target_header(visible)
        constants = '\nstatic const UCHAR physical_tail[70]={' + ','.join(str(v) for v in tail) + '};\n'
        (path / 'test.c').write_text(prefix + target + body + constants + checks)
        for enabled, ddr, buffered in ((1, 0, 0), (1, 1, 0), (1, 1, 1)):
            subprocess.run(['clang', '-std=c11', '-Wall', '-Werror', '-Wno-unused-function',
                            '-Wno-unused-but-set-variable', '-Wno-unknown-pragmas',
                            '-fsanitize=address,undefined', '-g', f'-DNV_T214_CRASHDUMP={enabled}',
                            f'-DNV_T214_DDR200={ddr}', f'-DNV_PIO_BUFFERED={buffered}',
                            str(path / 'test.c'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)
        # Existing DMA/filter/power tests also run against both partition views.
        (path / 'winarm-target.h').write_text(target)
        subprocess.run(['python3', str(root / 'tests/test-t214-data.py'), str(path / 'winarm-target.h')], check=True)
        # Optional cross-build harness can consume these public fixtures only.
        if os.environ.get('SD_DUMP_FIXTURE_DIR'):
            out = Path(os.environ['SD_DUMP_FIXTURE_DIR']) / ('visible' if visible else 'hidden')
            out.mkdir(parents=True, exist_ok=True)
            (out / 'winarm-target.h').write_text(target)
