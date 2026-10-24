/* stb_image_wuffs - v0.2 - public domain image loader API adapter - stb_image API, Wuffs inside
   No warranty implied; use at your own risk.

   A drop-in replacement for stb_image.h (v2.30). It exposes the same
   public API, macros and configuration switches, but does ZERO image parsing
   itself: every byte of every file is decoded by Wuffs
   (https://github.com/google/wuffs), a memory-safe, fuzzed, formally
   bounds-checked decoder library. This file only adapts Wuffs' C API to the
   stb_image API: format sniffing, I/O plumbing, channel/bit-depth conversion,
   vertical flipping, GIF/animation compositing and the zlib helpers.

   This is a single-header library. To create the implementation, define
   this macro in exactly one C or C++ file, before including this header:
     #define STB_IMAGE_IMPLEMENTATION

   The implementation #includes the Wuffs single-file library, by default as
   "wuffs-v0.4.c" (so put that file next to this one, or on your include
   path). Override with:
      #define STBI_WUFFS_SOURCE "path/to/wuffs-v0.4.c"
   If you already compile Wuffs yourself (non-static, with at least the modules
   listed under "Wuffs configuration" below) and have #included its header
   before this file, define STBI_WUFFS_EXTERNAL and this file won't include it.

   Switching from stb_image: replace `#include "stb_image.h"` with
   `#include "stb_image_wuffs.h"` (or rename this file to stb_image.h). The
   functions, types, enums and macros are the same (plus a few additions,
   listed under ADDITIONAL CONFIGURATION). Both use the include guard
   STBI_INCLUDE_STB_IMAGE_H, so they are mutually exclusive in a translation
   unit, as intended.

   #define STBI_ASSERT(x) before the #include to replace the default assert()
   (and avoid including <assert.h>). It's accepted for compatibility: this
   implementation never asserts on its inputs; it returns NULL with a
   failure reason instead.
   #define STBI_MALLOC, STBI_REALLOC and STBI_FREE (or STBI_REALLOC_SIZED in
   place of STBI_REALLOC) to replace the C allocation functions malloc,
   realloc and free. *All* heap allocations, including the Wuffs decoder
   state and work buffers, go through these macros.


   Notes:
      Formats:

      PNG   1/2/4/8/16-bit-per-channel, paletted, interlaced, APNG (first frame,
            or all frames via stbi_load_gif_from_memory unless STBI_NO_GIF)
      JPEG  baseline, extended-sequential & progressive, 8-bit,
            grey/YCbCr/CMYK/YCCK
      GIF   animated; *comp is always 4, as in stb_image
      BMP   1/2/4/8/16/24/32-bit, RLE, bitfields, embedded PNG/JPEG
      TGA   (Truevision TGA / TARGA; detected from its header, as in
            stb_image, since the format has no magic number)
      PNM   binary PGM (P5) and PPM (P6), 8/16-bit, as in stb_image (no PBM,
            no ASCII P1-P3)
      WebP  lossy and lossless, still images           (new, not in stb_image)
            (Wuffs v0.4 bug: a lossless WebP in the extended VP8X container,
            which is what encoders write when it carries EXIF/ICC/XMP
            metadata, is rejected. Another Wuffs v0.4 bug corrupts alpha
            unless the whole file is in memory, so callback and FILE input
            read the entire WebP before decoding it; see "I/O callbacks")
      QOI   Quite OK Image format                      (new, not in stb_image)
      ETC2  ETC2 compressed textures (PKM container)   (new, not in stb_image)

      Left out on purpose although Wuffs decodes them: WBMP and Handsum (no
      or only a 2-byte magic number, so random binary data could be
      "detected" as an image, breaking code that uses stbi_info() to ask
      "is this an image?"), and NIE and ThumbHash (niche Wuffs-ecosystem
      formats).

      NOT supported (Wuffs has no decoder for them): PSD, HDR (Radiance),
      PIC (Softimage). Loading them fails with "unknown image type", and
      stbi_is_hdr() always returns 0. iPhone "CgBI" PNGs are not supported.

   Supported:
      - decode from memory or through FILE (define STBI_NO_STDIO to remove code)
      - decode from arbitrary I/O callbacks
      - SIMD acceleration comes from Wuffs itself. With gcc, clang and
        clang-cl, Wuffs picks SSE4.2/AVX2 code at run time on x86_64. With
        MSVC's cl.exe it only uses SIMD when you compile with /arch:AVX2 (or
        define WUFFS_CONFIG__ENABLE_MSVC_CPU_ARCH__X86_64_V2 or _V3);
        otherwise it uses scalar code and prints a one-line #pragma message
        suggesting /arch:AVX2. STBI_NO_SIMD (WUFFS_CONFIG__AVOID_CPU_ARCH)
        turns Wuffs' SIMD off everywhere and silences that message (when
        this file compiles Wuffs; ignored with STBI_WUFFS_EXTERNAL).
      - Sanitizer clean: Wuffs is called only through each format's own
        functions, never its generic wuffs_base__image_decoder interface,
        whose function-pointer dispatch is undefined behavior that clang's
        -fsanitize=function (part of -fsanitize=undefined) and CFI reject.
        Wuffs' own warnings are silenced around its #include.

      This adapter never parses image bytes. The only places it looks inside
      Wuffs are two already-parsed decoder fields, read to report the same
      channel count stb_image does (Wuffs' public pixel format collapses e.g.
      16-bit grey and 16-bit RGB together): the PNG decoder's IHDR color type
      and tRNS state, and the BMP decoder's alpha channel mask.


   DIFFERENCES FROM stb_image (where behavior differs, and why):

      - Decoded pixels are produced by Wuffs, so they can differ slightly from
        stb_image's for lossy formats: JPEG RGB is typically within 2-3
        levels (IDCT/upsampling rounding). Grey output (desired_channels 1
        or 2) from a color JPEG is computed from the decoded RGB, whereas
        stb_image returns the raw Y plane; these differ by a few levels, more
        where RGB clips. Lossless formats decode to identical values, except
        as listed below.
      - Per-format differences, where Wuffs follows the format spec:
          BMP: a 32-bit BI_RGB file reports 3 channels and its 4th byte is
            ignored (it isn't alpha in BI_RGB). stb_image reports 4 and uses
            that byte as alpha when it isn't all zero. Bitfield BMPs with an
            alpha mask report 4 in both.
          TGA: 15/16-bit files report 4 channels, with alpha taken from the
            attribute bit, and 5-bit values widen as (v<<3)|(v>>2).
            stb_image reports 3 and widens as v*255/31 (differs by 1 at most).
          PNM: PGM/PPM files with maxval < 255 are rejected ("unsupported
            Netpbm file"); stb_image loads them unscaled. 16-bit PNM values
            are read as big-endian, as the format specifies; stb_image 2.30
            returns them byte-swapped on little-endian machines.
      - stbi_load_gif_from_memory() accepts any format Wuffs decodes, not
        only GIF: animated PNG (APNG) yields all its frames, still images
        yield one frame. (Wuffs v0.4 does not decode animated WebP.) Like
        stb_image's, the function doesn't exist under STBI_NO_GIF.
        Disposal and blending follow the GIF/APNG specs, so some disposal
        corner cases composite differently from stb_image.
      - Two things Wuffs accepts, as browsers and libjpeg-turbo do, where
        stb_image rejects the file: a GIF frame that extends past the
        canvas is clipped to it, and a JPEG comment marker with length 0 is
        treated as an empty comment.
      - PNG tRNS color-key pixels (the non-palette kind) decode as
        transparent black (0,0,0,0); stb_image keeps their RGB under alpha 0.
        This only shows if you request 1 or 3 channels from such a file or
        ignore alpha.
      - For an APNG whose default (IDAT) image is not part of the animation,
        stbi_load() returns the first animation frame rather than the IDAT
        fallback image.
      - Damaged files that stb_image decodes partially or accepts (a GIF cut
        off mid-frame, GIF frames with too much or no pixel data, bad
        checksums) are rejected; the one exception is a JPEG that ends
        early, which loads (see "Invalid files" below). Some valid files
        that stb_image doesn't support (RLE BMPs, GIFs without a leading LZW
        clear code...) load.
      - stbi_load_gif_from_memory() with vertical flip and req_comp 1..3
        flips each frame using its actual channel count (see the known
        stb_image issue CVE-2023-45662, nothings/stb#1540).
      - Output for a truncated progressive JPEG is deterministic. (stb_image
        2.30 can read uninitialized memory there, so its output may vary.)
      - If x, y, comp (or channels_in_file) are NULL they are simply not
        written (stb_image expects them to be valid pointers).
      - Images whose decoded size exceeds INT_MAX bytes are rejected up front,
        before decoding (stb_image has the same limit).
      - stbi_set_unpremultiply_on_load() and stbi_convert_iphone_png_to_rgb()
        (and their _thread variants) have no effect: Wuffs always returns
        non-premultiplied alpha and doesn't decode CgBI PNGs. Calling either
        with a true flag prints a one-line warning to stderr (nothing is
        printed under STBI_NO_STDIO).
      - Invalid files are rejected. PNG CRCs and zlib Adler-32 checksums
        are verified (stb_image skips these checks), and truncated or malformed
        files fail instead of returning a partial image.
        One exception, matching libjpeg and stb_image: a JPEG whose data
        ends early (after a valid header, inside the compressed pixel data)
        loads, with exactly the pixels Wuffs produced (Wuffs itself fills the
        part the data never reached). Any other JPEG error, and truncation of
        any other format, still fails.
      - stbi_loadf() converts 16-bit files (those stbi_is_16_bit() reports)
        from their full 16-bit values; stb_image goes through 8 bits first,
        so its results differ by up to about 1/255. 8-bit files give the
        same floats as stb_image. #define STBI_LOADF_VIA_8BIT for stb_image's
        behavior.
      - stbi_load_from_file() (and the _16 and loadf variants) leave the
        FILE just past the whole image: for an animated GIF that is after the
        last frame and the ';' trailer (stb_image stops after the first
        frame, and its stbi_loadf_from_file doesn't return unread bytes).
      - zlib functions: the Adler-32 trailer must be present and correct, and
        an initial_size <= 0 is handled (stb_image expects a positive size).
      - I/O callbacks: each read() asks for up to 128 bytes, the size of
        stb_image's own refill buffer. Callbacks can't give bytes back, so
        this keeps the stream from being read more than 128 bytes past the
        end of the image, which matters when images sit back to back in one
        stream. (WebP is the exception: it is read to the end of input; see
        "I/O callbacks" below.) FILE input from disk reads 64 KiB at a time
        and seeks back over what it didn't use. eof() may be NULL; read()
        returning 0 then marks the end of input. skip() is only used to
        jump to an image embedded in a BMP. NULL callbacks or a NULL read()
        fail with "bad callbacks".
      - desired_channels / req_comp outside 0..4 always fails ("bad
        req_comp") (stb_image checks this per format).
      - stbi_failure_reason() is only set when a call fails, and then names
        the actual cause (for stbi_loadf too). With STBI_FAILURE_USERMSG,
        every decoder error reads "Corrupt image or unsupported format".
      - stbi_is_16_bit() also reports BMPs with more than 8 bits per channel
        (stb_image only ever reports PNG, PSD and PNM), and stbi_load_16
        returns their full precision.
      - stbi_load_gif_from_memory(): *comp is always 4, for every input
        format (the buffer has 4 channels unless req_comp says otherwise). A
        GIF frame without its own Graphic Control Extension gets delay 0 as
        the GIF spec says (stb_image repeats the previous frame's delay). A
        GIF with no frames fails with "no frames" and leaves x, y, z and
        comp unwritten; *delays is set to NULL at entry, so it is NULL
        after any failure.


   Full documentation under "DOCUMENTATION" below.


LICENSE

  See end of file for license information (the exact same dual MIT / public
  domain license as stb_image.h). Wuffs, which this file #includes and
  compiles, has its own license: Apache-2.0 OR MIT.

 ============================    Contributors    =========================

   Built on the work of Sean Barrett (http://nothings.org/stb) and the
   stb_image contributors (API design and documentation), and of Nigel Tao
   and the Wuffs authors (all of the actual decoding). The adapter itself
   was written by Claude (Opus 5.5).
   
   Note this implementation is totally different from the original stb_image.h, 
   so any bugs are not the fault of the original stb_image.h, but of this 
   adapter or Wuffs.
*/

#ifndef STBI_INCLUDE_STB_IMAGE_H
#define STBI_INCLUDE_STB_IMAGE_H

