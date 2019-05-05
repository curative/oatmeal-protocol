/*
  oatmeal_protocol.h
  Copyright 2019, Shield Diagnostics and the Oatmeal Protocol contributors
  License: Apache 2.0

  Contains the OatmealMsg class and methods to encode/decode messages using the
  Oatmeal Serial protocol.

  Example Oatmeal frame:

    <CMDRxy[1,2,3],2>LJ

    '<'           start of frame
    "CMD"         command
    'R'           flag
    "xy"          token
    "[1,2,3],2"   args
    '>'           end of frame
    'L'           length check byte
    'J'           checksum

  See `protocol.md` for the formal spec.

*/
#ifndef OATMEAL_MESSAGE_H_
#define OATMEAL_MESSAGE_H_

/** Oatmeal C++ library MAJOR version number

We increment the:
 - MAJOR version when we make incompatible API changes
 - MINOR version when we add functionality in a backwards-compatible manner or
         make backwards-compatible bug fixes.

@see `OATMEAL_LIB_VERSION_MINOR`
*/
#define OATMEAL_LIB_VERSION_MAJOR 1
/** Oatmeal C++ library MINOR version number
@see `OATMEAL_LIB_VERSION_MAJOR` */
#define OATMEAL_LIB_VERSION_MINOR 1

/** Oatmeal Protocol MAJOR version number

Oatmeal Protocol version. We increment the:
 - MAJOR version when we make incompatible protocol changes
 - MINOR version when we add functionality in a backwards-compatible manner or
         make backwards-compatible bug fixes.

@see OATMEAL_PROTOCOL_VERSION_MINOR
*/
#define OATMEAL_PROTOCOL_VERSION_MAJOR 1
/** Oatmeal Protocol MINOR version number.
@see `OATMEAL_PROTOCOL_VERSION_MAJOR` */
#define OATMEAL_PROTOCOL_VERSION_MINOR 0


#ifndef OATMEAL_MAX_MSG_LEN
  /** An integer setting the max message size in bytes.

By default set to 127 bytes. Frames longer than this will be quietly dropped.
Frames dropped for being too long will be counted under the `n_frame_too_long`
counter in `OatmealPort`.

`OATMEAL_MAX_MSG_LEN` determines the size of an `OatmealMsg` on the stack, so
setting this to a large value will consume a lot of RAM even if only short
messages are sent and received.

Ideally this should be set to be 1 byte shorter than the max uart output buffer
size of the embedded chip you are using. If messages are longer than the output
buffer your code will block when trying to send while it waits for the buffer to
clear - this is slow since the baud rate of serial is much slower than most chip
clock speeds. */
  #define OATMEAL_MAX_MSG_LEN 127
#endif

const int OATMEAL_CHECKLEN_COEFF = 7;
const int OATMEAL_CHECKSUM_COEFF = 31;

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <inttypes.h>
#include <float.h>
#include <limits.h>

#ifndef LLONG_MAX
  #define ULLONG_MAX (~(unsigned long long)0)
  #define LLONG_MIN ((long long)(ULLONG_MAX))
  #define LLONG_MAX (-(LLONG_MIN+1))
  #define OATMEAL_NO_LLONG
#endif

/**
 * Constants and methods related to formatting Oatmeal frames.
 */
class OatmealFmt {
 private:
  static const int N_ESCAPED_CHARS = 7;
  static constexpr const char * const ESCAPED_CHARS = "\\\"<>\n\r\0";

 public:
  /** Byte used to mark the start of a frame */
  static const char START_BYTE = '<';
  /** Byte used to mark the end of a frame */
  static const char END_BYTE = '>';
  /** Byte used to separate arguments in a frame */
  static const char ARG_SEP = ',';
  /** Byte used to mark the start of a list in args of a frame */
  static const char LIST_START = '[';
  /** Byte used to mark the end of a list in args of a frame */
  static const char LIST_END = ']';
  /** Byte used to mark the start of a dict in args of a frame */
  static const char DICT_START = '{';
  /** Byte used to mark the end of a dict in args of a frame */
  static const char DICT_END = '}';
  /** Byte used to separate key-value pairs e.g. '=' in "key=value" */
  static const char DICT_KV_SEP = '=';

  /** hex characters look up table */
  static constexpr const char * const HEX_CHARS = "0123456789ABCDEF";

  /** Chars used in tokens.
  Printable characters without frame start/end bytes */
  static constexpr const char * const TOKEN_CHARS =
    "0123456789"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz";
  static const size_t N_TOKEN_CHARS = strlen(TOKEN_CHARS);

  /* Formatting strings */

  /** Default number of significant figures for formatting real numbers. */
  static const int DEFAULT_SIG_FIGS = 6;

  static inline void uint32_to_hex(char *hex, uint32_t val) {
    for (int i = 0; i < 8; i++) {
      hex[i] = OatmealFmt::HEX_CHARS[(val >> (28 - i*4)) & 0xf];
    }
    hex[8] = '\0';
  }

  /*
  The following functions append characters to a string, to represent various
  data types. Real numbers (floats, doubles) take a significant figures
  argument.
  Other functions replace this argument with an ignored parameter (`v`).
  */

  /** Encode byte data using Oatmeal's byte representation (without quoting).

  String (utf-8) and raw bytes use the same encoding scheme in Oatmeal Protocol.
  This method does that encoding but doensn't put double quotes around the data,
  as that is datatype dependent.

  @param dst: memory to place encoded data at. Data is not nul-terminated.
  @param max_dst_len: maximum number of bytes we can use to store the encoded
                      output.
  @param src: data to encode
  @param srclen: number of bytes to encode
  @returns The number of bytes used to encode the string in `dst`, or
           0 on failure.
  */
  static size_t encode_bytes(char *dst, size_t max_dst_len,
                             const uint8_t *src, size_t srclen) {
    const uint8_t *src_end = src + srclen;
    char *dst_init = dst, *dst_end = dst + max_dst_len;
    if (max_dst_len < srclen) { return 0; }
    for (; src < src_end && dst+2 <= dst_end; src++) {
      if (*src == '\\') { *(dst++) = '\\'; *(dst++) = '\\'; }
      else if (*src == '"') { *(dst++) = '\\'; *(dst++) = '"'; }
      else if (*src == '<') { *(dst++) = '\\'; *(dst++) = '('; }
      else if (*src == '>') { *(dst++) = '\\'; *(dst++) = ')'; }
      else if (*src == '\n') { *(dst++) = '\\'; *(dst++) = 'n'; }
      else if (*src == '\r') { *(dst++) = '\\'; *(dst++) = 'r'; }
      else if (*src == '\0') { *(dst++) = '\\'; *(dst++) = '0'; }
      else { *(dst++) = *src; }
    }
    /* Squeeze in a char if it's the last char, we have space for it and
       the char doesn't need escaping */
    if (src < src_end && dst+1 <= dst_end &&
        !memchr(ESCAPED_CHARS, *src, N_ESCAPED_CHARS)) {
      *(dst++) = *(src++);
    }
    /* Return zero if we ran out of memory to store the encoded data */
    if (src < src_end || dst > dst_end) { return 0; }
    return dst - dst_init;
  }

