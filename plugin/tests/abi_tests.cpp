#ifdef NDEBUG
#undef NDEBUG
#endif
#include "abi_compat.hpp"
#include <cassert>
int main(){
 wchar_t good[]=L"union_abi.dll", bad[]=L"UNION_ABX.DLL", extra[]=L"UNION_ABI.DLLx";
 nbxabi::UnicodeString n{static_cast<USHORT>((sizeof good)-2),sizeof good,good};
 assert(nbxabi::targetName(&n));n.Buffer=bad;assert(!nbxabi::targetName(&n));
 n.Buffer=extra;n.Length=sizeof extra-2;assert(!nbxabi::targetName(&n));
 assert(!nbxabi::targetName(nullptr));assert(nbxabi::status()==0);
 nbxabi::NotificationData event{};event.Loaded.BaseDllName=&n;
 nbxabi::onDll(1,&event,nullptr);assert(nbxabi::status()==0);
 nbxabi::onDll(2,&event,nullptr);assert(nbxabi::status()==0);
}