// DOCUMENTATION
//
// Limitations:
//    - JPEG (Wuffs v0.4): baseline, extended-sequential (8-bit) and
//      progressive only.
//      Rejected: 12/16-bit precision, arithmetic coding, lossless (SOF3),
//      hierarchical (SOF5-7), height defined by a DNL marker, fractional
//      chroma sampling.
//    - GIF always returns *comp=4
//    - no PSD, HDR or PIC
//
// Basic usage (see "HDR / float interface" below for float output):
//    int x,y,n;
//    unsigned char *data = stbi_load(filename, &x, &y, &n, 0);
//    // ... process data if not NULL ...
//    // ... x = width, y = height, n = # 8-bit components per pixel ...
//    // ... replace '0' with '1'..'4' to force that many components per pixel
//    // ... but 'n' will always be the number that it would have been if you said 0
//    stbi_image_free(data);
//
// Standard parameters:
//    int *x                 -- outputs image width in pixels
//    int *y                 -- outputs image height in pixels
//    int *channels_in_file  -- outputs # of image components in image file
//    int desired_channels   -- if non-zero, # of image components requested in result
//
// The return value from an image loader is an 'unsigned char *' which points
// to the pixel data, or NULL on an allocation failure or if the image is
// corrupt or invalid. The pixel data consists of *y scanlines of *x pixels,
// with each pixel consisting of N interleaved 8-bit components; the first
// pixel pointed to is top-left-most in the image. There is no padding between
// image scanlines or between pixels, regardless of format. The number of
// components N is 'desired_channels' if desired_channels is non-zero, or
// *channels_in_file otherwise. If desired_channels is non-zero,
// *channels_in_file has the number of components that _would_ have been
// output otherwise. E.g. if you set desired_channels to 4, you will always
// get RGBA output, but you can check *channels_in_file to see if it's trivially
// opaque because e.g. there were only 3 channels in the source image.
//
// An output image with N components has the following components interleaved
// in this order in each pixel:
//
//     N=#comp     components
//       1           grey
//       2           grey, alpha
//       3           red, green, blue
//       4           red, green, blue, alpha
//
// If image loading fails for any reason, the return value will be NULL,
// and *x, *y, *channels_in_file will be unchanged. The function
// stbi_failure_reason() can be queried for an extremely brief, end-user
// unfriendly explanation of why the load failed. Define STBI_NO_FAILURE_STRINGS
// to avoid compiling these strings at all, and STBI_FAILURE_USERMSG to get slightly
// more user-friendly ones. Failure strings from Wuffs look like "png: bad header".
//
// Paletted PNG, BMP, GIF and TGA images are automatically depalettized.
//
// To query the width, height and component count of an image without having to
// decode the full file, you can use the stbi_info family of functions:
//
//   int x,y,n,ok;
//   ok = stbi_info(filename, &x, &y, &n);
//   // returns ok=1 and sets x, y, n if image is a supported format,
//   // 0 otherwise.
//
// Note that stb_image pervasively uses ints in its public API for sizes,
// including sizes of memory buffers. This is now part of the API and thus
// hard to change without causing breakage. As a result, decoded images
// larger than INT_MAX bytes are rejected.
//
// Additionally, images that have any of their dimensions set to a larger
// value than the configurable STBI_MAX_DIMENSIONS (default 2**24 = 16777216)
// are rejected. #define STBI_MAX_DIMENSIONS on your own to change it.
//
// ===========================================================================
//
// UNICODE:
//
//   If compiling for Windows and you wish to use Unicode filenames, compile
//   with
//       #define STBI_WINDOWS_UTF8
//   and pass utf8-encoded filenames. Call stbi_convert_wchar_to_utf8 to convert
//   Windows wchar_t filenames to utf8.
//
// ===========================================================================
//
// Thread safety
//
// Any number of threads may load images concurrently: every call owns its
// decoder, buffers and I/O state, Wuffs has no global state, and
// stbi_failure_reason() is per-thread (when the compiler supports thread-locals
// and STBI_NO_THREAD_LOCALS isn't defined).
//
// The only shared state is what stb_image's API defines as process-wide:
// stbi_set_flip_vertically_on_load() and stbi_ldr_to_hdr_gamma/scale().
// Set those before starting threads; changing them while another thread is
// loading is a data race, exactly as in stb_image. For per-thread flipping use
// stbi_set_flip_vertically_on_load_thread(). Never share one FILE* or one
// callback object between concurrent loads. STBI_MALLOC/STBI_FREE must be
// thread-safe if you replace them.
//
// ===========================================================================
//
// I/O callbacks
//
// I/O callbacks allow you to read from arbitrary sources, like packaged
// files or some other source. Data read from callbacks are processed
// through an internal buffer (initially 64KB, grown on demand).
//
// The callbacks are "read" (reads some bytes of data), "skip" (skips some
// bytes of data) and "eof" (reports if the stream is at the end). Only
// "read" is required here: "eof" may be NULL (read() returning 0 then marks
// the end), and "skip" is only used to jump over data when a BMP file embeds
// a PNG or JPEG (with a NULL skip, such a BMP fails to load if its embedded
// image starts beyond the data already read).
//
// For stbi_load_from_file(), the file pointer is left pointing immediately
// after the image, as with stb_image, so several images stored back to back
// in one file can be loaded one after another. (Exception: after a lossless
// WebP whose last chunk has odd length, it is left before the 1 pad byte.)
//
// WebP from callbacks or a FILE is read to the end of input before it is
// decoded, so it takes as much memory as the file (at most 256 MiB; larger
// input fails with "read buffer too large"). This works around a Wuffs v0.4
// bug: a lossy WebP whose alpha is stored uncompressed got uninitialized
// memory in its alpha channel unless the whole alpha plane arrived in one
// read. A seekable FILE is still left just after the image; a pipe, or
// callback input, is consumed to its end.
//
// ===========================================================================
//
// HDR / float interface
//
// There is no HDR decoder. The float interface still works: LDR images are
// promoted to floating point values, run through the inverse of these
// constants (both default to the stb_image values). 16-bit files are
// converted from their 16-bit values (stb_image uses only the top 8 bits;
// #define STBI_LOADF_VIA_8BIT to do the same):
//
//     stbi_ldr_to_hdr_scale(1.0f);
//     stbi_ldr_to_hdr_gamma(2.2f);
//
//    float *data = stbi_loadf(filename, &x, &y, &n, 0);
//
// stb_image's pow(x, 2.2) is only an approximation of sRGB: it is darkest
// near black, where the real sRGB curve is a straight line. Define
// STBI_LDR_TO_HDR_SRGB to use the exact sRGB decoding function (IEC
// 61966-2-1) for the color channels instead. It is off by default, so
// results match stb_image.
//
// The gamma set with stbi_ldr_to_hdr_gamma() always decides the curve:
//
//     1.0         no curve: v/max, times the scale
//     2.2         (the default) pow(v/max, 2.2), or the exact sRGB curve
//                 with STBI_LDR_TO_HDR_SRGB
//     any other   pow(v/max, gamma)
//
// The scale set with stbi_ldr_to_hdr_scale() multiplies each value after it
// has been converted to linear. Alpha is never curved or scaled: it is
// v/max, as in stb_image. (max is 255, or 65535 for 16-bit files.)
//
// stbi_hdr_to_ldr_gamma()/stbi_hdr_to_ldr_scale() exist for API
// compatibility and have no effect. stbi_is_hdr() always returns 0.
//
// ===========================================================================
//
// Animated images
//
//    int *delays; int x, y, frames, comp;
//    stbi_uc *p = stbi_load_gif_from_memory(buf, len, &delays, &x, &y, &frames, &comp, 4);
//    // p holds 'frames' fully composited x*y images back to back;
//    // delays[i] is frame i's duration in milliseconds. Free both with
//    // stbi_image_free (delays may be passed as NULL if you don't want them).
//
// ===========================================================================
//
// ADDITIONAL CONFIGURATION
//
//  - You can suppress any of the decoders to reduce your code footprint by
//    #defining one or more of the following symbols before creating the
//    implementation (the matching Wuffs modules are then compiled out, when
//    this file compiles Wuffs):
//
//        STBI_NO_JPEG
//        STBI_NO_PNG
//        STBI_NO_BMP
//        STBI_NO_TGA
//        STBI_NO_GIF
//        STBI_NO_PNM       (.pgm and .ppm)
//        STBI_NO_PSD, STBI_NO_HDR, STBI_NO_PIC   (accepted; nothing to remove)
//
//    and, for the formats stb_image doesn't have:
//
//        STBI_NO_WEBP      STBI_NO_QOI       STBI_NO_ETC2
//
//  - You can request *only* certain decoders and suppress all other ones
//    (this will be more forward-compatible, as addition of new decoders
//    doesn't require you to disable them explicitly):
//
//        STBI_ONLY_JPEG    STBI_ONLY_PNG     STBI_ONLY_BMP     STBI_ONLY_TGA
//        STBI_ONLY_GIF     STBI_ONLY_PNM
//        STBI_ONLY_PSD, STBI_ONLY_HDR, STBI_ONLY_PIC   (accepted; select nothing)
//
//    and, for the formats stb_image doesn't have:
//
//        STBI_ONLY_WEBP    STBI_ONLY_QOI     STBI_ONLY_ETC2
//
//  - STBI_NO_SIMD turns off Wuffs' SIMD code (WUFFS_CONFIG__AVOID_CPU_ARCH;
//    ignored with STBI_WUFFS_EXTERNAL). STBI_NEON is accepted and has no
//    effect: with gcc/clang, Wuffs enables NEON by itself from the
//    compiler's target flags.
//
//  - If you use STBI_NO_PNG (or _ONLY_ without PNG), and you still
//    want the zlib decoder to be available, #define STBI_SUPPORT_ZLIB
//
//  - If you define STBI_MAX_DIMENSIONS, images greater than that size (in
//    either width or height) are rejected without further processing.
//
//  - STBI_LDR_TO_HDR_SRGB makes stbi_loadf() use the exact sRGB decoding
//    function where stb_image uses pow(x, 2.2), i.e. at the default gamma
//    of 2.2 (see "HDR / float interface"). Off by default.
//
//  - STBI_LOADF_VIA_8BIT makes stbi_loadf() reduce 16-bit files to 8 bits
//    before converting them to float, as stb_image does. By default they
//    are converted from their full 16-bit values.
//
//  - STBI_WUFFS_MAX_WORKBUF_BYTES (default 1 GiB + 64 KiB) caps the scratch
//    memory a Wuffs decoder may request; the cap is checked from the image
//    header, before the work buffer is allocated and before decoding
//    starts.
//
//  - Wuffs configuration: unless STBI_WUFFS_EXTERNAL is defined, the
//    implementation defines WUFFS_IMPLEMENTATION, WUFFS_CONFIG__STATIC_FUNCTIONS
//    and WUFFS_CONFIG__MODULES plus the WUFFS_CONFIG__MODULE__* macros each
//    enabled format needs (BASE, ADLER32, CRC32, DEFLATE, ZLIB, PNG, JPEG, GIF, LZW, BMP,
//    TARGA, NETPBM, WEBP, VP8, QOI, ETC2), then
//    includes STBI_WUFFS_SOURCE. Wuffs' functions become static, but more
//    than 200 Wuffs symbols (status-message strings, vtables, a few lookup
//    tables, the sizeof__/initialize/alloc functions) keep external linkage, which
//    Wuffs v0.4 gives no way to change. Consequences:
//       - Only ONE translation unit per program may define
//         STB_IMAGE_IMPLEMENTATION, even with STB_IMAGE_STATIC (stb_image
//         allows one private copy per TU; here that fails to link).
//       - If the program links Wuffs anywhere else (directly, or inside
//         another library), compile Wuffs once yourself, #include its
//         header before this file, and #define STBI_WUFFS_EXTERNAL.
//
//  - Compiler requirements: C99 or later, or C++. Unlike stb_image, the
//    implementation can't be compiled as C89, and it can't be #included
//    inside a user's extern "C" { } block or namespace (Wuffs pulls in
//    <memory> under C++). The implementation also brings <stdbool.h> and
//    <stdint.h> into its translation unit, so code there can't define its
//    own bool/true/false.

#ifndef STBI_NO_STDIO
#include <stdio.h>
#endif // STBI_NO_STDIO

#define STBI_VERSION 1
#define STBI_WUFFS_ADAPTER_VERSION 1

enum
{
   STBI_default = 0, // only used for desired_channels

   STBI_grey       = 1,
   STBI_grey_alpha = 2,
   STBI_rgb        = 3,
   STBI_rgb_alpha  = 4
};

#include <stdlib.h>
typedef unsigned char stbi_uc;
typedef unsigned short stbi_us;

#ifdef __cplusplus
extern "C" {
#endif

#ifndef STBIDEF
#ifdef STB_IMAGE_STATIC
#define STBIDEF static
#else
#define STBIDEF extern
#endif
#endif

//////////////////////////////////////////////////////////////////////////////
//
// PRIMARY API - works on images of any type
//

//
// load image by filename, open file, or memory buffer
//

typedef struct
{
   int      (*read)  (void *user,char *data,int size);   // fill 'data' with 'size' bytes.  return number of bytes actually read
   void     (*skip)  (void *user,int n);                 // skip the next 'n' bytes, or 'unget' the last -n bytes if negative
   int      (*eof)   (void *user);                       // returns nonzero if we are at end of file/data
} stbi_io_callbacks;

////////////////////////////////////
//
// 8-bits-per-channel interface
//

STBIDEF stbi_uc *stbi_load_from_memory   (stbi_uc           const *buffer, int len   , int *x, int *y, int *channels_in_file, int desired_channels);
STBIDEF stbi_uc *stbi_load_from_callbacks(stbi_io_callbacks const *clbk  , void *user, int *x, int *y, int *channels_in_file, int desired_channels);

#ifndef STBI_NO_STDIO
STBIDEF stbi_uc *stbi_load            (char const *filename, int *x, int *y, int *channels_in_file, int desired_channels);
STBIDEF stbi_uc *stbi_load_from_file  (FILE *f, int *x, int *y, int *channels_in_file, int desired_channels);
// for stbi_load_from_file, file pointer is left pointing immediately after image
#endif

#ifndef STBI_NO_GIF
STBIDEF stbi_uc *stbi_load_gif_from_memory(stbi_uc const *buffer, int len, int **delays, int *x, int *y, int *z, int *comp, int req_comp);
#endif

#ifdef STBI_WINDOWS_UTF8
STBIDEF int stbi_convert_wchar_to_utf8(char *buffer, size_t bufferlen, const wchar_t* input);
#endif

////////////////////////////////////
//
// 16-bits-per-channel interface
//

STBIDEF stbi_us *stbi_load_16_from_memory   (stbi_uc const *buffer, int len, int *x, int *y, int *channels_in_file, int desired_channels);
STBIDEF stbi_us *stbi_load_16_from_callbacks(stbi_io_callbacks const *clbk, void *user, int *x, int *y, int *channels_in_file, int desired_channels);

#ifndef STBI_NO_STDIO
STBIDEF stbi_us *stbi_load_16          (char const *filename, int *x, int *y, int *channels_in_file, int desired_channels);
STBIDEF stbi_us *stbi_load_from_file_16(FILE *f, int *x, int *y, int *channels_in_file, int desired_channels);
#endif

////////////////////////////////////
//
// float-per-channel interface
//
#ifndef STBI_NO_LINEAR
   STBIDEF float *stbi_loadf_from_memory     (stbi_uc const *buffer, int len, int *x, int *y, int *channels_in_file, int desired_channels);
   STBIDEF float *stbi_loadf_from_callbacks  (stbi_io_callbacks const *clbk, void *user, int *x, int *y,  int *channels_in_file, int desired_channels);

   #ifndef STBI_NO_STDIO
   STBIDEF float *stbi_loadf            (char const *filename, int *x, int *y, int *channels_in_file, int desired_channels);
   STBIDEF float *stbi_loadf_from_file  (FILE *f, int *x, int *y, int *channels_in_file, int desired_channels);
   #endif
#endif

#ifndef STBI_NO_HDR
   STBIDEF void   stbi_hdr_to_ldr_gamma(float gamma);
   STBIDEF void   stbi_hdr_to_ldr_scale(float scale);
#endif // STBI_NO_HDR

#ifndef STBI_NO_LINEAR
   STBIDEF void   stbi_ldr_to_hdr_gamma(float gamma);
   STBIDEF void   stbi_ldr_to_hdr_scale(float scale);
#endif // STBI_NO_LINEAR

// stbi_is_hdr is always defined, but always returns false (no HDR decoder)
STBIDEF int    stbi_is_hdr_from_callbacks(stbi_io_callbacks const *clbk, void *user);
STBIDEF int    stbi_is_hdr_from_memory(stbi_uc const *buffer, int len);
#ifndef STBI_NO_STDIO
STBIDEF int      stbi_is_hdr          (char const *filename);
STBIDEF int      stbi_is_hdr_from_file(FILE *f);
#endif // STBI_NO_STDIO


// get a VERY brief reason for failure
// on most compilers (and ALL modern mainstream compilers) this is threadsafe
STBIDEF const char *stbi_failure_reason  (void);

// free the loaded image -- this is just STBI_FREE()
STBIDEF void     stbi_image_free      (void *retval_from_stbi_load);

// get image dimensions & components without fully decoding
STBIDEF int      stbi_info_from_memory(stbi_uc const *buffer, int len, int *x, int *y, int *comp);
STBIDEF int      stbi_info_from_callbacks(stbi_io_callbacks const *clbk, void *user, int *x, int *y, int *comp);
STBIDEF int      stbi_is_16_bit_from_memory(stbi_uc const *buffer, int len);
STBIDEF int      stbi_is_16_bit_from_callbacks(stbi_io_callbacks const *clbk, void *user);

#ifndef STBI_NO_STDIO
STBIDEF int      stbi_info               (char const *filename,     int *x, int *y, int *comp);
STBIDEF int      stbi_info_from_file     (FILE *f,                  int *x, int *y, int *comp);
STBIDEF int      stbi_is_16_bit          (char const *filename);
STBIDEF int      stbi_is_16_bit_from_file(FILE *f);
#endif



// no effect (warns on stderr if set): Wuffs always returns non-premultiplied alpha
STBIDEF void stbi_set_unpremultiply_on_load(int flag_true_if_should_unpremultiply);

// no effect (warns on stderr if set): iPhone CgBI PNGs are not supported
STBIDEF void stbi_convert_iphone_png_to_rgb(int flag_true_if_should_convert);

// flip the image vertically, so the first pixel in the output array is the bottom left
STBIDEF void stbi_set_flip_vertically_on_load(int flag_true_if_should_flip);

// as above, but only applies to images loaded on the thread that calls the function
// this function is only available if your compiler supports thread-local variables;
// calling it will fail to link if your compiler doesn't
STBIDEF void stbi_set_unpremultiply_on_load_thread(int flag_true_if_should_unpremultiply);
STBIDEF void stbi_convert_iphone_png_to_rgb_thread(int flag_true_if_should_convert);
STBIDEF void stbi_set_flip_vertically_on_load_thread(int flag_true_if_should_flip);

// ZLIB client - used by PNG, available for other purposes

STBIDEF char *stbi_zlib_decode_malloc_guesssize(const char *buffer, int len, int initial_size, int *outlen);
STBIDEF char *stbi_zlib_decode_malloc_guesssize_headerflag(const char *buffer, int len, int initial_size, int *outlen, int parse_header);
STBIDEF char *stbi_zlib_decode_malloc(const char *buffer, int len, int *outlen);
STBIDEF int   stbi_zlib_decode_buffer(char *obuffer, int olen, const char *ibuffer, int ilen);

STBIDEF char *stbi_zlib_decode_noheader_malloc(const char *buffer, int len, int *outlen);
STBIDEF int   stbi_zlib_decode_noheader_buffer(char *obuffer, int olen, const char *ibuffer, int ilen);


#ifdef __cplusplus
}
#endif

