#include "esp32m/ui/subscriptions.hpp"
#include <cassert>
#include <iostream>
#include <map>
#include <set>

using esp32m::ui::StateSubscriptions;
using Session = StateSubscriptions::Session;
struct Frame { Session session; StateSubscriptions::Payload wire; JsonDocument doc; };
struct Fixture {
  std::map<std::string, JsonDocument> states;
  std::map<uint32_t, uint64_t> sessions{{1, 1}, {2, 1}, {3, 1}};
  std::vector<Frame> frames;
  std::function<void()> duringCapture;
  uint64_t serverNow = uint64_t(1) << 40;
  StateSubscriptions subscriptions;
  Fixture() : subscriptions(
    [&](const std::string& target, const std::string& view, JsonVariantConst query,
        JsonDocument& result) -> const char* {
      if (duringCapture) { auto f = std::move(duringCapture); duringCapture = {}; f(); }
      auto key = target + '/' + view;
      auto i = states.find(key);
      if (i == states.end()) return "unsupported-state";
      result.set(i->second);
      if (!query.isNull()) result["query"] = query;
      return nullptr;
    },
    [&](const Session& session, auto payload) {
      Frame frame{session, payload, JsonDocument()};
      assert(!deserializeJson(frame.doc, *payload));
      frames.push_back(std::move(frame));
      return true;
    },
    [&](const Session& session) { return sessions[session.cid] == session.generation; },
    {}, [&] { return serverNow; }) {}
  void state(const char* value, const char* key = "sensor/state") {
    assert(!deserializeJson(states[key], value));
  }
  Session session(uint32_t cid = 1) { return {nullptr, cid, sessions[cid]}; }
  void request(const char* name, const char* data, uint32_t now = 0, uint32_t cid = 1) {
    JsonDocument doc; assert(!deserializeJson(doc, data));
    assert(subscriptions.request(session(cid), name, 42, doc, now));
  }
  void subscribe(uint32_t cid = 1) {
    request("state-subscribe", R"({"id":"a","target":"sensor","view":"state"})", 0, cid);
  }
  JsonDocument stats() { JsonDocument d; subscriptions.stats(d.to<JsonObject>()); return d; }
};

void sharing() {
  Fixture f; f.state(R"({"value":12,"long":"abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"})");
  f.states["sensor/state"]["long"] = std::string(256, 'x');
  f.subscribe();
  auto firstBytes = f.stats()["baselineBytes"].as<size_t>();
  assert(firstBytes > 0);
  f.subscribe(2); f.subscribe(3);
  assert(f.stats()["baselineBytes"] == firstBytes);
  assert(f.stats()["streams"] == 1 && f.stats()["interests"] == 3);
  f.frames.clear(); f.subscriptions.process(1000); assert(f.frames.empty());
  f.states["sensor/state"]["value"] = 13;
  assert(f.subscriptions.invalidate("sensor", 1100));
  f.subscriptions.process(1124); assert(f.frames.empty());
  f.subscriptions.process(1125); assert(f.frames.size() == 3);
  assert(f.frames[0].wire.get() == f.frames[1].wire.get());
  auto data = f.frames[0].doc["data"];
  assert(data["rev"] == 2 && data["baseRev"] == 1);
  assert(data["patch"][0]["path"][0] == "value");
  assert(data["patch"][0]["value"] == 13);
  f.request("state-unsubscribe", R"({"id":"a"})");
  f.sessions[2] = 2; f.sessions[3] = 2;
  f.subscriptions.process(1200);
  assert(f.stats()["streams"] == 0 && f.stats()["allocatedBytes"] == 0);
  std::cout << "PASS shared allocator baseline, coalescing, shared payload, disconnect cleanup\n";
}

