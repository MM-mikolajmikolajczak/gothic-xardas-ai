#ifdef NDEBUG
#undef NDEBUG
#endif
#include "wave_queue.hpp"
#include <cassert>
static std::vector<WAVEHDR*> written;static int resets=0,closes=0,unprepared=0;static bool failWrite=false,failUnprepare=false;
static MMRESULT WINAPI fakeOpen(LPHWAVEOUT out,UINT,LPCWAVEFORMATEX,DWORD_PTR,DWORD_PTR,DWORD){*out=reinterpret_cast<HWAVEOUT>(1);return MMSYSERR_NOERROR;}
static MMRESULT WINAPI fakePrepare(HWAVEOUT,LPWAVEHDR h,UINT){h->dwFlags|=WHDR_PREPARED;return MMSYSERR_NOERROR;}
static MMRESULT WINAPI fakeWrite(HWAVEOUT,LPWAVEHDR h,UINT){if(failWrite)return MMSYSERR_ERROR;written.push_back(h);return MMSYSERR_NOERROR;}
static MMRESULT WINAPI fakeUnprepare(HWAVEOUT,LPWAVEHDR,UINT){if(failUnprepare)return WAVERR_STILLPLAYING;++unprepared;return MMSYSERR_NOERROR;}
static MMRESULT WINAPI fakeReset(HWAVEOUT){++resets;for(auto* h:written)h->dwFlags|=WHDR_DONE;return MMSYSERR_NOERROR;}
static MMRESULT WINAPI fakeClose(HWAVEOUT){++closes;return MMSYSERR_NOERROR;}
int main(){
 assert(!nbx::streamActionsReady(false,false,false));assert(!nbx::streamActionsReady(true,true,false));assert(!nbx::streamActionsReady(true,false,true));assert(nbx::streamActionsReady(true,false,false));
 std::vector<unsigned char> w={'R','I','F','F',38,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,0x22,0x56,0,0,0x44,0xac,0,0,2,0,16,0,'d','a','t','a',2,0,0,0,0,0};
 std::vector<char> pcm;assert(nbx::decodePcm(w,pcm)&&pcm.size()==2);auto bad=w;bad[22]=2;assert(!nbx::decodePcm(bad,pcm));bad=w;bad.pop_back();assert(!nbx::decodePcm(bad,pcm));bad=w;bad[40]=1;assert(!nbx::decodePcm(bad,pcm));

 const auto path=std::filesystem::temp_directory_path()/L"nbx_wave_queue_test.wav";
 {std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(w.data()),w.size());}
 nbx::WaveApi api{fakeOpen,fakePrepare,fakeWrite,fakeUnprepare,fakeReset,fakeClose};
 {
  nbx::WaveQueue q(api);assert(q.open());assert(q.append(path));assert(q.append(path));assert(written.size()==2&&written[0]!=written[1]);
  assert(written[0]->dwBufferLength==2&&written[0]->lpData[0]==0);q.poll();assert(q.active()&&unprepared==0);
  written[0]->dwFlags|=WHDR_DONE;q.poll();assert(q.active()&&unprepared==1);written.erase(written.begin());
  q.reset();assert(!q.active()&&resets==1&&closes==1&&unprepared==2);written.clear();
 }
 {
  nbx::WaveQueue q(api);assert(q.open());failWrite=true;assert(!q.append(path));failWrite=false;assert(!q.active());q.reset();assert(resets==2&&closes==2);
 }
 {
  nbx::WaveQueue q(api);assert(q.open());failWrite=true;failUnprepare=true;assert(!q.append(path));failWrite=false;failUnprepare=false;assert(!q.active());q.reset();assert(resets==2&&closes==2);
 }
 {
  nbx::WaveQueue q(api);assert(q.open());assert(q.append(path));
 }
 assert(resets==2&&closes==2);written.clear();std::filesystem::remove(path);
}
