# stb_image_wuffs

A single-header, drop-in replacement for [stb_image.h](https://github.com/nothings/stb)
that keeps stb_image's API and hands **all** decoding to
[Wuffs](https://github.com/google/wuffs), Google's very fast and memory-safe image decoder
library. The adapter itself parses no image data.

stb_image is a classic: one file, a tiny API, and it just works. This header
keeps that API unchanged and lets Wuffs do the decoding: its decoders are written in a
language that proves bounds and overflow safety at compile time, and are
fuzzed continuously.

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
| PNG (incl. APNG frames), JPEG, GIF, BMP, TGA, binary PNM | yes | yes |
| WebP (still), QOI, ETC2 | no | yes |
| PSD, HDR, PIC | yes | no |

Adding .HDR to Wuffs is planned next.

## Documentation

Everything else is in the comment block at the top of `stb_image_wuffs.h`:
configuration macros, the (few) differences from stb_image and why, thread safety,
the float conversion options, sRGB to linear conversion options, proper 16-bit/component image format conversion to floating point support, and the known Wuffs v0.4 limitations.

## License

Wuffs itself has its own license: Apache-2.0 or MIT.

stb_image_wuffs.h is derived from stb_image.h, so its dual license is unchanged, but the implementation is completely different. This means any bugs in stb_image_wuffs.h have nothing to do with the original stb_image.h.

The license is the same as stb_image: your choice of MIT (Copyright (c) 2017 Sean Barrett) or
public domain ([Unlicense](https://unlicense.org)); see the end of
`stb_image_wuffs.h`.

