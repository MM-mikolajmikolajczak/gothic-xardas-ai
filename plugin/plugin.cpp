#include <Windows.h>
#include <TlHelp32.h>
#include <bcrypt.h>
#include <mmsystem.h>
#include <ZenGin/zGothicAPI.h>
#undef min
#undef max
#include <detours.h>
#include "json.hpp"
#include "protocol.hpp"
#include "editor.hpp"
#include "abi_compat.hpp"
#include "wave_queue.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs=std::filesystem;
namespace g=Gothic_II_Addon;
using json=nlohmann::json;
namespace {
constexpr const char* kExeHash="0655b4811e008a353ce372fde463ef00b6101f3e89be850e2f14980140447c7f";
constexpr const char* kSword="ITMW_2H_URIZEL";
using CollectFn=void(__fastcall*)(g::oCInformationManager*);
using InputFn=int(__thiscall*)(void*,int);
using SelectFn=void(__fastcall*)(g::oCInformationManager*,int);
using UpdateFn=void(__fastcall*)(g::oCInformationManager*);
using ExitFn=void(__fastcall*)(g::oCInformationManager*);
using RenderFn=void(__thiscall*)(g::oCGame*);
using LoadGameFn=void(__thiscall*)(g::oCGame*,int,const g::zSTRING&);
using LoadSaveFn=void(__thiscall*)(g::oCGame*,int,int);
using ChangeLevelFn=void(__thiscall*)(g::oCGame*,const g::zSTRING&,const g::zSTRING&);
CollectFn realCollect=reinterpret_cast<CollectFn>(0x00661AA0);
InputFn realInput=reinterpret_cast<InputFn>(0x0068EBA0);
SelectFn realSelect=reinterpret_cast<SelectFn>(0x006620B0);
UpdateFn realUpdate=reinterpret_cast<UpdateFn>(0x00660BB0);
ExitFn realExit=reinterpret_cast<ExitFn>(0x006630D0);
RenderFn realRender=reinterpret_cast<RenderFn>(0x006C86A0);
LoadGameFn realLoadGame=reinterpret_cast<LoadGameFn>(0x006C65A0);
LoadSaveFn realLoadSave=reinterpret_cast<LoadSaveFn>(0x006C67D0);
ChangeLevelFn realChangeLevel=reinterpret_cast<ChangeLevelFn>(0x006C7290);
fs::path folder,root;
std::atomic<bool> fault{false},shutdown{false};
std::atomic<DWORD> gameThread{0};
std::atomic<unsigned> loadingDepth{0};
std::mutex ioMutex,logMutex;
std::deque<json> outbox,inbox,events,directorInbox;
std::optional<json> pending,playerReady;
struct VoiceChunk {fs::path path;ULONGLONG duration;};
std::deque<VoiceChunk> npcChunks;
unsigned npcChunksReceived=0,npcChunksPlayed=0;ULONGLONG npcChunksDuration=0,npcChunksBytes=0;
bool streamingNpc=false,finalAccepted=false,chunkFailed=false;
nbx::StreamLead streamLead;ULONGLONG starvedAt=0;
g::oCInformationManager* menu=nullptr;
g::oCNpc* currentHero=nullptr;
g::oCNpc* currentNpc=nullptr;
g::zCWorld* currentWorld=nullptr;
unsigned long long generation=0,serial=0;
std::string session;
bool submenu=false,busy=false,speaking=false;
bool editing=false,editorArmed=false;
std::wstring typedText,submittedText;
HWND editorWindow=nullptr;WNDPROC previousWindowProc=nullptr;
nbx::Modifiers modifiers;bool messageInput=false;unsigned restoredInputTrace=0;
unsigned enterTrace=0;
int injectedIndex=-1;
ULONGLONG timeout=0,voiceEnd=0,npcDuration=0;
nbx::WaveQueue npcWave;
nbx::WaitFrames waitFrames;std::string waitRequestId;
nbx::VoiceStep voicePhase=nbx::VoiceStep::Idle;
fs::path npcAudio;
std::string npcText;
json delayedActions;
std::vector<std::string> captions;
std::string actionMode,actionScene,actionPlayerText,actionScrollDigest;
void log(const std::string& text) noexcept {
 try {std::lock_guard lock(logMutex);std::ofstream f(root/L"native.log",std::ios::app);f<<GetTickCount64()<<" "<<text<<'\n';}catch(...){}
}
void event(json data){std::lock_guard lock(ioMutex);data["time_ms"]=GetTickCount64();events.push_back(std::move(data));}
std::string hashFile(const fs::path& path){
 std::ifstream f(path,std::ios::binary);if(!f)return {};
 BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE h=nullptr;
 if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0))return {};
 if(BCryptCreateHash(alg,&h,nullptr,0,nullptr,0,0)){BCryptCloseAlgorithmProvider(alg,0);return {};}
 std::array<unsigned char,65536> b{};while(f){f.read(reinterpret_cast<char*>(b.data()),b.size());if(f.gcount())BCryptHashData(h,b.data(),static_cast<ULONG>(f.gcount()),0);}
 std::array<unsigned char,32> digest{};const auto result=BCryptFinishHash(h,digest.data(),32,0);BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(alg,0);
 if(result)return {};std::ostringstream s;s<<std::hex<<std::setfill('0');for(auto v:digest)s<<std::setw(2)<<static_cast<int>(v);return s.str();
}
std::wstring wide(const std::string& s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);if(!n)return {};std::wstring w(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),w.data(),n);return w;}
std::string gameText(const std::string& utf8){const auto w=wide(utf8);if(w.empty())return {};int n=WideCharToMultiByte(1250,0,w.data(),static_cast<int>(w.size()),nullptr,0,"?",nullptr);std::string s(n,0);WideCharToMultiByte(1250,0,w.data(),static_cast<int>(w.size()),s.data(),n,"?",nullptr);return s;}
bool sameThread(){return gameThread.load()==GetCurrentThreadId();}
g::oCNpc* hero(){return *reinterpret_cast<g::oCNpc**>(0x00AB2684);}
bool xardas(g::oCInformationManager* m){
 if(!m||!m->Npc||!m->Player||m->Player!=hero()||m->IsDone)return false;
 auto* p=reinterpret_cast<g::zCParser*>(0x00AB40C0);
 const int i=p->GetIndex(g::zSTRING("NONE_100_XARDAS"));return i>=0&&m->Npc->GetInstance()==i;
}
void add(g::oCInformationManager* m,const std::string& text){auto converted=gameText(text);g::zSTRING s(converted.c_str());m->DlgChoice->AddChoice(s,0);}
void pasteClipboard(){
 if(!OpenClipboard(nullptr))return;
 struct ClipboardScope{~ClipboardScope(){CloseClipboard();}} clipboardScope;
 HANDLE handle=GetClipboardData(CF_UNICODETEXT);if(!handle)return;
 const SIZE_T bytes=GlobalSize(handle);const auto* ptr=static_cast<const wchar_t*>(GlobalLock(handle));if(!ptr)return;
 struct UnlockScope{HANDLE h;~UnlockScope(){GlobalUnlock(h);}} unlockScope{handle};
 size_t max=bytes/sizeof(wchar_t),len=0;while(len<max&&len<4096&&ptr[len])++len;
 nbx::appendText(typedText,std::wstring(ptr,len));
}
LRESULT CALLBACK editorWindowProc(HWND hwnd,UINT msg,WPARAM vk,LPARAM info){
 if(msg==WM_NCDESTROY){auto old=previousWindowProc;editorWindow=nullptr;previousWindowProc=nullptr;messageInput=false;return old?CallWindowProcA(old,hwnd,msg,vk,info):DefWindowProcA(hwnd,msg,vk,info);}
 if(msg==WM_KILLFOCUS)modifiers.reset();
 if(editing&&(msg==WM_KEYDOWN||msg==WM_SYSKEYDOWN||msg==WM_KEYUP||msg==WM_SYSKEYUP)){
  const bool down=msg==WM_KEYDOWN||msg==WM_SYSKEYDOWN;
  const bool extended=(static_cast<ULONG_PTR>(info)&(1u<<24))!=0;
  const UINT scan=(static_cast<ULONG_PTR>(info)>>16)&0xff;
  if(vk==VK_CONTROL){(extended?modifiers.rctrl:modifiers.lctrl)=down;return 0;}
  if(vk==VK_MENU){(extended?modifiers.ralt:modifiers.lalt)=down;return 0;}
  if(vk==VK_SHIFT){(scan==0x36?modifiers.rshift:modifiers.lshift)=down;return 0;}
  if(!down)return 0;
  if(!editorArmed)return 0;
  if(modifiers.shortcut()){
   if(vk=='V')try{pasteClipboard();}catch(...){log("Clipboard read failed");}
   return 0;
  }
  if(vk==VK_RETURN||vk==VK_ESCAPE||vk==VK_BACK)return 0; // Gothic's input callback handles these.
  BYTE keys[256]{};if(!GetKeyboardState(keys))return 0;
  keys[VK_SHIFT]=(modifiers.lshift||modifiers.rshift)?0x80:0;
  keys[VK_CONTROL]=modifiers.ctrl()?0x80:0;
  keys[VK_MENU]=(modifiers.lalt||modifiers.ralt)?0x80:0;
  keys[VK_RMENU]=modifiers.ralt?0x80:0;
  const HKL layout=GetKeyboardLayout(0);wchar_t chars[8]{};
  const int count=ToUnicodeEx(static_cast<UINT>(vk),scan,keys,chars,8,0,layout);
  if(count>0)try{nbx::appendText(typedText,std::wstring(chars,chars+count));}catch(...){log("Editor input failed");}
  return 0;
 }
 if(editing&&(msg==WM_CHAR||msg==WM_SYSCHAR||msg==WM_DEADCHAR||msg==WM_SYSDEADCHAR))return 0;
 return previousWindowProc?CallWindowProcA(previousWindowProc,hwnd,msg,vk,info):DefWindowProcA(hwnd,msg,vk,info);
}
bool installEditorWindowProc(){
 auto hwnd=*reinterpret_cast<HWND*>(0x008d422c);
 if(!hwnd||GetWindowThreadProcessId(hwnd,nullptr)!=GetCurrentThreadId())return false;
 if(hwnd==editorWindow&&previousWindowProc)return true;
 if(editorWindow)return false;
 SetLastError(0);auto old=reinterpret_cast<WNDPROC>(SetWindowLongPtrA(hwnd,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&editorWindowProc)));
 if(!old&&GetLastError())return false;
 editorWindow=hwnd;previousWindowProc=old;return old!=nullptr;
}
void restartBridgeChoices(g::oCInformationManager* m,const char* label){
 // Only bridge-owned transitions: finish the old selection before OpenSafe.
 m->DlgChoice->StopSelection();m->DlgChoice->CloseImmediately();
 m->DlgChoice->RemoveAllChoices();add(m,label);
 m->Mode=g::oCInformationManager::INFO_MGR_MODE_INFO;m->MustOpen=1;
 m->IsWaitingForSelection=0;m->IsWaitingForClose=0;m->IsWaitingForOpen=0;
}
void openEditor(g::oCInformationManager* m){
 modifiers.reset();messageInput=installEditorWindowProc();
 editing=true;editorArmed=false;typedText.clear();submittedText.clear();
 if(!messageInput)event({{"type","chat_message_input_unavailable"}});
 restartBridgeChoices(m,"Wpisz wiadomosc. Enter: wyslij, Esc: anuluj.");
 menu=m;submenu=true;busy=false;speaking=false;
 event({{"type","chat_editor_opened"}});
}
void finishWaitFrames(const char* reason){
 if(!waitFrames.active)return;
 const auto now=GetTickCount64();
 event({{"type","request_render_stats"},{"id",waitRequestId},{"reason",reason},{"frames",waitFrames.frames},{"elapsed_ms",waitFrames.elapsed(now)},{"mean_fps",waitFrames.fps(now)},{"max_frame_ms",waitFrames.maxFrameMs},{"wait_cap_fps",30}});
 waitFrames.clear();waitRequestId.clear();
}
void cancel(){
 npcWave.reset();
 streamLead.reset();starvedAt=0;
 actionScrollDigest.clear();
 finishWaitFrames("cancelled");
 pending.reset();playerReady.reset();npcChunks.clear();npcChunksReceived=0;npcChunksPlayed=0;npcChunksDuration=0;npcChunksBytes=0;streamingNpc=false;finalAccepted=false;chunkFailed=false;busy=false;speaking=false;voicePhase=nbx::VoiceStep::Idle;npcAudio.clear();npcText.clear();editing=false;editorArmed=false;typedText.clear();submittedText.clear();delayedActions=json::array();captions.clear();
 PlaySoundW(nullptr,nullptr,0);
}
void invalidateForLoad(){
 cancel();enterTrace=0;currentHero=nullptr;currentWorld=nullptr;currentNpc=nullptr;menu=nullptr;submenu=false;injectedIndex=-1;
 session=std::to_string(GetCurrentProcessId())+"_"+std::to_string(++generation);
 event({{"type","load_invalidate"},{"session_id",session}});
}
struct LoadScope {LoadScope(){++loadingDepth;invalidateForLoad();}~LoadScope(){--loadingDepth;}};
void __fastcall loadGame(g::oCGame* game,void*,int slot,const g::zSTRING& world){LoadScope scope;realLoadGame(game,slot,world);}
void __fastcall loadSave(g::oCGame* game,void*,int slot,int globals){LoadScope scope;realLoadSave(game,slot,globals);}
void __fastcall changeLevel(g::oCGame* game,void*,const g::zSTRING& world,const g::zSTRING& start){LoadScope scope;realChangeLevel(game,world,start);}
std::string utf8(const std::wstring& w){if(w.empty())return {};int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),static_cast<int>(w.size()),nullptr,0,nullptr,nullptr);if(!n)return {};std::string out(n,0);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),static_cast<int>(w.size()),out.data(),n,nullptr,nullptr);return out;}
json gameContext(){
 json ctx={{"player_level",currentHero->level},{"npc_instance","NONE_100_XARDAS"}};
 // Only engine values, bounded and checked; no arbitrary memory or prompt text.
 const auto& levelName=currentWorld->m_strlevelName;
 if(levelName.Length()>0&&levelName.Length()<=96){auto name=nbx::safeWorldName(std::string(levelName.ToChar(),levelName.Length()));if(!name.empty())ctx["world_name"]=name;}
 auto* game=*reinterpret_cast<g::oCGame**>(0x00AB0884);
 if(game&&!game->inLoadSaveGame&&!game->inLevelChange&&game->wldTimer){
  int hour=-1,minute=-1;game->wldTimer->GetTime(hour,minute);int day=game->wldTimer->GetDay();
  if(day>=0&&day<=100000&&hour>=0&&hour<24&&minute>=0&&minute<60)ctx["game_time"]={{"day",day},{"hour",hour},{"minute",minute}};
 }
 auto* parser=reinterpret_cast<g::zCParser*>(0x00AB40C0);auto* chapter=parser->GetSymbol(g::zSTRING("KAPITEL"));
 if(chapter&&chapter->type==2&&chapter->ele==1){int value=-1;chapter->GetValue(value,0);if(value>=0&&value<=20)ctx["chapter"]=value;}
 return ctx;
}
json preparedScroll(){
 // A fixed, explicitly prepared film document. This is not an arbitrary
 // inventory document reader or a model-selected file path.
 std::error_code ec;const auto path=fs::canonical(root/L"scroll-demo.txt",ec);if(ec)return nullptr;
 const auto base=fs::canonical(root,ec);if(ec||path.parent_path()!=base)return nullptr;
 const auto size=fs::file_size(path,ec);if(ec||!size||size>2048)return nullptr;
 std::ifstream f(path,std::ios::binary);std::string text(static_cast<size_t>(size),0);
 if(!f.read(text.data(),text.size())||wide(text).empty())return nullptr;
 const auto digest=nbx::sha256Bytes(text);if(digest.size()!=64)return nullptr;
 return {{"source","prepared_demo_scroll"},{"text",text},{"sha256",digest}};
}
void queueChat(g::oCInformationManager* m,const std::string& text){
 if(text.empty()||!currentWorld||!currentHero||!sameThread()||loadingDepth||m->Player!=currentHero||hero()!=currentHero||currentHero->homeWorld!=currentWorld||!xardas(m))return;
 cancel();menu=m;currentNpc=m->Npc;
 const std::string id=std::to_string(GetCurrentProcessId())+"_"+std::to_string(GetTickCount64())+"_"+std::to_string(++serial);
 json req={{"protocol_version",1},{"id",id},{"session_id",session},{"scene","chat"},{"player_level",currentHero->level},{"npc_instance","NONE_100_XARDAS"},{"player_text",text},{"world_context",gameContext()}};
 if(nbx::scrollPrompt(text)){auto document=preparedScroll();if(!document.is_null())req["scroll_document"]=std::move(document);}
 pending=req;timeout=GetTickCount64()+300000;busy=true;waitRequestId=id;waitFrames.begin(GetTickCount64());
 {std::lock_guard lock(ioMutex);outbox.push_back(req);}
 restartBridgeChoices(m,"Przerwij i wroc do rozmowy.");
 event({{"type","request_queued"},{"id",id},{"scene","chat"},{"player_level",currentHero->level}});
}
void __fastcall collect(g::oCInformationManager* m){
 realCollect(m);if(fault||!sameThread())return;
 try{if(!xardas(m)||!m->DlgChoice||m->IsWaitingForEnd)return;enterTrace=0;menu=m;submenu=false;injectedIndex=m->DlgChoice->Choices;add(m,"Wpisz wiadomosc do Xardasa...");event({{"type","dialogue_option_added"},{"index",injectedIndex}});}catch(...){fault=true;log("Collect hook fault; disabled");}
}
void __fastcall select(g::oCInformationManager* m,int index){
 if(fault||!sameThread()){realSelect(m,index);return;}
 try{
  if(m==menu&&xardas(m)){
   if(busy||speaking){cancel();openEditor(m);return;}
   if(!submenu&&index==injectedIndex){openEditor(m);return;}
  }
 }catch(...){fault=true;log("Selection hook fault; disabled");}
 realSelect(m,index);
}
int __fastcall inputHook(void* input,void*,int key){
 if(!editing&&sameThread()&&menu&&menu->DlgChoice&&(key&0xff)==28&&input==reinterpret_cast<char*>(menu->DlgChoice)+0xec&&enterTrace<3){
  ++enterTrace;auto* v=menu->DlgChoice;
  std::optional<json> trace;
  try{trace=json{{"type","dialogue_enter"},{"opened",v->HasOpened},{"closed",v->HasClosed},{"time_open",v->TimeOpen},{"time_close",v->TimeClose},{"done_before",v->IsDone},{"active_before",v->IsActivated},{"must_open",menu->MustOpen},{"waiting_selection",menu->IsWaitingForSelection},{"selected",v->ChoiceSelected}};}catch(...){log("Enter trace unavailable");}
  const int result=realInput(input,key);
  try{if(trace){(*trace)["handled"]=result;(*trace)["done_after"]=v->IsDone;(*trace)["active_after"]=v->IsActivated;event(std::move(*trace));}}catch(...){log("Enter trace unavailable");}return result;
 }
 if(!editing||!sameThread()||!menu||!menu->DlgChoice||input!=reinterpret_cast<char*>(menu->DlgChoice)+0xec){
  if(!editing&&sameThread()&&menu&&menu->DlgChoice&&restoredInputTrace&&input==reinterpret_cast<char*>(menu->DlgChoice)+0xec){
   auto* view=menu->DlgChoice;const int before=view->IsDone;const int result=realInput(input,key);
   --restoredInputTrace;event({{"type","restored_dialogue_input"},{"key",key},{"handled",result},{"done_before",before},{"done_after",view->IsDone},{"selected",view->ChoiceSelected}});return result;
  }
  return realInput(input,key);
 }
 try{
  if(!editorArmed)return 1;
  const unsigned scan=static_cast<unsigned>(key)&0xff;
  if(scan==1||scan==28||scan==14){
   auto result=nbx::editKey(typedText,scan);
   if(result==nbx::EditResult::Cancel){
    auto* m=menu;editing=false;submenu=false;menu=nullptr;injectedIndex=-1;
    // Complete the active selection and reset the view before the engine starts
    // another selection. Otherwise StartSelection leaves HasClosed set on an
    // already-open view and its native handler refuses Return.
    m->DlgChoice->StopSelection();m->DlgChoice->CloseImmediately();
    m->Mode=g::oCInformationManager::INFO_MGR_MODE_INFO;m->MustOpen=1;
    m->IsWaitingForSelection=0;m->IsWaitingForClose=0;m->IsWaitingForOpen=0;
    event({{"type","chat_editor_cancelled"}});collect(m);restoredInputTrace=16;
   }
   else if(result==nbx::EditResult::Submit){const auto submitted=typedText;const auto message=utf8(submitted);editing=false;queueChat(menu,message);submittedText=submitted;}
   return 1;
  }
  if(messageInput)return 1;
  auto* gameInput=*reinterpret_cast<g::zCInput**>(0x008d1650);
  auto pressed=[&](int code){return gameInput&&gameInput->KeyPressed(code)!=0;};
  const bool ctrl=pressed(0x1d)||pressed(0x9d)||(GetAsyncKeyState(VK_CONTROL)&0x8000);
  const bool rightAlt=pressed(0xb8)||(GetAsyncKeyState(VK_RMENU)&0x8000);
  if(ctrl&&!rightAlt){if(scan==0x2f)pasteClipboard();return 1;}
  if(scan>=0x80||key>0xff)return 1;
  BYTE keys[256]{};if(!GetKeyboardState(keys))return 1;
  if(pressed(0x2a)||pressed(0x36))keys[VK_SHIFT]|=0x80;
  if(rightAlt){keys[VK_CONTROL]|=0x80;keys[VK_MENU]|=0x80;keys[VK_RMENU]|=0x80;}
  const HKL layout=GetKeyboardLayout(0);const UINT vk=MapVirtualKeyExW(scan,MAPVK_VSC_TO_VK_EX,layout);wchar_t chars[8]{};
  const int count=ToUnicodeEx(vk,scan,keys,chars,8,0,layout);
  if(count>0)nbx::editKey(typedText,scan,std::wstring(chars,chars+count));
  return 1;
 }catch(...){fault=true;editing=false;log("Chat editor input fault; disabled");return 1;}
}
void __fastcall update(g::oCInformationManager* m){
 // Update inlines the original OnInfo(int) body in this exact executable. It
 // therefore never enters 0x6620B0 for keyboard selection. Intercept only after
 // the native choice-close transition has completed, before the inline dispatch.
 if(!fault&&sameThread()&&!editing){
  try{
   if(m==menu&&xardas(m)&&m->Mode==g::oCInformationManager::INFO_MGR_MODE_INFO&&
      !m->MustOpen&&!m->IsWaitingForEnd&&!m->IsWaitingForScript&&!m->IsWaitingForOpen&&
      !m->IsWaitingForSelection&&!m->IsWaitingForClose&&m->DlgChoice&&m->DlgChoice->HasClosed){
    const int index=m->DlgChoice->ChoiceSelected;
    if(submenu||busy||speaking||index==injectedIndex){select(m,index);return;}
   }
  }catch(...){fault=true;log("Update dispatch fault; disabled");}
 }
 // Native selection/camera transitions keep working while the worker computes.
 realUpdate(m);
}
void __fastcall exitDialogue(g::oCInformationManager* m){
 if(m==menu){cancel();menu=nullptr;submenu=false;injectedIndex=-1;}
 realExit(m);
}
void captionsFor(const std::string& value){
 captions.clear();std::istringstream words(gameText(value.substr(0,1600)));std::string word,line;
 while(words>>word){if(line.size()+word.size()+1>100){captions.push_back(line);line.clear();}if(!line.empty())line+=' ';line+=word;}
 if(!line.empty())captions.push_back(line);if(captions.size()>5)captions.resize(5);
}
bool validAudio(const fs::path& input){
 std::error_code ec;auto base=fs::canonical(root/L"audio",ec);if(ec)return false;auto p=fs::canonical(input,ec);if(ec)return false;
 auto a=base.native(),b=p.native();std::transform(a.begin(),a.end(),a.begin(),towlower);std::transform(b.begin(),b.end(),b.begin(),towlower);
 if(b.size()<=a.size()||b.compare(0,a.size(),a)!=0||(b[a.size()]!=L'\\'&&b[a.size()]!=L'/'))return false;
 if(p.extension()!=L".wav"&&p.extension()!=L".WAV")return false;
 auto size=fs::file_size(p,ec);if(ec||size<44||size>64*1024*1024)return false;
 std::ifstream f(p,std::ios::binary);char header[12]{};f.read(header,12);return !memcmp(header,"RIFF",4)&&!memcmp(header+8,"WAVE",4);
}
bool applyAction(const json& a,const std::string& mode,const std::string& scene){
 const std::string name=a.value("name","");
 if(name=="give_item"){
  if(a.value("item","")!=kSword){event({{"type","action_rejected"},{"reason","item_not_allowed"}});return true;}
  if(!nbx::canGive(scene,mode,actionPlayerText,actionScrollDigest)){event({{"type","action_rejected"},{"reason","sword_reserved"},{"level",currentHero->level}});return true;}
  if(nbx::isScrollMode(mode)){const auto document=preparedScroll();if(document.is_null()||document.value("sha256","")!=actionScrollDigest){event({{"type","action_rejected"},{"reason","scroll_changed"}});return true;}}
  auto* p=reinterpret_cast<g::zCParser*>(0x00AB40C0);int i=p->GetIndex(g::zSTRING(kSword));
  if(i<0){event({{"type","action_rejected"},{"reason","item_instance_missing"}});return true;}
  currentHero->CreateItems(i,1);event({{"type","item_created"},{"item",kSword},{"instance_index",i},{"actual_level",currentHero->level},{"mode",mode}});
  g::zSTRING msg("Otrzymano przedmiot: Uriziel");auto* scr=*reinterpret_cast<g::zCView**>(0x00AB6468);if(scr)scr->PrintTimedCX(4500,msg,5000,nullptr);
 }else if(name=="cast_death_spell"){
  if(!nbx::canCastDeath(scene,mode,actionPlayerText)){event({{"type","action_rejected"},{"reason","conversation_only"},{"scene",scene}});return true;}
  // Engine damage/death is real. Zero-damage VFX is ornamental and never bypasses life-state bookkeeping.
  auto* visual=*reinterpret_cast<g::zCParser**>(0x008CE6EC);
  if(visual&&visual->GetIndex(g::zSTRING("SPELLFX_DEATHBOLT"))>=0){
   g::oCVisualFX::CreateAndPlay(g::zSTRING("SPELLFX_DEATHBOLT"),static_cast<const g::zCVob*>(currentNpc),static_cast<const g::zCVob*>(currentHero),1,0,0,0);
  }
  if(menu){auto* m=menu;cancel();submenu=false;menu=nullptr;m->Exit();}
  currentHero->SetAttribute(0,0);currentHero->DoDie(currentNpc);
  event({{"type","death_executed"},{"actual_level",currentHero->level},{"mode",mode}});
  return false;
 }else event({{"type","action_rejected"},{"reason","unknown_action"},{"name",name}});
 return true;
}
bool startVoice(const fs::path& path,ULONGLONG duration,const char* speaker){
 if(!validAudio(path)){event({{"type","voice_rejected"},{"speaker",speaker},{"reason","outside_audio_root_or_invalid_wave"}});return false;}
 PlaySoundW(nullptr,nullptr,0);
 if(!PlaySoundW(path.c_str(),nullptr,SND_FILENAME|SND_ASYNC|SND_NODEFAULT)){event({{"type","voice_failed"},{"speaker",speaker},{"reason","PlaySoundW failed"}});return false;}
 voiceEnd=GetTickCount64()+duration;event({{"type","voice_started"},{"speaker",speaker},{"duration_ms",duration}});return true;
}
void receivePlayerReady(const json& response){
 if(!pending||playerReady||!menu||!currentHero||!currentWorld||!xardas(menu))return;
 if(!nbx::validPlayerReady(response,pending->value("id",""),session,pending->value("player_text",""))){event({{"type","player_ready_rejected"},{"reason","protocol_or_identity"}});return;}
 const auto path=fs::path(wide(response["player_wav_path"].get<std::string>()));
 const auto duration=response["player_duration_ms"].get<ULONGLONG>();
 if(!validAudio(path)){event({{"type","player_ready_rejected"},{"reason","audio_invalid"}});return;}
 if(!startVoice(path,duration,"player")){event({{"type","player_ready_rejected"},{"reason","playback_failed"}});return;}
 playerReady=response;speaking=true;voicePhase=nbx::VoiceStep::Player;captions.clear();
 event({{"type","player_ready_accepted"},{"id",pending->value("id","")}});
}
void receiveNpcChunk(const json& response){
 if(!pending||chunkFailed||!menu||!currentHero||!currentWorld||!xardas(menu))return;
 if(!nbx::validNpcChunk(response,pending->value("id",""),session,pending->value("player_text",""),npcChunksReceived)){
  chunkFailed=true;event({{"type","npc_chunk_rejected"},{"reason","protocol_or_sequence"}});return;
 }
 const auto& text=response["text"].get_ref<const std::string&>();
 if(npcChunksReceived&&text!=npcText){chunkFailed=true;event({{"type","npc_chunk_rejected"},{"reason","text_mismatch"}});return;}
 const auto duration=response["duration_ms"].get<ULONGLONG>();
 const auto path=fs::path(wide(response["wav_path"].get<std::string>()));
 std::error_code sizeError;const auto bytes=fs::file_size(path,sizeError);
 if(sizeError||npcChunksBytes+bytes>64ULL*1024*1024||npcChunksDuration+duration>120000||!validAudio(path)){
  chunkFailed=true;event({{"type","npc_chunk_rejected"},{"reason","audio_or_duration"}});return;
 }
 if(!npcChunksReceived)npcText=text;
 npcChunks.push_back({path,duration});++npcChunksReceived;npcChunksDuration+=duration;npcChunksBytes+=bytes;
 streamLead.observe(GetTickCount64(),duration,text);
 event({{"type","voice_chunk_received"},{"sequence",npcChunksReceived-1},{"duration_ms",duration}});
}
void receive(const json& response){
 if(!pending||!menu||!xardas(menu)||!currentHero||!currentWorld)return;
 if(nbx::validError(response,pending->value("id",""),session,pending->value("player_text",""))){
  finishWaitFrames("error");event({{"type","response_failed"},{"reason","generation_failed"}});
  auto* scr=*reinterpret_cast<g::zCView**>(0x00AB6468);if(scr)scr->PrintTimedCX(4000,g::zSTRING("Nie udalo sie przygotowac odpowiedzi. Sprobuj ponownie."),8000,nullptr);
  const auto retryText=wide(pending->value("player_text",""));auto* m=menu;cancel();openEditor(m);typedText=retryText;return;
 }
 if(!nbx::validResponse(response,pending->value("id",""),session)||response["player_text"]!=(*pending)["player_text"]){event({{"type","response_rejected"},{"reason","protocol_or_schema"}});return;}
 if(nbx::isScrollMode(response["mode"].get_ref<const std::string&>())){
  const auto document=pending->value("scroll_document",json::object());
  if(response["scroll_document_sha256"]!=document.value("sha256","")){event({{"type","response_rejected"},{"reason","scroll_snapshot_mismatch"}});auto* m=menu;cancel();openEditor(m);return;}
 }
 unsigned count=0;
 if(response.contains("npc_chunk_count")){
  if(!response["npc_chunk_count"].is_number_integer()||response["npc_chunk_count"]<1||response["npc_chunk_count"]>256){event({{"type","response_rejected"},{"reason","chunk_count_invalid"}});auto* m=menu;cancel();openEditor(m);return;}
  count=response["npc_chunk_count"].get<unsigned>();
 }
 if(chunkFailed||count!=npcChunksReceived||(count&&response["text"]!=npcText)){event({{"type","response_rejected"},{"reason","chunk_count_mismatch"}});auto* m=menu;cancel();openEditor(m);return;}
 if(playerReady&&!nbx::matchingPlayerReady(*playerReady,response)){
  event({{"type","response_rejected"},{"reason","player_audio_mismatch"}});auto* m=menu;cancel();openEditor(m);return;
 }
 finishWaitFrames("response");
 const auto playerAudio=fs::path(wide(response["player_wav_path"].get<std::string>()));
 const auto nextAudio=fs::path(wide(response["wav_path"].get<std::string>()));
 if(!validAudio(playerAudio)||!validAudio(nextAudio)){event({{"type","response_rejected"},{"reason","audio_invalid"}});auto* m=menu;cancel();openEditor(m);return;}
 delayedActions=response["actions"];actionMode=response["mode"].get<std::string>();actionScene="chat";
 actionPlayerText=pending->value("player_text","");
 actionScrollDigest=pending->value("scroll_document",json::object()).value("sha256","");
 npcAudio=nextAudio;npcDuration=response["duration_ms"].get<ULONGLONG>();npcText=response["text"].get<std::string>();
 const auto playerDuration=response["player_duration_ms"].get<ULONGLONG>();
 const bool playerAlreadyStarted=playerReady.has_value();
 pending.reset();playerReady.reset();busy=false;speaking=true;finalAccepted=true;
 if(!playerAlreadyStarted){voicePhase=nbx::VoiceStep::Player;captions.clear();}
 if(!playerAlreadyStarted&&!startVoice(playerAudio,playerDuration,"player")){auto* m=menu;cancel();openEditor(m);return;}
 // queueChat already opened the cancellation choice. Reopening it here can
 // race the native close/open animation while the player selects it.
 event({{"type","response_accepted"},{"mode",actionMode},{"scene","chat"},{"text",npcText}});
}
void tick(g::oCGame* game){
 if(!gameThread)gameThread=GetCurrentThreadId();if(!sameThread()||fault||loadingDepth)return;
 if(editing&&menu&&menu->IsWaitingForSelection){auto* input=*reinterpret_cast<g::zCInput**>(0x008d1650);if(input&&!input->KeyPressed(0x1c))editorArmed=true;}
 auto* h=hero();auto* world=h?h->homeWorld:nullptr;
 if(h!=currentHero||world!=currentWorld||game->inLoadSaveGame){
  cancel();currentHero=h;currentWorld=world;currentNpc=nullptr;menu=nullptr;submenu=false;injectedIndex=-1;
  session=std::to_string(GetCurrentProcessId())+"_"+std::to_string(++generation);
  event({{"type","session_changed"},{"session_id",session}});
 }
 if(!h||!world||game->inLoadSaveGame)return;
 std::deque<json> commands;{std::lock_guard lock(ioMutex);commands.swap(directorInbox);}
 for(const auto& command:commands){
  const int slot=command.value("fixture_slot",-1);
  if(command.value("command","")=="reload_fixture"&&slot>=1&&slot<=20){
   // Only the explicit copied-fixture slot admitted by the IO worker can enter here.
   event({{"type","fixture_reload"},{"slot",slot},{"command_id",command.value("id","")}});
   if(menu)menu->Exit();cancel();menu=nullptr;submenu=false;currentWorld=nullptr;currentHero=nullptr;
   game->LoadSavegame(slot,1);return;
  }
 }
 std::deque<json> responses;{std::lock_guard lock(ioMutex);responses.swap(inbox);}
 for(auto& data:responses){
  if(data.is_object()&&data.contains("phase")&&data["phase"]=="npc_chunk")receiveNpcChunk(data);
  else if(data.is_object()&&data.contains("phase"))receivePlayerReady(data);
  else receive(data);
 }
 const auto now=GetTickCount64();if(pending&&now>timeout){event({{"type","request_timeout"}});finishWaitFrames("timeout");cancel();if(menu&&xardas(menu))openEditor(menu);}
 if(speaking&&voicePhase==nbx::VoiceStep::Player&&now>=voiceEnd){
  if(streamLead.ready(npcChunks.size(),finalAccepted)){
   PlaySoundW(nullptr,nullptr,0);
   if(!npcWave.open()){auto* m=menu;cancel();if(m&&xardas(m))openEditor(m);return;}
   captionsFor("Xardas: "+npcText);streamingNpc=true;voicePhase=nbx::VoiceStep::Xardas;
   event({{"type","voice_stream_started"},{"buffered_ms",streamLead.buffered()},{"required_ms",streamLead.required()},{"final_accepted",finalAccepted}});
  }else if(finalAccepted&&npcChunks.empty()){
   voicePhase=nbx::VoiceStep::Xardas;captionsFor("Xardas: "+npcText);
   if(!startVoice(npcAudio,npcDuration,"xardas")){auto* m=menu;cancel();if(m&&xardas(m))openEditor(m);return;}
  }
 }
 if(speaking&&streamingNpc&&voicePhase==nbx::VoiceStep::Xardas){
  npcWave.poll();
  if(!npcWave.active()&&npcChunks.empty()&&!finalAccepted&&!starvedAt){starvedAt=now;event({{"type","device_queue_starved"}});}
  while(!npcChunks.empty()){
   auto chunk=std::move(npcChunks.front());npcChunks.pop_front();
   if(!npcWave.append(chunk.path)){auto* m=menu;cancel();if(m&&xardas(m))openEditor(m);return;}
   if(starvedAt){event({{"type","device_queue_recovered"},{"gap_ms",now-starvedAt}});starvedAt=0;}
   ++npcChunksPlayed;event({{"type","voice_chunk_queued"},{"sequence",npcChunksPlayed-1}});
  }
  npcWave.poll();
 }
 const bool doneStream=streamingNpc&&nbx::streamActionsReady(finalAccepted,!npcChunks.empty(),npcWave.active());
 const bool doneFull=!streamingNpc&&finalAccepted&&voicePhase==nbx::VoiceStep::Xardas&&now>=voiceEnd;
 if(speaking&&(doneStream||doneFull)){
  npcWave.reset();auto actions=std::move(delayedActions);const auto mode=actionMode;const auto scene=actionScene;speaking=false;voicePhase=nbx::VoiceStep::Idle;captions.clear();
  if(menu&&xardas(menu)&&currentNpc==menu->Npc){nbx::runActions(actions,[]{return menu&&xardas(menu)&&currentNpc==menu->Npc&&hero()==currentHero&&currentHero->homeWorld==currentWorld;},[&](const json& action){return applyAction(action,mode,scene);});if(menu&&xardas(menu))openEditor(menu);}
 }

}
void __fastcall render(g::oCGame* game,void*){
 const auto frameStart=GetTickCount64();
 try{tick(game);if(!fault&&sameThread()&&busy)waitFrames.sample(GetTickCount64());}catch(...){fault=true;cancel();log("Native bridge fault; disabled all actions");}
 realRender(game);
 try{if(!fault&&busy){auto* scr=*reinterpret_cast<g::zCView**>(0x00AB6468);if(scr)scr->PrintCX(3700,g::zSTRING("Xardas zbiera mysli..."));}if(!fault&&(editing||(!submittedText.empty()&&(busy||speaking)))){auto* scr=*reinterpret_cast<g::zCView**>(0x00AB6468);if(scr){const auto& value=editing?typedText:submittedText;for(size_t offset=0;offset<value.size()||offset==0;offset+=60){auto line=gameText(utf8(nbx::editLine(value,offset,60)));if(offset==0)line="Bezimienny: "+line;if(editing&&offset+60>=value.size())line+="_";scr->PrintCX((editing?6600:4200)+static_cast<int>(offset/60)*220,g::zSTRING(line.c_str()));}}}if(!fault&&speaking&&voicePhase==nbx::VoiceStep::Xardas){auto* scr=*reinterpret_cast<g::zCView**>(0x00AB6468);if(scr)for(size_t i=0;i<captions.size();++i)scr->PrintCX(5700+static_cast<int>(i)*220,g::zSTRING(captions[i].c_str()));}}catch(...){fault=true;}
 const auto delay=nbx::waitFrameDelay(!fault&&sameThread()&&busy&&!loadingDepth,frameStart,GetTickCount64());
 if(delay)Sleep(delay);
}
void writeAtomic(const fs::path& p,const json& data){auto temp=p;temp+=L".tmp";{std::ofstream f(temp,std::ios::binary|std::ios::trunc);f<<data.dump(2);if(!f)throw std::runtime_error("write failed");}std::error_code ec;fs::rename(temp,p,ec);if(ec)throw std::runtime_error("atomic rename failed");}
void worker(){
 struct Waiting {std::string id;bool partialDelivered=false;unsigned nextChunk=0;};
 std::vector<Waiting> waiting;
 while(!shutdown){
  try{
   std::deque<json> requests,logs;{std::lock_guard lock(ioMutex);requests.swap(outbox);logs.swap(events);}
   if(!logs.empty()){std::ofstream f(root/L"native-events.jsonl",std::ios::app);for(auto& e:logs)f<<e.dump()<<'\n';}
   for(auto& req:requests){std::string id=req["id"];writeAtomic(root/L"requests"/(id+".json"),req);waiting.push_back({id,false});}
   for(auto it=waiting.begin();it!=waiting.end();){
    std::error_code ec;
    if(!it->partialDelivered){
     auto partial=root/L"responses"/(it->id+".player.json");
     if(fs::exists(partial,ec)&&!ec){
      it->partialDelivered=true;const auto size=fs::file_size(partial,ec);
      if(!ec&&size<=65536){std::ifstream f(partial,std::ios::binary);json data=json::parse(f,nullptr,false);if(!data.is_discarded()){std::lock_guard lock(ioMutex);inbox.push_back(std::move(data));}}
     }
    }
    while(it->nextChunk<256){
     auto part=root/L"responses"/(it->id+".npc."+std::to_string(it->nextChunk)+".json");
     ec.clear();if(!fs::exists(part,ec)||ec)break;
     const auto size=fs::file_size(part,ec);if(ec||size>65536)break;
     std::ifstream f(part,std::ios::binary);json data=json::parse(f,nullptr,false);
     if(data.is_discarded())break;
     {std::lock_guard lock(ioMutex);inbox.push_back(std::move(data));}++it->nextChunk;
    }
    auto p=root/L"responses"/(it->id+".json");
    if(!fs::exists(p,ec)){++it;continue;}
    const auto size=fs::file_size(p,ec);if(ec||size>65536){log("Response rejected: size limit");it=waiting.erase(it);continue;}
    std::ifstream f(p,std::ios::binary);json data=json::parse(f,nullptr,false);
    if(!data.is_discarded()){
     if(data.is_object()&&data.contains("npc_chunk_count")&&data["npc_chunk_count"].is_number_integer()&&data["npc_chunk_count"]>=1&&data["npc_chunk_count"]<=256&&it->nextChunk<data["npc_chunk_count"].get<unsigned>()){++it;continue;}
     std::lock_guard lock(ioMutex);inbox.push_back(std::move(data));
    }
    it=waiting.erase(it);
   }
   // Director controls are opt-in and operate on a copied filming fixture only.
   // They are never accepted through model responses.
   auto fixturePath=root/L"fixture.json";
   if(fs::exists(fixturePath)&&fs::file_size(fixturePath)<16384){
    std::ifstream f(fixturePath);json fixture=json::parse(f,nullptr,false);
    if(fixture.is_object()&&fixture.value("enabled",false)&&fixture.contains("fixture_slot")&&fixture["fixture_slot"].is_number_integer()&&fixture.contains("fixture_directory")&&fixture["fixture_directory"].is_string()){
     int slot=fixture["fixture_slot"];std::error_code ec;
     auto target=fs::canonical(fs::path(wide(fixture["fixture_directory"])),ec);
     auto expected=fs::canonical(folder.parent_path()/L"saves"/("savegame"+std::to_string(slot)),ec);
     if(!ec&&slot>=1&&slot<=20&&target==expected&&fs::exists(root/L"fixture-manifest.json")){
      int count=0;
      for(const auto& entry:fs::directory_iterator(root/L"commands")){
       if(++count>16)break;if(!entry.is_regular_file()||entry.path().extension()!=L".json"||entry.file_size()>4096)continue;
       std::ifstream input(entry.path());json c=json::parse(input,nullptr,false);bool admitted=false;
       if(c.is_object()&&c.contains("id")&&c["id"].is_string()&&c.contains("command")&&c["command"].is_string()&&c["command"]=="reload_fixture"){
        c["fixture_slot"]=slot;{std::lock_guard lock(ioMutex);directorInbox.push_back(c);}admitted=true;
       }
       auto done=entry.path();done+=admitted?L".accepted":L".rejected";fs::rename(entry.path(),done,ec);
      }
     }
    }
   }
   if(waiting.size()>8)waiting.erase(waiting.begin(),waiting.end()-8);
  }catch(const std::exception& e){log(std::string("IO worker: ")+e.what());}
  Sleep(80);
 }
}
DWORD WINAPI initialize(void*){
 try{
  wchar_t exe[MAX_PATH]{};GetModuleFileNameW(nullptr,exe,MAX_PATH);folder=fs::path(exe).parent_path();root=folder/L"NBXardasAI";
  for(const auto* dir:{L"requests",L"responses",L"commands",L"audio"})fs::create_directories(root/dir);
  // The callback runs on the loader thread; no game hook is installed before
  // the exact ABI patch has succeeded. Never force the ABI library to load.
  const auto abiDeadline=GetTickCount64()+30000;
  while(nbxabi::status()==0&&GetTickCount64()<abiDeadline)Sleep(50);
  const LONG abiStatus=nbxabi::status();
  log(std::string("ABI notification status ")+std::to_string(abiStatus));
  if(abiStatus!=1){log("ABI unavailable or not matched; no hooks installed");return 0;}
  if(hashFile(exe)!=kExeHash){log("Unsupported executable; no hooks installed");return 0;}
  // Hooks and the worker live until process exit; unloading this module is unsupported.
  HMODULE pinnedModule=nullptr;
  if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
       reinterpret_cast<LPCWSTR>(&initialize),&pinnedModule)){
   log("Module pin failed; no hooks installed, error "+std::to_string(GetLastError()));return 0;
  }
  LONG result=NO_ERROR;
  {
   // Snapshot and acquire every handle before starting the transaction or
   // suspending any thread. IDs created after this snapshot remain unobserved.
   std::vector<DWORD> ids;
   HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
   if(snapshot==INVALID_HANDLE_VALUE){log("Detours thread snapshot failed "+std::to_string(GetLastError()));return 0;}
   struct CloseHandleScope {HANDLE h;~CloseHandleScope(){CloseHandle(h);}} closeSnapshot{snapshot};
   THREADENTRY32 entry{};entry.dwSize=sizeof(entry);
   if(!Thread32First(snapshot,&entry)){log("Detours thread enumeration failed "+std::to_string(GetLastError()));return 0;}
   do {
    if(entry.th32OwnerProcessID==GetCurrentProcessId()&&entry.th32ThreadID!=GetCurrentThreadId())ids.push_back(entry.th32ThreadID);
   } while(Thread32Next(snapshot,&entry));
   if(GetLastError()!=ERROR_NO_MORE_FILES){log("Detours thread enumeration failed "+std::to_string(GetLastError()));return 0;}
   std::vector<HANDLE> threads;threads.reserve(ids.size());
   struct CloseThreads {std::vector<HANDLE>& handles;~CloseThreads(){for(auto h:handles)CloseHandle(h);}} closeThreads{threads};
   for(DWORD id:ids){
    HANDLE thread=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_SET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,id);
    if(!thread){log("Detours thread open failed "+std::to_string(GetLastError()));return 0;}
    threads.push_back(thread);
   }
   result=DetourTransactionBegin();
   if(result){log("Detours transaction failed "+std::to_string(result));return 0;}
   struct AbortTransaction {bool active=true;~AbortTransaction(){if(active)DetourTransactionAbort();}} transaction;
   // Prepare trampolines before suspending the other threads.
   if(!result)result=DetourAttach(reinterpret_cast<PVOID*>(&realInput),inputHook);
   if(!result)result=DetourAttach(reinterpret_cast<PVOID*>(&realCollect),collect);
   if(!result)result=DetourAttach(reinterpret_cast<PVOID*>(&realSelect),select);
   if(!result)result=DetourAttach(reinterpret_cast<PVOID*>(&realUpdate),update);
   if(!result)result=DetourAttach(reinterpret_cast<PVOID*>(&realExit),exitDialogue);
   if(!result)result=DetourAttach(reinterpret_cast<PVOID*>(&realRender),render);
   if(!result)result=DetourAttach(reinterpret_cast<PVOID*>(&realLoadGame),loadGame);
   if(!result)result=DetourAttach(reinterpret_cast<PVOID*>(&realLoadSave),loadSave);
   if(!result)result=DetourAttach(reinterpret_cast<PVOID*>(&realChangeLevel),changeLevel);
   for(HANDLE thread:threads){if(result)break;result=DetourUpdateThread(thread);}
   if(!result){result=DetourTransactionCommit();transaction.active=false;}
   // On failure or exception the transaction guard aborts and resumes threads
   // before their handles are closed; logging occurs outside this scope.
  }
  if(result){log("Detours coordinated installation failed "+std::to_string(result));return 0;}
  log("NBXardasAI 1.0 initialized; coordinated thread snapshot hooks installed; exact EXE guard passed");
  worker();
 }catch(const std::exception& e){log(std::string("Initialization failed: ")+e.what());}return 0;
}
}
extern "C" __declspec(dllexport) const char* NBXardasAI_Version(){return "1.0";}
BOOL APIENTRY DllMain(HMODULE module,DWORD reason,LPVOID){
 if(reason==DLL_PROCESS_ATTACH){nbxabi::initialize(module);auto thread=CreateThread(nullptr,0,initialize,nullptr,0,nullptr);if(thread)CloseHandle(thread);}
 if(reason==DLL_PROCESS_DETACH)shutdown=true;
 return TRUE;
}
