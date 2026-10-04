/* Bounded ZIP/store writer for copied support files. No shell, compression
 * dependency, directory traversal, recursive collection or process dump. */
#ifndef SHADOW_ENGINE_SUPPORT_ZIP_H
#define SHADOW_ENGINE_SUPPORT_ZIP_H
#include <windows.h>
#include <stdint.h>
#include <string.h>
#define SUPPORT_ZIP_MAX_ENTRIES 8U
#define SUPPORT_ZIP_MAX_FILE (128U*1024U*1024U)
#define SUPPORT_ZIP_MAX_TOTAL (192U*1024U*1024U)
typedef struct SupportZipEntry {
    char name[96];
    uint32_t offset,size,crc;
} SupportZipEntry;
typedef struct SupportZip {
    HANDLE file;
    uint32_t offset,count,error;
    SupportZipEntry entries[SUPPORT_ZIP_MAX_ENTRIES];
    unsigned char buffer[65536];
} SupportZip;
static uint32_t support_zip_crc(uint32_t crc,const unsigned char *bytes,uint32_t size)
{
    static const uint32_t table[16]={
        0x00000000U,0x1DB71064U,0x3B6E20C8U,0x26D930ACU,
        0x76DC4190U,0x6B6B51F4U,0x4DB26158U,0x5005713CU,
        0xEDB88320U,0xF00F9344U,0xD6D6A3E8U,0xCB61B38CU,
        0x9B64C2B0U,0x86D3D2D4U,0xA00AE278U,0xBDBDF21CU};
    uint32_t i;
    for(i=0;i<size;++i) {
        crc^=bytes[i];
        crc=(crc>>4)^table[crc&15U];
        crc=(crc>>4)^table[crc&15U];
    }
    return crc;
}
static int support_zip_write(SupportZip *z,const void *bytes,uint32_t size)
{
    DWORD written=0;
    if(z->error) return 0;
    if(size>SUPPORT_ZIP_MAX_TOTAL-z->offset) { z->error=ERROR_BUFFER_OVERFLOW; return 0; }
    if(!WriteFile(z->file,bytes,size,&written,NULL) || written!=size) {
        z->error=GetLastError(); if(!z->error) z->error=ERROR_WRITE_FAULT; return 0;
    }
    z->offset+=size; return 1;
}
static int support_zip_u16(SupportZip *z,uint16_t value)
{ return support_zip_write(z,&value,2); }
static int support_zip_u32(SupportZip *z,uint32_t value)
{ return support_zip_write(z,&value,4); }
static SupportZipEntry *support_zip_begin(SupportZip *z,const char *name,uint32_t size)
{
    SupportZipEntry *e;
    size_t length=strlen(name);
    unsigned i;
    if(z->error) return NULL;
    if(!length || length>=sizeof(z->entries[0].name) || strchr(name,'/') || strchr(name,'\\') ||
       strchr(name,':') || strstr(name,"..") || z->count>=SUPPORT_ZIP_MAX_ENTRIES || size>SUPPORT_ZIP_MAX_FILE) {
        z->error=ERROR_INVALID_PARAMETER; return NULL;
    }
    for(i=0;i<z->count;++i) if(!strcmp(name,z->entries[i].name)) {
        z->error=ERROR_ALREADY_EXISTS; return NULL;
    }
    e=&z->entries[z->count]; memset(e,0,sizeof(*e)); strcpy(e->name,name);
    e->size=size; e->offset=z->offset; e->crc=0xFFFFFFFFU;
    support_zip_u32(z,0x04034B50U); support_zip_u16(z,20); support_zip_u16(z,8);
    support_zip_u16(z,0); support_zip_u16(z,0); support_zip_u16(z,33);
    support_zip_u32(z,0); support_zip_u32(z,0); support_zip_u32(z,0);
    support_zip_u16(z,(uint16_t)length); support_zip_u16(z,0);
    support_zip_write(z,name,(uint32_t)length);
    return z->error?NULL:e;
}
static int support_zip_end(SupportZip *z,SupportZipEntry *e)
{
    e->crc^=0xFFFFFFFFU;
    support_zip_u32(z,0x08074B50U); support_zip_u32(z,e->crc);
    support_zip_u32(z,e->size); support_zip_u32(z,e->size);
    if(z->error) return 0;
    ++z->count; return 1;
}
static int support_zip_memory(SupportZip *z,const char *name,const void *data,uint32_t size)
{
    SupportZipEntry *e=support_zip_begin(z,name,size);
    if(!e) return 0;
    e->crc=support_zip_crc(e->crc,(const unsigned char *)data,size);
    return support_zip_write(z,data,size) && support_zip_end(z,e);
}
/* Open first: a missing optional file adds no corrupt ZIP entry. The caller
 * records absence explicitly. Once begun, a short read fails the whole archive. */
static int support_zip_file(SupportZip *z,const char *name,const wchar_t *path)
{
    HANDLE source;
    DWORD length,high=0;
    SupportZipEntry *e;
    uint32_t left;
    DWORD read;
    int success=0;
    source=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(source==INVALID_HANDLE_VALUE) return 0;
    length=GetFileSize(source,&high);
    if(high || length>SUPPORT_ZIP_MAX_FILE) {
        CloseHandle(source); return 0;
    }
    e=support_zip_begin(z,name,length);
    if(!e) { CloseHandle(source); return 0; }
    left=e->size;
    while(left && !z->error) {
        DWORD amount=left>sizeof(z->buffer)?(DWORD)sizeof(z->buffer):left;
        if(!ReadFile(source,z->buffer,amount,&read,NULL) || read!=amount) {
            z->error=GetLastError(); if(!z->error) z->error=ERROR_READ_FAULT; break;
        }
        e->crc=support_zip_crc(e->crc,z->buffer,read);
        if(!support_zip_write(z,z->buffer,read)) break;
        left-=read;
    }
    if(!left && !z->error) success=support_zip_end(z,e);
    if(!CloseHandle(source)) { z->error=ERROR_READ_FAULT; success=0; }
    return success;
}
static int support_zip_finish(SupportZip *z)
{
    uint32_t i,start=z->offset,length;
    if(z->error) return 0;
    for(i=0;i<z->count;++i) {
        SupportZipEntry *e=&z->entries[i]; length=(uint32_t)strlen(e->name);
        support_zip_u32(z,0x02014B50U); support_zip_u16(z,20); support_zip_u16(z,20);
        support_zip_u16(z,8); support_zip_u16(z,0); support_zip_u16(z,0); support_zip_u16(z,33);
        support_zip_u32(z,e->crc); support_zip_u32(z,e->size); support_zip_u32(z,e->size);
        support_zip_u16(z,(uint16_t)length); support_zip_u16(z,0); support_zip_u16(z,0);
        support_zip_u16(z,0); support_zip_u16(z,0); support_zip_u32(z,0); support_zip_u32(z,e->offset);
        support_zip_write(z,e->name,length);
    }
    length=z->offset-start;
    support_zip_u32(z,0x06054B50U); support_zip_u16(z,0); support_zip_u16(z,0);
    support_zip_u16(z,(uint16_t)z->count); support_zip_u16(z,(uint16_t)z->count);
    support_zip_u32(z,length); support_zip_u32(z,start); support_zip_u16(z,0);
    if(z->error) return 0;
    if(!FlushFileBuffers(z->file)) { z->error=GetLastError(); return 0; }
    return 1;
}
#endif