  /** Format a (utf-8) string as a message argument.

  @param dst: memory to format into.
  @param dlen: number of bytes in `dst` that can be used (including nul-byte).
  @param src: the value to format
  @param v: (Ignored parameter)
  @returns 0 on error, number of bytes written otherwise (not incl. null byte)
  */
  static size_t format(char *dst, size_t dlen, const char *src, int v = 0) {
    (void)v;  /* ignore last parameter */
    if (src == nullptr) { return format_none(dst, dlen); }
    if (dlen < 3) { return 0; }
    dst[0] = '"';
    size_t unencoded_bytes = strlen(src);
    size_t n = OatmealFmt::encode_bytes(dst+1, dlen-2,
                                        (const uint8_t*)src, unencoded_bytes);
    if (unencoded_bytes > 0 && !n) { dst[0] = '\0'; return 0; }
    dst[n+1] = '"';
    dst[n+2] = '\0';
    return n+2;
  }

  /** Format raw bytes as a message argument.

  @param dst: memory to format into.
  @param dlen: number of bytes in `dst` that can be used (including nul-byte).
  @param src: pointer to the data to be represented.
  @param srclen: number of bytes to be represented.
  @returns 0 on error, number of bytes written otherwise (not incl. null byte)
  */
  static size_t format_bytes(char *dst, size_t dlen,
                             const uint8_t *src, size_t srclen) {
    if (src == nullptr) { return format_none(dst, dlen); }
    if (dlen < 4) { return 0; }
    dst[0] = '0'; /* raw bytes represented by 0"" */
    dst[1] = '"';
    size_t n = OatmealFmt::encode_bytes(dst+2, dlen-3, src, srclen);
    if (srclen > 0 && !n) { dst[0] = '\0'; return 0; }
    dst[n+2] = '"';
    dst[n+3] = '\0';
    return n+3;
  }

  /** Format a (utf-8) string as a message argument.
  @see format(char*, size_t, const char*, int v) */
  static size_t format(char *dst, size_t dlen, char *src, int v = 0) {
    (void)v;  /* ignore last parameter */
    return OatmealFmt::format(dst, dlen, (const char*)src, v);
  }

  /** Format raw bytes as a message argument.
  @see format_bytes(char*, size_t, const uint8_t*, size_t) */
  static size_t format_bytes(char *dst, size_t dlen, uint8_t *src, int v = 0) {
    (void)v;  /* ignore last parameter */
    return OatmealFmt::format_bytes(dst, dlen, (const uint8_t*)src, v);
  }

  /** Format a boolean value as a message argument.

  @param dst: memory to format into.
  @param dlen: number of bytes in `dst` that can be used (including nul-byte).
  @param x: value to format.
  @param v: (Ignored).
  @returns 0 on error, number of bytes written otherwise (not incl. null byte)
  */
  static size_t format(char *dst, size_t dlen, bool x, int v = 0) {
    (void)v;  /* ignore last parameter */
    if (dlen < 2) { return 0; }
    dst[0] = "FT"[x];
    dst[1] = '\0';
    return 1;
  }

  /** Format a double as a message argument.

  Format a double with a given number of significant figures, into the buffer
  pointed to by `dst` of length `dlen`. Formats using snprintf's %g specifier,
  which formats as either as an ordinary decimal number or with scientific
  notation depending upon the number's order of magnitude. See
  https://stackoverflow.com/a/54162153/1709587

  Appends a null byte and returns the number of bytes written (not including
  the null byte) on success.

  @param dst: memory to format into.
  @param dlen: number of bytes in dst that can be used (including nul-byte).
  @param val: the value to format
  @param sig_figs: maximum number of significant figures to use.
  @returns number of bytes written on success, 0 on failure.
  */
  static size_t format(char *dst, size_t dlen, double val,
                       int sig_figs = DEFAULT_SIG_FIGS) {
    int nbytes = snprintf(dst, dlen, "%.*g", sig_figs, val);

    /* snprintf can fail in two ways:
     1. There's an "encoding error" when it returns -1
     2. There isn't enough room in the buffer for it to write everything, in
        which case it writes as much as there's room for and returns the
        number of characters it WANTED to write, just as if there were enough
        room.
     Note that the maximum number of bytes writable by snprintf (dlen) includes
     the null byte but the return value (nbytes) excludes the null byte. */
    if (nbytes <= 0 || (unsigned)nbytes + 1 > dlen) {
      dst[0] = '\0';
      return 0;
    }

    return nbytes;
  }

  /** Format a float as a message argument.
  @see `format(char*, size_t, double, int)` */
  static size_t format(char *dst, size_t dlen, float val,
                       int sig_figs = DEFAULT_SIG_FIGS) {
    return OatmealFmt::format(dst, dlen, static_cast<double>(val), sig_figs);
  }


  /** Format an integer as a message argument.

  Format a single integer as a string and append it along with a null byte to
  the buffer pointed to by `dst` of size `dlen`. The parameter `v` is ignored.

  @param dst: memory to format into.
  @param dlen: number of bytes in dst that can be used (including nul-byte).
  @param val: the value to format
  @param v: ignored.
  @returns The number of bytes written excluding the null byte (0 on failure)
  */
  template<typename T>
  static size_t format(char *dst, size_t dlen, T val, int v = 0) {
    (void)v; /* Ignore last parameter */
    char buf[41];  // long enough for -2**127 (128 bit)
    char *ptr = buf;
    bool neg = false;
    // Construct number backwards
    // Negative handling code looks weird because -min > max value for signed
    // ints (e.g. int8_t range is -128..127). We handle by doing negative mod
    // for first digit.
    if (val == 0) { *ptr = '0'; ptr++; }
    if (val < 0) { neg = true; *ptr = '0' - (val % -10); val /= -10; ptr++; }
    for (; val != 0; val /= 10) { *ptr = '0' + (val % 10); ptr++; }
    if (neg) { *ptr = '-'; ptr++; }
    int nbytes = ptr-buf;
    if (static_cast<uint8_t>(nbytes) > dlen) { return 0; }
    // Reverse copy into destination buffer
    for (int i = 0, j = nbytes-1; i < nbytes; i++, j--) { dst[i] = buf[j]; }
    dst[nbytes] = '\0';
    return nbytes;
  }

  /** Format a missing value (None/NULL/nil).

  @param dst: memory to format None into.
  @param dlen: number of bytes in dst that can be used (including nul-byte).
  @returns The number of bytes written excluding the null byte (0 on failure)
  */
  static size_t format_none(char *dst, size_t dlen) {
    if (dlen < 2) { return 0; }
    dst[0] = 'N';
    dst[1] = '\0';
    return 1;
  }