//
//
////   end header file   /////////////////////////////////////////////////////
#endif // STBI_INCLUDE_STB_IMAGE_H

#ifdef STB_IMAGE_IMPLEMENTATION

#if defined(STBI_ONLY_JPEG) || defined(STBI_ONLY_PNG) || defined(STBI_ONLY_BMP) \
  || defined(STBI_ONLY_TGA) || defined(STBI_ONLY_GIF) || defined(STBI_ONLY_PSD) \
  || defined(STBI_ONLY_HDR) || defined(STBI_ONLY_PIC) || defined(STBI_ONLY_PNM) \
  || defined(STBI_ONLY_ZLIB) || defined(STBI_ONLY_WEBP) || defined(STBI_ONLY_QOI) \
  || defined(STBI_ONLY_ETC2)
   #ifndef STBI_ONLY_JPEG
   #define STBI_NO_JPEG
   #endif
   #ifndef STBI_ONLY_PNG
   #define STBI_NO_PNG
   #endif
   #ifndef STBI_ONLY_BMP
   #define STBI_NO_BMP
   #endif
   #ifndef STBI_ONLY_PSD
   #define STBI_NO_PSD
   #endif
   #ifndef STBI_ONLY_TGA
   #define STBI_NO_TGA
   #endif
   #ifndef STBI_ONLY_GIF
   #define STBI_NO_GIF
   #endif
   #ifndef STBI_ONLY_HDR
   #define STBI_NO_HDR
   #endif
   #ifndef STBI_ONLY_PIC
   #define STBI_NO_PIC
   #endif
   #ifndef STBI_ONLY_PNM
   #define STBI_NO_PNM
   #endif
   #ifndef STBI_ONLY_WEBP
   #define STBI_NO_WEBP
   #endif
   #ifndef STBI_ONLY_QOI
   #define STBI_NO_QOI
   #endif
   #ifndef STBI_ONLY_ETC2
   #define STBI_NO_ETC2
   #endif
#endif

#if defined(STBI_NO_PNG) && !defined(STBI_SUPPORT_ZLIB) && !defined(STBI_NO_ZLIB)
#define STBI_NO_ZLIB
#endif

//////////////////////////////////////////////////////////////////////////////
//
//  Wuffs
//

#ifndef STBI_WUFFS_EXTERNAL
   #ifndef WUFFS_IMPLEMENTATION
   #define WUFFS_IMPLEMENTATION
   #endif
   #ifndef WUFFS_CONFIG__STATIC_FUNCTIONS
   #define WUFFS_CONFIG__STATIC_FUNCTIONS
   #endif
   #define WUFFS_CONFIG__MODULES
   #define WUFFS_CONFIG__MODULE__BASE
   #if !defined(STBI_NO_PNG) || !defined(STBI_NO_ZLIB)
   #define WUFFS_CONFIG__MODULE__ADLER32
   #define WUFFS_CONFIG__MODULE__DEFLATE
   #define WUFFS_CONFIG__MODULE__ZLIB
   #endif
   #ifndef STBI_NO_PNG
   #define WUFFS_CONFIG__MODULE__CRC32
   #define WUFFS_CONFIG__MODULE__PNG
   #endif
   #ifndef STBI_NO_JPEG
   #define WUFFS_CONFIG__MODULE__JPEG
   #endif
   #ifndef STBI_NO_GIF
   #define WUFFS_CONFIG__MODULE__GIF
   #define WUFFS_CONFIG__MODULE__LZW
   #endif
   #ifndef STBI_NO_BMP
   #define WUFFS_CONFIG__MODULE__BMP
   #endif
   #ifndef STBI_NO_TGA
   #define WUFFS_CONFIG__MODULE__TARGA
   #endif
   #ifndef STBI_NO_PNM
   #define WUFFS_CONFIG__MODULE__NETPBM
   #endif
   #ifndef STBI_NO_WEBP
   #define WUFFS_CONFIG__MODULE__VP8
   #define WUFFS_CONFIG__MODULE__WEBP
   #endif
   #ifndef STBI_NO_QOI
   #define WUFFS_CONFIG__MODULE__QOI
   #endif
   #ifndef STBI_NO_ETC2
   #define WUFFS_CONFIG__MODULE__ETC2
   #endif

   #if defined(STBI_NO_SIMD) && !defined(WUFFS_CONFIG__AVOID_CPU_ARCH)
   #define WUFFS_CONFIG__AVOID_CPU_ARCH
   #endif

   #ifndef STBI_WUFFS_SOURCE
   #define STBI_WUFFS_SOURCE "wuffs-v0.4.c"
   #endif

   #ifdef WUFFS_INCLUDE_GUARD
   #error "stb_image_wuffs.h: Wuffs was already #included in this file, so it can't be configured and compiled here. Include stb_image_wuffs.h first, or compile Wuffs yourself and #define STBI_WUFFS_EXTERNAL."
   #endif
   #if defined(__has_include)
   #if !__has_include(STBI_WUFFS_SOURCE)
   #error "stb_image_wuffs.h: can't find the Wuffs source (wuffs-v0.4.c). Put it next to stb_image_wuffs.h or on the include path, or #define STBI_WUFFS_SOURCE \"path/to/wuffs-v0.4.c\"."
   #endif
   #endif

   // Wuffs' C is generated code that warns under -Wall -Wextra / -W4; it is
   // compiled inside the user's translation unit here, so silence it locally.
   #if defined(_MSC_VER) && !defined(__clang__)
   #pragma warning(push, 0)
   #elif defined(__clang__)
   #pragma clang diagnostic push
   #pragma clang diagnostic ignored "-Weverything"
   #elif defined(__GNUC__)
   #pragma GCC diagnostic push
   #pragma GCC diagnostic ignored "-Wpragmas"
   #pragma GCC diagnostic ignored "-Wunused-parameter"
   #pragma GCC diagnostic ignored "-Wunused-function"
   #pragma GCC diagnostic ignored "-Wunused-variable"
   #pragma GCC diagnostic ignored "-Wunused-but-set-variable"
   #pragma GCC diagnostic ignored "-Wsign-compare"
   #pragma GCC diagnostic ignored "-Wtype-limits"
   #pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
   #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
   #pragma GCC diagnostic ignored "-Wclass-memaccess"
   #pragma GCC diagnostic ignored "-Wcast-function-type"
   #pragma GCC diagnostic ignored "-Wpedantic"
   // on 32-bit targets, where size_t is narrower than Wuffs' uint64_t
   #pragma GCC diagnostic ignored "-Wconversion"
   #pragma GCC diagnostic ignored "-Wsign-conversion"
   #pragma GCC diagnostic ignored "-Wshadow"
   #endif

   #include STBI_WUFFS_SOURCE

   #if defined(_MSC_VER) && !defined(__clang__)
   #pragma warning(pop)
   #elif defined(__clang__)
   #pragma clang diagnostic pop
   #elif defined(__GNUC__)
   #pragma GCC diagnostic pop
   #endif
#endif // STBI_WUFFS_EXTERNAL

#include <stdarg.h>
#include <stddef.h> // ptrdiff_t on osx
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <errno.h>

#ifndef STBI_NO_LINEAR
#include <math.h>  // pow
#endif

#ifndef STBI_NO_STDIO
#include <stdio.h>
#if defined(_WIN32) && !defined(__CYGWIN__)
#include <io.h>     // _get_osfhandle, _fileno
#include <share.h>  // _SH_DENYNO
#endif
#endif

#ifndef STBI_ASSERT
#include <assert.h>
#define STBI_ASSERT(x) assert(x)
#endif

#ifdef __cplusplus
#define STBI_EXTERN extern "C"
#else
#define STBI_EXTERN extern
#endif

#ifndef STBI_NO_THREAD_LOCALS
   #if defined(__cplusplus) &&  __cplusplus >= 201103L
      #define STBI_THREAD_LOCAL       thread_local
   #elif defined(__GNUC__) && __GNUC__ < 5
      #define STBI_THREAD_LOCAL       __thread
   #elif defined(_MSC_VER)
      #define STBI_THREAD_LOCAL       __declspec(thread)
   #elif defined (__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_THREADS__)
      #define STBI_THREAD_LOCAL       _Thread_local
   #endif

   #ifndef STBI_THREAD_LOCAL
      #if defined(__GNUC__)
        #define STBI_THREAD_LOCAL       __thread
      #endif
   #endif
#endif

typedef uint16_t stbi__uint16;

#ifdef _MSC_VER
#define STBI_NOTUSED(v)  (void)(v)
#else
#define STBI_NOTUSED(v)  (void)sizeof(v)
#endif

#if defined(STBI_MALLOC) && defined(STBI_FREE) && (defined(STBI_REALLOC) || defined(STBI_REALLOC_SIZED))
// ok
#elif !defined(STBI_MALLOC) && !defined(STBI_FREE) && !defined(STBI_REALLOC) && !defined(STBI_REALLOC_SIZED)
// ok
#else
#error "Must define all or none of STBI_MALLOC, STBI_FREE, and STBI_REALLOC (or STBI_REALLOC_SIZED)."
#endif

#ifndef STBI_MALLOC
#define STBI_MALLOC(sz)           malloc(sz)
#define STBI_REALLOC(p,newsz)     realloc(p,newsz)
#define STBI_FREE(p)              free(p)
#endif

#ifndef STBI_REALLOC_SIZED
#define STBI_REALLOC_SIZED(p,oldsz,newsz) STBI_REALLOC(p,newsz)
#endif

#ifndef STBI_MAX_DIMENSIONS
#define STBI_MAX_DIMENSIONS (1 << 24)
#endif

// largest Wuffs decoder work buffer we will allocate, checked before the
// allocation. 1 GiB + 64 KiB lets 16384x16384 RGBA8 PNGs through.
#ifndef STBI_WUFFS_MAX_WORKBUF_BYTES
#define STBI_WUFFS_MAX_WORKBUF_BYTES ((1ULL << 30) + 65536)
#endif

// initial size of the read buffer used for callback / FILE input
#define STBI__IOBUF_INITIAL  (64 * 1024)
// the read buffer grows on demand, but never beyond this
#define STBI__IOBUF_MAX      (256 * 1024 * 1024)
// largest single read() request. User callbacks get stb_image's 128 bytes: a
// stream can't be "un-read" through the callback API, so this bounds how far
// past the image it is consumed. FILE input reads in big chunks, because the
// unconsumed bytes are fseek'ed back afterwards.
#define STBI__READ_SIZE_CALLBACKS  128
#define STBI__READ_SIZE_FILE       STBI__IOBUF_INITIAL

///////////////////////////////////////////////
//
//  stbi__context struct and start_xxx functions

// stbi__context structure is our basic context used by all images, so it
#ifndef STBI_NO_STDIO
// state of a FILE being read; the stdio callbacks' user pointer
typedef struct
{
   FILE *f;
   int seekable;                   // a regular file: seeks really move
   int skip_failed;                // a skip couldn't move past all its bytes
   uint64_t consumed;              // bytes taken from f since the call began
} stbi__file_io;
#endif

// contains all the IO context, plus some basic image information
typedef struct
{
   wuffs_base__io_buffer buf;      // wraps either the caller's memory or 'owned'

   stbi_io_callbacks io;
   void *io_user_data;
   int read_from_callbacks;
   stbi_uc *owned;                 // read buffer for callback input
   int drain_trailer;              // consume the whole image (FILE input)
   size_t read_size;               // largest read() request
   int io_error;                   // a read failed (not just end of input)
#ifndef STBI_NO_STDIO
   stbi__file_io fio;              // FILE input state (fio.f is NULL otherwise)
#endif

   uint32_t fourcc;                // format of the image being decoded
} stbi__context;

static
#ifdef STBI_THREAD_LOCAL
STBI_THREAD_LOCAL
#endif
const char *stbi__g_failure_reason;

STBIDEF const char *stbi_failure_reason(void)
{
   return stbi__g_failure_reason;
}

#ifndef STBI_NO_FAILURE_STRINGS
static int stbi__err(const char *str)
{
   stbi__g_failure_reason = str;
   return 0;
}
#endif

// stbi__err - error
// stbi__errpf - error returning pointer to float
// stbi__errpuc - error returning pointer to unsigned char

#ifdef STBI_NO_FAILURE_STRINGS
   // a call, not a bare 0, so stbi__err(...); as a statement doesn't warn
   static int stbi__err_none(void) { return 0; }
   #define stbi__err(x,y)  stbi__err_none()
#elif defined(STBI_FAILURE_USERMSG)
   #define stbi__err(x,y)  stbi__err(y)
#else
   #define stbi__err(x,y)  stbi__err(x)
#endif

#define stbi__errpf(x,y)   ((float *)(size_t) (stbi__err(x,y)?NULL:NULL))
#define stbi__errpuc(x,y)  ((unsigned char *)(size_t) (stbi__err(x,y)?NULL:NULL))
#define stbi__errpdec(x,y) ((void *)(size_t) (stbi__err(x,y)?NULL:NULL))

// records a Wuffs status as the failure reason. Wuffs messages are static
// strings like "#png: bad header"; the leading '#'/'$'/'@' is dropped.
static int stbi__wuffs_err(wuffs_base__status status)
{
   const char *msg = status.repr;
   if (msg == NULL)
      msg = "wuffs: unknown error";
   else if (*msg == '#' || *msg == '$' || *msg == '@')
      ++msg;
   STBI_NOTUSED(msg);
   return stbi__err(msg, "Corrupt image or unsupported format");
}

STBIDEF void stbi_image_free(void *retval_from_stbi_load)
{
   STBI_FREE(retval_from_stbi_load);
}

// The only shared state: the stb_image API has process-wide setters
// (stbi_set_flip_vertically_on_load, stbi_ldr_to_hdr_gamma/scale) that take
// no context and, by contract, affect every later load on every thread.
// Like stb_image, changing them while another thread is loading is a data
// race; use stbi_set_flip_vertically_on_load_thread for per-thread control.
// Loads themselves share nothing: each call owns its decoder and buffers.
static int stbi__vertically_flip_on_load_global = 0;

STBIDEF void stbi_set_flip_vertically_on_load(int flag_true_if_should_flip)
{
   stbi__vertically_flip_on_load_global = flag_true_if_should_flip;
}

// these two options can't be honored, so there is nothing to store; say so
// on stderr rather than silently
#define STBI__UNPREMULTIPLY_WARNING \
   "stb_image_wuffs: warning: stbi_set_unpremultiply_on_load() has no effect; " \
   "Wuffs always returns non-premultiplied alpha\n"
#define STBI__IPHONE_WARNING \
   "stb_image_wuffs: warning: stbi_convert_iphone_png_to_rgb() has no effect; " \
   "iPhone (CgBI) PNGs are not supported and fail to load\n"

static void stbi__warn_unsupported(int flag, const char *msg)
{
#ifndef STBI_NO_STDIO
   if (flag)
      // the one unchecked result: a best-effort warning, from a void setter,
      // with nowhere else to report that stderr itself failed
      (void) fputs(msg, stderr);
#else
   STBI_NOTUSED(flag);
   STBI_NOTUSED(msg);
#endif
}

