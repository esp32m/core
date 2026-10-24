#pragma once

#include <cstdint>

namespace esp32m::net::wifi {

  // All calls belong to the Wi-Fi worker. Event handlers only post event bits.
  class RoamingPolicy {
   public:
    static constexpr uint32_t AttemptInterval = 60000;
    static constexpr uint32_t NeighborTimeout = 5000;
    static constexpr uint32_t TransitionTimeout = 10000;
    static constexpr uint32_t AssociationTimeout = 15000;

    enum class Action { None, RequestNeighbors, QueryBtm, Scan };

    void connected(uint32_t now) {
      _stage = Stage::Idle;
      _lastAttempt = now;
      _association = false;
    }

    void disconnected(bool roaming, uint32_t now) {
      _stage = Stage::Idle;
      _association = roaming;
      _associationStarted = now;
    }

    bool associationPending(uint32_t now) {
      if (_association && elapsed(now, _associationStarted, AssociationTimeout))
        _association = false;
      return _association;
    }

    void neighborReport(uint32_t now) {
      if (_stage == Stage::Neighbor)
        _deadlineStarted = now - NeighborTimeout;
    }

    void neighborRequestFailed(uint32_t now) {
      neighborReport(now);
    }

    void transitionRequestFailed(uint32_t now) {
      if (_stage == Stage::Transition)
        _deadlineStarted = now - TransitionTimeout;
    }

    void scanComplete(uint32_t now) {
      _stage = Stage::Idle;
      _lastAttempt = now;
    }

    Action poll(uint32_t now, bool enabled, bool connected, bool weak,
                bool scanAvailable) {
      if (!enabled || !connected || !weak) {
        _stage = Stage::Idle;
        return Action::None;
      }
      switch (_stage) {
        case Stage::Idle:
          if (!elapsed(now, _lastAttempt, AttemptInterval))
            return Action::None;
          _lastAttempt = _deadlineStarted = now;
          _stage = Stage::Neighbor;
          return Action::RequestNeighbors;
        case Stage::Neighbor:
          if (elapsed(now, _deadlineStarted, NeighborTimeout)) {
            _stage = Stage::Transition;
            _deadlineStarted = now;
            return Action::QueryBtm;
          }
          break;
        case Stage::Transition:
          if (scanAvailable &&
              elapsed(now, _deadlineStarted, TransitionTimeout)) {
            _stage = Stage::Scan;
            return Action::Scan;
          }
          break;
        case Stage::Scan:
          break;
      }
      return Action::None;
    }

    static bool betterCandidate(int currentRssi, int candidateRssi, int threshold,
                                bool sameBssid) {
      return !sameBssid && candidateRssi >= threshold &&
             candidateRssi >= currentRssi + 10;
    }

   private:
    enum class Stage { Idle, Neighbor, Transition, Scan };
    Stage _stage = Stage::Idle;
    uint32_t _lastAttempt = 0, _deadlineStarted = 0, _associationStarted = 0;
    bool _association = false;

    static bool elapsed(uint32_t now, uint32_t since, uint32_t interval) {
      return uint32_t(now - since) >= interval;
    }
  };

}  // namespace esp32m::net::wifi
