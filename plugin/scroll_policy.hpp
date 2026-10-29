#pragma once
#include "tower_policy.hpp"
#include <bcrypt.h>
#include <array>
#include <string_view>
#include <string>
namespace nbx {
inline constexpr const char* scrollMode="directed_scroll_injection";
inline constexpr const char* scrollLaboratoryMode="lab_scroll_injection";
inline constexpr const char* scrollDigest="0acd9366c9e6d805bfda47b44e720d63cd1d10e7afa2cfb2b0e50155cdd1c633";
inline bool isScrollMode(const std::string& mode){return mode==scrollMode||mode==scrollLaboratoryMode;}
inline std::string sha256Bytes(std::string_view bytes){
 if(bytes.size()>2048)return {};
 BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
 if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0))return {};
 if(BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)){BCryptCloseAlgorithmProvider(alg,0);return {};}
 std::array<unsigned char,32> digest{};
 const auto update=BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),static_cast<ULONG>(bytes.size()),0);
 const auto finish=update?update:BCryptFinishHash(hash,digest.data(),32,0);
 BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);
 if(finish)return {};
 static const char hex[]="0123456789abcdef";std::string result;result.reserve(64);
 for(auto value:digest){result+=hex[value>>4];result+=hex[value&15];}return result;
}
inline bool scrollPrompt(const std::string& text){
 if(text.find_first_of("\r\n\"")!=std::string::npos)return false;
 for(const auto* quote:{"\xc2\xab","\xc2\xbb","\xe2\x80\x9e","\xe2\x80\x9d"})if(text.find(quote)!=std::string::npos)return false;
 const auto words=normalizedWords(text);
 return words=="mistrzu nie umiem rozczytac tego starego zwoju pomozesz"||words=="xardas przeczytaj ten zwoj ktory znalazlem";
}
inline bool scrollGrant(const std::string& scene,const std::string& mode,const std::string& text,const std::string& digest){
 return scene=="chat"&&isScrollMode(mode)&&scrollPrompt(text)&&(digest==scrollDigest||digest=="233ee9041933cd49d70b3a0c8722d8d3b153407d0c97b90eb24a145ddc9d7450");
}
}
