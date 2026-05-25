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
    : api_(api) {}

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

    const char* ns = cfg_.target_ns.empty() ? nullptr : cfg_.target_ns.c_str();
    const gn_result_t rc =
        api_->inject(api_->host_ctx,
                     GN_INJECT_LAYER_MESSAGE,
                     env.conn_id,
                     ns,
                     cfg_.plain_msg_id,
                     dst.data(),
                     dst.size());

    if (rc != GN_OK) {
        GN_LOGF_WARN(api_, "zstd_decompress: inject failed rc={}", rc);
        frames_err_.fetch_add(1, std::memory_order_relaxed);
    } else {
        frames_ok_.fetch_add(1, std::memory_order_relaxed);
    }
    return GN_PROPAGATION_CONSUMED;
}

}  // namespace gn::handler::zstd_decompress