  /** Format a list as a message argument.

  Format a list of values as a string (e.g. "[1,2,3]") and append it along with
  a NUL-byte to the buffer pointed to by `dst` of size `dlen`. The parameter
  `sig_figs` is only used for formatting floats/doubles.

  @param dst: memory to format None into.
  @param dlen: number of bytes in dst that can be used (including nul-byte).
  @param arr: pointer to an array of values to format
  @param n: number of elements in the list to format
  @param sig_figs: maximum number of significant figures to use for real values
  @returns The number of bytes written excluding the null byte (0 on failure)
  */
  template<typename T>
  static size_t format_list(char *dst, size_t dlen, const T *arr, int16_t n,
                            int sig_figs = DEFAULT_SIG_FIGS) {
    if (arr == nullptr) { return format_none(dst, dlen); }
    char *end = dst+dlen, *ptr = dst;
    *(ptr++) = OatmealFmt::LIST_START;
    if (n > 0) {
      ptr += OatmealFmt::format(ptr, end-ptr, arr[0], sig_figs);
    }
    for (uint16_t i = 1; i < n; i++) {
      *(ptr++) = OatmealFmt::ARG_SEP;
      ptr += OatmealFmt::format(ptr, end-ptr, arr[i], sig_figs);
    }
    *(ptr++) = OatmealFmt::LIST_END;
    *ptr = '\0';
    return ptr-dst;
  }

  /* -- Parsing -- */

 private:
  /**
  Parse a number from the beginning of a string pointed to by `str` of length
  `len`. If valid, the result is stored in `result`. If number continues beyond
  `len` bytes, it is considered an error.
  @returns The number of bytes parsed or 0 on error.
  */
  template<typename T>
  static inline size_t _parse_signed(T *result, const char *str, size_t len,
                                     T min_val, T max_val) {
    *result = 0;
    char *endptr = NULL;
    #ifdef OATMEAL_NO_LLONG
      long tmp = strtol(str, &endptr, 10);
    #else
      long long tmp = strtoll(str, &endptr, 10);
    #endif
    if (!(min_val <= tmp && tmp <= max_val && endptr <= str+len)) { return 0; }
    *result = tmp;
    return endptr-str;
  }

  template<typename T>
  static inline size_t _parse_unsigned(T *result, const char *str, size_t len,
                                       T max_val) {
    *result = 0;
    char *endptr = NULL;
    unsigned long tmp = strtoul(str, &endptr, 10);
    if (!(tmp <= max_val && endptr <= str+len)) { return 0; }
    *result = tmp;
    return endptr-str;
  }

 public:
  /** Parse an integer (signed char) from the start of a string.

  @param result: pointer to memory to store the parsed integer.
  @param str: string start to parse the integer from.
  @param len: max number of bytes of `str` to use (not including any nul-byte).
  @returns The number of bytes parsed or 0 on failure. */
  static inline size_t parse(signed char *result, const char *str, size_t len) {
    return _parse_signed(result, str, len,
                         (signed char)SCHAR_MIN, (signed char)SCHAR_MAX);
  }

  /** Parse an integer message argument (unsigned char) from the start of a string.
  @see parse(signed char*, const char*, size_t) */
  static inline size_t parse(unsigned char *result, const char *str, size_t len) {
    return _parse_unsigned(result, str, len, (unsigned char)UCHAR_MAX);
  }

  /** Parse an integer message argument (short) from the start of a string.
  @see parse(signed char*, const char*, size_t) */
  static inline size_t parse(short *result, const char *str, size_t len) {
    return _parse_signed(result, str, len, (short)SHRT_MIN, (short)SHRT_MAX);
  }

  /** Parse an integer message argument (unsigned short) from the start of a string.
  @see parse(signed char*, const char*, size_t) */
  static inline size_t parse(unsigned short *result, const char *str, size_t len) {
    return _parse_unsigned(result, str, len, (unsigned short)USHRT_MAX);
  }

  /** Parse an integer message argument (int) from the start of a string.
  @see parse(signed char*, const char*, size_t) */
  static inline size_t parse(int *result, const char *str, size_t len) {
    return _parse_signed(result, str, len, (int)INT_MIN, (int)INT_MAX);
  }

  /** Parse an integer message argument (unsigned int) from the start of a string.
  @see parse(signed char*, const char*, size_t) */
  static inline size_t parse(unsigned int *result, const char *str, size_t len) {
    return _parse_unsigned(result, str, len, (unsigned int)UINT_MAX);
  }

  /** Parse an integer message argument (long) from the start of a string.
  @see parse(signed char*, const char*, size_t) */
  static inline size_t parse(long *result, const char *str, size_t len) {
    return _parse_signed(result, str, len, (long)LONG_MIN, (long)LONG_MAX);
  }

  /** Parse an integer message argument (unsigned long) from the start of a string.
  @see parse(signed char*, const char*, size_t) */
  static inline size_t parse(unsigned long *result, const char *str, size_t len) {
    return _parse_unsigned(result, str, len, (unsigned long)ULONG_MAX);
  }

  /** Parse an integer message argument (long long) from the start of a string.
  @see parse(signed char*, const char*, size_t) */
  static inline size_t parse(long long *result, const char *str, size_t len) {
    return _parse_signed(result, str, len,
                         (long long)LLONG_MIN, (long long)LLONG_MAX);
  }

  /** Parse an integer message arg (unsigned long long) from the string start.
  @see parse(signed char*, const char*, size_t) */
  static inline size_t parse(unsigned long long *result,
                             const char *str, size_t len) {
    return _parse_unsigned(result, str, len, (unsigned long long)ULLONG_MAX);
  }

 private:
  template<typename T>
  static inline size_t parse_decimal(T *result, const char *str, size_t len,
                                     T min_val, T max_val) {
    *result = 0;
    char *endptr = NULL;
    // Note: strtof() is missing for some boards
    double tmp = strtod(str, &endptr);
    if (!(min_val <= tmp && tmp <= max_val && endptr <= str+len)) { return 0; }
    *result = tmp;
    return endptr-str;
  }

 public:
  /** Parse a real value message argument (float) from the start of a string.
  @param result: pointer to memory to store the parsed float.
  @param str: string start to parse the real value from.
  @param len: max number of bytes of `str` to use (not including any nul-byte).
  @returns The number of bytes parsed or 0 on failure. */
  static inline size_t parse(float *result, const char *str, size_t len) {
    return parse_decimal(result, str, len, FLT_MIN, FLT_MAX);
  }

  /** Parse a real value message argument (double) from the start of a string.
  @param result: pointer to memory to store the parsed double.
  @param str: string start to parse the real value from.
  @param len: max number of bytes of `str` to use (not including any nul-byte).
  @returns The number of bytes parsed or 0 on failure. */
  static inline size_t parse(double *result, const char *str, size_t len) {
    return parse_decimal(result, str, len, DBL_MIN, DBL_MAX);
  }

  /** Parse a boolean message argument from the start of a string.
  @param result: pointer to memory to store the parsed boolean value.
  @param str: string start to parse the boolean value from.
  @param len: max number of bytes of `str` to use (not including any nul-byte).
  @returns The number of bytes parsed (1 on success) or 0 on failure.  */
  static inline size_t parse(bool *result, const char *str, size_t len) {
    if (len < 1) { return 0; }
    char c = toupper(*str);
    if (c != 'T' && c != 'F') { return 0; }
    *result = (c == 'T');
    return 1;
  }

