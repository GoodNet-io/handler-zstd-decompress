# goodnet-handler-zstd-decompress

Middleware handler that decompresses ZSTD-wrapped payloads and
re-injects them under the configured plain `msg_id`. Sits at the
front of the handler chain (priority 255) for a "compressed"
`msg_id` (default `0x0701`); the decompressed result is injected
under `plain_msg_id` (default `0x0700`) so downstream handlers
receive uncompressed bytes without modification.

**Kind**: handler · **License**: MIT (see `LICENSE`)

Also registers as the `gn.compress` extension (`sdk/extensions/compress.h`)
so other plugins can call `compress` / `decompress` / `compress_bound`
without a direct libzstd dependency.

## Wire contract

Two modes controlled by `encode_target_msg_id`:

**`fixed` mode** (default):

```
GNET payload (outer msg_id = 0x0701):
  [ raw ZSTD frame ]
```

Decompressed bytes are injected under `plain_msg_id`.

**`inband` mode** (see `docs/contracts/compressed-object.en.md`):

```
GNET payload (outer msg_id = 0x0701):
  [0]     algo byte  (0x01 = ZSTD)
  [1..4]  target_msg_id  big-endian uint32
  [5..]   ZSTD-compressed application bytes
```

The 5-byte header is parsed from the raw payload before decompression.
The decompressed result is injected under the extracted `target_msg_id`,
allowing a single compressed stream to multiplex multiple message types.

## Composed processing pattern

```
sender (compressed, msg_id=0x0701)
  → ZstdDecompressHandler  priority=255  (decompress → inject 0x0700)
    → downstream handler   priority=100  (sees plain bytes)
```

## Config keys

| Key                              | Type   | Default      |
| -------------------------------- | ------ | ------------ |
| `zstd_decompress.compressed_msg_id` | uint32 | `0x0701`  |
| `zstd_decompress.plain_msg_id`      | uint32 | `0x0700`  |
| `zstd_decompress.target_ns`         | string | `gnet-v1` |
| `zstd_decompress.max_decompressed`  | uint32 | `4194304` (4 MiB) |
| `zstd_decompress.encode_target_msg_id` | string | `fixed`  |

`plain_msg_id` is operator-configurable to any non-reserved value
(see `docs/contracts/system-handlers.en.md` for the reserved `0x10..0x1F` range).

## gn.compress extension

Other plugins query the extension after the handler is loaded:

```c
gn_compress_api_t api = {};
gn_result_t rc = query_extension_checked(host_api, GN_COMPRESS_EXT,
                     GN_COMPRESS_API_VERSION, &api);
if (rc == GN_OK) {
    uint8_t out[api.compress_bound(api.ctx, in_sz)];
    size_t  out_sz = 0;
    api.compress(api.ctx, in, in_sz, out, sizeof(out), &out_sz, 3);
}
```

Returns `GN_ERR_OUTPUT_TOO_SMALL` when `out_cap` is insufficient;
caller may retry with a larger buffer.

## Dependencies

- **libzstd** — linked at build time via `pkg-config libzstd`.