STBIDEF void stbi_set_unpremultiply_on_load(int flag_true_if_should_unpremultiply)
{
   stbi__warn_unsupported(flag_true_if_should_unpremultiply, STBI__UNPREMULTIPLY_WARNING);
}

STBIDEF void stbi_convert_iphone_png_to_rgb(int flag_true_if_should_convert)
{
   stbi__warn_unsupported(flag_true_if_should_convert, STBI__IPHONE_WARNING);
}

#ifndef STBI_THREAD_LOCAL
#define stbi__vertically_flip_on_load  stbi__vertically_flip_on_load_global
#else
static STBI_THREAD_LOCAL int stbi__vertically_flip_on_load_local, stbi__vertically_flip_on_load_set;

STBIDEF void stbi_set_flip_vertically_on_load_thread(int flag_true_if_should_flip)
{
   stbi__vertically_flip_on_load_local = flag_true_if_should_flip;
   stbi__vertically_flip_on_load_set = 1;
}

STBIDEF void stbi_set_unpremultiply_on_load_thread(int flag_true_if_should_unpremultiply)
{
   stbi__warn_unsupported(flag_true_if_should_unpremultiply, STBI__UNPREMULTIPLY_WARNING);
}

STBIDEF void stbi_convert_iphone_png_to_rgb_thread(int flag_true_if_should_convert)
{
   stbi__warn_unsupported(flag_true_if_should_convert, STBI__IPHONE_WARNING);
}

#define stbi__vertically_flip_on_load  (stbi__vertically_flip_on_load_set       \
                                         ? stbi__vertically_flip_on_load_local  \
                                         : stbi__vertically_flip_on_load_global)
#endif // STBI_THREAD_LOCAL

#ifndef STBI_NO_LINEAR
static float stbi__l2h_gamma=2.2f, stbi__l2h_scale=1.0f;

STBIDEF void   stbi_ldr_to_hdr_gamma(float gamma) { stbi__l2h_gamma = gamma; }
STBIDEF void   stbi_ldr_to_hdr_scale(float scale) { stbi__l2h_scale = scale; }
#endif

// there is no HDR decoder, so these have nothing to configure. defined even
// under STBI_NO_HDR (which STBI_ONLY_* implies), as stb_image does, so other
// translation units that see the declarations still link.
STBIDEF void   stbi_hdr_to_ldr_gamma(float gamma) { STBI_NOTUSED(gamma); }
STBIDEF void   stbi_hdr_to_ldr_scale(float scale) { STBI_NOTUSED(scale); }

// initialize a memory-decode context
static void stbi__start_mem(stbi__context *s, stbi_uc const *buffer, int len)
{
   memset(s, 0, sizeof(*s));
   s->buf = wuffs_base__ptr_u8__reader((uint8_t *) buffer, (buffer && len > 0) ? (size_t) len : 0, true);
}

// initialize a callback-based context
static int stbi__start_callbacks(stbi__context *s, stbi_io_callbacks const *c, void *user)
{
   memset(s, 0, sizeof(*s));
   if (!c || !c->read)
      return stbi__err("bad callbacks", "Internal error");
   s->io = *c;
   s->io_user_data = user;
   s->read_from_callbacks = 1;
   s->read_size = STBI__READ_SIZE_CALLBACKS;
   s->owned = (stbi_uc *) STBI_MALLOC(STBI__IOBUF_INITIAL);
   if (s->owned == NULL)
      return stbi__err("outofmem", "Out of memory");
   s->buf = wuffs_base__ptr_u8__writer(s->owned, STBI__IOBUF_INITIAL);
   return 1;
}

static void stbi__end(stbi__context *s)
{
   if (s->owned)
      STBI_FREE(s->owned);
   s->owned = NULL;
}

#ifndef STBI_NO_STDIO

// 64-bit FILE offsets on Windows, where long is 32 bits
#if defined(_WIN32) && !defined(__CYGWIN__)
typedef __int64 stbi__fpos;
#define stbi__fseek(f, o, w)  _fseeki64((f), (o), (w))
STBI_EXTERN __declspec(dllimport) unsigned long __stdcall GetFileType(void *hFile);
#else
typedef long stbi__fpos;
#define stbi__fseek(f, o, w)  fseek((f), (o), (w))
#endif

// whether seeking on f really moves its position. on Windows, fseek on a
// pipe "succeeds" without seeking, so its result can't be trusted; ask the
// OS whether f is a disk file instead. elsewhere, ftell fails on pipes and
// sockets (errno is preserved).
static int stbi__file_seekable(FILE *f)
{
#if defined(_WIN32) && !defined(__CYGWIN__)
   intptr_t h = _get_osfhandle(_fileno(f));
   return h != -1 && GetFileType((void *) h) == 1; // FILE_TYPE_DISK
#else
   int saved = errno, ok = ftell(f) != -1L;
#ifdef EOVERFLOW
   if (!ok && errno == EOVERFLOW) ok = 1; // seekable, just past LONG_MAX
#endif
   errno = saved;
   return ok;
#endif
}

// fread's short count covers both end of file and errors; stbi__refill tells
// them apart with ferror()
static int stbi__stdio_read(void *user, char *data, int size)
{
   stbi__file_io *io = (stbi__file_io *) user;
   size_t got = fread(data, 1, (size_t) size, io->f);
   io->consumed += got;
   return (int) got;
}

// skips forward n bytes: by seeking on a regular file, otherwise (pipes) by
// reading and discarding. if the input runs out, the following reads report
// end of file or the error, so a failed skip can't misalign the decoder.
static void stbi__stdio_skip(void *user, int n)
{
   stbi__file_io *io = (stbi__file_io *) user;
   char scratch[4096];
   if (n <= 0)
      return;
   if (io->seekable && stbi__fseek(io->f, (stbi__fpos) n, SEEK_CUR) == 0) {
      io->consumed += (uint64_t) n;
      return;
   }
   while (n > 0) {
      size_t want = n < (int) sizeof(scratch) ? (size_t) n : sizeof(scratch);
      size_t got = fread(scratch, 1, want, io->f);
      io->consumed += got;
      if (got == 0) {
         // end of file or a read error. the stb callback API gives skip() no
         // way to report it, so record it for stbi__advance_to to check
         io->skip_failed = 1;
         return;
      }
      n -= (int) got;
   }
}

static int stbi__stdio_eof(void *user)
{
   stbi__file_io *io = (stbi__file_io *) user;
   return feof(io->f) || ferror(io->f);
}

static const stbi_io_callbacks stbi__stdio_callbacks =
{
   stbi__stdio_read,
   stbi__stdio_skip,
   stbi__stdio_eof,
};

static int stbi__start_file(stbi__context *s, FILE *f)
{
   // a FILE whose error indicator is already set can't tell us whether a
   // later short read is end of file or another error, so refuse it
   if (f == NULL || ferror(f)) {
      memset(s, 0, sizeof(*s));
      return stbi__err("read error", "Error reading file");
   }
   if (!stbi__start_callbacks(s, &stbi__stdio_callbacks, NULL))
      return 0;
   s->fio.f = f;
   s->fio.seekable = stbi__file_seekable(f);
   s->io_user_data = &s->fio;
   s->drain_trailer = 1;
   // a regular file reads in big chunks (the unread rest is seeked back
   // afterwards); a pipe can't give bytes back, so read it like callbacks
   s->read_size = s->fio.seekable ? STBI__READ_SIZE_FILE : STBI__READ_SIZE_CALLBACKS;
   return 1;
}

// seeks f back by n bytes, in steps that fit any stbi__fpos. returns 0 if a
// seek fails (a regular file shouldn't do that).
static int stbi__seek_back(FILE *f, uint64_t n)
{
   while (n > 0) {
      stbi__fpos k = n > ((uint64_t) 1 << 30) ? (stbi__fpos) 1 << 30 : (stbi__fpos) n;
      if (stbi__fseek(f, -k, SEEK_CUR) != 0)
         return 0;
      n -= (uint64_t) k;
   }
   return 1;
}

// after a successful load: returns the unconsumed read-ahead to the FILE so it
// is left just past the image. a pipe can't take bytes back (by design: they
// are lost, at most 128 bytes, as with stb_image). returns 0, with the failure
// reason set, if a regular file can't seek back.
static int stbi__unget_file(stbi__context *s)
{
   if (s->fio.seekable && !stbi__seek_back(s->fio.f, s->buf.meta.wi - s->buf.meta.ri))
      return stbi__err("seek error", "Error seeking in file");
   return 1;
}

// after info/is_16_bit, or a failed load: puts the FILE back where the call
// found it, by seeking back over everything read or skipped (no ftell needed,
// so this also works past 2 GiB on 32-bit systems). not possible on a pipe (by
// design). returns 0 if a regular file can't seek back; the failure reason is
// left to the caller (a failed load keeps its original reason).
static int stbi__rewind_file(stbi__context *s)
{
   return !s->fio.seekable || stbi__seek_back(s->fio.f, s->fio.consumed);
}

#endif // !STBI_NO_STDIO

// pulls more input into the read buffer. returns 0 (with the failure reason
// set) if no more input can be had.
static int stbi__refill(stbi__context *s)
{
   size_t avail;
   int n;

   if (!s->read_from_callbacks || s->buf.meta.closed)
      return stbi__err("unexpected end of file", "Corrupt or truncated image");

   wuffs_base__io_buffer__compact(&s->buf);
   avail = s->buf.data.len - s->buf.meta.wi;
   if (avail == 0) {
      // the decoder wants more lookahead than fits; grow the buffer
      size_t newlen = s->buf.data.len * 2;
      stbi_uc *p;
      if (newlen > STBI__IOBUF_MAX)
         return stbi__err("read buffer too large", "Corrupt image");
      p = (stbi_uc *) STBI_REALLOC_SIZED(s->owned, s->buf.data.len, newlen);
      if (p == NULL)
         return stbi__err("outofmem", "Out of memory");
      s->owned = p;
      s->buf.data.ptr = p;
      s->buf.data.len = newlen;
      avail = newlen - s->buf.meta.wi;
   }

   if (avail > s->read_size)
      avail = s->read_size;
   n = (s->io.read)(s->io_user_data, (char *) (s->buf.data.ptr + s->buf.meta.wi), (int) avail);
   // a read() that reports more bytes than asked for, or a negative count, is
   // an I/O error, never end of input (which a truncated JPEG would accept)
   if (n < 0 || (size_t) n > avail) {
      s->io_error = 1;
      s->buf.meta.closed = true;
      return stbi__err("read error", "Error reading file");
   }
   s->buf.meta.wi += (size_t) n;
#ifndef STBI_NO_STDIO
   // likewise a FILE whose error indicator got set (checked on every read,
   // not just short ones)
   if (s->fio.f && ferror(s->fio.f)) {
      s->io_error = 1;
      s->buf.meta.closed = true;
      return stbi__err("read error", "Error reading file");
   }
#endif
   // read() returning nothing is end of input. eof() is optional, as in
   // stb_image (which only calls it for some formats), and only asked after
   // a short read.
   if (n == 0 || ((size_t) n < avail && s->io.eof && (s->io.eof)(s->io_user_data)))
      s->buf.meta.closed = true;
   return 1;
}

#ifndef STBI_NO_WEBP
// WORKAROUND for a Wuffs v0.4 WebP bug: for a lossy WebP whose alpha plane is
// stored uncompressed, the decoder copies only the alpha bytes already in its
// input buffer and never waits for the rest, so any part of the plane not yet
// read is left as uninitialized memory in the output's alpha channel. memory
// input always holds the whole file and decodes correctly, so callback and
// FILE input read the entire WebP (up to STBI__IOBUF_MAX bytes) before
// decoding it. no file data is parsed: this only reads until end of input.
// returns 0 (with the failure reason set) on a read error, out of memory, or
// input larger than the cap.
static int stbi__buffer_all(stbi__context *s)
{
   if (!s->read_from_callbacks)
      return 1;
   // everything is read anyway, so read in big chunks
   s->read_size = STBI__IOBUF_INITIAL;
   while (!s->buf.meta.closed)
      if (!stbi__refill(s))
         return 0;
   return 1;
}
#endif

// moves the read position forward to absolute stream position 'pos'
static int stbi__advance_to(stbi__context *s, uint64_t pos)
{
   uint64_t cur = wuffs_base__io_buffer__reader_position(&s->buf);
   uint64_t buffered = s->buf.meta.wi - s->buf.meta.ri;
   uint64_t n;

   if (pos < cur)
      return stbi__err("bad redirect", "Corrupt image");
   n = pos - cur;
   if (n <= buffered) {
      s->buf.meta.ri += (size_t) n;
      return 1;
   }
   if (!s->read_from_callbacks || s->buf.meta.closed || !s->io.skip)
      return stbi__err("unexpected end of file", "Corrupt or truncated image");

   n -= buffered;
   s->buf.meta.ri = s->buf.meta.wi;
   wuffs_base__io_buffer__compact(&s->buf);
   s->buf.meta.pos += n;
   while (n > 0) {
      int k = n > INT_MAX ? INT_MAX : (int) n;
      (s->io.skip)(s->io_user_data, k);
      n -= (uint64_t) k;
   }
#ifndef STBI_NO_STDIO
   if (s->fio.f && (s->fio.skip_failed || ferror(s->fio.f))) {
      s->io_error = 1;
      s->buf.meta.closed = true;
      return stbi__err("read error", "Error reading file");
   }
#endif
   return 1;
}

//////////////////////////////////////////////////////////////////////////////
//
//  Decoder selection and image config

// The decoders are always called through each format's own functions, never
// through Wuffs' generic wuffs_base__image_decoder interface. That interface
// dispatches via function pointers whose type differs from the functions'
// (void* self vs. the decoder's own type), which is undefined behavior that
// clang's -fsanitize=function (part of -fsanitize=undefined) and CFI builds
// reject. A decoder is therefore an opaque pointer plus its fourcc.

// X(fourcc suffix, Wuffs package) for every enabled format
#ifndef STBI_NO_BMP
#define STBI__FMT_BMP(X)       X(BMP, bmp)
#else
#define STBI__FMT_BMP(X)
#endif
#ifndef STBI_NO_ETC2
#define STBI__FMT_ETC2(X)      X(ETC2, etc2)
#else
#define STBI__FMT_ETC2(X)
#endif
#ifndef STBI_NO_GIF
#define STBI__FMT_GIF(X)       X(GIF, gif)
#else
#define STBI__FMT_GIF(X)
#endif
#ifndef STBI_NO_JPEG
#define STBI__FMT_JPEG(X)      X(JPEG, jpeg)
#else
#define STBI__FMT_JPEG(X)
#endif
#ifndef STBI_NO_PNM
#define STBI__FMT_PNM(X)       X(NPBM, netpbm)
#else
#define STBI__FMT_PNM(X)
#endif
#ifndef STBI_NO_PNG
#define STBI__FMT_PNG(X)       X(PNG, png)
#else
#define STBI__FMT_PNG(X)
#endif
#ifndef STBI_NO_QOI
#define STBI__FMT_QOI(X)       X(QOI, qoi)
#else
#define STBI__FMT_QOI(X)
#endif
#ifndef STBI_NO_TGA
#define STBI__FMT_TGA(X)       X(TGA, targa)
#else
#define STBI__FMT_TGA(X)
#endif
#ifndef STBI_NO_WEBP
#define STBI__FMT_WEBP(X)      X(WEBP, webp)
#else
#define STBI__FMT_WEBP(X)
#endif

#define STBI__ALL_FORMATS(X) \
   STBI__FMT_BMP(X) STBI__FMT_ETC2(X) STBI__FMT_GIF(X) STBI__FMT_JPEG(X) STBI__FMT_PNM(X) \
   STBI__FMT_PNG(X) STBI__FMT_QOI(X) STBI__FMT_TGA(X) STBI__FMT_WEBP(X)