 private:
  /** Decode data

  @param dst: memory to store decoded message
  @param max_dst_len: maximum number of bytes to decode
                 (fails if decoded data is longer)
  @param src: data to decode
  @param srclen: maximum length of encoded data to decode
                 (fails if encoded data doesn't end within this many encoded-bytes).
  @returns The number of encoded bytes decoded, or 0 on failure. */
  static inline size_t decode_bytes(uint8_t *dst, size_t max_dst_len,
                                    const char *src, size_t srclen,
                                    size_t *dstlen = nullptr) {
    const char *src_init = src, *src_end = src + srclen;
    uint8_t *dst_init = dst, *dst_end = dst + max_dst_len;
    bool backslash_escaped = false;
    if (dstlen) { *dstlen = 0; }
    if (*src != '"') { return 0; }
    for (++src; src < src_end; src++) {
      if (dst >= dst_end) { return 0; }  /* out of memory for result */
      else if (backslash_escaped) {
        if (*src == '\\') { *(dst++) = '\\'; }
        else if (*src == '"') { *(dst++) = '"'; }
        else if (*src == '(') { *(dst++) = '<'; }
        else if (*src == ')') { *(dst++) = '>'; }
        else if (*src == 'n') { *(dst++) = '\n'; }
        else if (*src == 'r') { *(dst++) = '\r'; }
        else if (*src == '0') { *(dst++) = '\0'; }
        else { return 0; } /* Invalid escape sequence */
        backslash_escaped = false;
      } else if (*src == '\\') {
        backslash_escaped = true;
      } else if (*src == '"') {
        break; /* end of string */
      } else {
        *(dst++) = *src;
      }
    }
    /* string didn't hit end-quotes before we hit the end of the encoded data */
    if (src == src_end) { return 0; }
    /* store decoded length if given pointer */
    if (dstlen) { *dstlen = dst - dst_init; }
    src++; /* consume the last quote byte */
    return src - src_init;
  }

 public:
  /** Parse a string arguemnt into a utf-8 string.

  Parse a string pointed to by `str` of length `len` into `dst` of size `n_dst`
  Returns `len` and null terminates `dst`. `dst` must be of length `len+1` to
  be successful to allow for null termination.

  @param dst: Memory to put the string argument into (as a utf-8 string).
  @param n_dst: size of `dst` in bytes to store string including null byte
  @param dstlen: memory to store string length of `dst` in bytes (optional)
  @param src: string to parse a string argument from the beginning of
  @param srclen: the length of the source string and maximum number of bytes
                 we'll try to parse.
  @returns The number of bytes parsed or 0 on error.
  */
  static inline size_t parse_str(char *dst, size_t n_dst, size_t *dstlen,
                                 const char *src, size_t srclen) {
    size_t str_len = 0;
    size_t n = decode_bytes(reinterpret_cast<uint8_t*>(dst), n_dst,
                            src, srclen, &str_len);
    if (n && str_len+1 <= n_dst) {
      dst[str_len] = '\0';
      if (dstlen) { *dstlen = str_len; }
      return n;
    }
    return 0;
  }

  /** Parse bytes encoded as `0"blah"`.

  @param dst: pointer to memory to store byte data in
  @param n_dst: size of `dst` in bytes to store data
  @param dstlen: memory to store length of `dst` in bytes
  @param src: string to parse
  @param srclen: total length of string (will parse as much as possible)
  @returns The number of bytes parsed or 0 on error.
  */
  static inline size_t parse_bytes(uint8_t *dst, size_t n_dst, size_t *dstlen,
                                   const char *src, size_t srclen) {
    if (srclen < 3 || src[0] != '0') { return 0; }
    size_t n = decode_bytes(dst, n_dst, src+1, srclen, dstlen);
    return n ? 1+n : 0;
  }

  /* Parse a None/NULL/nil value, represented by 'N' */
  static inline size_t parse_null(const char *src, size_t srclen) {
    return (srclen > 0 && *src == 'N');
  }

  /* Parse a dictionary key.

  Dictionary keys must match the regex [a-zA-Z0-9_]+ and be followed by an
  equals sign (=). Dictionary keys are not quoted.
  On success the key is copied into `dst` (not including the '='), and null
  terminated. If the key contains invalid bytes or dst isn't big enough to store
  it, parsing fails and returns 0.

  @param dst: memory to copy null-terminated 'key' string into.
  @param n_dst: number of bytes including null byte that can be written to `dst`.
  @param src: string to parse dictionary key from the start of
  @param srclen: length of string `src` i.e. `strlen(src)` (not including null byte)

  @returns number of characters in the key (not including null byte) or
           0 on failure. */
  static inline size_t parse_dict_key(char *dst, size_t n_dst,
                                      const char *src, size_t srclen) {
    char *dst_init = dst, *dst_end = dst+n_dst;
    const char *src_end = src+srclen;
    while (1) {
      if (dst + 1 < dst_end && src < src_end) {
        if (('a' <= *src && *src <= 'z') ||
            ('A' <= *src && *src <= 'Z') ||
            ('0' <= *src && *src <= '9') ||
            *src == '_') {
          *dst = *src;
          dst++; src++;
        } else if (*src == '=') {
          *dst = '\0';
          return dst - dst_init;
        } else { break; }
      } else { break; }
    }
    *dst_init = '\0';
    return 0;
  }
};


/**
An immutable Oatmeal message that doesn't store it's own frame data. Instead
it points to a buffer it does not own.
*/
class OatmealMsgReadonly {
 protected:
  const char *frameptr;  // Pointer to underlying frame (not null-terminated)
  size_t len = 0;  // Length in bytes of this message's frame

 public:
  static const size_t OPCODE_OFFSET = 1;  // opcode is command+flag
  static const size_t CMD_OFFSET    = 1;
  static const size_t FLAG_OFFSET   = 4;
  static const size_t TOKEN_OFFSET  = 5;
  static const size_t ARGS_OFFSET   = 7;

  static const size_t CMD_LEN        = 3;  // HRT
  static const size_t FLAG_LEN       = 1;  // B
  static const size_t TOKEN_LEN      = 2;  // xx
  static const size_t DELIMITERS_LEN = 2;  // <>
  static const size_t CHECKSUM_LEN   = 2;  // checklen, checksum

  static const size_t OPCODE_LEN  = 4;  // opcode is command+flag

  /** Min message length including checksum */
  static const size_t MIN_MSG_LEN = CMD_LEN + FLAG_LEN + TOKEN_LEN +
                                    CHECKSUM_LEN + DELIMITERS_LEN;
  /** Max message length including checksum */
  static const size_t MAX_MSG_LEN = OATMEAL_MAX_MSG_LEN;
  static const size_t MAX_FRAME_END_OFFSET = MAX_MSG_LEN - CHECKSUM_LEN - 1;

