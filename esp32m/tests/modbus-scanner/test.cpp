#include <ArduinoJson.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using esp_err_t = int;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_NO_MEM=257, ESP_ERR_INVALID_ARG=258,
 ESP_ERR_INVALID_STATE=259, ESP_ERR_NOT_SUPPORTED=262, ESP_ERR_TIMEOUT=263,
 ESP_ERR_INVALID_RESPONSE=264;
constexpr int MB_RTU=0, MB_ASCII=1, pdPASS=1, pdTRUE=1;
using TaskHandle_t=void*;
#define pdMS_TO_TICKS(x) (x)
#define logE(...) ((void)0)
namespace fake {
 uint64_t now=1000;
 void (*task)(void*)=nullptr;
 void* taskArg=nullptr;
 bool taskOk=true;
 unsigned changes=0;
 struct Wait {};
}
int xTaskCreate(void (*fn)(void*), const char*, int stack, void* arg, int, TaskHandle_t* task) {
 assert(stack>=4096);
 if(!fake::taskOk) return 0;
 fake::task=fn; fake::taskArg=arg; *task=reinterpret_cast<void*>(1); return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t task) { assert(task); }
void ulTaskNotifyTake(int, int) { throw fake::Wait{}; }
void esp_task_wdt_add(void*) {}
void esp_task_wdt_reset() {}
int64_t esp_timer_get_time() { return fake::now*1000; }