// allocates (with STBI_MALLOC) and initializes the Wuffs decoder for a
// format. STBI_FREE releases it.
static void *stbi__new_decoder(uint32_t fourcc)
{
   size_t sz;
   void *d;
   switch (fourcc) {
#define STBI__X(F, pkg) case WUFFS_BASE__FOURCC__##F: sz = sizeof__wuffs_##pkg##__decoder(); break;
   STBI__ALL_FORMATS(STBI__X)
#undef STBI__X
   default:
      return stbi__errpdec("unknown image type", "Image not of any known type, or corrupt");
   }
   d = STBI_MALLOC(sz);
   if (d == NULL)
      return stbi__errpdec("outofmem", "Out of memory");
   memset(d, 0, sz);
   switch (fourcc) {
#define STBI__X(F, pkg) \
   case WUFFS_BASE__FOURCC__##F: \
      if (wuffs_##pkg##__decoder__initialize((wuffs_##pkg##__decoder *) d, sz, WUFFS_VERSION, \
                                             WUFFS_INITIALIZE__ALREADY_ZEROED).repr == NULL) \
         return d; \
      break;
   STBI__ALL_FORMATS(STBI__X)
#undef STBI__X
   default: break;
   }
   STBI_FREE(d);
   return stbi__errpdec("wuffs version mismatch", "Internal error");
}

static wuffs_base__status stbi__bad_decoder(void)
{
   return wuffs_base__make_status(wuffs_base__error__bad_argument);
}

#ifndef STBI_NO_GIF  // only GIF's strictness quirks are set
static wuffs_base__status stbi__dec_set_quirk(uint32_t fourcc, void *d, uint32_t key, uint64_t value)
{
   switch (fourcc) {
#define STBI__X(F, pkg) case WUFFS_BASE__FOURCC__##F: return wuffs_##pkg##__decoder__set_quirk((wuffs_##pkg##__decoder *) d, key, value);
   STBI__ALL_FORMATS(STBI__X)
#undef STBI__X
   default: return stbi__bad_decoder();
   }
}
#endif

static wuffs_base__status stbi__dec_image_config(uint32_t fourcc, void *d, wuffs_base__image_config *ic, wuffs_base__io_buffer *src)
{
   switch (fourcc) {
#define STBI__X(F, pkg) case WUFFS_BASE__FOURCC__##F: return wuffs_##pkg##__decoder__decode_image_config((wuffs_##pkg##__decoder *) d, ic, src);
   STBI__ALL_FORMATS(STBI__X)
#undef STBI__X
   default: return stbi__bad_decoder();
   }
}

static wuffs_base__status stbi__dec_frame_config(uint32_t fourcc, void *d, wuffs_base__frame_config *fc, wuffs_base__io_buffer *src)
{
   switch (fourcc) {
#define STBI__X(F, pkg) case WUFFS_BASE__FOURCC__##F: return wuffs_##pkg##__decoder__decode_frame_config((wuffs_##pkg##__decoder *) d, fc, src);
   STBI__ALL_FORMATS(STBI__X)
#undef STBI__X
   default: return stbi__bad_decoder();
   }
}

static wuffs_base__status stbi__dec_frame(uint32_t fourcc, void *d, wuffs_base__pixel_buffer *pb, wuffs_base__io_buffer *src,
                                          wuffs_base__pixel_blend blend, wuffs_base__slice_u8 work)
{
   switch (fourcc) {
#define STBI__X(F, pkg) case WUFFS_BASE__FOURCC__##F: return wuffs_##pkg##__decoder__decode_frame((wuffs_##pkg##__decoder *) d, pb, src, blend, work, NULL);
   STBI__ALL_FORMATS(STBI__X)
#undef STBI__X
   default: return stbi__bad_decoder();
   }
}

static wuffs_base__status stbi__dec_tell_me_more(uint32_t fourcc, void *d, wuffs_base__io_buffer *dst, wuffs_base__more_information *minfo, wuffs_base__io_buffer *src)
{
   switch (fourcc) {
#define STBI__X(F, pkg) case WUFFS_BASE__FOURCC__##F: return wuffs_##pkg##__decoder__tell_me_more((wuffs_##pkg##__decoder *) d, dst, minfo, src);
   STBI__ALL_FORMATS(STBI__X)
#undef STBI__X
   default: return stbi__bad_decoder();
   }
}

static wuffs_base__range_ii_u64 stbi__dec_workbuf_len(uint32_t fourcc, void *d)
{
   wuffs_base__range_ii_u64 empty;
   switch (fourcc) {
#define STBI__X(F, pkg) case WUFFS_BASE__FOURCC__##F: return wuffs_##pkg##__decoder__workbuf_len((const wuffs_##pkg##__decoder *) d);
   STBI__ALL_FORMATS(STBI__X)
#undef STBI__X
   default: break;
   }
   empty.min_incl = 0;
   empty.max_incl = 0;
   return empty;
}

// sniffs the format, creates its decoder and decodes the image config
// (dimensions and pixel format). returns NULL with the failure reason set.
static void *stbi__open(stbi__context *s, wuffs_base__image_config *ic)
{
   void *dec;
   wuffs_base__status status;
   int32_t fourcc;
   int redirected = 0;

   for (;;) {
      fourcc = wuffs_base__magic_number_guess_fourcc(
            wuffs_base__io_buffer__reader_slice(&s->buf), s->buf.meta.closed);
      if (fourcc >= 0)
         break;
      // fourcc < 0 means "need a longer prefix"
      if (!s->read_from_callbacks || s->buf.meta.closed) {
         fourcc = 0;
         break;
      }
      if (!stbi__refill(s))
         return NULL; // read error or out of memory; reason already set
   }

   for (;;) {
      if (fourcc <= 0)
         return stbi__errpdec("unknown image type", "Image not of any known type, or corrupt");
      dec = stbi__new_decoder((uint32_t) fourcc);
      if (dec == NULL)
         return NULL;
      s->fourcc = (uint32_t) fourcc;

      // no leniency quirks: invalid files (bad checksums, surplus or missing
      // data, truncation) are rejected, never partially loaded. where Wuffs
      // offers opt-in strictness, take it.
      #ifndef STBI_NO_GIF
      if (fourcc == (int32_t) WUFFS_BASE__FOURCC__GIF) {
         // if any of these can't be applied, don't decode with weaker checks
         static const uint32_t quirks[3] = {
            WUFFS_GIF__QUIRK_IMAGE_BOUNDS_ARE_STRICT,
            WUFFS_GIF__QUIRK_REJECT_EMPTY_FRAME,
            WUFFS_GIF__QUIRK_REJECT_EMPTY_PALETTE,
         };
         int q;
         for (q = 0; q < 3; ++q) {
            status = stbi__dec_set_quirk(s->fourcc, dec, quirks[q], 1);
            if (status.repr != NULL) {
               stbi__wuffs_err(status);
               STBI_FREE(dec);
               return NULL;
            }
         }
      }
      #endif

      *ic = wuffs_base__null_image_config();
      for (;;) {
         status = stbi__dec_image_config(s->fourcc, dec, ic, &s->buf);
         if (status.repr == NULL)
            return dec;
         if (status.repr == wuffs_base__suspension__short_read) {
            if (stbi__refill(s)) continue;
         } else if (status.repr == wuffs_base__note__i_o_redirect && !redirected) {
            break;
         } else {
            stbi__wuffs_err(status);
         }
         STBI_FREE(dec);
         return NULL;
      }

      // the file is a container (e.g. a BMP wrapping a PNG or JPEG): skip to
      // the embedded image and start again with that format's decoder
      {
         wuffs_base__io_buffer empty = wuffs_base__empty_io_buffer();
         wuffs_base__more_information minfo = wuffs_base__empty_more_information();
         status = stbi__dec_tell_me_more(s->fourcc, dec, &empty, &minfo, &s->buf);
         STBI_FREE(dec);
         if (status.repr != NULL || minfo.flavor != WUFFS_BASE__MORE_INFORMATION__FLAVOR__IO_REDIRECT)
            return stbi__errpdec("bad redirect", "Corrupt image");
         if (!stbi__advance_to(s, wuffs_base__more_information__io_redirect__range(&minfo).min_incl))
            return NULL;
         fourcc = (int32_t) wuffs_base__more_information__io_redirect__fourcc(&minfo);
         redirected = 1;
      }
   }
}

static int stbi__get_dims(wuffs_base__image_config *ic, int *w, int *h)
{
   uint32_t ww = wuffs_base__pixel_config__width(&ic->pixcfg);
   uint32_t hh = wuffs_base__pixel_config__height(&ic->pixcfg);
   if (ww == 0 || hh == 0)
      return stbi__err("0-pixel image", "Corrupt image");
   if (ww > (uint32_t) STBI_MAX_DIMENSIONS || hh > (uint32_t) STBI_MAX_DIMENSIONS)
      return stbi__err("too large", "Very large image (corrupt?)");
   *w = (int) ww;
   *h = (int) hh;
   return 1;
}

// PRIVATE WUFFS DECODER FIELDS
//
// stbi__file_channels() is the only code that reads Wuffs' private decoder
// state (the private_impl members of wuffs_png__decoder and
// wuffs_bmp__decoder). These are internal fields, not Wuffs API: Wuffs may
// rename them or change their meaning in any release. They are read only to
// report the same channels_in_file as stb_image for a few files where Wuffs'
// public pixel format loses the distinction (16-bit PNGs, grey PNGs with a
// tRNS color key, BMPs with >8-bit channel masks). They hold values Wuffs has
// already parsed; no file bytes are read here. They were verified against
// Wuffs v0.4.0 (build 4048, the wuffs-v0.4.c shipped next to this file);
// re-check them whenever Wuffs is updated.

// the stb_image notion of "components in the file"
static int stbi__file_channels(stbi__context *s, void *dec, wuffs_base__image_config *ic)
{
   wuffs_base__pixel_format f = wuffs_base__pixel_config__pixel_format(&ic->pixcfg);
   int color = (wuffs_base__pixel_format__coloration(&f) == 3) ? 3 : 1;
   int alpha = wuffs_base__pixel_format__transparency(&f) != WUFFS_BASE__PIXEL_ALPHA_TRANSPARENCY__OPAQUE;

   STBI_NOTUSED(dec); // unused when both PNG and BMP are compiled out
   if (s->fourcc == WUFFS_BASE__FOURCC__GIF)
      return 4; // as documented by stb_image
   #ifndef STBI_NO_PNG
   if (s->fourcc == WUFFS_BASE__FOURCC__PNG) {
      // PRIVATE FIELDS (see above). Wuffs reports one generic BGRA format for
      // every 16-bit PNG and for grey+tRNS, so recover stb_image's count from
      // the IHDR color type and tRNS state the Wuffs decoder has parsed.
      const wuffs_png__decoder *png = (const wuffs_png__decoder *) dec;
      int trns = png->private_impl.f_remap_transparency != 0;
      switch (png->private_impl.f_color_type) {
         case 0: return 1 + trns;
         case 2: return 3 + trns;
         case 4: return 2;
         case 6: return 4;
         default: break; // 3 (paletted) is handled below
      }
   }
   #endif
   #ifndef STBI_NO_PNM
   // PGM/PPM have no alpha; Wuffs reports 16-bit ones as generic BGRA 16-bit
   if (s->fourcc == WUFFS_BASE__FOURCC__NPBM)
      return color;
   #endif
   #ifndef STBI_NO_BMP
   // PRIVATE FIELD (see above). BMPs with >8-bit channel masks are reported as
   // generic BGRA 16-bit; like Wuffs' own decoder, take alpha from whether the
   // file has an alpha mask
   if (s->fourcc == WUFFS_BASE__FOURCC__BMP && f.repr == WUFFS_BASE__PIXEL_FORMAT__BGRA_NONPREMUL_4X16LE)
      return color + (((const wuffs_bmp__decoder *) dec)->private_impl.f_channel_masks[3] != 0);
   #endif
   // paletted images have an alpha-capable palette format; trust the
   // decoder when it knows the image is opaque (e.g. PNG without tRNS)
   if (alpha && wuffs_base__pixel_format__is_indexed(&f) &&
       wuffs_base__image_config__first_frame_is_opaque(ic))
      alpha = 0;
   return color + alpha;
}

static int stbi__is_16bit_format(wuffs_base__image_config *ic)
{
   wuffs_base__pixel_format f = wuffs_base__pixel_config__pixel_format(&ic->pixcfg);
   // the low nibble encodes channel 0's bit depth; codes above 8 mean >8 bits
   return (f.repr & 0x0F) > 0x08;
}

// allocates the decoder's work buffer, after checking its size against
// STBI_WUFFS_MAX_WORKBUF_BYTES (before the work buffer is allocated)
static int stbi__alloc_workbuf(stbi__context *s, void *dec, int w, int h, stbi_uc **work, size_t *worklen)
{
   wuffs_base__range_ii_u64 r = stbi__dec_workbuf_len(s->fourcc, dec);
   uint64_t n = r.max_incl;
   *work = NULL;
   *worklen = 0;
   if (n > (uint64_t) STBI_WUFFS_MAX_WORKBUF_BYTES)
      n = r.min_incl;
   // Wuffs v0.4's WebP workbuf_len() leaves out the w*h alpha plane that its
   // VP8 decoder needs for a lossy image with an ALPH chunk (only seen later,
   // in decode_frame), which then fails with "bad workbuf length" on small or
   // narrow images. always add it; both terms are bounded here, so no wrap.
   if (s->fourcc == WUFFS_BASE__FOURCC__WEBP && n <= (uint64_t) STBI_WUFFS_MAX_WORKBUF_BYTES)
      n += (uint64_t) w * (uint64_t) h;
   if (n > (uint64_t) STBI_WUFFS_MAX_WORKBUF_BYTES || n > (uint64_t) SIZE_MAX)
      return stbi__err("too large", "Very large image (corrupt?)");
   if (n == 0)
      return 1;
   *work = (stbi_uc *) STBI_MALLOC((size_t) n);
   if (*work == NULL)
      return stbi__err("outofmem", "Out of memory");
   *worklen = (size_t) n;
   return 1;
}

// returns an UNINITIALIZED w*h buffer of 'pixfmt' and points 'pb' at it.
// it is deliberately not zero-filled up front: for a file that merely
// declares huge dimensions that would touch (commit) the whole buffer before
// any pixel data has been read. instead the caller clears only the pixels
// the first frame won't write (stbi__clear_outside) and decodes the first
// frame with the SRC blend, which writes every pixel inside its bounds.
static stbi_uc *stbi__alloc_pixbuf(wuffs_base__pixel_buffer *pb, uint32_t pixfmt, int w, int h, size_t bpp)
{
   wuffs_base__pixel_config pc = wuffs_base__null_pixel_config();
   wuffs_base__status status;
   uint64_t len64 = (uint64_t) w * (uint64_t) h * bpp;
   size_t len;
   stbi_uc *p;

   if (len64 > (uint64_t) SIZE_MAX)
      return stbi__errpuc("too large", "Image too large to decode");
   len = (size_t) len64;
   p = (stbi_uc *) STBI_MALLOC(len);
   if (p == NULL)
      return stbi__errpuc("outofmem", "Out of memory");

   wuffs_base__pixel_config__set(&pc, pixfmt, WUFFS_BASE__PIXEL_SUBSAMPLING__NONE, (uint32_t) w, (uint32_t) h);
   *pb = wuffs_base__null_pixel_buffer();
   status = wuffs_base__pixel_buffer__set_from_slice(pb, &pc, wuffs_base__make_slice_u8(p, len));
   if (status.repr != NULL) {
      STBI_FREE(p);
      stbi__wuffs_err(status);
      return NULL;
   }
   return p;
}