  OatmealMsgReadonly(const char *frame, size_t length) :
    frameptr(frame), len(length) {}

  /** Check if this message has the given opcode (command+flag)
  @returns `true` if this message has the given opcode (cmd+flag: 4 bytes) */
  bool is_opcode(const char *opcode_str) const {
    return strncmp(opcode(), opcode_str, OPCODE_LEN) == 0;
  }

  /** Check if this message has the given command.
  @returns `true` if this message has the given command (3 bytes) */
  bool is_command(const char *command) const {
    return strncmp(opcode(), command, CMD_LEN) == 0;
  }

  /** Get a point to this message's underlying frame (byte representation) */
  const char* frame() const { return frameptr; }

  /** Get the length of this message's frame in bytes. */
  size_t length() const { return len; }

  /** Get a pointer to this message's opcode (not null terminated) */
  const char* opcode() const {
    return frameptr + OPCODE_OFFSET;
  }
  /** Get this message's flag character (ASCII encoded) */
  char flag() const {
    return frameptr[FLAG_OFFSET];
  }
  /** Get a pointer to this message's token (ASCII; not null terminated) */
  const char* token() const {
    return frameptr + TOKEN_OFFSET;
  }

  /** Copy this message's command string to the memory passed.
  Copy the command string from this message to the memory pointed to by `dst`
  and null-terminates it. `dst` must be at least `CMD_LEN+1` (4) bytes long.
  @returns `dst` */
  char* copy_cmd(char *dst) const {
    memcpy(dst, opcode(), CMD_LEN);
    dst[CMD_LEN] = '\0';
    return dst;
  }

  /** Copy this message's token string to the memory passed.
  Copy the token string from this message to the memory pointed to by `dst`
  and null-terminates it. `dst` must be at least `TOKEN_LEN+1` (3) bytes long.
  @returns `dst` */
  char* copy_token(char *dst) const {
    memcpy(dst, token(), TOKEN_LEN);
    dst[TOKEN_LEN] = '\0';
    return dst;
  }

  /** Copy this message's command and token strings.
  Copy both command and token strings as null-terminated strings from this
  message to the memory pointed to by the caller.
  @see copy_cmd(char*)
  @see copy_token(char*)
  */
  void copy_cmd_token(char *cmd, char *token) const {
    copy_cmd(cmd);
    copy_token(token);
  }

  /** Get a pointer to the args within this message */
  const char* args() const {
    return frameptr + ARGS_OFFSET;
  }
  /** Get the number of bytes of args in this complete message */
  size_t args_len() const {
    return len - ARGS_OFFSET - CHECKSUM_LEN - 1;
  }

  /** Convert a uint16_t to a printable ASCII char using the Oatmeal mapping. */
  static char checkbyte_uint16_to_ascii(uint16_t v) {
    v = (v % (127-33-2)) + 33;
    v += (v >= '<'); /* '<' == ASCII value 60 */
    v += (v >= '>'); /* '<' == ASCII value 62 */
    return v;
  }

  /** Calculate the length checksum byte for a given message length.
  @returns a ASCII printable byte to use as a length check byte. */
  static char length_checksum(size_t len) {
    return checkbyte_uint16_to_ascii(len * OATMEAL_CHECKLEN_COEFF);
  }

  /** Compute the checksum for an array of bytes.
  @returns The checksum as a printable ASCII character. */
  static char compute_checksum(const char *buf, size_t len) {
    uint8_t checksum = 0;
    for (size_t i = 0; i < len; i++) {
      checksum = (checksum + buf[i]) * OATMEAL_CHECKSUM_COEFF;
    }
    /* Convert checksum into printable ASCII character */
    return checkbyte_uint16_to_ascii(checksum);
  }

  /** Check that an Oatmeal message frame is valid.
  Checks that:
  - the length is within the valid range
  - frame start / end byte are in the correct places
  - length and checksum bytes are correct
  @returns `true` if `buf` points to a valid frame. */
  static bool validate_frame(const char *buf, size_t len) {
    return len >= MIN_MSG_LEN &&
           len <= MAX_MSG_LEN &&
           buf[0] == OatmealFmt::START_BYTE &&
           buf[len-3] == OatmealFmt::END_BYTE &&
           buf[len-2] == length_checksum(len) &&
           buf[len-1] == compute_checksum(buf, len-1);
  }
};

/**
OatmealMsg represents a single message and provides methods to construct it in
a step-wise manner.
*/
class OatmealMsg : public OatmealMsgReadonly {
 private:
  char buf[MAX_MSG_LEN+1];  /* +1 for NUL-byte */

  /** Append a value (int, float, double, bool, string)
  @returns the number of bytes written (no nul-byte) or 0 on failure. */
  template<typename T>
  size_t _write_val(T val, int sig_figs = OatmealFmt::DEFAULT_SIG_FIGS) {
    size_t n = OatmealFmt::format(buf+len, MAX_FRAME_END_OFFSET-len,
                                  val, sig_figs);
    len += n;
    return n;
  }

  size_t reset_len(size_t orig_len) {
    len = orig_len;
    buf[len] = '\0';
    return 0;
  }

  /** Append a value (int, float, double, bool, string)
  @returns the number of bytes written (no nul-byte) or 0 on failure. */
  template<typename T>
  size_t _append_val(T val, int sig_figs = OatmealFmt::DEFAULT_SIG_FIGS) {
    size_t orig_len = len;
    // Next two lines increment len if successful
    separator_if_needed();
    size_t n = _write_val(val, sig_figs);
    if (!n) { return reset_len(orig_len); }
    return len - orig_len;
  }

  template<typename T>
  size_t _append_dict_key_value(const char *key, T val,
                                int sig_figs = OatmealFmt::DEFAULT_SIG_FIGS) {
    size_t orig_len = len;
    if (!append_dict_key(key) || !_append_val(val, sig_figs)) {
      return reset_len(orig_len);
    }
    return len - orig_len;
  }

 public:
  OatmealMsg() : OatmealMsgReadonly(buf, 0) {}

  /** Copy a frame into this message from another to make them identical. */
  void copy_from(const OatmealMsgReadonly &src) {
    len = src.length();
    memcpy(buf, src.frame(), len);
    buf[len] = '\0';
  }

  /* Constructing outgoing Oatmeal messages */

  /** Construct a message with a given command, flag and token
  @returns the number of bytes written (no nul-byte) or 0 on failure. */
  void start(const char *cmd, char flag, const char *token) {
    buf[0] = OatmealFmt::START_BYTE;
    memcpy(buf + CMD_OFFSET, cmd, CMD_LEN);
    buf[FLAG_OFFSET] = flag;
    memcpy(buf + TOKEN_OFFSET, token, TOKEN_LEN);
    len = 1 + CMD_LEN + 1 + TOKEN_LEN;
    buf[len] = '\0';
  }

