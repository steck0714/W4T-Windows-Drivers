/* SPDX-License-Identifier: MIT */
/* Erista platform I/O. Mapped registers come from the ACPI memory
 * resources; PMIC access goes straight to the two ACPI-declared I2C5 targets
 * (MAX77621 0x1C, MAX77620 0x3C) through the SPB Resource Hub. */
#include <ntifs.h>
#include <ntstrsafe.h>
#include <spb.h>
#include "platform_io.h"
#include "../platform/t210_resources.h"

static NTSTATUS failure(TG_GPU_PLATFORM *p,NTSTATUS status)
{
    if(p && !NT_SUCCESS(status) && NT_SUCCESS(p->IoStatus))p->IoStatus=status;
    return status;
}

static NTSTATUS OpenTarget(HANDLE *handle,LARGE_INTEGER id,ULONG share)
{
    WCHAR buffer[64];UNICODE_STRING name;OBJECT_ATTRIBUTES attributes;
    IO_STATUS_BLOCK iosb={0};NTSTATUS status;
    RtlInitEmptyUnicodeString(&name,buffer,sizeof(buffer));
    status=RtlUnicodeStringPrintf(&name,L"\\Device\\RESOURCE_HUB\\%016I64x",(ULONG64)id.QuadPart);
    if(!NT_SUCCESS(status))return status;
    InitializeObjectAttributes(&attributes,&name,OBJ_KERNEL_HANDLE|OBJ_CASE_INSENSITIVE,NULL,NULL);
    return ZwCreateFile(handle,GENERIC_READ|GENERIC_WRITE|SYNCHRONIZE,&attributes,&iosb,NULL,
        FILE_ATTRIBUTE_NORMAL,share,FILE_OPEN,FILE_NON_DIRECTORY_FILE|FILE_SYNCHRONOUS_IO_NONALERT,NULL,0);
}

NTSTATUS TgGpuOpenPlatform(TG_GPU_PLATFORM *p,const TG_GPU_RESOURCES *resources)
{
    NTSTATUS status;
    PAGED_CODE();
    if(!p || !resources || p->Bridge || p->Pmic620 || p->Resources || resources->ConnectionCount!=2)
        return STATUS_INVALID_PARAMETER;
    for(UINT i=0;i<TgGpuApertureCount;++i)
        if(!resources->Mapping[i] || resources->Memory[i].Type!=CmResourceTypeMemory ||
           resources->Memory[i].u.Memory.Length<4)return STATUS_DEVICE_CONFIGURATION_ERROR;
    p->IoStatus=STATUS_SUCCESS;
    status=OpenTarget(&p->Bridge,resources->Connection[0],0);
    if(!NT_SUCCESS(status)){p->Bridge=NULL;return failure(p,status);}
    status=OpenTarget(&p->Pmic620,resources->Connection[1],FILE_SHARE_READ|FILE_SHARE_WRITE);
    if(!NT_SUCCESS(status)){
        (VOID)ZwClose(p->Bridge);p->Bridge=NULL;p->Pmic620=NULL;return failure(p,status);
    }
    p->Resources=resources;return STATUS_SUCCESS;
}
NTSTATUS TgGpuClosePlatform(TG_GPU_PLATFORM *p)
{
    NTSTATUS status=STATUS_SUCCESS,second=STATUS_SUCCESS;
    PAGED_CODE();
    if(!p)return STATUS_INVALID_PARAMETER;
    if(!p->Bridge && !p->Pmic620){p->Resources=NULL;return STATUS_SUCCESS;}
    if(p->Bridge){status=ZwClose(p->Bridge);if(NT_SUCCESS(status))p->Bridge=NULL;}
    if(p->Pmic620){second=ZwClose(p->Pmic620);if(NT_SUCCESS(second))p->Pmic620=NULL;}
    if(NT_SUCCESS(status)&&NT_SUCCESS(second)){p->Resources=NULL;return STATUS_SUCCESS;}
    return failure(p,NT_SUCCESS(status)?second:status);
}

