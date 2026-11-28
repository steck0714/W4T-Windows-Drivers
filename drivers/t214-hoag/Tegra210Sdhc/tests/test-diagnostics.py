#!/usr/bin/env python3
"""Exercise the real miniport snapshot queue without a Windows boot disk."""
from pathlib import Path
import re
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / 'src/nvtsdhc.c').read_text()
diag = source[source.index('typedef struct _NV_DIAG {'):source.index('} NV_DIAG;') + len('} NV_DIAG;')]
start = source.index('#if NV_DIAG_CLOCK\n/* Snapshot only')
code = source[start:source.index('\n#if NV_DIAG_REG\n\nstatic VOID\nNvDiagThread', start)]
register_slot = source[source.index('static VOID NvRegisterRuntimeSlot'):source.index('static UCHAR\nNvDiagErrorKind')]
cleanup = source[source.index('VOID\nSdhcCleanup('):source.index('// Host routine implementations.')]
fields = sorted(set(re.findall(r'SdhcExtension->(\w+)', code)) -
                {'Capabilities', 'T214Bounce', 'T214Scr', 'T214SwitchStatus',
                 'T214DdrPassMap', 'T214DdrReadyMap', 'OwnerMiniport'})
prefix = r'''
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
typedef uint32_t ULONG; typedef int32_t LONG; typedef int64_t LONG64,LONGLONG;
typedef uint16_t USHORT; typedef uint8_t UCHAR,BOOLEAN;
typedef wchar_t WCHAR; typedef const wchar_t *PCWSTR;
typedef void *PVOID,*HANDLE; typedef void VOID; typedef int KTIMER,KDPC;
typedef int UNICODE_STRING,OBJECT_ATTRIBUTES;
typedef void *PDRIVER_OBJECT, *PSD_MINIPORT;
typedef struct IO_ITEM *PIO_WORKITEM;
typedef void (*IO_CALLBACK)(PVOID,PVOID,PIO_WORKITEM);
struct IO_ITEM { IO_CALLBACK f; PVOID context,object; };
typedef struct ENTRY {struct ENTRY *Flink,*Blink;} LIST_ENTRY,*PLIST_ENTRY;
typedef struct {PSD_MINIPORT Miniport; _Alignas(16) unsigned char PrivateExtension[];} SDPORT_SLOT_EXTENSION,*PSDPORT_SLOT_EXTENSION;
typedef unsigned KIRQL,KSPIN_LOCK;
#define _In_opt_
#define TRUE 1
#define DISPATCH_LEVEL 2
#define POOL_FLAG_NON_PAGED 1
#define CONTAINING_RECORD(p,t,f) ((t*)((char*)(p)-offsetof(t,f)))
static void InitializeListHead(PLIST_ENTRY h){h->Flink=h->Blink=h;}
static int IsListEmpty(PLIST_ENTRY h){return h->Flink==h;}
static void InsertTailList(PLIST_ENTRY h,PLIST_ENTRY e){e->Blink=h->Blink;e->Flink=h;h->Blink->Flink=e;h->Blink=e;}
static void RemoveEntryList(PLIST_ENTRY e){e->Blink->Flink=e->Flink;e->Flink->Blink=e->Blink;}
static PLIST_ENTRY RemoveHeadList(PLIST_ENTRY h){PLIST_ENTRY e=h->Flink;RemoveEntryList(e);return e;}
static KIRQL irql;
static KIRQL KeGetCurrentIrql(void){return irql;}
static void KeAcquireSpinLock(KSPIN_LOCK *lock,KIRQL *old){assert(!*lock && irql<=2);*old=irql;irql=2;*lock=1;}
static void KeReleaseSpinLock(KSPIN_LOCK *lock,KIRQL old){assert(*lock);*lock=0;irql=old;}
static KSPIN_LOCK NvSlotLock;
static PDRIVER_OBJECT NvRuntimeDriver=(PVOID)1;
static LIST_ENTRY NvActiveSlots;
#define _In_
#define NT_ASSERT assert
#define RTL_NUMBER_OF(a) (sizeof(a)/sizeof((a)[0]))
#define UNREFERENCED_PARAMETER(a) ((void)(a))
#define NV_DIAG_CLOCK 1
#define NV_T214_SDR104 1
#define NV_T214_DDR200 1
#define NV_T210_LITE_WINARM_WRITE 1
#define OBJ_KERNEL_HANDLE 1
#define OBJ_CASE_INSENSITIVE 2
#define KEY_SET_VALUE 1
#define REG_OPTION_NON_VOLATILE 0
#define DelayedWorkQueue 0
#define NT_SUCCESS(s) ((s)>=0)
static int in_callback,queued,writes,create_failure;
static uint64_t now;
static ULONG last_commands,last_adma,last_saves,last_pass_map,last_ready_map;
static PIO_WORKITEM work;
static unsigned pins,allocated,fail_alloc,releases;
static void (*on_registry_io)(void);
static LONG InterlockedExchange(volatile LONG *p,LONG v){LONG old=*p;*p=v;return old;}
static LONG InterlockedCompareExchange(volatile LONG *p,LONG v,LONG cmp){LONG old=*p;if(old==cmp)*p=v;return old;}
static LONG64 InterlockedExchange64(volatile LONG64 *p,LONG64 v){LONG64 old=*p;*p=v;return old;}
static LONG64 InterlockedCompareExchange64(volatile LONG64 *p,LONG64 v,LONG64 cmp){LONG64 old=*p;if(old==cmp)*p=v;return old;}
static uint64_t KeQueryInterruptTime(void){return now;}
static void RtlInitUnicodeString(UNICODE_STRING *s,PCWSTR n){(void)s;(void)n;}
static void InitializeObjectAttributes(OBJECT_ATTRIBUTES*a,UNICODE_STRING*b,int c,void*d,void*e){(void)a;(void)b;(void)c;(void)d;(void)e;}
static int ZwCreateKey(HANDLE *key,int a,OBJECT_ATTRIBUTES*b,int c,void*d,int e,void*f){
 (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;assert(!in_callback && !NvSlotLock && pins==1);
 if(on_registry_io){void (*hook)(void)=on_registry_io;on_registry_io=NULL;hook();}
 *key=(void*)1;return create_failure?-1:0;
}
static void NvDiagSetValue(HANDLE key,PCWSTR name,ULONG value){
 assert(key && !in_callback);writes++;
 if(!wcscmp(name,L"Commands"))last_commands=value;
 if(!wcscmp(name,L"AdmaBlocks"))last_adma=value;
 if(!wcscmp(name,L"Saves"))last_saves=value;
 if(!wcscmp(name,L"Ddr200PassMap3"))last_pass_map=value;
 if(!wcscmp(name,L"Ddr200ReadyMap3"))last_ready_map=value;
}
static void ZwClose(HANDLE key){assert(key && !in_callback);}
static size_t IoSizeofWorkItem(void){return sizeof(struct IO_ITEM);}
static void *ExAllocatePool2(int flags,size_t size,unsigned tag){
 (void)flags;(void)tag;if(fail_alloc)return NULL;allocated++;return calloc(1,size);
}
static void IoInitializeWorkItem(PVOID object,PIO_WORKITEM w){assert(object==NvRuntimeDriver);w->object=object;}
static void IoQueueWorkItemEx(PIO_WORKITEM w,IO_CALLBACK f,int q,void*c){
 (void)q;assert(!queued && NvSlotLock);queued=1;pins++;work=w;w->f=f;w->context=c;
}
static void IoUninitializeWorkItem(PIO_WORKITEM w){assert(pins==1 && w->object==NvRuntimeDriver);w->object=NULL;}
static void ExFreePoolWithTag(PIO_WORKITEM w,unsigned tag){(void)tag;assert(!w->object && allocated);allocated--;free(w);}
static void run_worker(void){
 assert(queued && !in_callback && !NvSlotLock);PIO_WORKITEM w=work;queued=0;work=NULL;
 w->f(w->object,w->context,w);pins--;
}
'''
prefix += (Path(__file__).resolve().parents[1] / 'src/t214_ddr200_policy.h').read_text()
prefix += diag + '\nstatic NV_DIAG NvDiag;\n'
prefix += 'typedef struct { struct { ULONG BaseClockFrequencyKhz; } Capabilities; void *T214Bounce;\n'
prefix += 'UCHAR T214Scr[8],T214SwitchStatus[64];\n'
prefix += 'PSD_MINIPORT OwnerMiniport; LIST_ENTRY ActiveLink;\n'
prefix += 'ULONG T214DdrPassMap[4],T214DdrReadyMap[4];\n'
prefix += '\n'.join('ULONG ' + name + ';' for name in fields)
prefix += '\n} SDHC_EXTENSION,*PSDHC_EXTENSION;\n'
prefix += r'''
static PSDHC_EXTENSION NvDiagSaveOwner;
static void T214Release(PSDHC_EXTENSION slot){assert(!NvSlotLock && !slot->OwnerMiniport);releases++;}
'''
tests = r'''
static PSDHC_EXTENSION new_slot(PSD_MINIPORT owner) {
 SDPORT_SLOT_EXTENSION *p=calloc(1,offsetof(SDPORT_SLOT_EXTENSION,PrivateExtension)+sizeof(SDHC_EXTENSION));
 assert(p);p->Miniport=owner;
 PSDHC_EXTENSION slot=(PSDHC_EXTENSION)p->PrivateExtension;
 NvRegisterRuntimeSlot(slot);return slot;
}
static void free_slot(PSDHC_EXTENSION s){free(CONTAINING_RECORD(s,SDPORT_SLOT_EXTENSION,PrivateExtension));}
static int owner_a,owner_b;
static PSDHC_EXTENSION removed_during_write;
static void remove_while_writing(void) {
 assert(pins==1 && !NvSlotLock);
 SdhcCleanup(&owner_b);
 free_slot(removed_during_write);
}
int main(void){
 InitializeListHead(&NvActiveSlots);
 PSDHC_EXTENSION s=new_slot(&owner_a), other=new_slot(&owner_b);
 NvDiag.RegistryPath[0]=L'x';NvDiag.Commands=123;s->T214DmaBlocks=456;
 s->T214DdrPassMap[3]=0x55555555;s->T214DdrReadyMap[3]=0x7fffffff;
 in_callback=1;NvDiagSaveClock(s);assert(queued && !writes && allocated==1 && pins==1);
 for(int i=0;i<100000;i++)NvDiagSaveClock(s);
 assert(queued && !writes && allocated==1);in_callback=0;
 s->T214DmaBlocks=0;NvDiag.Commands=999;
 run_worker();assert(last_commands==123 && last_adma==456 && last_saves==1 && !allocated && !pins);
 assert(last_pass_map==0x55555555 && last_ready_map==0x7fffffff);
 now=299999999;NvDiagSavePeriodic(s);assert(!queued);
 now=300000000;NvDiagSavePeriodic(s);assert(queued);run_worker();assert(last_commands==999 && last_saves==2);
 in_callback=1;NvDiagSaveClock(s);assert(queued);in_callback=0;run_worker();assert(last_saves==3);
 create_failure=1;NvDiagSaveClock(s);run_worker();assert(!NvDiagSaveQueued && !pins && !allocated);create_failure=0;
 fail_alloc=1;NvDiagSaveClock(s);assert(!queued && !NvDiagSaveQueued && !pins);fail_alloc=0;
 s->CrashdumpMode=1;NvDiagSaveClock(s);s->CrashdumpMode=0;assert(!queued);
 irql=3;NvDiagSaveClock(s);irql=0;assert(!queued);
 NvDiag.RegistryPath[0]=0;NvDiagSaveClock(s);assert(!queued);NvDiag.RegistryPath[0]=L'x';

 /* Stop before the worker starts, including re-add with the same owner. */
 NvDiagSaveClock(s);SdhcCleanup(&owner_a);assert(releases==1 && queued && pins==1);
 NvDiagSaveClock(s);free_slot(s);
 s=new_slot(&owner_a);SdhcCleanup((PVOID)99);
 int before=writes;run_worker();assert(writes==before && !allocated && !pins);
 NvDiagSaveClock(other);SdhcCleanup(&owner_a);free_slot(s);
 run_worker();assert(writes>before && releases==2 && last_saves==4);

 /* Stop after registry I/O starts. Cleanup must not wait for I/O on this SD.
  * The work item pins the driver and only reads its independent snapshot. */
 removed_during_write=other;on_registry_io=remove_while_writing;
 NvDiagSaveClock(other);run_worker();assert(releases==3 && last_saves==5 && !allocated && !pins);
 SdhcCleanup(&owner_b);assert(releases==3 && IsListEmpty(&NvActiveSlots));
 puts("PASS: driver-pinned snapshot, failed allocation, distinct owners, stop/re-add, pending and executing worker teardown");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='lite-sd-diag-') as temp:
    path = Path(temp)
    (path / 'test.c').write_text(prefix + register_slot + code + cleanup + tests)
    subprocess.run(['clang', '-std=c11', '-Werror', '-Wno-unknown-pragmas',
                    '-fsanitize=address,undefined', '-g', str(path / 'test.c'),
                    '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