  /** End a message with a frame end byte and checksum bytes to a message frame.
  After calling this method you cannot add any more arguments.
  @returns the number of bytes written (no nul-byte) or 0 on failure. */
  void finish() {
    /* length_checksum includes the entire frame (end byte and check bytes) */
    char checklen = length_checksum(len+3);
    buf[len++] = OatmealFmt::END_BYTE;
    buf[len++] = checklen;
    /* Checksum includes the length check byte */
    buf[len] = compute_checksum(buf, len); len++;
    buf[len] = '\0';
  }

  /** Append a single raw character and updates `len`.
  @returns the number of bytes written (no nul-byte) or 0 on failure.
  @see OatmealPort::write(const char c) */
  size_t write(const char c) {
    if (len < MAX_FRAME_END_OFFSET) {
      buf[len++] = c;
      buf[len] = '\0';
      return 1;
    }
    return 0;
  }

  /** Write bytes onto the end of the message.
  @returns the number of bytes written (no nul-byte) or 0 on failure.
  @see OatmealPort::write(const char *) */
  size_t write(const char *str) {
    size_t orig_len = len;
    while (*str && len < MAX_FRAME_END_OFFSET) { buf[len++] = *(str++); }
    buf[len] = '\0';
    if (*str) { return reset_len(orig_len); }
    return orig_len - len;
  }

  /** Append a `n` bytes pointed to by `ptr`.
  @returns the number of bytes written (no nul-byte) or 0 on failure.
  @see OatmealPort::write(const char*, size_t) */
  size_t write(const char *ptr, size_t n) {
    if (len + n > MAX_FRAME_END_OFFSET) { return 0; }
    for (size_t i = 0; i < n; i++) { buf[len++] = ptr[i]; }
    buf[len] = '\0';
    return n;
  }

  /** Encode and write a byte to this frame as part of a str/data message argument.
  @returns the number of bytes written (1 on success, 0 on failure).
  @see OatmealPort::write_encoded(const char c) */
  size_t write_encoded(const char c) {
    return write_encoded(&c, 1);
  }

  /** Encode and write `n` bytes pointed to by `ptr`.
  @returns the number of bytes written (no nul-byte) or 0 on failure.
  @see OatmealPort::write_encoded(const char*, size_t) */
  size_t write_encoded(const char *ptr, size_t nbytes) {
    size_t n = OatmealFmt::encode_bytes(buf+len, MAX_FRAME_END_OFFSET-len,
                                        (const uint8_t*)ptr, nbytes);
    len += n;
    buf[len] = '\0';
    return n;
  }

  /** Append a value as hex.
  @returns Number of frame bytes written out, or 0 on failure
           (i.e. ran out of buffer space).
  @see OatmealPort::write_hex() */
  size_t write_hex(uint32_t val) {
    if (len + 8 > MAX_FRAME_END_OFFSET) { return 0; }
    char hex[9];
    OatmealFmt::uint32_to_hex(hex, val);
    return write(hex, 8);
  }

  /** Append to the frame a representation of a double
  @returns the number of bytes written (no nul-byte) or 0 on failure.
  @see OatmealPort::write(double, int) */
  size_t write(double val, int sig_figs = OatmealFmt::DEFAULT_SIG_FIGS) {
    return _write_val(val, sig_figs);
  }

  /** Append to the frame a representation of a float.
  @returns the number of bytes written (no nul-byte) or 0 on failure.
  @see OatmealPort::write(float, int) */
  size_t write(float val, int sig_figs = OatmealFmt::DEFAULT_SIG_FIGS) {
    return _write_val(val, sig_figs);
  }

  /** Append to the frame a value (int, float, double, bool, string)
  @returns the number of bytes written (no nul-byte) or 0 on failure.
  @see OatmealPort::write(T) */
  template<typename T>
  size_t write(T val) {
    return _write_val(val);
  }

  /* Argument construction */

  /** Append an arg separator onto the message.
  @returns the number of bytes written (no nul-byte) or 0 on failure.
  @see OatmealPort::separator() */
  size_t separator() {
    return write(OatmealFmt::ARG_SEP);
  }

  /** Append an arg separator onto the message only if needed.
  @returns the number of bytes written (no nul-byte) or 0 on failure.
  @see OatmealPort::separator_if_needed() */
  size_t separator_if_needed() {
    if (len > OatmealMsg::ARGS_OFFSET &&
        buf[len-1] != OatmealFmt::LIST_START &&
        buf[len-1] != OatmealFmt::DICT_START &&
        buf[len-1] != OatmealFmt::DICT_KV_SEP &&
        buf[len-1] != OatmealFmt::ARG_SEP) {
      return write(OatmealFmt::ARG_SEP);
    } else {
      return 0;
    }
  }

  /** Append a null terminated string argument.
  @returns Number of frame bytes written out, or 0 on failure
  @see OatmealPort::append(const char*) */
  size_t append(const char *str) {
    return _append_val(str);
  }

  /** Append a data bytes argument to the message.
  @returns Number of frame bytes written out, or 0 on failure
  @see OatmealPort::append(const uint8_t*, size_t) */
  size_t append(const uint8_t *data, size_t n_bytes) {
    if (data == nullptr) { return append_none(); }
    size_t orig_len = len;
    separator_if_needed();
    size_t n = OatmealFmt::format_bytes(buf+len, MAX_FRAME_END_OFFSET-len,
                                        (const uint8_t*)data, n_bytes);
    if (!n) { return reset_len(orig_len); }
    len += n;
    return len - orig_len;
  }

  /** Append an integer or string to the list of arguments.
  @returns Number of frame bytes written out, or 0 on failure
  @see OatmealPort::append(T) */
  template<typename T>
  size_t append(T val) {
    return _append_val(val);
  }

  /** Append a double to the list of arguemnts.
  @returns Number of frame bytes written out, or 0 on failure
  @see OatmealPort::append(double, int) */
  size_t append(double val, int sig_figs = OatmealFmt::DEFAULT_SIG_FIGS) {
    return _append_val(val, sig_figs);
  }

  /** Append a float to the list of arguemnts.
  @returns Number of frame bytes written out, or 0 on failure
  @see OatmealPort::append(float, int) */
  size_t append(float val, int sig_figs = OatmealFmt::DEFAULT_SIG_FIGS) {
    return _append_val(val, sig_figs);
  }

  /** Append a list start character.
  @returns The number of bytes appended, 0 on failure
  @see OatmealPort::append_list_start() */
  size_t append_list_start() {
    size_t orig_len = len;
    separator_if_needed();
    if (!write(OatmealFmt::LIST_START)) { return reset_len(orig_len); }
    return len - orig_len;
  }

  /** Append a list end character.
  @returns The number of bytes appended, 0 on failure
  @see OatmealPort::append_list_end() */
  size_t append_list_end() {
    return write(OatmealFmt::LIST_END);
  }

  /** Append a dict start character.
  @returns The number of bytes appended, 0 on failure
  @see OatmealPort::append_dict_start() */
  size_t append_dict_start() {
    size_t orig_len = len;
    separator_if_needed();
    if (!write(OatmealFmt::DICT_START)) { return reset_len(orig_len); }
    return len - orig_len;
  }

