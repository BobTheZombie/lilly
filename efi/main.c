/* SPDX-License-Identifier: MIT */
#include <efi.h>
#include <efilib.h>
#define ENTRIES 32
struct entry { CHAR16 name[201],title[201],kernel[256],initrd[256],options[2048]; };
static struct entry entries[ENTRIES];
static EFI_FILE_HANDLE root;
static BOOLEAN copy_ascii(CHAR16 *out,UINTN cap,const char *s,BOOLEAN path) {
    UINTN n=0;
    if(path && *s!='/')return FALSE;
    if(path)for(const char *p=s;*p;p++)if(p[0]=='.'&&p[1]=='.')return FALSE;
    for(;*s;s++) {
        if((unsigned char)*s<32||(unsigned char)*s>126||n+1>=cap)return FALSE;
        out[n++]=(path&&*s=='/')?'\\':(CHAR16)*s;
    }
    out[n]=0;return TRUE;
}
static char *read_file(CHAR16 *path) {
    EFI_FILE_HANDLE file;EFI_STATUS status=uefi_call_wrapper(root->Open,5,root,&file,path,EFI_FILE_MODE_READ,0);
    if(EFI_ERROR(status))return NULL;
    UINTN size=0;status=uefi_call_wrapper(file->GetInfo,4,file,&GenericFileInfo,&size,NULL);
    if(status!=EFI_BUFFER_TOO_SMALL || size>4096){uefi_call_wrapper(file->Close,1,file);return NULL;}
    EFI_FILE_INFO *info=AllocatePool(size);
    if(!info){uefi_call_wrapper(file->Close,1,file);return NULL;}
    status=uefi_call_wrapper(file->GetInfo,4,file,&GenericFileInfo,&size,info);
    if(EFI_ERROR(status)){FreePool(info);uefi_call_wrapper(file->Close,1,file);return NULL;}
    UINTN bytes=(UINTN)info->FileSize;FreePool(info);
    if(EFI_ERROR(status)||bytes>16384){uefi_call_wrapper(file->Close,1,file);return NULL;}
    char *data=AllocateZeroPool(bytes+1);if(!data){uefi_call_wrapper(file->Close,1,file);return NULL;}
    UINTN got=bytes;status=uefi_call_wrapper(file->Read,3,file,&got,data);uefi_call_wrapper(file->Close,1,file);
    if(EFI_ERROR(status)||got!=bytes){FreePool(data);return NULL;}
    for(UINTN i=0;i<bytes;i++)if(!data[i]){FreePool(data);return NULL;}
    return data;
}
static BOOLEAN equal(const char *a,const char *b) {while(*a&&*a==*b){a++;b++;}return *a==*b;}
static char *next_line(char **cursor,char **value) {
    while(**cursor) {
        char *line=*cursor,*p=line;
        while(*p&&*p!='\n')p++;
        if(*p)*p++=0;
        *cursor=p;
        while(*line==' '||*line=='\t'||*line=='\r')line++;
        if(!*line||*line=='#')continue;
        p=line;while(*p&&*p!=' '&&*p!='\t')p++;
        if(*p)*p++=0;
        while(*p==' '||*p=='\t')p++;
        *value=p;
        char *end=p;while(*end)end++;
        while(end>p&&(end[-1]=='\r'||end[-1]==' '||end[-1]=='\t'))*--end=0;
        return line;
    }
    return NULL;
}
static BOOLEAN parse_entry(struct entry *e,char *data) {
    char *cursor=data,*value,*key;UINTN seen=0;
    while((key=next_line(&cursor,&value))) {
        if(equal(key,"title")){if(seen&1)return FALSE;seen|=1;if(!copy_ascii(e->title,201,value,FALSE))return FALSE;}
        else if(equal(key,"linux")){if(seen&2)return FALSE;seen|=2;if(!copy_ascii(e->kernel,256,value,TRUE))return FALSE;}
        else if(equal(key,"initrd")){if(seen&4)return FALSE;seen|=4;if(!copy_ascii(e->initrd,256,value,TRUE))return FALSE;}
        else if(equal(key,"options")){if(seen&8)return FALSE;seen|=8;if(!copy_ascii(e->options,2048,value,FALSE))return FALSE;}
        else if(!equal(key,"version"))return FALSE;
    }
    return (seen&3)==3;
}
static UINTN load_entries(void) {
    EFI_FILE_HANDLE directory;
    if(EFI_ERROR(uefi_call_wrapper(root->Open,5,root,&directory,L"\\loader\\entries",EFI_FILE_MODE_READ,0)))return 0;
    EFI_FILE_INFO *info=AllocatePool(2048);if(!info){uefi_call_wrapper(directory->Close,1,directory);return 0;}
    UINTN count=0;
    for(;;) {
        UINTN size=2048;EFI_STATUS status=uefi_call_wrapper(directory->Read,3,directory,&size,info);
        if(EFI_ERROR(status)||!size||count==ENTRIES)break;
        UINTN n=StrLen(info->FileName);
        if((info->Attribute&EFI_FILE_DIRECTORY)||n<12||n>200||StrnCmp(info->FileName,L"lilly-",6)||StrCmp(info->FileName+n-5,L".conf"))continue;
        BOOLEAN safe=TRUE;
        for(UINTN j=0;j<n;j++){CHAR16 c=info->FileName[j];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c=='_'))safe=FALSE;}
        if(!safe)continue;
        CHAR16 path[256];SPrint(path,sizeof path,L"\\loader\\entries\\%s",info->FileName);
        char *data=read_file(path);if(!data)continue;
        if(parse_entry(&entries[count],data)){StrCpy(entries[count].name,info->FileName);count++;}
        else SetMem(&entries[count],sizeof entries[count],0);
        FreePool(data);
    }
    FreePool(info);uefi_call_wrapper(directory->Close,1,directory);
    /* Directory iteration order is firmware-specific. Stable names select a
       deterministic default when no explicit default is configured. */
    for(UINTN i=0;i<count;i++)for(UINTN j=i+1;j<count;j++)if(StrCmp(entries[i].name,entries[j].name)>0) {
        struct entry temp=entries[i];entries[i]=entries[j];entries[j]=temp;
    }
    return count;
}
EFI_STATUS efi_main(EFI_HANDLE image,EFI_SYSTEM_TABLE *system) {
    InitializeLib(image,system);
    EFI_LOADED_IMAGE *loaded=NULL;
    EFI_STATUS status=uefi_call_wrapper(BS->HandleProtocol,3,image,&LoadedImageProtocol,(void **)&loaded);
    if(EFI_ERROR(status))return status;
    EFI_FILE_IO_INTERFACE *fs=NULL;
    status=uefi_call_wrapper(BS->HandleProtocol,3,loaded->DeviceHandle,&FileSystemProtocol,(void **)&fs);
    if(EFI_ERROR(status))return status;
    status=uefi_call_wrapper(fs->OpenVolume,2,fs,&root);if(EFI_ERROR(status))return status;
    UINTN count=load_entries(),selected=0,seconds=5;
    if(!count){Print(L"LILLYBOOT: no valid lilly-*.conf boot entries\n");return EFI_NOT_FOUND;}
    char *config=read_file(L"\\loader\\lilly.conf");
    if(config) {
        char *cursor=config,*value,*key;
        while((key=next_line(&cursor,&value))) {
            if(equal(key,"default")) {
                CHAR16 name[201];if(copy_ascii(name,201,value,FALSE))for(UINTN i=0;i<count;i++)if(!StrCmp(name,entries[i].name))selected=i;
            } else if(equal(key,"timeout")) {
                UINTN n=0;BOOLEAN valid=*value!=0;
                for(char *p=value;*p;p++){if(*p<'0'||*p>'9'||n>60){valid=FALSE;break;}n=n*10+(UINTN)(*p-'0');}
                if(valid&&n<=60)seconds=n;
            }
        }
        FreePool(config);
    }
    Print(L"LILLYBOOT 0.3 - Linux EFI loader\n");
    for(UINTN i=0;i<count;i++)Print(L" %d: %s%s\n",i+1,entries[i].title,i==selected?L" (default)":L"");
    Print(L"Use arrows or digits, Enter to boot, Escape to return to firmware.\n");
    EFI_EVENT timer;
    status=uefi_call_wrapper(BS->CreateEvent,5,EVT_TIMER,0,NULL,NULL,&timer);
    if(!EFI_ERROR(status)&&seconds) {
        uefi_call_wrapper(BS->SetTimer,3,timer,TimerPeriodic,(UINT64)10000000);
        EFI_EVENT events[2]={timer,ST->ConIn->WaitForKey};
        while(seconds) {
            UINTN which;status=uefi_call_wrapper(BS->WaitForEvent,3,2,events,&which);if(EFI_ERROR(status))break;
            if(!which){seconds--;continue;}
            EFI_INPUT_KEY key;
            if(EFI_ERROR(uefi_call_wrapper(ST->ConIn->ReadKeyStroke,2,ST->ConIn,&key)))continue;
            if(key.ScanCode==SCAN_ESC){uefi_call_wrapper(BS->CloseEvent,1,timer);return EFI_ABORTED;}
            if(key.UnicodeChar=='\r')break;
            if(key.ScanCode==SCAN_UP)selected=selected?selected-1:count-1;
            else if(key.ScanCode==SCAN_DOWN)selected=(selected+1)%count;
            else if(key.UnicodeChar>='1'&&key.UnicodeChar<='9'&&(UINTN)(key.UnicodeChar-'1')<count)selected=(UINTN)(key.UnicodeChar-'1');
            Print(L"Selected: %s\n",entries[selected].title);seconds=60;
        }
    }
    if(!EFI_ERROR(status))uefi_call_wrapper(BS->CloseEvent,1,timer);
    struct entry *e=&entries[selected];
    EFI_DEVICE_PATH *device=FileDevicePath(loaded->DeviceHandle,e->kernel);if(!device)return EFI_OUT_OF_RESOURCES;
    EFI_HANDLE kernel=NULL;
    status=uefi_call_wrapper(BS->LoadImage,6,FALSE,image,device,NULL,0,&kernel);FreePool(device);
    if(EFI_ERROR(status)){Print(L"LILLYBOOT: firmware rejected %s: %r\n",e->kernel,status);return status;}
    EFI_LOADED_IMAGE *child=NULL;
    status=uefi_call_wrapper(BS->HandleProtocol,3,kernel,&LoadedImageProtocol,(void **)&child);
    if(EFI_ERROR(status))return status;
    CHAR16 *options=AllocateZeroPool(8192);if(!options)return EFI_OUT_OF_RESOURCES;
    if(e->initrd[0])SPrint(options,8192,L"%s initrd=%s",e->options,e->initrd);
    else SPrint(options,8192,L"%s",e->options);
    child->LoadOptions=options;child->LoadOptionsSize=(UINT32)((StrLen(options)+1)*sizeof(CHAR16));
    Print(L"LILLYBOOT: booting %s\n",e->title);
    status=uefi_call_wrapper(BS->StartImage,3,kernel,NULL,NULL);
    Print(L"LILLYBOOT: kernel returned: %r\n",status);
    FreePool(options);uefi_call_wrapper(BS->UnloadImage,1,kernel);
    return status;
}