/* One SPB transfer. The caller decides whether a failure is recorded (see Spb). */
static NTSTATUS SpbOnce(HANDLE target,UCHAR reg,UCHAR *value,BOOLEAN write)
{
    IO_STATUS_BLOCK iosb={0};NTSTATUS status;
    UCHAR out[2]={reg,write?*value:0},in=0;
    if(write){
        SPB_TRANSFER_LIST_AND_ENTRIES(1) seq;
        SPB_TRANSFER_LIST_INIT(&seq.List,1);
        seq.List.Transfers[0]=SPB_TRANSFER_LIST_ENTRY_INIT_SIMPLE(SpbTransferDirectionToDevice,0,out,2);
        status=ZwDeviceIoControlFile(target,NULL,NULL,NULL,&iosb,IOCTL_SPB_EXECUTE_SEQUENCE,
            &seq,sizeof(seq),NULL,0);
        if(status==STATUS_PENDING){status=ZwWaitForSingleObject(target,FALSE,NULL);if(status==STATUS_SUCCESS)status=iosb.Status;}
        if(NT_SUCCESS(status) && iosb.Information!=2)status=STATUS_DEVICE_PROTOCOL_ERROR;
    } else {
        SPB_TRANSFER_LIST_AND_ENTRIES(2) seq;
        SPB_TRANSFER_LIST_INIT(&seq.List,2);
        seq.List.Transfers[0]=SPB_TRANSFER_LIST_ENTRY_INIT_SIMPLE(SpbTransferDirectionToDevice,0,out,1);
        seq.List.Transfers[1]=SPB_TRANSFER_LIST_ENTRY_INIT_SIMPLE(SpbTransferDirectionFromDevice,0,&in,1);
        status=ZwDeviceIoControlFile(target,NULL,NULL,NULL,&iosb,IOCTL_SPB_EXECUTE_SEQUENCE,
            &seq,sizeof(seq),NULL,0);
        if(status==STATUS_PENDING){status=ZwWaitForSingleObject(target,FALSE,NULL);if(status==STATUS_SUCCESS)status=iosb.Status;}
        if(NT_SUCCESS(status) && iosb.Information!=2)status=STATUS_DEVICE_PROTOCOL_ERROR;
        if(NT_SUCCESS(status))*value=in;
    }
    return status;
}

/* PMIC register accesses are idempotent (write reg+value / read reg), and I2C5 is shared with the battery, fan and
 * charger drivers, so a transfer can fail transiently (bus arbitration, a NACK during someone else's transaction).
 * Retry a few times before reporting; only the failure that survives is recorded in IoStatus (a recovered transient
 * must not poison it). Runs at PASSIVE_LEVEL like the transfer itself. windows-switch v0.8.0's swbatt GPU power
 * bridge made the same change ("retries PMIC I2C transfers"). */
#define TG_GPU_SPB_ATTEMPTS 3u
static NTSTATUS Spb(TG_GPU_PLATFORM *p,HANDLE target,UCHAR reg,UCHAR *value,BOOLEAN write)
{
    NTSTATUS status=STATUS_UNSUCCESSFUL;ULONG attempt;LARGE_INTEGER interval;
    for(attempt=0;attempt<TG_GPU_SPB_ATTEMPTS;attempt++){
        status=SpbOnce(target,reg,value,write);
        if(NT_SUCCESS(status)||status==STATUS_INVALID_PARAMETER||status==STATUS_INVALID_HANDLE||
           status==STATUS_ACCESS_DENIED||status==STATUS_INSUFFICIENT_RESOURCES||status==STATUS_CANCELLED)break;
        if(attempt+1<TG_GPU_SPB_ATTEMPTS){interval.QuadPart=-10000;(VOID)KeDelayExecutionThread(KernelMode,FALSE,&interval);} /* 1 ms */
    }
    return failure(p,status);
}

NTSTATUS TgGpuPlatformRequest(TG_GPU_PLATFORM *p,struct tg_gpu_pmic_request *op)
{
    HANDLE target;UCHAR value;NTSTATUS status;
    PAGED_CODE();
    if(!p || !p->Bridge || !p->Pmic620 || !op)return STATUS_INVALID_PARAMETER;
    /* Local answers for the shared preflight/diagnostic queries. */
    if(op->reg==TG_GPU_PMIC_QUERY_ADDRESS){op->value=TG_GPU_PMIC_MAX77621_GPU;return STATUS_SUCCESS;}
    if(op->reg==TG_GPU_PMIC_QUERY_TEMP){op->value=0;return STATUS_SUCCESS;} /* no thermal gate on Erista */
    if(op->reg==TG_GPU_PMIC_QUERY_PLLP){op->value=0x48000000u;return STATUS_SUCCESS;}
    if(op->reg>0xff)return failure(p,STATUS_INVALID_PARAMETER);
    if(op->address==TG_GPU_PMIC_MAX77621_GPU)target=p->Bridge;
    else if(op->address==TG_GPU_PMIC_MAX77620)target=p->Pmic620;
    else return failure(p,STATUS_INVALID_PARAMETER);
    value=(UCHAR)op->value;
    status=Spb(p,target,(UCHAR)op->reg,&value,op->write?TRUE:FALSE);
    if(NT_SUCCESS(status)&&!op->write)op->value=value;
    return status;
}