  /** Append a dict end character.
  @returns The number of bytes appended, 0 on failure
  @see OatmealPort::append_dict_end() */
  size_t append_dict_end() {
    return write(OatmealFmt::DICT_END);
  }

  /** Append (separator if needed then) a dictionary key and equals sign.
  Call e.g. `append(val)` after this method to append a value for the key.
  @returns the number of bytes written (no nul-byte) or 0 on failure.
  @see OatmealPort::append_dict_key(const char*) */
  size_t append_dict_key(const char *key) {
    size_t orig_len = len;
    separator_if_needed();
    if (!write(key) || !write(OatmealFmt::DICT_KV_SEP)) {
      return reset_len(orig_len);
    }
    return len - orig_len;
  }

  /** Append a key=value pair to a dictionary for a float value.
  @returns the number of bytes written (no nul-byte) or 0 on failure.
  @see OatmealPort::append_dict_key_value(const char*, float, int) */
  size_t append_dict_key_value(const char *key, float val,
                               int sig_figs = OatmealFmt::DEFAULT_SIG_FIGS) {
    return _append_dict_key_value(key, val, sig_figs);
  }

  /** Append a key=value pair to a dictionary for a double value.
  @returns the number of bytes written (no nul-byte) or 0 on failure.
  @see OatmealPort::append_dict_key_value(const char*, double, int) */
  size_t append_dict_key_value(const char *key, double val,
                               int sig_figs = OatmealFmt::DEFAULT_SIG_FIGS) {
    return _append_dict_key_value(key, val, sig_figs);
  }

  /** Append a key=value pair to a dictionary for an integer or string value.
  @returns the number of bytes written (no nul-byte) or 0 on failure.
  @see OatmealPort::append_dict_key_value(const char*, T) */
  template<typename T>
  size_t append_dict_key_value(const char *key, T val) {
    return _append_dict_key_value(key, val);
  }

  /** Append a key=value pair to a dictionary for a bytes value.
  @param key: null termintated string to use as the key
  @param data: bytes to use as the value
  @param len: number of bytes to represent in the value
  @returns the number of bytes written (no nul-byte) or 0 on failure.
  @see OatmealPort::append_dict_key_value(const char*, const uint8_t*, size_t) */
  size_t append_dict_key_value(const char *key,
                               const uint8_t *data, size_t len) {
    size_t orig_len = len;
    if (!append_dict_key(key) || !append(data, len)) {
      return reset_len(orig_len);
    }
    return len - orig_len;
  }

  /** Append a None/NULL/nil value
  @returns the number of bytes written (no nul-byte) or 0 on failure.
  @see OatmealPort::append_none() */
  size_t append_none() {
    size_t orig_len = len;
    separator_if_needed();
    if (!write('N')) { return reset_len(orig_len); }
    return len - orig_len;
  }
};


class OatmealArgParser {
 private:
  const char *args = nullptr;
  size_t remchars = 0;
  bool need_sep = false;  /* need separator before next arg */
  bool args_parsed = false; /* parsed at least one arg at current list level */
  uint8_t list_depth = 0;

  bool able_to_parse_next_arg() const {
    return (!need_sep || (remchars > 0 && *args == OatmealFmt::ARG_SEP));
  }

  template<typename T>
  bool parse_list_item(T *result, size_t max_str_len) {
    (void)max_str_len;
    return parse_arg(result);
  }

  /**
  @param max_str_len: max length of string including null-byte.
  */
  bool parse_list_item(char **result, size_t max_str_len) {
    return parse_str(*result, max_str_len);
  }

  /**
  @param max_str_len: max length of string including null-byte.
  */
  template<typename T>
  bool _parse_list(T *dst, size_t *n_list_items, size_t max_list_items,
                   size_t max_str_len = 0) {
    // Clone to attempt parsing, alter this instance only on success
    *n_list_items = 0;
    OatmealArgParser clone = *this;
    size_t n;
    if (need_sep && !clone.parse_sep()) { return false; }
    if (!clone.parse_list_start()) { return false; }
    for (n = 0; n < max_list_items; n++) {
      if (!clone.parse_list_item(dst+n, max_str_len)) { break; }
    }
    if (!clone.parse_list_end()) { return false; }
    *this = clone;
    *n_list_items = n;
    return true;
  }

  /** Consume `n` chars from the start of the arg string */
  void chomp(size_t n) {
    args += n;
    remchars -= n;
  }

 public:
  /** Start parsing a message if it has the correct opcode.

  If the message passed has the given opcode, initialise parsing and return true.
  Otherwise just returns false.

  @param msg: message whose arguments we should start parsing
  @param opcode: opcode (command+flag) that to compare message to: only start
    parsing if the message opcode matches that passed.
  @returns `true` if initialised (message passed has the given opcode),
    otherwise returns `false` and this object remains unchanged.
  */
  bool start(const OatmealMsgReadonly &msg, const char *opcode) {
    if (!msg.is_opcode(opcode)) { return false; }
    init(msg);
    return true;
  }

  /** Initialise this parser.
  @returns `true` */
  bool init(const char *_args, size_t _argslen) {
    args = _args;
    remchars = _argslen;
    args_parsed = need_sep = false;
    list_depth = 0;
    return true;
  }

  /** Initialise this parser.
  @returns `true` */
  bool init(const OatmealMsgReadonly &msg) {
    return init(msg.args(), msg.args_len());
  }

  /*
  Parse methods return success as a bool. The instance is unchanged if parsing
  is not successful.
  */

  /** Parse a separator character `,`.
  @returns `true` on success, otherwise this object remains unchanged. */
  bool parse_sep() {
    if (remchars == 0 || !need_sep) { return false; }
    if (*args == OatmealFmt::ARG_SEP) {
      chomp(1);
      need_sep = false;
      return true;
    }
    return false;
  }

 private:
  bool _parse_collection_start(bool is_dict) {
    if (!able_to_parse_next_arg()) { return false; }
    size_t sep = need_sep, needed_chars = 1+sep;
    if (remchars < needed_chars) { return false; }
    char start_char = is_dict ? OatmealFmt::DICT_START : OatmealFmt::LIST_START;
    if (args[sep] == start_char) {
      chomp(needed_chars);
      list_depth++;
      args_parsed = need_sep = false;
      return true;
    }
    return false;
  }

  bool _parse_collection_end(bool is_dict) {
    /* If we've seen an arg but don't need a separator then we _just_ saw an
    separator -> closing the list is not valid e.g. `[1,2,]` */
    if (remchars == 0 || (args_parsed && !need_sep)) { return false; }
    if (list_depth == 0) { return false; }
    char end_char = is_dict ? OatmealFmt::DICT_END : OatmealFmt::LIST_END;
    if (args[0] == end_char) {
      chomp(1);
      list_depth--;
      args_parsed = need_sep = true;
      return true;
    }
    return false;
  }

