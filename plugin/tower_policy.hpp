#pragma once
#include <Windows.h>
#include <string>
#include <vector>
namespace nbx {
// Mirror backend/tower_policy.py. Both are exercised against the same UTF-8 cases.
inline std::string normalizedWords(const std::string& text){
 if(text.empty()||text.size()>1024)return {};
 int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
 if(n<=0)return {};
 std::wstring wide(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),wide.data(),n);
 const std::wstring polish=L"\u0105\u0107\u0119\u0142\u0144\u00f3\u015b\u017a\u017c\u0104\u0106\u0118\u0141\u0143\u00d3\u015a\u0179\u017b";
 const std::string plain="acelnoszzacelnoszz";
 std::string words=" ";
 for(wchar_t c:wide){
  auto pos=polish.find(c);if(pos!=std::wstring::npos)c=plain[pos];
  if(c>=L'A'&&c<=L'Z')c+=L'a'-L'A';
  if((c>=L'a'&&c<=L'z')||(c>=L'0'&&c<=L'9'))words+=static_cast<char>(c);
  else if(words.back()!=' ')words+=' ';
 }
 if(words.back()!=' ')words+=' ';
 return words.size()>1?words.substr(1,words.size()-2):std::string{};
}
inline bool towerEmergency(const std::string& text){
 if(text.find('?')!=std::string::npos)return false;
 std::string body=normalizedWords(text);
 for(const std::string prefix:{"xardasie ","mistrzu "})if(body.starts_with(prefix)){body.erase(0,prefix.size());break;}
 for(const std::string suffix:{" daj mi uriziela"," daj mi miecz"," potrzebuje miecza"," nie ma czasu"," szybko"})if(body.ends_with(suffix)){body.erase(body.size()-suffix.size());break;}
 std::vector<std::string> threats,places;
 for(const auto* verb:{"chce","probuje"})for(const auto* action:{"dorwac","dopasc","zabic"})threats.push_back(std::string(verb)+" mnie "+action);
 for(const auto* verb:{"stoi","jest","czeka","znajduje sie"})for(const auto* whose:{"","twoja ","ta "})places.push_back(std::string(verb)+" tuz przed "+whose+"wieza");
 for(const auto& tail:threats){
  for(const auto& place:places)
   if(body=="sniacy "+tail+" i "+place||body=="sniacy "+tail+" "+place||body=="sniacy "+place+" i "+tail)return true;
 }
 return false;
}
}
