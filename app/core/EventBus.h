// SPDX-License-Identifier: Apache-2.0
// app/core/EventBus.h — Lock-free SPSC bounded event ring & MeshEvent payload (ADR-006, ADR-007)
#pragma once

#include <atomic>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "Types.h"

namespace faro::core {

enum class MeshEventType : uint8_t {
  None = 0,
  DirectMessageRecv,
  ChannelMessageRecv,
  QueueSizeChanged,
  ContactDiscovered,
  AckRecv,
  TraceRecv,
  ControlDataRecv,
  ChannelDataRecv,
  RemoteResponse,      // answer to our login / status / telemetry / binary request (raw bytes in text[])
  NodeDiscovered,      // answer to our zero-hop discover request
  NotifyStateChanged
};

struct MeshEvent {
  MeshEventType type = MeshEventType::None;
  uint32_t timestamp = 0;
  int8_t   snr_q4 = 0;
  int16_t  rssi = -100;
  uint8_t  hops = 0;

  // Sender / Contact / Channel metadata
  uint8_t  pub_key[32] = {0};
  char     name[32] = {0};
  NodeRole role = NodeRole::ChatNode;
  bool     is_new_contact = false;
  uint8_t  channel_index = 0;
  uint8_t  txt_type = 0;
  int32_t  lat_e6 = 0;
  int32_t  lon_e6 = 0;
  uint8_t  path_len = 0xFF;
  uint8_t  path[64] = {0};

  // Text / binary / ACK / Queue
  char     text[160] = {0};
  uint8_t  data_len = 0;     // RemoteResponse: bytes used in text[]
  uint8_t  resp_kind = 0;    // RemoteResponse: RemoteRequestKind
  int8_t   snr2_q4 = 0;      // NodeDiscovered: how the node heard us
  uint32_t ack_crc = 0;
  uint16_t queue_size = 0;

  // Trace path result
  TraceResult trace = {};

};

template <typename T, size_t Capacity = 64>
class EventRing {
  static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
public:
  bool push(const T& item) {
    const size_t w = _write.load(std::memory_order_relaxed);
    const size_t next = (w + 1) & (Capacity - 1);
    if (next == _read.load(std::memory_order_acquire)) {
      _dropped.store(_dropped.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
      return false;  // ring full
    }
    _slots[w] = item;
    _write.store(next, std::memory_order_release);
    return true;
  }

  bool pop(T& out) {
    const size_t r = _read.load(std::memory_order_relaxed);
    if (r == _write.load(std::memory_order_acquire)) {
      return false;  // ring empty
    }
    out = _slots[r];
    _read.store((r + 1) & (Capacity - 1), std::memory_order_release);
    return true;
  }

  bool empty() const {
    return _read.load(std::memory_order_acquire) == _write.load(std::memory_order_acquire);
  }

  size_t size() const {
    const size_t w = _write.load(std::memory_order_acquire);
    const size_t r = _read.load(std::memory_order_acquire);
    return (w - r) & (Capacity - 1);
  }

  uint32_t droppedCount() const {
    return _dropped.load(std::memory_order_relaxed);
  }

  void clear() {
    _read.store(0, std::memory_order_relaxed);
    _write.store(0, std::memory_order_relaxed);
  }

private:
  T _slots[Capacity] = {};
  std::atomic<size_t> _write{0};
  std::atomic<size_t> _read{0};
  std::atomic<uint32_t> _dropped{0};
};

using MeshEventBus = EventRing<MeshEvent, 16>;

}  // namespace faro::core