// zeroes the pixels of a w*h image that lie outside rectangle r (the first
// frame's bounds; usually the whole image, so nothing is cleared)
static void stbi__clear_outside(stbi_uc *p, int w, int h, size_t bpp, wuffs_base__rect_ie_u32 r)
{
   size_t row = (size_t) w * bpp;
   uint32_t x0 = r.min_incl_x, x1 = r.max_excl_x, y0 = r.min_incl_y, y1 = r.max_excl_y, yy;
   if (x1 > (uint32_t) w) x1 = (uint32_t) w;
   if (y1 > (uint32_t) h) y1 = (uint32_t) h;
   if (x0 >= x1 || y0 >= y1) {
      memset(p, 0, row * (size_t) h);
      return;
   }
   if (y0 > 0)
      memset(p, 0, row * y0);
   if (y1 < (uint32_t) h)
      memset(p + row * y1, 0, row * ((size_t) h - y1));
   if (x0 > 0 || x1 < (uint32_t) w) {
      for (yy = y0; yy < y1; ++yy) {
         stbi_uc *line = p + row * yy;
         memset(line, 0, x0 * bpp);
         memset(line + x1 * bpp, 0, ((size_t) w - x1) * bpp);
      }
   }
}

// decodes the next frame's config (bounds, disposal, duration), refilling the
// input as needed. returns 0 with the failure reason set on error, and also
// when there are no more frames (reason "no frames").
static int stbi__next_frame_config(stbi__context *s, void *dec, wuffs_base__frame_config *fc, int *end)
{
   wuffs_base__status status;
   *end = 0;
   for (;;) {
      *fc = wuffs_base__null_frame_config();
      status = stbi__dec_frame_config(s->fourcc, dec, fc, &s->buf);
      if (status.repr == NULL)
         return 1;
      if (status.repr == wuffs_base__suspension__short_read) {
         if (stbi__refill(s)) continue;
         return 0; // reason set by stbi__refill
      }
      if (status.repr == wuffs_base__note__end_of_data) {
         *end = 1;
         return stbi__err("no frames", "Corrupt image");
      }
      return stbi__wuffs_err(status);
   }
}

//////////////////////////////////////////////////////////////////////////////
//
//  Pixel conversion (from Wuffs' RGB/RGBA/BGRA16 to stb_image's layouts)

static stbi_uc stbi__compute_y(int r, int g, int b)
{
   return (stbi_uc) (((r*77) + (g*150) +  (29*b)) >> 8);
}

static stbi__uint16 stbi__compute_y_16(int r, int g, int b)
{
   return (stbi__uint16) (((r*77) + (g*150) +  (29*b)) >> 8);
}

// converts npix RGB8 (src_n 3) or RGBA8 (src_n 4) pixels, in place, to
// n-channel stb_image layout (n <= src_n, so writes never overtake reads)
static void stbi__rgbx8_to_n(stbi_uc *p, size_t npix, int src_n, int n)
{
   stbi_uc *d = p;
   size_t i;
   if (n == src_n)
      return;
   switch (n) {
      case 1:
         for (i=0; i < npix; ++i, p += src_n, d += 1)
            d[0] = stbi__compute_y(p[0], p[1], p[2]);
         break;
      case 2:
         for (i=0; i < npix; ++i, p += src_n, d += 2) {
            stbi_uc a = src_n == 4 ? p[3] : 255;
            d[0] = stbi__compute_y(p[0], p[1], p[2]);
            d[1] = a;
         }
         break;
      default: // 3, from 4
         for (i=0; i < npix; ++i, p += src_n, d += 3) {
            stbi_uc r = p[0], g = p[1], b = p[2];
            d[0] = r; d[1] = g; d[2] = b;
         }
         break;
   }
}

// converts npix little-endian BGRA16 pixels, in place, to n-channel 16-bit
// stb_image layout.
static void stbi__bgra16_to_n(stbi_uc *p, size_t npix, int n)
{
   stbi__uint16 *d = (stbi__uint16 *) p;
   size_t i;
   for (i=0; i < npix; ++i, p += 8, d += n) {
      int b = p[0] | (p[1] << 8);
      int g = p[2] | (p[3] << 8);
      int r = p[4] | (p[5] << 8);
      int a = p[6] | (p[7] << 8);
      switch (n) {
         case 1:  d[0] = stbi__compute_y_16(r, g, b); break;
         case 2:  d[0] = stbi__compute_y_16(r, g, b); d[1] = (stbi__uint16) a; break;
         case 3:  d[0] = (stbi__uint16) r; d[1] = (stbi__uint16) g; d[2] = (stbi__uint16) b; break;
         default: d[0] = (stbi__uint16) r; d[1] = (stbi__uint16) g; d[2] = (stbi__uint16) b; d[3] = (stbi__uint16) a; break;
      }
   }
}

// shrinks a buffer after in-place conversion; keeps the old one on failure.
// copies into a fresh STBI_MALLOC block rather than calling STBI_REALLOC with
// a smaller size: stb_image only ever reallocs to grow, and a custom
// STBI_REALLOC_SIZED written for that (malloc newsz, copy oldsz) would
// overflow if handed a shrink.
static void *stbi__shrink(void *p, size_t oldsz, size_t newsz)
{
   void *q;
   if (newsz >= oldsz)
      return p;
   q = STBI_MALLOC(newsz ? newsz : 1);
   if (q == NULL)
      return p;
   memcpy(q, p, newsz);
   STBI_FREE(p);
   return q;
}

static void stbi__vertical_flip(void *image, int w, int h, int bytes_per_pixel)
{
   int row;
   size_t bytes_per_row = (size_t)w * bytes_per_pixel;
   stbi_uc temp[2048];
   stbi_uc *bytes = (stbi_uc *)image;

   for (row = 0; row < (h>>1); row++) {
      stbi_uc *row0 = bytes + row*bytes_per_row;
      stbi_uc *row1 = bytes + (h - row - 1)*bytes_per_row;
      // swap row0 with row1
      size_t bytes_left = bytes_per_row;
      while (bytes_left) {
         size_t bytes_copy = (bytes_left < sizeof(temp)) ? bytes_left : sizeof(temp);
         memcpy(temp, row0, bytes_copy);
         memcpy(row0, row1, bytes_copy);
         memcpy(row1, temp, bytes_copy);
         row0 += bytes_copy;
         row1 += bytes_copy;
         bytes_left -= bytes_copy;
      }
   }
}

//////////////////////////////////////////////////////////////////////////////
//
//  Main decode paths

// A JPEG whose data ends early (in the entropy-coded pixel data, after a
// valid header) is accepted, like libjpeg and stb_image do. The pixels are
// exactly what Wuffs wrote: it writes every pixel, filling the part the data
// never reached itself (checked by tests/uninit_check.c). Only running out of input
// qualifies; any other JPEG error, and truncation of any other format, still
// fails.
static int stbi__jpeg_truncated(stbi__context *s, wuffs_base__status status)
{
#ifndef STBI_NO_JPEG
   if (s->fourcc != WUFFS_BASE__FOURCC__JPEG || s->io_error)
      return 0;
   if (status.repr == wuffs_jpeg__error__truncated_input)
      return 1;
   // a suspension we couldn't satisfy: the input is closed and exhausted
   return status.repr == wuffs_base__suspension__short_read && s->buf.meta.closed;
#else
   STBI_NOTUSED(s);
   STBI_NOTUSED(status);
   return 0;
#endif
}

// shrinks a converted result to its final size and reports its shape
static void *stbi__finish(void *pixels, size_t oldsz, size_t newsz, int *x, int *y, int *comp, int w, int h, int n_file)
{
   *x = w;
   *y = h;
   *comp = n_file;
   return stbi__shrink(pixels, oldsz, newsz);
}

// decodes the first frame. *bpc is the bit depth of the result: 8 or 16, or
// 0 for the file's own depth (16 for files with more than 8 bits per
// channel, else 8), in which case *bpc is set to the depth chosen.
static void *stbi__load_main(stbi__context *s, int *x, int *y, int *comp, int req_comp, int *bpc_io)
{
   wuffs_base__image_config ic;
   void *dec;
   wuffs_base__pixel_buffer pb;
   wuffs_base__status status;
   stbi_uc *pixels, *work;
   size_t worklen, npix, i;
   uint32_t pixfmt;
   int w, h, n_file, n_out, use16, out_bytes, bpp, bpc;

   if (req_comp < 0 || req_comp > 4)
      return stbi__errpuc("bad req_comp", "Internal error");

   dec = stbi__open(s, &ic);
   if (dec == NULL)
      return NULL;
   if (!stbi__get_dims(&ic, &w, &h)) {
      STBI_FREE(dec);
      return NULL;
   }
   n_file = stbi__file_channels(s, dec, &ic);
   n_out = req_comp ? req_comp : n_file;
   // files with more than 8 bits per channel are always decoded at 16 bits,
   // so that grey conversion runs at full precision before any reduction to
   // 8 bits, as in stb_image.
   use16 = stbi__is_16bit_format(&ic);
   bpc = *bpc_io ? *bpc_io : (use16 ? 16 : 8);
   *bpc_io = bpc;
   out_bytes = bpc / 8;
   npix = (size_t) w * (size_t) h;
   if ((uint64_t) w * (uint64_t) h * (uint64_t) n_out * (uint64_t) out_bytes > (uint64_t) INT_MAX) {
      STBI_FREE(dec);
      return stbi__errpuc("too large", "Image too large to decode");
   }
#ifndef STBI_NO_WEBP
   // see stbi__buffer_all: works around a Wuffs bug in streamed WebP alpha
   if (s->fourcc == WUFFS_BASE__FOURCC__WEBP && !stbi__buffer_all(s)) {
      STBI_FREE(dec);
      return NULL;
   }
#endif

   if (use16) {
      pixfmt = WUFFS_BASE__PIXEL_FORMAT__BGRA_NONPREMUL_4X16LE; bpp = 8;
   } else if ((n_out == 3 || n_out == 1) && (n_file == 3 || n_file == 1)) {
      // RGB is the requested layout (3), or what grey is computed from (1).
      // only for files without alpha: Wuffs composites alpha onto black when
      // it writes RGB, while stb_image simply drops the alpha channel.
      pixfmt = WUFFS_BASE__PIXEL_FORMAT__RGB; bpp = 3;
   } else {
      // RGBA is the requested layout (4), or what grey+alpha comes from (2)
      pixfmt = WUFFS_BASE__PIXEL_FORMAT__RGBA_NONPREMUL; bpp = 4;
   }

   pixels = stbi__alloc_pixbuf(&pb, pixfmt, w, h, bpp);
   if (pixels == NULL) {
      STBI_FREE(dec);
      return NULL;
   }
   if (!stbi__alloc_workbuf(s, dec, w, h, &work, &worklen)) {
      STBI_FREE(pixels);
      STBI_FREE(dec);
      return NULL;
   }

   {
      wuffs_base__frame_config fc;
      int end;
      if (!stbi__next_frame_config(s, dec, &fc, &end)) {
         if (work) STBI_FREE(work);
         STBI_FREE(pixels);
         STBI_FREE(dec);
         return NULL;
      }
      stbi__clear_outside(pixels, w, h, (size_t) bpp, wuffs_base__frame_config__bounds(&fc));
   }

   for (;;) {
      status = stbi__dec_frame(s->fourcc, dec, &pb, &s->buf, WUFFS_BASE__PIXEL_BLEND__SRC,
                               wuffs_base__make_slice_u8(work, worklen));
      if (status.repr == NULL)
         break;
      if (status.repr == wuffs_base__suspension__short_read) {
         if (stbi__refill(s)) continue;
      }
      if (stbi__jpeg_truncated(s, status))
         break;
      if (status.repr != wuffs_base__suspension__short_read)
         stbi__wuffs_err(status);
      if (work) STBI_FREE(work);
      STBI_FREE(pixels);
      STBI_FREE(dec);
      return NULL;
   }
   if (s->drain_trailer) {
      // Wuffs stops reading once it has the pixels. walk the rest of the
      // image (e.g. PNG chunks up to IEND) so that a FILE is left just past
      // it, as stb_image does. problems in the trailer are not errors.
      const char *keep = stbi__g_failure_reason;
      for (;;) {
         status = stbi__dec_frame_config(s->fourcc, dec, NULL, &s->buf);
         if (status.repr == NULL) continue;
         if (status.repr == wuffs_base__suspension__short_read && stbi__refill(s)) continue;
         break;
      }
      stbi__g_failure_reason = keep;
   }
   if (work) STBI_FREE(work);
   STBI_FREE(dec);

   if (use16) {
      stbi__bgra16_to_n(pixels, npix, n_out);
      if (bpc == 8) {
         // top half of each 16-bit value, as stb_image; in place, front to back
         stbi__uint16 *wide = (stbi__uint16 *) pixels;
         for (i=0; i < npix * n_out; ++i)
            pixels[i] = (stbi_uc) (wide[i] >> 8);
      }
      return stbi__finish(pixels, npix * 8, npix * n_out * out_bytes, x, y, comp, w, h, n_file);
   }

   stbi__rgbx8_to_n(pixels, npix, bpp, n_out);

   if (bpc == 16) {
      // 8-bit file, 16-bit result: replicate to high and low byte, maps 0->0, 255->0xffff
      stbi__uint16 *wide = (stbi__uint16 *) STBI_MALLOC(npix * n_out * 2);
      if (wide == NULL) {
         STBI_FREE(pixels);
         return stbi__errpuc("outofmem", "Out of memory");
      }
      for (i=0; i < npix * n_out; ++i)
         wide[i] = (stbi__uint16) ((pixels[i] << 8) + pixels[i]);
      STBI_FREE(pixels);
      return stbi__finish(wide, npix * n_out * 2, npix * n_out * 2, x, y, comp, w, h, n_file);
   }
   return stbi__finish(pixels, npix * (size_t) bpp, npix * n_out, x, y, comp, w, h, n_file);
}

static unsigned char *stbi__load_and_postprocess_8bit(stbi__context *s, int *x, int *y, int *comp, int req_comp)
{
   int w, h, c, bpc = 8;
   unsigned char *result = (unsigned char *) stbi__load_main(s, &w, &h, &c, req_comp, &bpc);

   if (result == NULL)
      return NULL;

   if (stbi__vertically_flip_on_load)
      stbi__vertical_flip(result, w, h, (req_comp ? req_comp : c) * (int) sizeof(stbi_uc));

   if (x) *x = w;
   if (y) *y = h;
   if (comp) *comp = c;
   return result;
}

static stbi__uint16 *stbi__load_and_postprocess_16bit(stbi__context *s, int *x, int *y, int *comp, int req_comp)
{
   int w, h, c, bpc = 16;
   stbi__uint16 *result = (stbi__uint16 *) stbi__load_main(s, &w, &h, &c, req_comp, &bpc);

   if (result == NULL)
      return NULL;

   if (stbi__vertically_flip_on_load)
      stbi__vertical_flip(result, w, h, (req_comp ? req_comp : c) * (int) sizeof(stbi__uint16));

   if (x) *x = w;
   if (y) *y = h;
   if (comp) *comp = c;
   return result;
}

static int stbi__info_main(stbi__context *s, int *x, int *y, int *comp)
{
   wuffs_base__image_config ic;
   void *dec = stbi__open(s, &ic);
   int w, h, c;

   if (dec == NULL)
      return 0;
   if (!stbi__get_dims(&ic, &w, &h)) {
      STBI_FREE(dec);
      return 0;
   }
   c = stbi__file_channels(s, dec, &ic);
   STBI_FREE(dec);
   if (x) *x = w;
   if (y) *y = h;
   if (comp) *comp = c;
   return 1;
}

static int stbi__is_16_main(stbi__context *s)
{
   wuffs_base__image_config ic;
   void *dec = stbi__open(s, &ic);
   int r;

   if (dec == NULL)
      return 0;
   r = stbi__is_16bit_format(&ic);
   STBI_FREE(dec);
   return r;
}

