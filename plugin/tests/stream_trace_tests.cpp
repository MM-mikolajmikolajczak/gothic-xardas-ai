#ifdef NDEBUG
#undef NDEBUG
#endif
#include "wave_queue.hpp"
#include "json.hpp"
#include <cassert>
#include <iostream>
using json=nlohmann::json;
struct Simulation {ULONGLONG gaps=0,start=0;};
Simulation replay(const json& trace,bool adaptive,ULONGLONG spacing=1){
 nbx::StreamLead lead;ULONGLONG end=0;bool started=false;Simulation result;
 const auto text=trace.at("text").get<std::string>();
 unsigned seen=0;ULONGLONG queued=0;
 for(const auto& chunk:trace.at("chunks")){
  const ULONGLONG at=chunk.at("arrival_ms").get<ULONGLONG>()*spacing;
  const auto duration=chunk.at("duration_ms").get<ULONGLONG>();
  lead.observe(at,duration,text);++seen;
  const bool final=seen==trace.at("chunks").size();
  if(!started){
   queued+=duration;
   if(adaptive?lead.ready(seen,final):(seen>=2||final)){
    started=true;result.start=at;end=at+queued;
   }
  }else{
   if(at>end){result.gaps+=at-end;end=at;}
   end+=duration;
  }
 }
 assert(started);return result;
}
int main(int argc,char** argv){
 nbx::StreamLead lead;assert(!lead.ready(0,false));assert(!lead.ready(0,true));
 lead.observe(100,640,"Short reply");assert(!lead.ready(1,false));assert(lead.ready(1,true));
 lead.observe(500,640,"Short reply");assert(!lead.ready(2,false));
 lead.observe(900,640,"Short reply");assert(lead.ready(3,false));
 lead.reset();assert(lead.buffered()==0&&!lead.ready(0,true));
 lead.observe(100,640,std::string(100,'x'));lead.observe(900,640,"");lead.observe(1700,640,"");assert(!lead.ready(3,false));assert(lead.required()>1920);
 assert(lead.ready(3,true));lead.reset();lead.observe(1,8000,"Cached reply");assert(lead.ready(1,true));
 lead.reset();lead.observe(1000,640,"Hi");lead.observe(900,640,"Hi");lead.observe(1000,640,"Hi");assert(lead.required()==1800);
 assert(argc==2);std::ifstream input(argv[1]);json traces;input>>traces;
 unsigned regressionsCaught=0;
 for(const auto& trace:traces){
  auto before=replay(trace,false),after=replay(trace,true);
  if(before.gaps)++regressionsCaught;
  assert(after.gaps==0);
  std::cout<<trace.at("id")<<" old_gap_ms="<<before.gaps<<" adaptive_gap_ms="<<after.gaps<<" extra_start_ms="<<after.start-before.start<<'\n';
  // Stress a producer twice as slow. Waiting for final is allowed, gaps are not.
  assert(replay(trace,true,2).gaps==0);
 }
 assert(regressionsCaught>=4);
}
