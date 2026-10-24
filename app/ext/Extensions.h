// SPDX-License-Identifier: Apache-2.0
// app/ext/Extensions.h — Public extension interfaces, null implementations, and registry (ARCHITECTURE §6)
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace faro::ext {

class ITransportGuard {
public:
  virtual ~ITransportGuard() = default;
  virtual bool allowConnection(uint32_t peer_ipv4, const uint8_t* auth_payload, size_t len,
                               uint32_t now_sec) = 0;
};

class NullTransportGuard final : public ITransportGuard {
public:
  bool allowConnection(uint32_t, const uint8_t*, size_t, uint32_t) override { return true; }
};

class IPolicyEngine {
public:
  virtual ~IPolicyEngine() = default;
  virtual bool allowScreen(uint8_t screen_id) const = 0;
  virtual bool allowRadioParams(uint32_t freq_hz, int8_t tx_dbm, uint32_t bw_hz) const = 0;
  virtual bool isRestrictedOperatorMode() const = 0;
};

class NullPolicyEngine final : public IPolicyEngine {
public:
  bool allowScreen(uint8_t) const override { return true; }
  bool allowRadioParams(uint32_t, int8_t, uint32_t) const override { return true; }
  bool isRestrictedOperatorMode() const override { return false; }
};

class IAuditSink {
public:
  virtual ~IAuditSink() = default;
  virtual void recordEvent(const char* category, const char* detail, uint32_t timestamp) = 0;
};

class NullAuditSink final : public IAuditSink {
public:
  void recordEvent(const char*, const char*, uint32_t) override {}
};

class IProvisioner {
public:
  virtual ~IProvisioner() = default;
  virtual bool applySignedBundle(const uint8_t* data, size_t len) = 0;
};

class NullProvisioner final : public IProvisioner {
public:
  bool applySignedBundle(const uint8_t*, size_t) override { return false; }
};

class IGroupDataHandler {
public:
  virtual ~IGroupDataHandler() = default;
  virtual bool onGroupData(uint8_t channel_idx, uint16_t data_type,
                           const uint8_t* payload, size_t len) = 0;
};

class NullGroupDataHandler final : public IGroupDataHandler {
public:
  bool onGroupData(uint8_t, uint16_t, const uint8_t*, size_t) override { return false; }
};

struct ExtensionRegistry {
  ITransportGuard*   transport_guard    = nullptr;
  IPolicyEngine*     policy_engine      = nullptr;
  IAuditSink*        audit_sink         = nullptr;
  IProvisioner*      provisioner        = nullptr;
  IGroupDataHandler* group_data_handler = nullptr;

  static ExtensionRegistry& instance() {
    static NullTransportGuard   s_null_tg;
    static NullPolicyEngine     s_null_pe;
    static NullAuditSink        s_null_as;
    static NullProvisioner      s_null_pr;
    static NullGroupDataHandler s_null_gd;
    static ExtensionRegistry    s_reg{&s_null_tg, &s_null_pe, &s_null_as, &s_null_pr, &s_null_gd};
    return s_reg;
  }
};

}  // namespace faro::ext
