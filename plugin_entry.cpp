// SPDX-License-Identifier: MIT
#include <sdk/cpp/handler_plugin.hpp>
#include "zstd_decompress.hpp"

GN_HANDLER_PLUGIN(
    ::gn::handler::zstd_decompress::ZstdDecompressHandler,
    "goodnet_handler_zstd_decompress",
    "0.1.0")