//////////////////////////////////////////////////////////////////////////////
//
//  Public entry points

#ifndef STBI_NO_STDIO

#if defined(_WIN32) && defined(STBI_WINDOWS_UTF8)
STBI_EXTERN __declspec(dllimport) int __stdcall MultiByteToWideChar(unsigned int cp, unsigned long flags, const char *str, int cbmb, wchar_t *widestr, int cchwide);
STBI_EXTERN __declspec(dllimport) int __stdcall WideCharToMultiByte(unsigned int cp, unsigned long flags, const wchar_t *widestr, int cchwide, char *str, int cbmb, const char *defchar, int *used_default);
#endif

#if defined(_WIN32) && defined(STBI_WINDOWS_UTF8)
STBIDEF int stbi_convert_wchar_to_utf8(char *buffer, size_t bufferlen, const wchar_t* input)
{
   int r;
   if (bufferlen > (size_t) INT_MAX)
      bufferlen = (size_t) INT_MAX;
   r = WideCharToMultiByte(65001 /* UTF8 */, 0, input, -1, buffer, (int) bufferlen, NULL, NULL);
   if (r == 0 && buffer != NULL && bufferlen > 0)
      buffer[0] = 0; // don't leave a partial, unterminated conversion behind
   return r;
}
#endif

// opens with full sharing on Windows (like fopen; fopen_s refuses files
// another process has open for writing)
static FILE *stbi__fopen(char const *filename, char const *mode)
{
   FILE *f = NULL;
   if (filename == NULL || mode == NULL)
      return NULL;
#if defined(_WIN32) && defined(STBI_WINDOWS_UTF8)
   {
      // any path length; invalid UTF-8 is an error, not silently replaced
      wchar_t wMode[64];
      wchar_t *wFilename;
      int n = MultiByteToWideChar(65001 /* UTF8 */, 8 /* MB_ERR_INVALID_CHARS */, filename, -1, NULL, 0);
      if (n <= 0)
         return NULL;
      if (0 == MultiByteToWideChar(65001 /* UTF8 */, 8 /* MB_ERR_INVALID_CHARS */, mode, -1, wMode, sizeof(wMode)/sizeof(*wMode)))
         return NULL;
      wFilename = (wchar_t *) STBI_MALLOC((size_t) n * sizeof(wchar_t));
      if (wFilename == NULL)
         return NULL;
      if (MultiByteToWideChar(65001 /* UTF8 */, 8 /* MB_ERR_INVALID_CHARS */, filename, -1, wFilename, n) == n)
         f = _wfsopen(wFilename, wMode, _SH_DENYNO);
      STBI_FREE(wFilename);
   }
#elif defined(_WIN32) && !defined(__CYGWIN__)
   f = _fsopen(filename, mode, _SH_DENYNO);
#else
   f = fopen(filename, mode);
#endif
   return f;
}


STBIDEF stbi_uc *stbi_load(char const *filename, int *x, int *y, int *comp, int req_comp)
{
   FILE *f = stbi__fopen(filename, "rb");
   unsigned char * result;
   int w = 0, h = 0, c = 0;
   if (!f) return stbi__errpuc("can't fopen", "Unable to open file");
   result = stbi_load_from_file(f, &w, &h, &c, req_comp);
   // fclose's result is deliberately ignored: the C standard closes the
   // stream whether or not it succeeds, and a read-only FILE has nothing
   // to flush, so a failure says nothing about the (already validated)
   // bytes that were read. failing here would only discard a good image.
   (void) fclose(f);
   if (result) {
      if (x) *x = w;
      if (y) *y = h;
      if (comp) *comp = c;
   }
   return result;
}

STBIDEF stbi_uc *stbi_load_from_file(FILE *f, int *x, int *y, int *comp, int req_comp)
{
   unsigned char *result;
   int w, h, c;
   stbi__context s;
   if (!stbi__start_file(&s,f)) return NULL;
   result = stbi__load_and_postprocess_8bit(&s,&w,&h,&c,req_comp);
   if (result && !stbi__unget_file(&s)) {   // leave f just past the image
      STBI_FREE(result);
      result = NULL;
   } else if (!result) {
      (void) stbi__rewind_file(&s);           // leave f where it was; keep the load's reason
   }
   stbi__end(&s);
   if (result) {
      if (x) *x = w;
      if (y) *y = h;
      if (comp) *comp = c;
   }
   return result;
}

STBIDEF stbi__uint16 *stbi_load_from_file_16(FILE *f, int *x, int *y, int *comp, int req_comp)
{
   stbi__uint16 *result;
   int w, h, c;
   stbi__context s;
   if (!stbi__start_file(&s,f)) return NULL;
   result = stbi__load_and_postprocess_16bit(&s,&w,&h,&c,req_comp);
   if (result && !stbi__unget_file(&s)) {   // leave f just past the image
      STBI_FREE(result);
      result = NULL;
   } else if (!result) {
      (void) stbi__rewind_file(&s);           // leave f where it was; keep the load's reason
   }
   stbi__end(&s);
   if (result) {
      if (x) *x = w;
      if (y) *y = h;
      if (comp) *comp = c;
   }
   return result;
}

STBIDEF stbi_us *stbi_load_16(char const *filename, int *x, int *y, int *comp, int req_comp)
{
   FILE *f = stbi__fopen(filename, "rb");
   stbi__uint16 * result;
   int w = 0, h = 0, c = 0;
   if (!f) return (stbi_us *) stbi__errpuc("can't fopen", "Unable to open file");
   result = stbi_load_from_file_16(f, &w, &h, &c, req_comp);
   // fclose's result is deliberately ignored: the C standard closes the
   // stream whether or not it succeeds, and a read-only FILE has nothing
   // to flush, so a failure says nothing about the (already validated)
   // bytes that were read. failing here would only discard a good image.
   (void) fclose(f);
   if (result) {
      if (x) *x = w;
      if (y) *y = h;
      if (comp) *comp = c;
   }
   return result;
}


#endif //!STBI_NO_STDIO

STBIDEF stbi_us *stbi_load_16_from_memory(stbi_uc const *buffer, int len, int *x, int *y, int *channels_in_file, int desired_channels)
{
   stbi__context s;
   stbi__start_mem(&s,buffer,len);
   return stbi__load_and_postprocess_16bit(&s,x,y,channels_in_file,desired_channels);
}

STBIDEF stbi_us *stbi_load_16_from_callbacks(stbi_io_callbacks const *clbk, void *user, int *x, int *y, int *channels_in_file, int desired_channels)
{
   stbi__context s;
   stbi_us *result;
   if (!stbi__start_callbacks(&s, clbk, user)) return NULL;
   result = stbi__load_and_postprocess_16bit(&s,x,y,channels_in_file,desired_channels);
   stbi__end(&s);
   return result;
}

STBIDEF stbi_uc *stbi_load_from_memory(stbi_uc const *buffer, int len, int *x, int *y, int *comp, int req_comp)
{
   stbi__context s;
   stbi__start_mem(&s,buffer,len);
   return stbi__load_and_postprocess_8bit(&s,x,y,comp,req_comp);
}

STBIDEF stbi_uc *stbi_load_from_callbacks(stbi_io_callbacks const *clbk, void *user, int *x, int *y, int *comp, int req_comp)
{
   stbi__context s;
   stbi_uc *result;
   if (!stbi__start_callbacks(&s, clbk, user)) return NULL;
   result = stbi__load_and_postprocess_8bit(&s,x,y,comp,req_comp);
   stbi__end(&s);
   return result;
}

#ifndef STBI_NO_GIF
// frame duration (flicks: 705600000 per second) to whole milliseconds,
// clamped to [0, INT_MAX]. divides first: a corrupt file can carry a duration
// near INT64_MAX, and multiplying that by 1000 would overflow.
static int stbi__flicks_to_ms(wuffs_base__flicks d)
{
   const wuffs_base__flicks per_ms = (wuffs_base__flicks) (WUFFS_BASE__FLICKS_PER_SECOND / 1000);
   wuffs_base__flicks ms;
   if (d <= 0)
      return 0;
   ms = d / per_ms;
   return ms > (wuffs_base__flicks) INT_MAX ? INT_MAX : (int) ms;
}

// decodes every frame, compositing each onto a persistent canvas following
// the format's disposal/blending rules. works for every enabled format
// (animated GIF/APNG; still images give one frame).
STBIDEF stbi_uc *stbi_load_gif_from_memory(stbi_uc const *buffer, int len, int **delays, int *x, int *y, int *z, int *comp, int req_comp)
{
   stbi__context s;
   wuffs_base__image_config ic;
   void *dec;
   wuffs_base__frame_config fc;
   wuffs_base__pixel_buffer pb;
   wuffs_base__status status;
   stbi_uc *canvas = NULL, *prev = NULL, *out = NULL, *work = NULL;
   int *frame_delays = NULL;
   size_t worklen = 0, stride;
   int w, h, n, layers = 0;

   if (delays) *delays = NULL;
   if (req_comp < 0 || req_comp > 4)
      return stbi__errpuc("bad req_comp", "Internal error");

   stbi__start_mem(&s, buffer, len);
   dec = stbi__open(&s, &ic);
   if (dec == NULL)
      return NULL;
   if (!stbi__get_dims(&ic, &w, &h))
      goto fail;
   n = req_comp ? req_comp : 4;
   stride = (size_t) w * (size_t) h * 4;
   if ((uint64_t) stride > (uint64_t) INT_MAX) {
      stbi__err("too large", "Image too large to decode");
      goto fail;
   }

   canvas = stbi__alloc_pixbuf(&pb, WUFFS_BASE__PIXEL_FORMAT__RGBA_NONPREMUL, w, h, 4);
   if (canvas == NULL || !stbi__alloc_workbuf(&s, dec, w, h, &work, &worklen))
      goto fail;

   for (;;) {
      wuffs_base__animation_disposal disposal;
      wuffs_base__pixel_blend blend;
      stbi_uc *grown;

      int end;
      if (!stbi__next_frame_config(&s, dec, &fc, &end)) {
         // end of the animation, or an invalid file. an invalid file is
         // rejected as a whole: unlike stb_image, the frames decoded before
         // a corrupt or truncated one are not returned
         if (end && layers > 0)
            break;
         goto fail;
      }
      if (layers == 0)
         // the canvas starts uninitialized: clear what the first frame
         // won't cover; the first frame itself is drawn with SRC below
         stbi__clear_outside(canvas, w, h, 4, wuffs_base__frame_config__bounds(&fc));

      disposal = wuffs_base__frame_config__disposal(&fc);
      if (disposal == WUFFS_BASE__ANIMATION_DISPOSAL__RESTORE_PREVIOUS) {
         if (prev == NULL) {
            prev = (stbi_uc *) STBI_MALLOC(stride);
            if (prev == NULL) goto outofmem;
         }
         memcpy(prev, canvas, stride);
      }

      // over the cleared (transparent) canvas, SRC_OVER and SRC give the
      // same first frame, but only SRC writes every pixel of its bounds
      blend = (layers == 0 || wuffs_base__frame_config__overwrite_instead_of_blend(&fc))
            ? WUFFS_BASE__PIXEL_BLEND__SRC : WUFFS_BASE__PIXEL_BLEND__SRC_OVER;
      status = stbi__dec_frame(s.fourcc, dec, &pb, &s.buf, blend, wuffs_base__make_slice_u8(work, worklen));
      if (status.repr != NULL) {
         stbi__wuffs_err(status);
         goto fail;
      }

      if ((uint64_t) (layers + 1) * (uint64_t) stride > (uint64_t) INT_MAX) {
         stbi__err("too large", "Image too large to decode");
         goto fail;
      }
      grown = (stbi_uc *) (out ? STBI_REALLOC_SIZED(out, layers * stride, (layers + 1) * stride)
                               : STBI_MALLOC(stride));
      if (grown == NULL) goto outofmem;
      out = grown;
      memcpy(out + layers * stride, canvas, stride);

      if (delays) {
         int *grown_delays = (int *) (frame_delays
               ? STBI_REALLOC_SIZED(frame_delays, layers * sizeof(int), (layers + 1) * sizeof(int))
               : STBI_MALLOC(sizeof(int)));
         if (grown_delays == NULL) goto outofmem;
         frame_delays = grown_delays;
         frame_delays[layers] = stbi__flicks_to_ms(wuffs_base__frame_config__duration(&fc));
      }
      ++layers;

      if (disposal == WUFFS_BASE__ANIMATION_DISPOSAL__RESTORE_BACKGROUND) {
         status = wuffs_base__pixel_buffer__set_color_u32_argb_premul_fill_rect(
               &pb, wuffs_base__frame_config__bounds(&fc), wuffs_base__frame_config__background_color(&fc));
         if (status.repr != NULL) {
            stbi__wuffs_err(status);
            goto fail;
         }
      } else if (disposal == WUFFS_BASE__ANIMATION_DISPOSAL__RESTORE_PREVIOUS) {
         memcpy(canvas, prev, stride);
      }
   }

   STBI_FREE(canvas);
   if (prev) STBI_FREE(prev);
   if (work) STBI_FREE(work);
   STBI_FREE(dec);

   stbi__rgbx8_to_n(out, (size_t) layers * w * h, 4, n);
   out = (stbi_uc *) stbi__shrink(out, layers * stride, (size_t) layers * w * h * n);

   if (stbi__vertically_flip_on_load) {
      int i;
      for (i=0; i < layers; ++i)
         stbi__vertical_flip(out + (size_t) i * w * h * n, w, h, n);
   }

   if (x) *x = w;
   if (y) *y = h;
   if (z) *z = layers;
   if (comp) *comp = 4;
   if (delays) *delays = frame_delays;
   return out;

outofmem:
   stbi__err("outofmem", "Out of memory");
fail:
   if (canvas) STBI_FREE(canvas);
   if (prev) STBI_FREE(prev);
   if (work) STBI_FREE(work);
   if (out) STBI_FREE(out);
   if (frame_delays) STBI_FREE(frame_delays);
   STBI_FREE(dec);
   return NULL;
}
#endif

#ifndef STBI_NO_LINEAR
// one color value c in [0,1] to linear, times the scale. gamma 1.0 means
// no curve. gamma 2.2 (the default) is stb_image's stand-in for sRGB: with
// STBI_LDR_TO_HDR_SRGB it becomes the exact sRGB curve. any other gamma is
// pow(c, gamma), as in stb_image.
static float stbi__l2h_curve(float c, float gamma, float scale)
{
   if (gamma == 1.0f)
      return c * scale;
#ifdef STBI_LDR_TO_HDR_SRGB
   if (gamma == 2.2f) {
      // the sRGB decoding function (IEC 61966-2-1): linear below 0.04045,
      // the 2.4 power curve above it
      float lin = c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
      return lin * scale;
   }
#endif
   // the same pow() as stb_image
   return (float) (pow(c, gamma) * scale);
}

