// SPDX-License-Identifier: MIT
/// @file   plugins/handlers/zstd_decompress/zstd_decompress.cpp

#include "zstd_decompress.hpp"

#include <cstring>
#include <vector>

#include <zstd.h>

#include <sdk/cpp/log.hpp>
#include <sdk/types.h>

namespace gn::handler::zstd_decompress {

ZstdDecompressHandler::ZstdDecompressHandler(const host_api_t* api)
    : api_(api) {

    // Initialize gn.compress extension vtable.
    compress_vtable_.api_size = sizeof(gn_compress_api_t);
    compress_vtable_.ctx      = this;

    compress_vtable_.compress = [](void* /*ctx*/,
                                   const uint8_t* in,
                                   size_t         in_sz,
                                   uint8_t*       out,
                                   size_t         out_cap,
                                   size_t*        out_sz,
                                   int            level) -> gn_result_t {
        const std::size_t bound = ZSTD_compressBound(in_sz);
        if (out_cap < bound) return GN_ERR_PAYLOAD_TOO_LARGE;
        const std::size_t r = ZSTD_compress(out, out_cap, in, in_sz, level);
        if (ZSTD_isError(r)) return GN_ERR_INVALID_ENVELOPE;
        *out_sz = r;
        return GN_OK;
    };

    compress_vtable_.compress_bound = [](void* /*ctx*/, size_t in_sz) -> size_t {
        return ZSTD_compressBound(in_sz);
    };

    compress_vtable_.decompress = [](void* /*ctx*/,
                                     const uint8_t* in,
                                     size_t         in_sz,
                                     uint8_t*       out,
                                     size_t         out_cap,
                                     size_t*        out_sz) -> gn_result_t {
        const unsigned long long expected = ZSTD_getFrameContentSize(in, in_sz);
        if (expected == ZSTD_CONTENTSIZE_ERROR) return GN_ERR_INVALID_ENVELOPE;
        if (expected != ZSTD_CONTENTSIZE_UNKNOWN && expected > out_cap)
            return GN_ERR_PAYLOAD_TOO_LARGE;
        const std::size_t r = ZSTD_decompress(out, out_cap, in, in_sz);
        if (ZSTD_isError(r)) return GN_ERR_INVALID_ENVELOPE;
        *out_sz = r;
        return GN_OK;
    };
}

gn_propagation_t ZstdDecompressHandler::handle_message(
    const gn_message_t& env) {

    const std::uint8_t* src  = env.payload;
    const std::size_t   srcN = env.payload_size;

    if (src == nullptr || srcN == 0) {
        frames_err_.fetch_add(1, std::memory_order_relaxed);
        return GN_PROPAGATION_CONSUMED;
    }

    const std::uint32_t cap = cfg_.max_decompressed;

    // Known-size path: content size stored in frame header.
    const unsigned long long content_size =
        ZSTD_getFrameContentSize(src, srcN);

    std::vector<std::uint8_t> dst;

    if (content_size == ZSTD_CONTENTSIZE_ERROR) {
        GN_LOGF_WARN(api_, "zstd_decompress: invalid frame on conn {}", env.conn_id);
        frames_err_.fetch_add(1, std::memory_order_relaxed);
        return GN_PROPAGATION_CONSUMED;
    }

    if (content_size != ZSTD_CONTENTSIZE_UNKNOWN) {
        if (content_size > cap) {
            GN_LOGF_WARN(api_, "zstd_decompress: frame too large ({} > {})",
                         content_size, cap);
            frames_err_.fetch_add(1, std::memory_order_relaxed);
            return GN_PROPAGATION_CONSUMED;
        }
        dst.resize(static_cast<std::size_t>(content_size));
        const std::size_t r =
            ZSTD_decompress(dst.data(), dst.size(), src, srcN);
        if (ZSTD_isError(r)) {
            GN_LOGF_WARN(api_, "zstd_decompress: error: {}", ZSTD_getErrorName(r));
            frames_err_.fetch_add(1, std::memory_order_relaxed);
            return GN_PROPAGATION_CONSUMED;
        }
        dst.resize(r);
    } else {
        // Streaming frame — decompress in chunks into a growing buffer.
        ZSTD_DStream* const ds = ZSTD_createDStream();
        if (!ds) {
            frames_err_.fetch_add(1, std::memory_order_relaxed);
            return GN_PROPAGATION_CONSUMED;
        }
        ZSTD_initDStream(ds);

        ZSTD_inBuffer in{src, srcN, 0};
        dst.resize(ZSTD_DStreamOutSize());
        std::size_t written = 0;
        bool error = false;

        while (in.pos < in.size) {
            const std::size_t chunk = ZSTD_DStreamOutSize();
            if (written + chunk > cap) {
                GN_LOGF_WARN(api_, "zstd_decompress: streaming frame exceeds cap {}",
                             cap);
                error = true;
                break;
            }
            if (written + chunk > dst.size()) {
                dst.resize(written + chunk);
            }
            ZSTD_outBuffer out{dst.data() + written, chunk, 0};
            const std::size_t r = ZSTD_decompressStream(ds, &out, &in);
            if (ZSTD_isError(r)) {
                GN_LOGF_WARN(api_, "zstd_decompress: stream error: {}",
                             ZSTD_getErrorName(r));
                error = true;
                break;
            }
            written += out.pos;
        }
        ZSTD_freeDStream(ds);

        if (error) {
            frames_err_.fetch_add(1, std::memory_order_relaxed);
            return GN_PROPAGATION_CONSUMED;
        }
        dst.resize(written);
    }

    // Resolve inject parameters — may be overridden by inband routing.
    std::uint32_t        target_msg_id = cfg_.plain_msg_id;
    const std::uint8_t*  inject_data   = dst.data();
    std::size_t          inject_size   = dst.size();

    if (cfg_.encode_target_msg_id == "inband") {
        if (dst.size() < 4) {
            GN_LOGF_WARN(api_, "zstd_decompress: inband: decompressed payload too short ({} bytes)",
                         dst.size());
            frames_err_.fetch_add(1, std::memory_order_relaxed);
            return GN_PROPAGATION_CONSUMED;
        }
        target_msg_id = (static_cast<std::uint32_t>(dst[0]) << 24) |
                        (static_cast<std::uint32_t>(dst[1]) << 16) |
                        (static_cast<std::uint32_t>(dst[2]) << 8)  |
                         static_cast<std::uint32_t>(dst[3]);
        inject_data = dst.data() + 4;
        inject_size = dst.size() - 4;
    }

    const char* ns = cfg_.target_ns.empty() ? nullptr : cfg_.target_ns.c_str();
    const gn_result_t rc =
        api_->inject(api_->host_ctx,
                     GN_INJECT_LAYER_MESSAGE,
                     env.conn_id,
                     ns,
                     target_msg_id,
                     inject_data,
                     inject_size);

    if (rc != GN_OK) {
        GN_LOGF_WARN(api_, "zstd_decompress: inject failed rc={}", rc);
        frames_err_.fetch_add(1, std::memory_order_relaxed);
    } else {
        frames_ok_.fetch_add(1, std::memory_order_relaxed);
    }
    return GN_PROPAGATION_CONSUMED;
}

}  // namespace gn::handler::zstd_decompress
