// SPDX-License-Identifier: MIT
/// @file   plugins/handlers/zstd_decompress/zstd_decompress.hpp
/// @brief  Middleware handler that decompresses zstd-wrapped payloads
///         and re-injects them under a plain msg_id.
///
/// Sits at the front of the handler chain (priority 255) for a
/// "compressed" msg_id (default 0x0701). Any peer that wants to send
/// a compressed JSON-RPC frame sends it as msg_id=0x0701; this handler
/// decompresses the payload and re-injects it as msg_id=0x0700 so
/// web_api_proxy (and any other handler on the plain msg_id) receives
/// the uncompressed bytes. No change required in web_api_proxy.
///
/// Composed processing pattern:
///
///   client (compressed)
///     → ws_inject (inject 0x0701 → "gnet-v1")
///       → ZstdDecompressHandler priority=255 (decompress → inject 0x0700)
///         → WebApiProxyHandler priority=100 (see plain JSON-RPC)
///
/// Compression of replies is the sender's concern — the kernel's send
/// path is unaffected; the browser JS SDK compresses before sending.
///
/// Wire contract:
///   msg_id 0x0701 — zstd-compressed frame; payload = raw zstd data
///   msg_id 0x0700 — plain JSON-RPC (web_api_proxy's existing contract)
///
/// Config (TOML / JSON section "zstd_decompress"):
///   compressed_msg_id  uint32  default 0x0701
///   plain_msg_id       uint32  default 0x0700
///   target_ns          string  default "gnet-v1"
///   max_decompressed   uint32  bytes, default 4 MiB

#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include <sdk/handler.h>
#include <sdk/host_api.h>
#include <sdk/types.h>

namespace gn::handler::zstd_decompress {

inline constexpr std::uint32_t kDefaultCompressedMsgId = 0x0701;
inline constexpr std::uint32_t kDefaultPlainMsgId      = 0x0700;
inline constexpr std::uint32_t kDefaultMaxDecompressed  = 4u * 1024u * 1024u;
inline constexpr const char*   kDefaultTargetNs         = "gnet-v1";

struct Config {
    std::uint32_t compressed_msg_id = kDefaultCompressedMsgId;
    std::uint32_t plain_msg_id      = kDefaultPlainMsgId;
    std::string   target_ns         = kDefaultTargetNs;
    std::uint32_t max_decompressed  = kDefaultMaxDecompressed;
};

class ZstdDecompressHandler {
public:
    explicit ZstdDecompressHandler(const host_api_t* api);

    // Handler plugin concept — queried by GN_HANDLER_PLUGIN macro.
    static constexpr const char*    protocol_id() noexcept { return "gnet-v1"; }
    static constexpr std::uint32_t  msg_id()      noexcept { return kDefaultCompressedMsgId; }
    static constexpr std::uint8_t   priority()    noexcept { return 255; }

    gn_propagation_t handle_message(const gn_message_t& envelope);

    // Called by plugin_entry after construction to apply operator config.
    void set_config(const Config& cfg) noexcept { cfg_ = cfg; }
    const Config& config() const noexcept { return cfg_; }

    // Metrics — accessible through the gn.handler.zstd-decompress extension.
    std::uint64_t frames_decompressed() const noexcept {
        return frames_ok_.load(std::memory_order_relaxed);
    }
    std::uint64_t frames_error() const noexcept {
        return frames_err_.load(std::memory_order_relaxed);
    }

private:
    const host_api_t*     api_;
    Config                cfg_;
    std::atomic<uint64_t> frames_ok_{0};
    std::atomic<uint64_t> frames_err_{0};
};

}  // namespace gn::handler::zstd_decompress
