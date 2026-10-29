#pragma once
#include <Windows.h>
#include <mmsystem.h>
#undef min
#undef max
#include <filesystem>
#include <fstream>
#include <list>
#include <memory>
#include <vector>
#include <cstring>
#include <algorithm>
#include <string_view>
namespace nbx {
// Plan the initial lead from measured production time, not just chunk count.
// This is an estimate; the device queue is monitored separately for starvation.
class StreamLead {
 unsigned count=0;ULONGLONG firstAt=0,lastAt=0,firstDuration=0,total=0,estimate=0;
public:
 void reset(){*this=StreamLead{};}
 void observe(ULONGLONG at,ULONGLONG duration,std::string_view text){
  // About 80-100 ms per character in the installed Polish voice; use the
  // upper end as a planning estimate, then use observed production speed.
  if(!count){firstAt=lastAt=at;firstDuration=duration;size_t chars=0;for(unsigned char c:text)if((c&0xc0)!=0x80)++chars;estimate=std::min<ULONGLONG>(120000,chars*100ULL);}
  lastAt=std::max(lastAt,at);total+=duration;++count;
 }
 ULONGLONG required()const{
  if(count<3||total<=firstDuration)return 1800;
  const double r=double(lastAt-firstAt)/double(total-firstDuration);
  // Allow for frame/IO jitter and slightly slower subsequent chunks.
  const double conservative=std::max(1.0,r*1.08);
  const double remainingLead=double(std::max(total,estimate))*(1.0-1.0/conservative)+500.0;
  return std::min<ULONGLONG>(120000,std::max<ULONGLONG>(1800,static_cast<ULONGLONG>(remainingLead)));
 }
 bool ready(size_t queued,bool finalAccepted)const{return queued>0&&(finalAccepted||(count>=3&&total>=required()));}
 ULONGLONG buffered()const{return total;}
};
inline bool streamActionsReady(bool finalAccepted,bool pendingFiles,bool deviceBuffers){return finalAccepted&&!pendingFiles&&!deviceBuffers;}
inline unsigned le32(const unsigned char* p){return unsigned(p[0])|(unsigned(p[1])<<8)|(unsigned(p[2])<<16)|(unsigned(p[3])<<24);}
inline unsigned le16(const unsigned char* p){return unsigned(p[0])|(unsigned(p[1])<<8);}
inline bool decodePcm(const std::vector<unsigned char>& wav,std::vector<char>& pcm){
 if(wav.size()<12||memcmp(wav.data(),"RIFF",4)||memcmp(wav.data()+8,"WAVE",4)||le32(wav.data()+4)!=wav.size()-8)return false;
 bool format=false,data=false;size_t offset=12;
 while(offset+8<=wav.size()){
  const auto* h=wav.data()+offset;const auto size=le32(h+4);offset+=8;
  if(size>wav.size()-offset)return false;
  if(!memcmp(h,"fmt ",4)){
   if(format||size<16||le16(wav.data()+offset)!=1||le16(wav.data()+offset+2)!=1||le32(wav.data()+offset+4)!=22050||le32(wav.data()+offset+8)!=44100||le16(wav.data()+offset+12)!=2||le16(wav.data()+offset+14)!=16)return false;
   format=true;
  }else if(!memcmp(h,"data",4)){
   if(data||!size||(size&1))return false;
   pcm.assign(wav.begin()+offset,wav.begin()+offset+size);data=true;
  }
  const size_t padded=size+(size&1u);if(padded>wav.size()-offset)return false;offset+=padded;
 }
 return format&&data&&offset==wav.size();
}
struct WaveApi {
 decltype(&waveOutOpen) open=&waveOutOpen;
 decltype(&waveOutPrepareHeader) prepare=&waveOutPrepareHeader;
 decltype(&waveOutWrite) write=&waveOutWrite;
 decltype(&waveOutUnprepareHeader) unprepare=&waveOutUnprepareHeader;
 decltype(&waveOutReset) reset=&waveOutReset;
 decltype(&waveOutClose) close=&waveOutClose;
};
class WaveQueue {
 struct Buffer {std::vector<char> data;WAVEHDR header{};};
 WaveApi api;HWAVEOUT device=nullptr;std::list<std::unique_ptr<Buffer>> buffers;
 // If a driver refuses reset/unprepare, retain its headers/data rather than
 // free memory that the driver may still reference. Device is intentionally leaked.
 void abandon() noexcept {for(auto& b:buffers)b.release();buffers.clear();device=nullptr;}
public:
 explicit WaveQueue(WaveApi ops={}):api(ops){} WaveQueue(const WaveQueue&)=delete;WaveQueue& operator=(const WaveQueue&)=delete;
 // Process-pinned module: explicit cancellation/completion resets during normal
 // execution; at CRT detach never call audio drivers under the loader lock.
 ~WaveQueue(){abandon();}
 bool open(){if(device)return true;WAVEFORMATEX f{};f.wFormatTag=WAVE_FORMAT_PCM;f.nChannels=1;f.nSamplesPerSec=22050;f.wBitsPerSample=16;f.nBlockAlign=2;f.nAvgBytesPerSec=44100;return api.open(&device,WAVE_MAPPER,&f,0,0,CALLBACK_NULL)==MMSYSERR_NOERROR;}
 bool append(const std::filesystem::path& path){
  std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)return false;auto end=file.tellg();if(end<12||end>64*1024*1024)return false;
  std::vector<unsigned char> bytes(static_cast<size_t>(end));file.seekg(0);if(!file.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))return false;
  auto b=std::make_unique<Buffer>();if(!decodePcm(bytes,b->data))return false;
  buffers.push_back(std::move(b));auto& item=*buffers.back();item.header.lpData=item.data.data();item.header.dwBufferLength=static_cast<DWORD>(item.data.size());
  if(api.prepare(device,&item.header,sizeof(WAVEHDR))!=MMSYSERR_NOERROR){buffers.pop_back();return false;}
  if(api.write(device,&item.header,sizeof(WAVEHDR))!=MMSYSERR_NOERROR){if(api.unprepare(device,&item.header,sizeof(WAVEHDR))!=MMSYSERR_NOERROR){abandon();return false;}buffers.pop_back();return false;}
  return true;
 }
 void poll(){while(!buffers.empty()&&(buffers.front()->header.dwFlags&WHDR_DONE)){if(api.unprepare(device,&buffers.front()->header,sizeof(WAVEHDR))!=MMSYSERR_NOERROR)break;buffers.pop_front();}}
 bool active()const{return !buffers.empty();}
 void reset(){
  if(!device)return;
  if(api.reset(device)!=MMSYSERR_NOERROR){abandon();return;}
  for(auto& b:buffers)if(api.unprepare(device,&b->header,sizeof(WAVEHDR))!=MMSYSERR_NOERROR){abandon();return;}
  buffers.clear();api.close(device);device=nullptr;
 }
};
}