void identity() {
  Fixture f; f.state(R"({"v":1})");
  f.request("state-subscribe", R"({"id":"a","target":"sensor","view":"state","query":{"z":2,"a":1}})");
  f.request("state-subscribe", R"({"id":"b","target":"sensor","view":"state","query":{"a":1,"z":2}})");
  assert(f.stats()["streams"] == 1 && f.stats()["interests"] == 2);
  f.request("state-subscribe", R"({"id":"a","target":"sensor","view":"state","query":{}})");
  assert(f.frames.back().doc["error"][0] == "subscription-conflict");
  f.frames.clear(); f.state(R"({"v":2})"); f.subscriptions.process(1000);
  assert(f.frames.size() == 1); // two IDs, one stream/session frame
  assert(f.frames[0].doc["data"]["state"]["v"] == 2);
  std::cout << "PASS canonical queries, conflicting ID, one delivery per session/stream\n";
}

void atomicSnapshot() {
  Fixture f; f.state(R"({"v":1})"); f.subscribe();
  f.state(R"({"v":2})"); f.subscribe(2);
  auto& oldUpdate = f.frames[f.frames.size() - 2];
  auto& ack = f.frames.back();
  assert(oldUpdate.doc["data"]["rev"] == 2 && ack.doc["data"]["rev"] == 2);
  assert(oldUpdate.session.cid == 1 && ack.session.cid == 2);
  f.frames.clear();
  f.duringCapture = [&] { f.state(R"({"v":3})"); f.subscriptions.invalidate("sensor", 1200); };
  f.request("state-resync", R"({"id":"a"})", 1200);
  assert(f.frames.back().doc["data"]["state"]["v"] == 3);
  f.frames.clear(); f.subscriptions.process(1225);
  assert(f.frames.empty());
  assert(f.stats()["captures"].as<unsigned>() == 4); // dirty generation is not lost
  std::cout << "PASS subscribe commit ordering, resync revision, concurrent invalidation\n";
}

void structuralChanges() {
  Fixture f; f.state(R"({"tuple":[1,null,"a"],"removed":4,"stable":"abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"})");
  f.states["sensor/state"]["stable"] = std::string(512, 'x');
  f.subscribe(); f.frames.clear();
  f.state(R"({"tuple":[1,3,"a"],"new":null,"stable":"abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"})");
  f.states["sensor/state"]["stable"] = std::string(512, 'x');
  f.subscriptions.process(1000);
  auto patch = f.frames[0].doc["data"]["patch"].as<JsonArrayConst>();
  assert(patch.size() == 3);
  assert(patch[0]["op"] == "remove" && patch[0]["path"][0] == "removed");
  assert(patch[1]["path"][0] == "tuple" && patch[1]["path"][1] == 1);
  assert(patch[2]["op"] == "set" && patch[2]["value"].isNull());
  f.frames.clear(); f.state("[true,null]"); f.subscriptions.process(2000);
  assert(f.frames[0].doc["data"]["state"].is<JsonArray>());
  assert(f.frames[0].doc["data"]["rev"] == 3);
  std::cout << "PASS object remove vs null, tuple index, root/type replacement and smaller snapshot\n";
}

void limits() {
  Fixture f; auto& state = f.states["sensor/state"];
  state["big"] = std::string(20000, 'a'); f.subscribe();
  assert(f.frames.back().doc["error"][0] == "snapshot-limit");
  assert(f.stats()["streams"] == 0 && f.stats()["allocatedBytes"] == 0);
  f.state(R"({"v":1})");
  for (unsigned i = 0; i < 32; i++) {
    std::string data = "{\"id\":\"i" + std::to_string(i) +
                       "\",\"target\":\"sensor\",\"view\":\"state\"}";
    f.request("state-subscribe", data.c_str());
  }
  assert(f.stats()["interests"] == 32 && f.stats()["streams"] == 1);
  f.request("state-subscribe", R"({"id":"extra","target":"sensor","view":"state"})");
  assert(f.frames.back().doc["error"][0] == "subscription-limit");
  f.sessions[1] = 2; f.subscriptions.process(0);
  assert(f.stats()["allocatedBytes"] == 0);
  std::cout << "PASS bounded JSON allocation, rejected capture cleanup, interest cap, generation cleanup\n";
}

