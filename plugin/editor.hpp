#pragma once
#include <string>
#include <cwctype>
namespace nbx {
struct Modifiers {
 bool lctrl=false,rctrl=false,lalt=false,ralt=false,lshift=false,rshift=false;
 bool ctrl()const{return lctrl||rctrl;}
 bool shortcut()const{return ctrl()&&!ralt;}
 void reset(){*this={};}
};
enum class EditResult { Continue, Submit, Cancel };
inline void appendText(std::wstring& text,const std::wstring& chars){
 for(wchar_t c:chars){
  if(text.size()>=240)break;
  if(c==L'\r'||c==L'\n'||c==L'\t'){if(text.empty()||text.back()!=L' ')text.push_back(L' ');continue;}
  if(c>=0x20&&!(c>=0x7f&&c<=0x9f)&&!(c>=0xd800&&c<=0xdfff)&&!std::iswcntrl(c))text.push_back(c);
 }
}
inline EditResult editKey(std::wstring& text, unsigned key, const std::wstring& chars=L"") {
 if(key==1){text.clear();return EditResult::Cancel;}
 if(key==28){for(wchar_t c:text)if(!std::iswspace(c))return EditResult::Submit;return EditResult::Continue;}
 if(key==14){if(!text.empty())text.pop_back();return EditResult::Continue;}
 appendText(text,chars);
 return EditResult::Continue;
}
inline std::wstring editLine(const std::wstring& text,size_t offset,size_t width){return text.substr(offset,width);}
}