 public:
  /** Parse a list start character `[`.
  Will also parse a separator character if we are expecting one.
  @returns `true` on success, otherwise this object remains unchanged. */
  bool parse_list_start() { return _parse_collection_start(false); }
  /** Parse a list end character `]`.
  @returns `true` on success, otherwise this object remains unchanged. */
  bool parse_list_end() { return _parse_collection_end(false); }
  /** Parse a dictionary start character `{`.
  Will also parse a separator character if we are expecting one.
  @returns `true` on success, otherwise this object remains unchanged. */
  bool parse_dict_start() { return _parse_collection_start(true); }
  /** Parse a dictionary end character `}`.
  @returns `true` on success, otherwise this object remains unchanged. */
  bool parse_dict_end() { return _parse_collection_end(true); }

  /** Parse a dictionary key (null-terminated string) into the memory passed.

  Must call `parse_dict_start()` before this method will succeed.
  Will also parse a separator character if we are expecting one.

  @param key: memory to copy null-terminated key string into
  @param n_key: number of bytes that can be written to `key` including null-byte
  @returns `true` on success, otherwise this object remains unchanged. */
  bool parse_dict_key(char *key, size_t n_key) {
    if (!able_to_parse_next_arg()) { return false; }
    size_t n, sep = need_sep;
    n = OatmealFmt::parse_dict_key(key, n_key, args+sep, remchars-sep);
    if (n == 0) { return false; }  // failed to parse anything
    if (remchars < sep+n+1+1) { return false; }  // Need enough chars for "=x"
    if (args[sep+n] != '=') { return false; }  // Check followed by '=' sign
    chomp(sep+n+1);
    args_parsed = true;
    need_sep = false;  // we've already parse the '=' separator
    return true;
  }

  /** Parse a dictionary key and value.

  Must call `parse_dict_start()` before this method will succeed.
  Will also parse a separator character if we are expecting one.

  @param key: memory to copy null-terminated key string into
  @param n_key: number of bytes that can be written to `key` including null-byte
  @param val: memory to store parsed dictionary key-value value.
  @returns `true` on success, otherwise this object remains unchanged. */
  template<typename T>
  bool parse_dict_key_value(char *key, size_t n_key, T *val) {
    OatmealArgParser clone = *this;
    if (parse_dict_key(key, n_key) && parse_arg(val)) {
      return true;
    } else {
      *this = clone;
      return false;
    }
  }

  /** Parse a dictionary key and value.

  Must call `parse_dict_start()` before this method will succeed.
  Will also parse a separator character if we are expecting one.

  @param key: memory to copy null-terminated key string into
  @param n_key: number of bytes that can be written to `key` including null-byte
  @param val: memory to store parsed dictionary key-value value string.
  @param n_val: number of bytes that can be written to `val`.
  @returns `true` on success, otherwise this object remains unchanged. */
  template<typename T>
  bool parse_dict_key_value(char *key, size_t n_key, char *val, size_t n_val) {
    OatmealArgParser clone = *this;
    if (parse_dict_key(key, n_key) && parse_str(val, n_val)) {
      return true;
    } else {
      *this = clone;
      return false;
    }
  }

  /** Parse a float, double or boolean message argument.
  Will also parse a separator character if we are expecting one.
  @returns `true` on success, otherwise this object remains unchanged. */
  template<typename T>
  bool parse_arg(T *result) {
    if (!able_to_parse_next_arg()) { return false; }
    size_t n, sep = need_sep;
    // printf("  parsing '%.*s'\n", (int)(remchars-sep), args+sep);
    n = OatmealFmt::parse(result, args+sep, remchars-sep);
    if (n == 0) { return false; }
    chomp(n+sep);
    args_parsed = need_sep = true;
    return true;
  }

  /** Parse a string argument (returned string is utf-8 encoded).
  Will also parse a separator character if we are expecting one.

  @param str: memory to store parsed string into.
  @param n_str: max length of string including null-byte.
  @param dstlen: length of string parsed in bytes (not including null byte)
  @returns `true` on success, otherwise this object remains unchanged. */
  bool parse_str(char *str, size_t n_str, size_t *dstlen = nullptr) {
    if (!able_to_parse_next_arg()) { return false; }
    size_t n, sep = need_sep;
    // printf("  parsing '%.*s'\n", (int)(remchars-sep), args+sep);
    n = OatmealFmt::parse_str(str, n_str, dstlen, args+sep, remchars-sep);
    if (n == 0) { return false; }
    chomp(n+sep);
    args_parsed = need_sep = true;
    return true;
  }

  /** Parse a bytes argument.
  Will also parse a separator character if we are expecting one.

  @param dst: memory at which to store byte data
  @param n_dst: max length of data
  @param dstlen: address to store the length in bytes of data stored
  @returns `true` on success, otherwise this object remains unchanged. */
  bool parse_bytes(uint8_t *dst, size_t n_dst, size_t *dstlen) {
    if (!able_to_parse_next_arg()) { return false; }
    size_t n, sep = need_sep;
    // printf("  parsing '%.*s'\n", (int)(remchars-sep), args+sep);
    n = OatmealFmt::parse_bytes(dst, n_dst, dstlen, args+sep, remchars-sep);
    if (n == 0) { return false; }
    chomp(n+sep);
    args_parsed = need_sep = true;
    return true;
  }

  /** Parse a 'null' value.
  Will also parse a separator character if we are expecting one.

  @returns `true` on success, otherwise this object remains unchanged. */
  bool parse_null() {
    if (!able_to_parse_next_arg()) { return false; }
    size_t n, sep = need_sep;
    // printf("  parsing '%.*s'\n", (int)(remchars-sep), args+sep);
    n = OatmealFmt::parse_null(args+sep, remchars-sep);
    if (n == 0) { return false; }
    chomp(n+sep);
    args_parsed = need_sep = true;
    return true;
  }

  /** Parse a list of integers, floats or doubles.
  Will also parse a separator character if we are expecting one.

  @param dst: pointer to memory to store parsed values.
  @param dst_len: pointer to memory to store number of items parsed
                 (if successful). Set to zero on failure.
  @param max_list_items: maximum number of strings we can parse
  @returns `true` on success, otherwise this object remains unchanged. */
  template<typename T>
  bool parse_list(T *dst, size_t *dst_len, size_t max_list_items) {
    return _parse_list(dst, dst_len, max_list_items);
  }

  /** Parse a list of strings.
  Will also parse a separator character if we are expecting one.

  @param dst: list of pointers to memory to store parsed strings.
  @param dst_len: pointer to memory to store number of strings parsed
                 (if successful). Set to zero on failure.
  @param max_list_items: maximum number of strings we can parse
  @param max_str_len: max length that any string can be, in bytes, including
    null-terminator byte.
  @returns `true` on success, otherwise this object remains unchanged. */
  bool parse_list_of_strs(char **dst, size_t *dst_len,
                          size_t max_list_items, size_t max_str_len) {
    return _parse_list(dst, dst_len, max_list_items, max_str_len);
  }

  /** Check if we have reached the end of a valid argument string successfully.
  @returns `true` on success */
  bool finished() const {
    return (remchars == 0 && list_depth == 0 && (!args_parsed || need_sep));
  }
};

#endif /* OATMEAL_MESSAGE_H_ */
