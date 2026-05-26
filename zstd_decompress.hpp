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
///   msg_id 0x0701 — zstd-compressed frame; payload is one of:
///     "fixed"  mode: raw ZSTD bytes (target routed to plain_msg_id)
///     "inband" mode: [1B algo=0x01][4B target_msg_id BE][ZSTD bytes]
///              See docs/contracts/compressed-object.en.md §2.
///   msg_id 0x0700 — default plain target.  Chosen to match common
///                   web-api-proxy deployments but the handler is
///                   application-agnostic; plain_msg_id is operator-
///                   configurable to any non-reserved value.
///
/// Config (TOML / JSON section "zstd_decompress"):
///   compressed_msg_id    uint32  default 0x0701
///   plain_msg_id         uint32  default 0x0700  (used only in "fixed" mode)
///   target_ns            string  default "gnet-v1"
///   max_decompressed     uint32  bytes, default 4 MiB
///   encode_target_msg_id string  "fixed" (default) or "inband"

#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include <sdk/extensions/compress.h>
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
    std::uint32_t plain_msg_id      = kDefaultPlainMsgId;  ///< target msg_id for
                                                            ///< decompressed payload
                                                            ///< in "fixed" mode; pick
                                                            ///< any non-reserved value
                                                            ///< (see system-handlers.en.md
                                                            ///< for the reserved range).
    std::string   target_ns         = kDefaultTargetNs;
    std::uint32_t max_decompressed  = kDefaultMaxDecompressed;
    /// "fixed"  — always inject decompressed payload under plain_msg_id.
    /// "inband" — raw payload carries a 5-byte header before the ZSTD data:
    ///            [0]=algo byte (0x01=ZSTD) [1..4]=target msg_id BE.
    ///            Header is read from the raw GNET payload, NOT from the
    ///            decompressed bytes.  See compressed-object.en.md §2.
    std::string   encode_target_msg_id = "fixed";
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

    // gn.compress extension surface — registered by GN_HANDLER_PLUGIN via
    // maybe_register_extension when the concept is satisfied.
    static constexpr const char*    extension_name()    noexcept { return GN_COMPRESS_EXT; }
    static constexpr std::uint32_t  extension_version() noexcept { return GN_COMPRESS_API_VERSION; }
    const void* extension_vtable() const noexcept { return &compress_vtable_; }

private:
    const host_api_t*     api_;
    Config                cfg_;
    std::atomic<uint64_t> frames_ok_{0};
    std::atomic<uint64_t> frames_err_{0};
    gn_compress_api_t     compress_vtable_{};
};

}  // namespace gn::handler::zstd_decompress