NTSTATUS TgGpuPlatformPreflight(TG_GPU_PLATFORM *p,UCHAR *address)
{
    struct tg_gpu_pmic_request request={0,TG_GPU_PMIC_QUERY_ADDRESS,0,0};NTSTATUS status;
    UCHAR id1,id2;
    if(!address)return STATUS_INVALID_PARAMETER;
    status=TgGpuPlatformRequest(p,&request);if(!NT_SUCCESS(status))return status;
    /* Both targets must answer before any rail write: MAX77620 CID4 (0x5C)
     * and MAX77621 CHIPID1 (0x04). Values are logged by the probe, not gated:
     * revisions differ between consoles. */
    request=(struct tg_gpu_pmic_request){TG_GPU_PMIC_MAX77620,0x5c,0,0};
    status=TgGpuPlatformRequest(p,&request);if(!NT_SUCCESS(status))return status;
    id1=(UCHAR)request.value;
    request=(struct tg_gpu_pmic_request){TG_GPU_PMIC_MAX77621_GPU,0x04,0,0};
    status=TgGpuPlatformRequest(p,&request);if(!NT_SUCCESS(status))return status;
    id2=(UCHAR)request.value;UNREFERENCED_PARAMETER(id1);UNREFERENCED_PARAMETER(id2);
    *address=TG_GPU_PMIC_MAX77621_GPU;return STATUS_SUCCESS;
}
static PULONG location(TG_GPU_PLATFORM *p,UINT32 address,BOOLEAN write)
{
    const TG_GPU_RESOURCES *r=p->Resources;
    if(!r || (address&3))return NULL;
    if(r->LicMapping && address>=T210_GPU_LIC_BASE && address-T210_GPU_LIC_BASE<=T210_GPU_LIC_SIZE-4)
        return (PULONG)((PUCHAR)r->LicMapping+address-T210_GPU_LIC_BASE);
    for(UINT i=0;i<TgGpuApertureCount;++i) {
        UINT64 base=(UINT64)r->Memory[i].u.Memory.Start.QuadPart;
        UINT length=r->Memory[i].u.Memory.Length;
        if(r->Mapping[i] && length>=4 && address>=base && (UINT64)address-base<=length-4) {
            /* CPU/DDR/ASID ownership stays with its existing driver. Fuses
             * and HIDREV are read-only. */
            if(write && (i==TgGpuMcAsid || i>=TgGpuHidrev))return NULL;
            return (PULONG)((PUCHAR)r->Mapping[i]+(UINT64)address-base);
        }
    }
    return NULL;
}
static UINT32 read32(void *context,UINT32 address)
{
    TG_GPU_PLATFORM *p=context;PULONG reg=location(p,address,FALSE);
    if(!reg){failure(p,STATUS_DEVICE_CONFIGURATION_ERROR);return UINT32_MAX;}
    return READ_REGISTER_ULONG(reg);
}
static void write32(void *context,UINT32 address,UINT32 value)
{
    TG_GPU_PLATFORM *p=context;PULONG reg=location(p,address,TRUE);
    if(!reg){failure(p,STATUS_DEVICE_CONFIGURATION_ERROR);return;}
    KeMemoryBarrier();WRITE_REGISTER_ULONG(reg,value);KeMemoryBarrier();
}
static bool read_pmic(void *context,UINT8 address,UINT8 reg,UINT8 *value)
{
    struct tg_gpu_pmic_request op={address,reg,0,0};
    if(!NT_SUCCESS(TgGpuPlatformRequest(context,&op)))return false;
    *value=(UINT8)op.value;return true;
}
static bool write_pmic(void *context,UINT8 address,UINT8 reg,UINT8 value)
{
    struct tg_gpu_pmic_request op={address,reg,1,value};
    return NT_SUCCESS(TgGpuPlatformRequest(context,&op));
}
static void delay(void *context,UINT32 us)
{
    LARGE_INTEGER interval;UNREFERENCED_PARAMETER(context);
    if(us<1000){KeStallExecutionProcessor(us);return;}
    interval.QuadPart=-(LONGLONG)us*10;(VOID)KeDelayExecutionThread(KernelMode,FALSE,&interval);
}
struct tg_gpu_power_io TgGpuPlatformIo(TG_GPU_PLATFORM *p)
{ return (struct tg_gpu_power_io){p,read32,write32,read_pmic,write_pmic,delay}; }