void burstsAndWrap() {
  Fixture f; f.state(R"({"v":1})");
  f.request("state-subscribe", R"({"id":"a","target":"sensor","view":"state"})", UINT32_MAX - 30);
  f.frames.clear();
  f.state(R"({"v":2})");
  f.subscriptions.invalidate("sensor", UINT32_MAX - 10);
  f.subscriptions.invalidate("sensor", 5);
  f.subscriptions.process(14); assert(f.frames.size() == 1);
  assert(f.stats()["captures"] == 2);
  f.frames.clear(); f.subscriptions.process(15); assert(f.frames.empty());
  std::cout << "PASS wrap-safe cadence and non-trailing burst coalescing\n";
}

void scalarTypes() {
  const std::pair<const char*, const char*> changes[] = {
    {"false", "0"}, {"true", "1"}, {"0", "false"}, {"1", "true"},
    {"0", "\"0\""}, {"\"1\"", "1"}, {"null", "0"}, {"0", "null"},
    {"null", "false"}, {"false", "null"}, {"null", "\"\""}, {"\"\"", "null"},
  };
  for (auto [before, after] : changes) {
    Fixture f;
    f.state((std::string("{\"v\":") + before + '}').c_str());
    f.states["sensor/state"]["stable"] = std::string(512, 'x');
    f.subscribe(); f.frames.clear();
    JsonDocument replacement; assert(!deserializeJson(replacement, after));
    f.states["sensor/state"]["v"].set(replacement.as<JsonVariantConst>());
    f.subscriptions.process(1000);
    assert(f.frames.size() == 1);
    auto patch = f.frames[0].doc["data"]["patch"].as<JsonArrayConst>();
    assert(patch.size() == 1 && patch[0]["op"] == "set" && patch[0]["path"][0] == "v");
    auto value = patch[0]["value"];
    assert(value.is<bool>() == replacement.is<bool>());
    assert(value.isNull() == replacement.isNull());
    assert(value.is<const char*>() == replacement.is<const char*>());
    assert(value == replacement.as<JsonVariantConst>());
  }
  std::cout << "PASS bool/number scalar transitions in both directions, string and null type changes\n";
}

void acquisitionTimestamps() {
  Fixture f;
  f.state(R"({"value":12,"sampledAtMs":1099511627000})");
  f.states["sensor/state"]["stable"] = std::string(256, 'x');
  f.subscribe();
  auto initial = f.frames.back().doc["data"];
  assert(initial["serverTimeMs"].as<uint64_t>() == f.serverNow);
  assert(initial["state"]["sampledAtMs"].as<uint64_t>() == 1099511627000ULL);
  assert(initial["state"]["serverTimeMs"].isUnbound());
  f.frames.clear();
  f.serverNow += 1000;
  f.subscriptions.process(1000);
  assert(f.frames.empty()); // Passing time alone never revises sample state.
  f.states["sensor/state"]["sampledAtMs"] = f.serverNow;
  f.subscriptions.invalidate("sensor", 1100);
  f.serverNow += 25;
  f.subscriptions.process(1125);
  assert(f.frames.size() == 1);
  auto update = f.frames.back().doc["data"];
  assert(update["serverTimeMs"].as<uint64_t>() == f.serverNow);
  auto patch = update["patch"].as<JsonArrayConst>();
  assert(patch.size() == 1 && patch[0]["path"][0] == "sampledAtMs");
  assert(patch[0]["value"].as<uint64_t>() == f.serverNow - 25);
  const auto revision = update["rev"].as<uint64_t>();
  f.frames.clear();
  f.serverNow += 5000;
  f.request("state-resync", R"({"id":"a"})", 6125);
  assert(f.frames.size() == 1);
  auto snapshot = f.frames[0].doc["data"];
  assert(snapshot["rev"].as<uint64_t>() == revision);
  assert(snapshot["serverTimeMs"].as<uint64_t>() == f.serverNow);
  assert(snapshot["state"]["sampledAtMs"].as<uint64_t>() == f.serverNow - 5025);
  std::cout << "PASS 64-bit clock metadata, quiet clock ticks, unchanged-value acquisitions and resync age\n";
}

int main() {
  sharing(); identity(); atomicSnapshot(); structuralChanges(); limits(); burstsAndWrap(); scalarTypes();
  acquisitionTimestamps();
}
