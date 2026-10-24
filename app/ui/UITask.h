// SPDX-License-Identifier: Apache-2.0
// app/ui/UITask.h — Thin MeshCore integration shim (ADR-001, ADR-006, ADR-007)
// Upstream companion_radio/main.cpp includes "UITask.h" and instantiates:
//   UITask ui_task(&board, &interface_manager);
//   ui_task.begin(disp, &sensors, the_mesh.getNodePrefs());
//   the_mesh.setListener(&ui_task);
// Keep this header free of LVGL or Faro internal headers so upstream TUs stay clean.
#pragma once

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/SensorManager.h>
#include <helpers/MultiSerialInterface.h>
#include "AbstractUITask.h"
#include "NodePrefs.h"

namespace faro {
class DeviceController;
}

class UITask : public AbstractUITask {
public:
  UITask(mesh::MainBoard* board, MultiSerialInterface* interfaces)
    : AbstractUITask(board, interfaces) {}

  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  // ---- AbstractUITask -------------------------------------------------------------
  void notify(UIEventType t = UIEventType::none) override;
  void loop() override;

  // ---- MyMesh::Listener (required) ------------------------------------------------
  void onMessageRecv(mesh::Packet* pkt, const ContactInfo& from, uint8_t txt_type,
                     uint32_t sender_timestamp, const char* text) override;
  void onChannelMessageRecv(mesh::Packet* pkt, ChannelDetails& channel_details,
                            const char* text) override;
  void onQueueSizeChanged(int msgcount) override;

  // ---- MyMesh::Listener (optional, used by Faro) ----------------------------------
  void onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len,
                           const uint8_t* path) override;
  void onACKRecv(uint32_t ack_crc) override;
  void onTraceRecv(mesh::Packet* pkt, uint32_t tag, uint32_t auth_code, uint8_t flags,
                   const uint8_t* path_snrs, const uint8_t* path_hashes, uint8_t path_len) override;
  void onControlDataRecv(const mesh::Packet* pkt) override;
  void onChannelDataRecv(mesh::Packet* pkt, const mesh::GroupChannel& channel, uint16_t data_type,
                         const uint8_t* data, size_t data_len) override;
  bool onUnhandledResponse(const ContactInfo& from, uint32_t tag, const uint8_t* data,
                           uint8_t len) override;
  void onRawDataRecv(mesh::Packet* pkt) override;

private:
  faro::DeviceController* _ctrl = nullptr;
};
