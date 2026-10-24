# stb_image_wuffs

A single-header, near drop-in replacement for [stb_image.h](https://github.com/nothings/stb) v2.30 that keeps stb_image's API and hands **all** decoding to
[Wuffs](https://github.com/google/wuffs), a memory-safe (but still quite fast) image decoder
library. The adapter itself parses no image data.

stb_image is a classic: one file, a tiny API, and it just works. This header
keeps that API unchanged and lets Wuffs do the decoding.

## Why

This alternative implementation of the stb_image API is built on [Wuffs, a
library designed to be hardened against adversarial inputs](https://nigeltao.github.io/blog/2021/fastest-safest-png-decoder.html): its decoders are
written in a language that proves bounds and overflow safety at compile time,
and they are fuzzed continuously. That makes it a good fit wherever images come
from sources you don't control, such as files downloaded from the web or
uploaded by users.

Joining two image APIs robustly is non-trivial in its own right. The adapter
has to reproduce stb_image's behavior (channel counts and conversions, 16-bit
and float output, vertical flip, FILE and callback I/O semantics, animated GIF
output, failure reasons) on top of Wuffs' streaming decoders, while handling
the quirks of both libraries. It's checked by a differential test against the
real stb_image, sanitizer and thread-safety stress tests, and builds with
MSVC, gcc, clang and tcc, 32-bit and 64-bit.

## Use

```c
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image_wuffs.h"      // was: #include "stb_image.h"

int w, h, n;
unsigned char *px = stbi_load("photo.jpg", &w, &h, &n, 4);
// .. use the image data ..
stbi_image_free(px);
```

Put `wuffs-v0.4.c` next to the header (or `#define STBI_WUFFS_SOURCE`). Those
two files are all you need.

Tested with `wuffs-v0.4.c` 0.4.0-alpha.12+4048.20261020, unmodified. 

## Speed

It's quite similar for .JPEG (0.77x-1.07x vs. stb_image.h), and for .PNG it's 1.4x-1.7x faster (Ryzen 9 9950X, gcc and clang).

## Formats

| | stb_image | stb_image_wuffs |
|---|---|---|
| PNG (incl. APNG frames), JPEG, GIF (inc. animated), BMP, TGA, binary PNM | yes | yes |
| WebP (still), QOI, ETC2 | no | yes |
| PSD, HDR, PIC | yes | no |

The most interesting/useful still unsupported format is .HDR, which I hope to tackle next.

## Documentation

See the comment block at the top of [stb_image_wuffs.h](https://github.com/richgel999/stb_image_wuffs/blob/main/stb_image_wuffs.h):
configuration macros, the (few) differences from stb_image and why, thread safety,
the float conversion options, sRGB to linear conversion options, proper 16-bit/component image format conversion to floating point support, and the known Wuffs v0.4 limitations.

## License

Wuffs (`wuffs-v0.4.c`): Apache-2.0 or MIT; see `LICENSE-WUFFS-APACHE` and
`LICENSE-WUFFS-MIT`.

stb_image_wuffs.h: the adapter header file library uses the same license as stb_image: your choice of MIT (Copyright (c) 2017 Sean Barrett) or
public domain ([Unlicense](https://unlicense.org)); see the end of
`stb_image_wuffs.h`. Importantly, stb_image_wuffs.h is derived from the original stb_image.h, so its dual license is unchanged, but the actual implementation is completely different. This means any bugs or issues in stb_image_wuffs.h have nothing to do with the original stb_image.h. This project is independent and not affiliated with the stb authors.
