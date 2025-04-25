#include "esp32m/ui/mqtt.hpp"
#include "esp32m/events/response.hpp"
#include "esp32m/json.hpp"

namespace esp32m {
  namespace ui {

    class MqttRequest : public Request {
     public:
      MqttRequest(const char *name, int id, const char *target,
                  const JsonVariantConst data)
          : Request(name, id, target, data, "mqtt") {}

     protected:
      void respondImpl(const char *source, const JsonVariantConst data,
                       bool isError) override {
        Mqtt::instance().respond(source, seq(), data, isError);
      }
      Response *makeResponseImpl() override {
        return new Response(Mqtt::instance().name(), name(), target(), seq());
      }
    };

    void Mqtt::init(Ui *ui) {
      Transport::init(ui);
      auto name = App::instance().hostname();
      if (asprintf(&_requestTopic, "esp32m/request/%s/#", name) < 0)
        _requestTopic = nullptr;
      if (asprintf(&_responseTopic, "esp32m/response/%s/", name) < 0)
        _responseTopic = nullptr;
      net::Mqtt::instance().subscribe(_requestTopic);
      EventManager::instance().subscribe([this](Event &ev) {
        Response *r = nullptr;
        if (net::mqtt::Incoming::is(ev)) {
          net::mqtt::Incoming &iev = (net::mqtt::Incoming &)ev;
          auto ctl = strlen(_requestTopic);
          auto topic = iev.topic();
          auto topiclen = topic.length();
          if ((topiclen >= ctl) &&
              !strncmp(topic.c_str(), _requestTopic, ctl - 1)) {
            auto devlen = topiclen - ctl + 1 /* / */ + 1 /* # */;
            auto cmdStart = topic.c_str() + ctl - 1 /* # */;
            auto firstSlash = (char *)memchr(cmdStart, '/', devlen - 1);
            if (firstSlash) {
              char *devname = strndup(cmdStart, firstSlash - cmdStart);
              char *command = strndup(firstSlash + 1,
                                      devlen - 1 - (firstSlash - cmdStart + 1));
              JsonDocument *doc = nullptr;
              auto data = iev.payload();
              if (data.size())
                doc = json::parse(data.c_str());
              MqttRequest req(command, 0, devname,
                              doc ? doc->as<JsonVariantConst>()
                                  : json::null<JsonVariantConst>());
              req.publish();
              free(devname);
              free(command);
              if (doc)
                free(doc);
            }
          }
        } else if (Response::is(ev, this->name(), &r)) {
          JsonDocument *doc = r->data();
          JsonVariantConst data = doc ? doc->as<JsonVariantConst>()
                                      : json::null<JsonVariantConst>();
          respond(r->source(), r->seq(), data, r->isError());
        }
      });
    }

    void Mqtt::respond(const char *source, int seq, const JsonVariantConst data,
                       bool isError) {
      size_t tl = strlen(_responseTopic) + strlen(source) + 1;
      if (seq)
        tl += 1 + 10;
      if (isError)
        tl += 6;
      char *topic = (char *)malloc(tl);
      char *tp = topic;
      size_t l = snprintf(tp, tl, "%s%s", _responseTopic, source);
      tp += l;
      tl -= l;
      if (seq && tl) {
        l = snprintf(tp, tl, "/%d", seq);
        tp += l;
        tl -= l;
      }
      if (isError && tl)
        strlcpy(tp, "/error", tl);
      std::string ds;
      serializeJson(data, ds);
      // char *ds = json::allocSerialize(data);
      net::Mqtt::instance().publish(topic, ds.c_str());
      free(topic);
/*      if (ds)
        free(ds);*/
    }

    Mqtt &Mqtt::instance() {
      static Mqtt i;
      return i;
    }

    esp_err_t Mqtt::wsSend(uint32_t cid, const char *text) {
      printf("%s\r\n", text);
      return ESP_OK;
    }

  }  // namespace ui
}  // namespace esp32m