namespace esp32m {
void delay(unsigned ms) { fake::now+=ms; }
struct Event { bool init=false; };
struct EventInit { static bool is(Event& event,int) { return event.init; } };
struct Request;
struct Response {
 Request* owner;
 std::unique_ptr<JsonDocument> document;
 bool partial=false;
 explicit Response(Request* owner):owner(owner) {}
 void setData(JsonDocument* doc) { document.reset(doc); }
 void setPartial(bool value) { partial=value; }
 void publish();
};
struct Request {
 std::string command;
 JsonDocument payload, response;
 bool internal=false, broadcast=false, responded=false;
 int error=0;
 std::vector<JsonDocument> progress;
 explicit Request(const char* name="scan"):command(name) {}
 bool is(const char* name) const { return command==name; }
 bool isInternal() const { return internal; }
 bool isBroadcast() const { return broadcast; }
 JsonVariantConst data() const { return payload.as<JsonVariantConst>(); }
 void respond(int code=0) { error=code; responded=true; }
 void respondError(int code,const char*) { respond(code); }
 Response* makeResponse() { return new Response(this); }
};
void Response::publish() {
 assert(document);
 if(partial) owner->progress.push_back(*document);
 else { owner->response.set(*document); owner->responded=true; }
}
struct ErrorList {
 int error=0;
 void check(int value,const char*,...) { error=value; }
};
struct RequestContext {
 Request& request; JsonVariantConst data; ErrorList errors;
 RequestContext(Request& request,JsonVariantConst data):request(request),data(data) {}
};
class AppObject {
 public:
 virtual ~AppObject()=default;
 virtual const char* name() const=0;
 bool call(Request& request) { return handleRequest(request); }
 void initTask() { Event event; event.init=true; handleEvent(event); }
 JsonDocument snapshot() { Request req("state-get"); RequestContext ctx(req,req.data());
   std::unique_ptr<JsonDocument> doc(getState(ctx)); return *doc; }
 JsonDocument config() { Request req("config-get"); req.internal=true; RequestContext ctx(req,req.data());
   std::unique_ptr<JsonDocument> doc(getConfig(ctx)); return *doc; }
 int configure(JsonDocument& data,bool internal=false) { Request req("config-set");req.internal=internal;
   RequestContext ctx(req,data.as<JsonVariantConst>()); setConfig(ctx);return ctx.errors.error; }
 protected:
 virtual JsonDocument* getConfig(RequestContext&) { return nullptr; }
 virtual bool setConfig(RequestContext&) { return false; }
 virtual JsonDocument* getState(RequestContext&) { return nullptr; }
 virtual bool handleRequest(Request&) { return false; }
 virtual void handleEvent(Event&) {}
};
namespace config { struct Changed { static void publish(void*,bool) { ++fake::changes; } }; }
namespace modbus {
enum Command { ReadCoils=1, ReadDiscrete=2, ReadHolding=3, ReadInput=4,
 WriteCoil=5,WriteRegister=6,WriteCoils=15,WriteRegisters=16 };
struct Config { struct { int mode=MB_RTU, port=2, baudrate=9600, parity=0; } ser_opts; };
struct Attempt { unsigned addr,cmd,reg,count; bool retry; unsigned quiet; std::vector<uint16_t> data; };
class Master {
 public:
 Config cfg;
 bool configured=true, ignoreWrite=false;
 int ackError=0;
 std::map<unsigned,int> errors;
 std::map<std::pair<unsigned,unsigned>,uint16_t> registers;
 std::vector<Attempt> attempts;
 std::function<void()> duringRequest;
 static Master& instance() { static Master value; return value; }
 bool isConfigured() const { return configured; }
 const Config& config() const { return cfg; }
 // Deliberately no configureSerial/start/stop APIs: scanner must never use them.
 int request(uint8_t addr,Command cmd,uint16_t reg,uint16_t count,void* data,bool retry=true,unsigned quiet=0) {
   assert(count && count<=125);
   auto* words=static_cast<uint16_t*>(data);auto* bits=static_cast<uint8_t*>(data);
   Attempt t{addr,unsigned(cmd),reg,count,retry,quiet,{}};
   const bool writing=cmd==5||cmd==6||cmd==15||cmd==16;
   if(writing) {
     for(unsigned i=0;i<count;++i) {
       auto v=cmd==5?uint16_t(words[0]!=0):cmd==15?uint16_t((bits[i/8]>>(i%8))&1):words[i];
       t.data.push_back(v);
       if(!ignoreWrite) registers[{addr,reg+i}]=v;
     }
     if(cmd==5) assert(words[0]==0||words[0]==0xff00);
   }
   attempts.push_back(t);
   fake::now+=1250;
   if(duringRequest) duringRequest();
   if(writing) return ackError;
   auto error=errors.count(addr)?errors[addr]:(addr==1||addr==3?ESP_OK:ESP_ERR_TIMEOUT);
   if(error) return error;
   const bool packed=cmd==1||cmd==2;
   std::memset(data,0,packed?(count+7)/8:count*2);
   for(unsigned i=0;i<count;++i) {
     const auto key=std::make_pair(unsigned(addr),unsigned(reg+i));
     uint16_t v=registers.count(key)?registers[key]:packed?uint16_t((reg+i)%2):uint16_t(8905+reg+i);
     if(packed) { if(v) bits[i/8]|=1<<(i%8); }
     else words[i]=v;
   }
   return ESP_OK;
 }
};
}
}
#include "production.inc"
using esp32m::Request;
auto& scanner=esp32m::bus::scanner::Modbus::instance();
auto& master=esp32m::modbus::Master::instance();
Request job(const char* kind="scan",unsigned cmd=3,unsigned count=1) {
 Request r(kind);
 r.payload["from"]=1;r.payload["to"]=1;r.payload["addr"]=1;
 r.payload["cmd"]=cmd;r.payload["regs"]=3;r.payload["regc"]=count;
 return r;
}
void reset() {
 master=esp32m::modbus::Master{};
 fake::now+=1000;
 auto defaults=job(); assert(scanner.configure(defaults.payload)==ESP_OK);
}
void drain() {
 try { fake::task(fake::taskArg); } catch(const fake::Wait&) { return; }
 assert(false);
}
void send(Request& r) {
 assert(scanner.call(r));
 if(!r.responded) drain();
 assert(r.responded);
}
int main() {
 auto early=job();send(early);assert(early.error==ESP_ERR_INVALID_STATE);
 fake::taskOk=false;scanner.initTask();
 auto unavailable=job();send(unavailable);assert(unavailable.error==ESP_ERR_INVALID_STATE);
 fake::taskOk=true;scanner.initTask();
 reset();
 auto single=job();send(single);
 assert(single.response["progress"]==100 && single.response["scanned"]==1);
 assert(single.response["results"][0][0]==1 && single.response["results"][0][2]==8908);
 assert(master.attempts.size()==1 && master.attempts[0].cmd==3 && master.attempts[0].reg==3);
 assert(!master.attempts[0].retry && master.attempts[0].quiet==100);
 assert(single.progress.front()["progress"]==0 && single.progress.back()["progress"]==100);
 assert(master.cfg.ser_opts.baudrate==9600 && master.cfg.ser_opts.port==2);
 std::cout<<"PASS single-address FC03/R3 scan, progress and unchanged bus\n";

 reset();fake::now=(uint64_t{1}<<32)-50;
 auto range=job();range.payload["to"]=3;
 assert(scanner.call(range));
 auto duplicate=job();duplicate.payload["regs"]=99;send(duplicate);
 assert(duplicate.error==ESP_ERR_INVALID_STATE);
 auto changed=job();changed.payload["regs"]=99;
 assert(scanner.configure(changed.payload)==ESP_ERR_INVALID_STATE);
 drain();
 assert(master.attempts.size()==3 && master.attempts.back().reg==3);
 assert(range.response["results"][1][1]==ESP_ERR_TIMEOUT && range.response["results"][2][1]==0);
 assert(range.response["elapsedMs"].as<uint64_t>()>=3750 && range.response["progress"]==100);
 assert(scanner.snapshot()["busy"]==false);
 std::cout<<"PASS immutable busy jobs, retained results, timeout classification and 64-bit elapsed time\n";

 reset();auto cancelled=job();cancelled.payload["to"]=3;
 assert(scanner.call(cancelled));
 Request stop("cancel");send(stop);drain();
 assert(master.attempts.empty() && cancelled.response["cancelled"]==true && cancelled.response["progress"]==0);
 reset();auto running=job();running.payload["to"]=3;
 master.duringRequest=[] { Request stop("cancel");send(stop);assert(scanner.snapshot()["busy"]==true); };
 send(running);
 assert(master.attempts.size()==1 && running.response["cancelled"]==true && running.response["scanned"]==1);
 std::cout<<"PASS cancellation before start and during a request; state reads do not block on bus I/O\n";

 reset();
 const char* bads[]={
  R"({"from":0})",R"({"to":248})",R"({"from":3,"to":1})",R"({"addr":0})",
  R"({"addr":248})",R"({"regs":-1})",R"({"regs":65535,"regc":2})",
  R"({"regc":0})",R"({"regc":126})",R"({"cmd":7})",R"({"cmd":23})",
  R"({"cmd":3.5})",R"({"cmd":"3"})",R"({"baud":19200})",R"({"uart":1})",
  R"({"mode":"ascii"})",R"({"parity":2})"
 };
 for(auto json:bads) {
  auto r=job();JsonDocument patch;deserializeJson(patch,json);
  for(auto pair:patch.as<JsonObjectConst>())r.payload[pair.key()]=pair.value();
  send(r);assert(r.error==ESP_ERR_INVALID_ARG && master.attempts.empty());
 }
 auto disguised=job("request",6);send(disguised);assert(disguised.error==ESP_ERR_INVALID_ARG);
 auto broadcast=job();broadcast.broadcast=true;send(broadcast);assert(broadcast.error==ESP_ERR_INVALID_ARG);
 master.configured=false;auto noBus=job();send(noBus);assert(noBus.error==ESP_ERR_INVALID_STATE);
 master.configured=true;
 auto legacy=job();legacy.payload["uart"]=1;legacy.payload["baud"]=4800;
 assert(scanner.configure(legacy.payload,true)==ESP_OK && master.cfg.ser_opts.baudrate==9600);
 std::cout<<"PASS strict validation, no broadcast, no hidden writes, no legacy UART reconfiguration\n";

 reset();
 for(unsigned cmd:{5U,6U,15U,16U}) {
  auto r=job("write",cmd);send(r);assert(r.error==ESP_ERR_INVALID_ARG);
  r=job("write",cmd);r.payload["confirm"]=true;r.payload["data"].to<JsonArray>().add(cmd==5||cmd==15?2:65536);
  send(r);assert(r.error==ESP_ERR_INVALID_ARG);
 }
 assert(master.attempts.empty());
 auto write=job("write",6);write.payload["confirm"]=true;write.payload["data"].to<JsonArray>().add(9000);
 master.ackError=ESP_ERR_TIMEOUT;send(write);
 assert(master.attempts.size()==2 && master.attempts[0].cmd==6 && master.attempts[1].cmd==3);
 assert(write.response["verified"]==true && write.response["writeError"]==ESP_ERR_TIMEOUT && write.response["values"][0]==9000);
 reset();auto mismatch=job("write",6);mismatch.payload["confirm"]=true;mismatch.payload["data"].to<JsonArray>().add(9000);
 master.ignoreWrite=true;send(mismatch);
 assert(mismatch.response["verified"]==false && mismatch.response["error"]==ESP_ERR_INVALID_RESPONSE && master.attempts.size()==2);
 reset();auto lost=job("write",6);lost.payload["confirm"]=true;lost.payload["data"].to<JsonArray>().add(9000);
 master.errors[1]=ESP_ERR_TIMEOUT;send(lost);
 assert(lost.response["verified"]==false && lost.response["verifyError"]==ESP_ERR_TIMEOUT && master.attempts.size()==2);
 std::cout<<"PASS single-shot writes, independent read-back, lost ACK, mismatch and unverified outcome\n";

 reset();auto multi=job("write",16,2);multi.payload["confirm"]=true;
 auto data=multi.payload["data"].to<JsonArray>();data.add(0x1234);data.add(0xffff);
 send(multi);assert(master.attempts[0].cmd==16 && multi.response["verified"]==true);
 assert(multi.response["values"][0]==0x1234 && multi.response["values"][1]==0xffff);
 reset();auto coils=job("write",15,9);coils.payload["confirm"]=true;auto values=coils.payload["data"].to<JsonArray>();
 for(unsigned i=0;i<9;++i)values.add(i%2);
 send(coils);assert(coils.response["verified"]==true && coils.response["values"].size()==9);
 for(unsigned i=0;i<9;++i)assert(coils.response["values"][i]==i%2);
 reset();auto coil=job("write",5);coil.payload["confirm"]=true;coil.payload["data"].to<JsonArray>().add(1);
 send(coil);assert(coil.response["verified"]==true);
 reset();auto bits=job("request",2,9);send(bits);assert(bits.response["values"][0]==1 && bits.response["values"][1]==0);
 reset();auto words=job("request",4,125);send(words);assert(words.response["values"].size()==125);
 for(const auto& a:master.attempts)assert(!a.retry && a.quiet==100);
 std::cout<<"PASS register and packed-bit decoding, FC05/15 encoding, bounded maximum read\n";

 reset();master.errors[1]=ESP_ERR_INVALID_STATE;auto failure=job();failure.payload["to"]=247;send(failure);
 assert(master.attempts.size()==1 && failure.response["error"]==ESP_ERR_INVALID_STATE && failure.response["busy"]==false);
 reset();auto full=job();full.payload["to"]=247;send(full);
 assert(full.response["results"].size()==247 && full.response["progress"]==100);
 assert(measureJson(full.response)<6000);
 std::cout<<"PASS setup-failure completion and bounded full-range response\n";
}
