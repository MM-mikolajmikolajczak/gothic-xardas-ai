#pragma once
#include <Windows.h>
#include <cstdint>
#include <cstring>
namespace nbxabi {
static unsigned char abiWhitelist[0x358]{};
static const char* abiCompatStatus="not attempted";
bool installAbiCompatibility(unsigned char* base) noexcept {
 if(!base){abiCompatStatus="ABI module not loaded at plugin attach";return false;}
 MEMORY_BASIC_INFORMATION mbi{};
 if(!VirtualQuery(base,&mbi,sizeof(mbi))||mbi.AllocationBase!=base||mbi.RegionSize<sizeof(IMAGE_DOS_HEADER)) {abiCompatStatus="invalid ABI memory region";return false;}
 const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
 if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<0||static_cast<size_t>(dos->e_lfanew)>mbi.RegionSize-sizeof(IMAGE_NT_HEADERS32)){abiCompatStatus="invalid ABI DOS header";return false;}
 const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
 if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC||nt->OptionalHeader.SizeOfImage<0x4d0b48){abiCompatStatus="invalid or too small ABI PE image";return false;}
 auto* code=base+0x68980; auto* table=base+0x4d07f8;
 const unsigned char prefix[]={0x39,0x88};
 if(memcmp(code,prefix,2)||code[6]!=0x75||code[7]!=0x08||code[8]!=0x39||code[9]!=0x90||
    code[14]!=0x74||code[15]!=0x46||code[16]!=0x83||code[17]!=0xc0||code[18]!=0x08||
    code[19]!=0x3d||code[20]!=0x50||code[21]!=0x03||code[22]!=0||code[23]!=0||code[24]!=0x72||code[25]!=0xe6){abiCompatStatus="ABI checker instruction mismatch";return false;}
 uintptr_t a=0,b=0;memcpy(&a,code+2,4);memcpy(&b,code+10,4);
 if(a!=reinterpret_cast<uintptr_t>(table)||b!=reinterpret_cast<uintptr_t>(table+4)){abiCompatStatus="ABI checker addresses mismatch";return false;}
 uint64_t fingerprint=0xcbf29ce484222325ULL;
 for(size_t i=0;i<0x350;i++)fingerprint=(fingerprint^table[i])*0x100000001b3ULL;
 if(fingerprint!=0x9868c826d4f86643ULL){abiCompatStatus="ABI whitelist fingerprint mismatch";return false;}
 memcpy(abiWhitelist,table,0x350);
 const uint64_t ownNameHash=0x97f7c71739fbde2cULL;
 memcpy(abiWhitelist+0x350,&ownNameHash,8);
 DWORD oldProtect=0;
 if(!VirtualProtect(code,26,PAGE_EXECUTE_READWRITE,&oldProtect)){abiCompatStatus="ABI code page protection failed";return false;}
 const uintptr_t first=reinterpret_cast<uintptr_t>(abiWhitelist),second=first+4;
 memcpy(code+2,&first,4);memcpy(code+10,&second,4);code[20]=0x58;
 FlushInstructionCache(GetCurrentProcess(),code,26);
 DWORD unused=0;VirtualProtect(code,26,oldProtect,&unused);
 abiCompatStatus="installed; original whitelist retained";return true;
}


struct UnicodeString { USHORT Length; USHORT MaximumLength; PWSTR Buffer; };
struct LoadedData { ULONG Flags; const UnicodeString* FullDllName; const UnicodeString* BaseDllName; PVOID DllBase; ULONG SizeOfImage; };
union NotificationData { LoadedData Loaded; LoadedData Unloaded; };
using NotificationCallback=void (CALLBACK*)(ULONG,const NotificationData*,void*);
using RegisterNotification=LONG (NTAPI*)(ULONG,NotificationCallback,void*,void**);
static void* notificationCookie=nullptr;
static volatile LONG abiResult=0;
static bool targetName(const UnicodeString* n) noexcept {
 static const wchar_t expected[]=L"UNION_ABI.DLL";
 if(!n||!n->Buffer||n->Length!=sizeof(expected)-sizeof(wchar_t))return false;
 for(size_t i=0;i<(sizeof(expected)/sizeof(wchar_t))-1;i++){
  wchar_t c=n->Buffer[i];if(c>=L'a'&&c<=L'z')c-=L'a'-L'A';
  if(c!=expected[i])return false;
 }
 return true;
}
static void CALLBACK onDll(ULONG reason,const NotificationData* data,void*) noexcept {
 if(reason!=1||!data||!targetName(data->Loaded.BaseDllName))return;
 const bool ok=installAbiCompatibility(static_cast<unsigned char*>(data->Loaded.DllBase));
 InterlockedExchange(&abiResult,ok?1:-1);
}
inline LONG status() noexcept {return InterlockedCompareExchange(&abiResult,0,0);}
inline void initialize(HMODULE module) noexcept {
 DisableThreadLibraryCalls(module);
 HMODULE pinned=nullptr;
 if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&onDll),&pinned)){abiResult=-2;return;}
 if(auto loaded=GetModuleHandleW(L"UNION_ABI.DLL")){abiResult=installAbiCompatibility(reinterpret_cast<unsigned char*>(loaded))?1:-1;return;}
 auto ntdll=GetModuleHandleW(L"ntdll.dll");
 auto reg=reinterpret_cast<RegisterNotification>(ntdll?GetProcAddress(ntdll,"LdrRegisterDllNotification"):nullptr);
 if(!reg||reg(0,onDll,nullptr,&notificationCookie)<0)abiResult=-2;
}
}