// converts an 8-bit (bpc 8) or 16-bit (bpc 16) image to float and frees it.
// color goes through stbi__l2h_curve; alpha (the last channel when comp is
// even) is only normalized, as in stb_image. pow() can't fail for the sRGB
// curve; in the default path a gamma that makes pow() fail (e.g. negative)
// still gives stb_image's inf/NaN results, but doesn't leave errno changed
// behind a successful load.
static float   *stbi__ldr_to_hdr(void *data, int x, int y, int comp, int bpc)
{
   size_t i, npix = (size_t) x * (size_t) y;
   int k, n, saved_errno;
   // read once, so one image never mixes two settings
   float gamma = stbi__l2h_gamma, scale = stbi__l2h_scale;
   float *output;
   if (!data) return NULL;
   if ((uint64_t) x * (uint64_t) y * (uint64_t) comp * sizeof(float) > (uint64_t) INT_MAX) {
      STBI_FREE(data);
      return stbi__errpf("too large", "Image too large to decode");
   }
   output = (float *) STBI_MALLOC(npix * (size_t) comp * sizeof(float));
   if (output == NULL) { STBI_FREE(data); return stbi__errpf("outofmem", "Out of memory"); }
   // compute number of non-alpha components
   if (comp & 1) n = comp; else n = comp-1;
   saved_errno = errno;
   if (bpc == 16) {
      // 65536 possible values: too many for a table, so each sample is
      // converted on its own
      const stbi__uint16 *d = (const stbi__uint16 *) data;
      for (i=0; i < npix; ++i) {
         for (k=0; k < n; ++k)
            output[i*comp + k] = stbi__l2h_curve((float) d[i*comp + k] / 65535.0f, gamma, scale);
         if (n < comp)
            output[i*comp + n] = (float) d[i*comp + n] / 65535.0f;
      }
   } else {
      // 256 possible values: convert each once
      const stbi_uc *d = (const stbi_uc *) data;
      float lut[256];
      for (k=0; k < 256; ++k)
         lut[k] = stbi__l2h_curve((float) k / 255.0f, gamma, scale);
      for (i=0; i < npix; ++i) {
         for (k=0; k < n; ++k)
            output[i*comp + k] = lut[d[i*comp + k]];
         if (n < comp)
            output[i*comp + n] = d[i*comp + n]/255.0f;
      }
   }
   errno = saved_errno;
   STBI_FREE(data);
   return output;
}

// 16-bit files are converted from their full 16-bit values, unless
// STBI_LOADF_VIA_8BIT asks for stb_image's way (through 8 bits)
static float *stbi__loadf_main(stbi__context *s, int *x, int *y, int *comp, int req_comp)
{
   int w, h, c;
#ifdef STBI_LOADF_VIA_8BIT
   int bpc = 8;
#else
   int bpc = 0;
#endif
   float *result;
   void *data = stbi__load_main(s, &w, &h, &c, req_comp, &bpc);
   if (data == NULL)
      return NULL;
   if (stbi__vertically_flip_on_load)
      stbi__vertical_flip(data, w, h, (req_comp ? req_comp : c) * (bpc / 8));
   result = stbi__ldr_to_hdr(data, w, h, req_comp ? req_comp : c, bpc);
   if (result) {
      if (x) *x = w;
      if (y) *y = h;
      if (comp) *comp = c;
   }
   return result;
}

STBIDEF float *stbi_loadf_from_memory(stbi_uc const *buffer, int len, int *x, int *y, int *comp, int req_comp)
{
   stbi__context s;
   stbi__start_mem(&s,buffer,len);
   return stbi__loadf_main(&s,x,y,comp,req_comp);
}

STBIDEF float *stbi_loadf_from_callbacks(stbi_io_callbacks const *clbk, void *user, int *x, int *y, int *comp, int req_comp)
{
   stbi__context s;
   float *result;
   if (!stbi__start_callbacks(&s, clbk, user)) return NULL;
   result = stbi__loadf_main(&s,x,y,comp,req_comp);
   stbi__end(&s);
   return result;
}

#ifndef STBI_NO_STDIO
STBIDEF float *stbi_loadf(char const *filename, int *x, int *y, int *comp, int req_comp)
{
   FILE *f = stbi__fopen(filename, "rb");
   float * result;
   int w = 0, h = 0, c = 0;
   if (!f) return stbi__errpf("can't fopen", "Unable to open file");
   result = stbi_loadf_from_file(f, &w, &h, &c, req_comp);
   // fclose's result is deliberately ignored: the C standard closes the
   // stream whether or not it succeeds, and a read-only FILE has nothing
   // to flush, so a failure says nothing about the (already validated)
   // bytes that were read. failing here would only discard a good image.
   (void) fclose(f);
   if (result) {
      if (x) *x = w;
      if (y) *y = h;
      if (comp) *comp = c;
   }
   return result;
}

STBIDEF float *stbi_loadf_from_file(FILE *f, int *x, int *y, int *comp, int req_comp)
{
   float *result;
   int w, h, c;
   stbi__context s;
   if (!stbi__start_file(&s,f)) return NULL;
   result = stbi__loadf_main(&s,&w,&h,&c,req_comp);
   if (result && !stbi__unget_file(&s)) {   // leave f just past the image
      STBI_FREE(result);
      result = NULL;
   } else if (!result) {
      (void) stbi__rewind_file(&s);           // leave f where it was; keep the load's reason
   }
   stbi__end(&s);
   if (result) {
      if (x) *x = w;
      if (y) *y = h;
      if (comp) *comp = c;
   }
   return result;
}
#endif // !STBI_NO_STDIO

#endif // !STBI_NO_LINEAR

// there is no HDR decoder, so nothing is ever HDR. these are defined for
// API compatibility.

STBIDEF int stbi_is_hdr_from_memory(stbi_uc const *buffer, int len)
{
   STBI_NOTUSED(buffer);
   STBI_NOTUSED(len);
   return 0;
}

#ifndef STBI_NO_STDIO
STBIDEF int      stbi_is_hdr          (char const *filename)
{
   STBI_NOTUSED(filename);
   return 0;
}

STBIDEF int stbi_is_hdr_from_file(FILE *f)
{
   STBI_NOTUSED(f);
   return 0;
}
#endif // !STBI_NO_STDIO

STBIDEF int      stbi_is_hdr_from_callbacks(stbi_io_callbacks const *clbk, void *user)
{
   STBI_NOTUSED(clbk);
   STBI_NOTUSED(user);
   return 0;
}

#ifndef STBI_NO_STDIO
STBIDEF int stbi_info(char const *filename, int *x, int *y, int *comp)
{
    FILE *f = stbi__fopen(filename, "rb");
    int result, w = 0, h = 0, c = 0;
    if (!f) return stbi__err("can't fopen", "Unable to open file");
    result = stbi_info_from_file(f, &w, &h, &c);
    (void) fclose(f); // deliberately ignored: read-only FILE (see stbi_load)
    if (result) {
       if (x) *x = w;
       if (y) *y = h;
       if (comp) *comp = c;
    }
    return result;
}

STBIDEF int stbi_info_from_file(FILE *f, int *x, int *y, int *comp)
{
   int r;
   stbi__context s;
   int w, h, c;
   if (!stbi__start_file(&s, f)) return 0;
   r = stbi__info_main(&s,&w,&h,&c);
   if (!stbi__rewind_file(&s)) {   // leave f where it was (not possible on a pipe)
      if (r) stbi__err("seek error", "Error seeking in file");
      r = 0;
   }
   stbi__end(&s);
   if (r) {
      if (x) *x = w;
      if (y) *y = h;
      if (comp) *comp = c;
   }
   return r;
}

STBIDEF int stbi_is_16_bit(char const *filename)
{
    FILE *f = stbi__fopen(filename, "rb");
    int result;
    if (!f) return stbi__err("can't fopen", "Unable to open file");
    result = stbi_is_16_bit_from_file(f);
    (void) fclose(f); // deliberately ignored: read-only FILE (see stbi_load)
    return result;
}

STBIDEF int stbi_is_16_bit_from_file(FILE *f)
{
   int r;
   stbi__context s;
   if (!stbi__start_file(&s, f)) return 0;
   r = stbi__is_16_main(&s);
   if (!stbi__rewind_file(&s)) {   // leave f where it was (not possible on a pipe)
      stbi__err("seek error", "Error seeking in file");
      r = 0;
   }
   stbi__end(&s);
   return r;
}
#endif // !STBI_NO_STDIO

STBIDEF int stbi_info_from_memory(stbi_uc const *buffer, int len, int *x, int *y, int *comp)
{
   stbi__context s;
   stbi__start_mem(&s,buffer,len);
   return stbi__info_main(&s,x,y,comp);
}

STBIDEF int stbi_info_from_callbacks(stbi_io_callbacks const *c, void *user, int *x, int *y, int *comp)
{
   stbi__context s;
   int r;
   if (!stbi__start_callbacks(&s, c, user)) return 0;
   r = stbi__info_main(&s,x,y,comp);
   stbi__end(&s);
   return r;
}

STBIDEF int stbi_is_16_bit_from_memory(stbi_uc const *buffer, int len)
{
   stbi__context s;
   stbi__start_mem(&s,buffer,len);
   return stbi__is_16_main(&s);
}

STBIDEF int stbi_is_16_bit_from_callbacks(stbi_io_callbacks const *c, void *user)
{
   stbi__context s;
   int r;
   if (!stbi__start_callbacks(&s, c, user)) return 0;
   r = stbi__is_16_main(&s);
   stbi__end(&s);
   return r;
}

//////////////////////////////////////////////////////////////////////////////
//
//  ZLIB client (Wuffs' zlib and raw deflate decoders)

#ifndef STBI_NO_ZLIB

// decodes ilen bytes at ibuffer. if expandable, obuffer is ignored and a
// buffer of initially olen bytes is allocated and grown as needed; otherwise
// the output must fit in obuffer[0..olen). parse_header selects zlib framing
// (RFC 1950) vs raw deflate (RFC 1951).
// stands in for a NULL output buffer of length 0. it is never written (there
// is no room), so sharing it between threads is safe; it is static so that
// returning it as the "success" pointer never returns a local's address.
static char stbi__zlib_no_output[1];

static char *stbi__zlib_main(const char *ibuffer, int ilen, char *obuffer, int olen,
                             int expandable, int parse_header, int *outlen)
{
   wuffs_base__io_buffer src, dst;
   wuffs_base__status status;
   wuffs_base__range_ii_u64 wlen;
   uint8_t work_small[64];
   uint8_t *work = work_small;
   size_t worklen;
   size_t sz = parse_header ? sizeof__wuffs_zlib__decoder() : sizeof__wuffs_deflate__decoder();
   void *dec;

   // like stb_image, treat a NULL buffer of length 0 as an empty buffer
   // rather than as bad arguments (no input then fails as truncated; no
   // output room still lets an empty stream succeed with 0 bytes). Wuffs
   // needs non-NULL pointers. the result is only tested for NULL by the
   // *_buffer callers, never dereferenced.
   if (ibuffer == NULL && ilen == 0)
      ibuffer = "";
   if (!expandable && obuffer == NULL && olen == 0)
      obuffer = stbi__zlib_no_output;
   if (expandable && olen < 1)
      olen = 1; // any initial_size <= 0 just starts small

   if (ibuffer == NULL || ilen < 0 || olen < 0 || (!expandable && obuffer == NULL)) {
      stbi__err("bad zlib args", "Internal error");
      return NULL;
   }

   dec = STBI_MALLOC(sz);
   if (dec == NULL) {
      stbi__err("outofmem", "Out of memory");
      return NULL;
   }
   memset(dec, 0, sz);
   if (parse_header) {
      status = wuffs_zlib__decoder__initialize((wuffs_zlib__decoder *) dec, sz, WUFFS_VERSION, WUFFS_INITIALIZE__ALREADY_ZEROED);
      wlen = wuffs_zlib__decoder__workbuf_len((wuffs_zlib__decoder *) dec);
   } else {
      status = wuffs_deflate__decoder__initialize((wuffs_deflate__decoder *) dec, sz, WUFFS_VERSION, WUFFS_INITIALIZE__ALREADY_ZEROED);
      wlen = wuffs_deflate__decoder__workbuf_len((wuffs_deflate__decoder *) dec);
   }
   if (status.repr != NULL) {
      STBI_FREE(dec);
      stbi__err("wuffs version mismatch", "Internal error");
      return NULL;
   }

   worklen = (size_t) wlen.max_incl;
   if (worklen > sizeof(work_small)) {
      work = (uint8_t *) STBI_MALLOC(worklen);
      if (work == NULL) {
         STBI_FREE(dec);
         stbi__err("outofmem", "Out of memory");
         return NULL;
      }
   }

   if (expandable) {
      if (olen < 1) olen = 1;
      obuffer = (char *) STBI_MALLOC((size_t) olen);
      if (obuffer == NULL) goto outofmem;
   }

   src = wuffs_base__ptr_u8__reader((uint8_t *) ibuffer, (size_t) ilen, true);
   dst = wuffs_base__ptr_u8__writer((uint8_t *) obuffer, (size_t) olen);
   for (;;) {
      status = parse_header
             ? wuffs_zlib__decoder__transform_io((wuffs_zlib__decoder *) dec, &dst, &src, wuffs_base__make_slice_u8(work, worklen))
             : wuffs_deflate__decoder__transform_io((wuffs_deflate__decoder *) dec, &dst, &src, wuffs_base__make_slice_u8(work, worklen));
      if (status.repr == NULL)
         break;
      if (status.repr == wuffs_base__suspension__short_write && expandable) {
         size_t newlen;
         char *grown;
         if (dst.data.len > (size_t) (INT_MAX / 2)) {
            stbi__err("too large", "Decompressed data too large");
            goto fail;
         }
         newlen = dst.data.len * 2;
         grown = (char *) STBI_REALLOC_SIZED(obuffer, dst.data.len, newlen);
         if (grown == NULL) goto outofmem;
         obuffer = grown;
         dst.data.ptr = (uint8_t *) grown;
         dst.data.len = newlen;
         continue;
      }
      if (status.repr == wuffs_base__suspension__short_write)
         stbi__err("output buffer limit", "Corrupt PNG");
      else if (status.repr == wuffs_base__suspension__short_read)
         stbi__err("unexpected end", "Corrupt PNG");
      else
         stbi__wuffs_err(status);
      goto fail;
   }

   if (work != work_small) STBI_FREE(work);
   STBI_FREE(dec);
   if (outlen) *outlen = (int) dst.meta.wi;
   return obuffer;

outofmem:
   stbi__err("outofmem", "Out of memory");
fail:
   if (work != work_small) STBI_FREE(work);
   STBI_FREE(dec);
   if (expandable && obuffer) STBI_FREE(obuffer);
   return NULL;
}

STBIDEF char *stbi_zlib_decode_malloc_guesssize(const char *buffer, int len, int initial_size, int *outlen)
{
   return stbi__zlib_main(buffer, len, NULL, initial_size, 1, 1, outlen);
}

STBIDEF char *stbi_zlib_decode_malloc(char const *buffer, int len, int *outlen)
{
   return stbi_zlib_decode_malloc_guesssize(buffer, len, 16384, outlen);
}

STBIDEF char *stbi_zlib_decode_malloc_guesssize_headerflag(const char *buffer, int len, int initial_size, int *outlen, int parse_header)
{
   return stbi__zlib_main(buffer, len, NULL, initial_size, 1, parse_header, outlen);
}

STBIDEF int stbi_zlib_decode_buffer(char *obuffer, int olen, char const *ibuffer, int ilen)
{
   int n;
   if (stbi__zlib_main(ibuffer, ilen, obuffer, olen, 0, 1, &n))
      return n;
   else
      return -1;
}

STBIDEF char *stbi_zlib_decode_noheader_malloc(char const *buffer, int len, int *outlen)
{
   return stbi__zlib_main(buffer, len, NULL, 16384, 1, 0, outlen);
}

STBIDEF int stbi_zlib_decode_noheader_buffer(char *obuffer, int olen, const char *ibuffer, int ilen)
{
   int n;
   if (stbi__zlib_main(ibuffer, ilen, obuffer, olen, 0, 0, &n))
      return n;
   else
      return -1;
}
#endif // !STBI_NO_ZLIB

#endif // STB_IMAGE_IMPLEMENTATION

/*
------------------------------------------------------------------------------
This software is available under 2 licenses -- choose whichever you prefer.
------------------------------------------------------------------------------
ALTERNATIVE A - MIT License
Copyright (c) 2017 Sean Barrett
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
------------------------------------------------------------------------------
ALTERNATIVE B - Public Domain (www.unlicense.org)
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.
In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
------------------------------------------------------------------------------
*/
