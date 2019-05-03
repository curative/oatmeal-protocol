/*
  test_oatmeal_protocol.cpp
  Copyright 2019, Shield Diagnostics and the Oatmeal Protocol contributors
  License: Apache 2.0
*/

#include <cstdlib>
#include <cmath>
#include <limits.h>
#include "oatmeal_message.h"

/* Print a line detailing the test about to be run */
void _print_test_case(const char *func, const char *args) {
  const char spacer[35] = "..................................";
  printf("Running %s() %.*s '%s'\n", func,
         static_cast<int>(strlen(spacer) - strlen(func)), spacer, args);
}

/* Report that parsing failed */
#define PRINT_PARSING_FAILED(msg) do { \
  fprintf(stderr, "%s:%i  Parsing failed: '%s'\n", \
          __FILE__, __LINE__, (msg).frame()); \
} while (0);

/* Reset the parser with a null terminated arg string and print the test case
   to be run */
bool _set_up_test_case(OatmealArgParser *parser,
                       const char *func, const char *args) {
  _print_test_case(func, args);
  return parser->init(args, strlen(args));
}

template<typename T>
bool _test_format_parse_int(T x) {
  char str[100];
  T val = 0;
  OatmealFmt::format(str, sizeof(str), x);
  OatmealFmt::parse(&val, str, strlen(str));
  if (val != x) {
    fprintf(stderr, "%s:%i failed %li %li\n", __FILE__, __LINE__,
            static_cast<long>(x), static_cast<long>(val));
    return false;
  }
  return true;
}

template<typename T>
bool _test_format_and_parse_integer(T val) {
  char tmp[100], truth[100];
  T result = 0;
  if (val < 0) {
    sprintf(truth, "%lli", (long long)val);
  } else {
    sprintf(truth, "%llu", (unsigned long long)val);
  }
  return OatmealFmt::format(tmp, sizeof(tmp), val) == strlen(truth) &&
         strcmp(tmp, truth) == 0 &&
         OatmealFmt::parse(&result, tmp, strlen(tmp)) == strlen(truth) &&
         result == val;
}


template<typename T>
bool _test_fmt_parse_limits(T min_v, T max_v) {
  return _test_format_and_parse_integer((T)min_v) &
         _test_format_and_parse_integer((T)(min_v+1)) &
         _test_format_and_parse_integer((T)0) &
         _test_format_and_parse_integer((T)(max_v-1)) &
         _test_format_and_parse_integer((T)max_v);
}

bool test_format_ints() {
  /* Test that we can format and parse integers correctly */
  printf("Running %s()...\n", __func__);

  bool pass =
    /* fixed types int8_t, uint8_t etc. */
     _test_fmt_parse_limits((int8_t)INT8_MIN, (int8_t)INT8_MAX) &&
     _test_fmt_parse_limits((uint8_t)0, (uint8_t)UINT8_MAX) &&
     _test_fmt_parse_limits((int16_t)INT16_MIN, (int16_t)INT16_MAX) &&
     _test_fmt_parse_limits((uint16_t)0, (uint16_t)UINT16_MAX) &&
     _test_fmt_parse_limits((int32_t)INT32_MIN, (int32_t)INT32_MAX) &&
     _test_fmt_parse_limits((uint32_t)0, (uint32_t)UINT32_MAX) &&
     _test_fmt_parse_limits((int64_t)INT64_MIN, (int64_t)INT64_MAX) &&
     _test_fmt_parse_limits((uint64_t)0, (uint64_t)UINT64_MAX) &&
     /* short */
     _test_fmt_parse_limits((short)SHRT_MIN, (short)SHRT_MAX) &&
     _test_fmt_parse_limits((unsigned short)0, (unsigned short)USHRT_MAX) &&
     /* int */
     _test_fmt_parse_limits((int)INT_MIN, (int)INT_MAX) &&
     _test_fmt_parse_limits((unsigned int)0, (unsigned int)UINT_MAX) &&
     /* long */
     _test_fmt_parse_limits((long)LONG_MIN, (long)LONG_MAX) &&
     _test_fmt_parse_limits((unsigned long)0, (unsigned long)ULONG_MAX) &&
     /* long long */
     _test_fmt_parse_limits((long long)LLONG_MIN, (long long)LLONG_MAX) &&
     _test_fmt_parse_limits((unsigned long long)0, (unsigned long long)ULLONG_MAX);

  if (!pass) { return false; }

  // Format and parse all int8 and uint8 values
  for (int i = 0; i < 256; i++) {
    if (!_test_format_and_parse_integer((uint8_t)i)) { return false; }
  }
  for (int i = INT8_MIN; i <= INT8_MAX; i++) {
    if (!_test_format_and_parse_integer((int8_t)i)) { return false; }
  }

  return true;
}

bool verbose_test() {
  OatmealArgParser parser;

  int32_t num, nums[5];
  char str[10];
  bool bools[2];
  float decimal_f;
  double decimal_d;
  size_t n_nums, n_bools, cap;

  _set_up_test_case(&parser, __func__, "12,[1,2,3],\"hello\",[T,F],1.23,12.3");

  if (!parser.parse_arg(&num)) { return false; }
  printf("  Got num: %i\n", static_cast<int>(num));

  cap = sizeof(nums) / sizeof(nums[0]);
  if (!parser.parse_list(nums, &n_nums, cap)) { return false; }
  printf("  Got num: %i, %i, %i (n=%zu)\n",
         static_cast<int>(nums[0]), static_cast<int>(nums[1]),
         static_cast<int>(nums[2]), n_nums);

  if (!parser.parse_str(str, sizeof(str))) { return false; }
  printf("  Got str: '%s'\n", str);

  cap = sizeof(bools) / sizeof(bools[0]);
  if (!parser.parse_list(bools, &n_bools, cap)) { return false; }
  printf("  Got bools: %c, %c (n=%zu)\n",
         "TF"[bools[0]], "TF"[bools[1]], n_bools);

  if (!parser.parse_arg(&decimal_f)) { return false; }
  printf("  Got float: %f\n", decimal_f);

  if (!parser.parse_arg(&decimal_d)) { return false; }
  printf("  Got double: %f\n", decimal_d);

  return parser.finished();
}

bool test_mixed_args() {
  OatmealArgParser parser;
  const char msgargs[] = "12,[1,2,3],\"hello\",[T,F],1.23,12.3";

  int32_t num, nums[3];
  char str[10];
  bool bools[5];
  float decimal_f;
  double decimal_d;
  size_t n_nums, n_bools;

  char format[100], *strptr, *endptr;

  _set_up_test_case(&parser, __func__, msgargs);

  if (parser.parse_arg(&num) &&
      parser.parse_list(nums, &n_nums, sizeof(nums)/sizeof(nums[0])) &&
      parser.parse_str(str, sizeof(str)) &&
      parser.parse_list(bools, &n_bools, sizeof(bools)/sizeof(bools[0])) &&
      parser.parse_arg(&decimal_f) &&
      parser.parse_arg(&decimal_d) &&
      parser.finished()) {
    // Success
    strptr = format;
    endptr = format + sizeof(format);
    strptr += OatmealFmt::format(strptr, endptr-strptr, num);
    *(strptr++) = OatmealFmt::ARG_SEP;
    strptr += OatmealFmt::format_list(strptr, endptr-strptr, nums, n_nums);
    *(strptr++) = OatmealFmt::ARG_SEP;
    strptr += OatmealFmt::format(strptr, endptr-strptr, str);
    *(strptr++) = OatmealFmt::ARG_SEP;
    strptr += OatmealFmt::format_list(strptr, endptr-strptr, bools, n_bools);
    *(strptr++) = OatmealFmt::ARG_SEP;
    strptr += OatmealFmt::format(strptr, endptr-strptr, decimal_f);
    *(strptr++) = OatmealFmt::ARG_SEP;
    strptr += OatmealFmt::format(strptr, endptr-strptr, decimal_d);
    if (strcmp(format, msgargs) != 0) {
      fprintf(stderr, "Strings mismatch: '%s' vs '%s'\n", format, msgargs);
      return false;
    }
    return true;
  }
  return false;
}

bool test_list_of_strs() {
  static const int MAX_STR_LEN = 10;
  OatmealArgParser parser;

  char strs[2][MAX_STR_LEN], single_str[MAX_STR_LEN];
  char *list_of_strs[2] = {strs[0], strs[1]};
  size_t n_strs = 0;
  uint8_t intval = 0;

  _set_up_test_case(&parser, __func__, "[\"hi\",\"bye\"],\"hello\",0123");
  if (parser.finished()) { return false; }

  return parser.parse_list_of_strs(list_of_strs, &n_strs, 2, MAX_STR_LEN) &&
         n_strs == 2 &&
         strcmp(list_of_strs[0], "hi") == 0 &&
         strcmp(list_of_strs[1], "bye") == 0 &&
         parser.parse_str(single_str, MAX_STR_LEN) &&
         strcmp(single_str, "hello") == 0 &&
         parser.parse_arg(&intval) && intval == 123 &&
         parser.finished();
}

bool test_complex_args() {
  static const int MAX_STR_LEN = 10;
  static const int MAX_LIST_LEN = 2;
  OatmealArgParser parser;

  char str0[MAX_STR_LEN], str1[MAX_STR_LEN];
  int8_t intval0 = 0, intval1 = 0, intval2 = 0;
  uint8_t int_list[MAX_LIST_LEN];
  size_t int_list_len = 0;
  float floatval = 0.0f;
  bool pass = true;

  pass = _set_up_test_case(&parser, __func__,
                           "[\"hi\",[-1,1.2]],1,[],2,[],\"asdf\"") &&
         parser.parse_list_start() && /* '[' */
         parser.parse_str(str0, MAX_STR_LEN) && strcmp(str0, "hi") == 0 &&
         parser.parse_list_start() && /* '[' */
         parser.parse_arg(&intval0) && intval0 == -1 &&
         parser.parse_arg(&floatval) && floatval == 1.2f &&
         !parser.parse_sep() &&
         parser.parse_list_end() && /* ']' */
         !parser.parse_sep() &&
         parser.parse_list_end() && /* ']' */
         parser.parse_arg(&intval1) && intval1 == 1 &&
         parser.parse_list(int_list, &int_list_len, MAX_LIST_LEN) && /* '[]' */
         int_list_len == 0 &&
         !parser.finished() &&
         parser.parse_arg(&intval2) && intval2 == 2 &&
         parser.parse_list_start() && parser.parse_list_end() && /* '[]' */
         parser.parse_str(str1, MAX_STR_LEN) && strcmp(str1, "asdf") == 0 &&
         parser.finished();

  pass &= _set_up_test_case(&parser, __func__, "[]") &&
          parser.parse_list(int_list, &int_list_len, 4) && int_list_len == 0;

  return pass;
}

bool test_explicit_sep_parsing() {
  OatmealArgParser parser;

  char msgargs[] = "1,[2,3]";
  uint32_t intval = 0;

  if (!_set_up_test_case(&parser, __func__, msgargs) ||
     !parser.parse_arg(&intval) || intval != 1 ||
     !parser.parse_list_start() ||
     !parser.parse_arg(&intval) || intval != 2 ||
     !parser.parse_arg(&intval) || intval != 3 ||
     !parser.parse_list_end() ||
     !parser.finished()) {
    return false;
  }

  /* Reset and try parsing with explicity parsing the separators */

  if (!_set_up_test_case(&parser, __func__, msgargs) ||
     !parser.parse_arg(&intval) || intval != 1 ||
     !parser.parse_sep() ||
     !parser.parse_list_start() ||
     !parser.parse_arg(&intval) || intval != 2 ||
     !parser.parse_sep() ||
     !parser.parse_arg(&intval) || intval != 3 ||
     !parser.parse_list_end() ||
     !parser.finished()) {
    return false;
  }

  return true;
}

/* Check that we can parse the None/nil/NULL value 'N' */
bool test_parsing_none() {
  OatmealArgParser parser;
  uint8_t int8val = 0;
  uint32_t int32val = 0;
  bool pass = true;

  pass &= _set_up_test_case(&parser, __func__, "N") &&
          parser.parse_null() &&
          parser.finished();

  pass &= _set_up_test_case(&parser, __func__, "N,N") &&
          parser.parse_null() &&
          parser.parse_null() &&
          parser.finished();

  pass &= _set_up_test_case(&parser, __func__, "12345,N,[],0") &&
          parser.parse_arg(&int32val) && int32val == 12345 &&
          parser.parse_null() &&
          parser.parse_list_start() &&
          parser.parse_list_end() &&
          parser.parse_arg(&int8val) && int8val == 0 &&
          parser.finished();

  return pass;
}

/* Check that all parse_foo methods fail. This should be the case after we've
   reached a syntax error in our args string, since after that it should not be
   valid to continue parsing in any way. */
bool _all_parsing_functions_fail(OatmealArgParser parser) {
  int64_t v_int64 = 0;
  int32_t v_int32 = 0;
  int16_t v_int16 = 0;
  int8_t v_int8 = 0;
  uint64_t v_uint64 = 0;
  uint32_t v_uint32 = 0;
  uint16_t v_uint16 = 0;
  uint8_t v_uint8 = 0;
  float v_float = 0.0;
  double v_double = 0.0;
  char v_str[128], key[128];

  return !parser.parse_sep() &&
         !parser.parse_list_start() &&
         !parser.parse_list_end() &&
         !parser.parse_dict_start() &&
         !parser.parse_dict_end() &&
         !parser.parse_dict_key(key, sizeof(key)) &&
         !parser.parse_arg(&v_uint8) &&
         !parser.parse_arg(&v_uint16) &&
         !parser.parse_arg(&v_uint32) &&
         !parser.parse_arg(&v_uint64) &&
         !parser.parse_arg(&v_int8) &&
         !parser.parse_arg(&v_int16) &&
         !parser.parse_arg(&v_int32) &&
         !parser.parse_arg(&v_int64) &&
         !parser.parse_arg(&v_float) &&
         !parser.parse_arg(&v_double) &&
         !parser.parse_str(v_str, sizeof(v_str)) &&
         !parser.parse_null() &&
         !parser.finished();
}

bool test_parsing_fails() {
  OatmealArgParser parser;
  uint8_t v_int8 = 0, lst[4] = {0};
  size_t lst_len = 0;
  char key[100];

  bool pass = true;

  pass &= _set_up_test_case(&parser, __func__, "]") &&
          _all_parsing_functions_fail(parser);

  pass &= _set_up_test_case(&parser, __func__, "[,]") &&
          parser.parse_list_start() &&
          _all_parsing_functions_fail(parser);

  pass &= _set_up_test_case(&parser, __func__, "[") &&
          parser.parse_list_start() &&
          _all_parsing_functions_fail(parser);

  pass &= _set_up_test_case(&parser, __func__, "]") &&
          _all_parsing_functions_fail(parser);

  pass &= _set_up_test_case(&parser, __func__, "1,") &&
          parser.parse_arg(&v_int8) && v_int8 == 1 &&
          parser.parse_sep() &&
          _all_parsing_functions_fail(parser);

  pass &= _set_up_test_case(&parser, __func__, "[,2]") &&
          parser.parse_list_start() &&
          _all_parsing_functions_fail(parser);

  pass &= _set_up_test_case(&parser, __func__, "[4,5,]") &&
          parser.parse_list_start() &&
          parser.parse_arg(&v_int8) && v_int8 == 4 &&
          parser.parse_sep() &&
          parser.parse_arg(&v_int8) && v_int8 == 5 &&
          parser.parse_sep() &&
          _all_parsing_functions_fail(parser);

  pass &= _set_up_test_case(&parser, __func__, "[1,2]]") &&
          parser.parse_list_start() &&
          parser.parse_arg(&v_int8) && v_int8 == 1 &&
          parser.parse_sep() &&
          parser.parse_arg(&v_int8) && v_int8 == 2 &&
          parser.parse_list_end() &&
          _all_parsing_functions_fail(parser);

  pass &= _set_up_test_case(&parser, __func__, "1,,3") &&
          parser.parse_arg(&v_int8) && v_int8 == 1 &&
          parser.parse_sep() &&
          _all_parsing_functions_fail(parser);

  pass &= _set_up_test_case(&parser, __func__, "[1]3") &&
          !parser.parse_arg(&v_int8) &&
          parser.parse_list_start() &&
          parser.parse_arg(&v_int8) && v_int8 == 1 &&
          !parser.parse_sep() &&
          parser.parse_list_end() &&
          _all_parsing_functions_fail(parser);

  pass &= _set_up_test_case(&parser, __func__, "[52][61]") &&
          !parser.parse_list(lst, &lst_len, 0) &&
          parser.parse_list(lst, &lst_len, 4) && lst_len == 1 && lst[0] == 52 &&
          _all_parsing_functions_fail(parser);

  pass &= _set_up_test_case(&parser, __func__, ",]") &&
          _all_parsing_functions_fail(parser);

  pass &= _set_up_test_case(&parser, __func__, "[]]") &&
          parser.parse_list(lst, &lst_len, 4) && lst_len == 0 &&
          _all_parsing_functions_fail(parser);

  pass &= _set_up_test_case(&parser, __func__, ",") &&
          _all_parsing_functions_fail(parser);

  // Dictionaries
  // Our parser doen't know when you're inside a dictionary, so can't protect
  // against trying to parse non-key-value args inside dictionaries.
  pass &= _set_up_test_case(&parser, __func__, "{") &&
          parser.parse_dict_start() &&
          _all_parsing_functions_fail(parser);

  pass &= _set_up_test_case(&parser, __func__, "}") &&
          _all_parsing_functions_fail(parser);

  pass &= _set_up_test_case(&parser, __func__, "{123}") &&
          parser.parse_dict_start() &&
          !parser.parse_dict_key(key, sizeof(key)) &&
          !parser.parse_dict_end() &&
          !parser.finished();

  pass &= _set_up_test_case(&parser, __func__, "{a=1,1}") &&
          parser.parse_dict_start() &&
          parser.parse_dict_key(key, sizeof(key)) &&
          parser.parse_arg(&v_int8) &&
          !parser.parse_dict_key(key, sizeof(key)) &&
          !parser.parse_dict_end() &&
          !parser.finished();

  pass &= _set_up_test_case(&parser, __func__, "{a=1,b=2,}") &&
          parser.parse_dict_start() &&
          parser.parse_dict_key(key, sizeof(key)) &&
          parser.parse_arg(&v_int8) &&
          parser.parse_dict_key(key, sizeof(key)) &&
          parser.parse_arg(&v_int8) &&
          parser.parse_sep() &&
          _all_parsing_functions_fail(parser);

  pass &= _set_up_test_case(&parser, __func__, "{},") &&
          parser.parse_dict_start() &&
          parser.parse_dict_end() &&
          parser.parse_sep() &&
          _all_parsing_functions_fail(parser);

  pass &= _set_up_test_case(&parser, __func__, "{,a=1}") &&
          parser.parse_dict_start() &&
          _all_parsing_functions_fail(parser);

  pass &= _set_up_test_case(&parser, __func__, ",{a=1}") &&
          _all_parsing_functions_fail(parser);

  pass &= _set_up_test_case(&parser, __func__, "{\"a\"=1}") &&
          !parser.parse_dict_key(key, sizeof(key)) &&
          !parser.parse_dict_end() &&
          !parser.finished();

  return pass;
}

bool test_parse_dicts() {
  printf("Running %s()...\n", __func__);

  OatmealMsg msg;
  OatmealArgParser parser;
  char v_str[100], key[100];
  int v_int;
  float v_float;
  bool v_bool;
  uint8_t v_bytes[100];
  size_t n_bytes;

  /* 1. Test empty and nested dicts */

  // <TSTRXY{}>wR
  msg.start("TST", 'R', "XY");
  msg.append_dict_start();
  msg.append_dict_end();
  msg.finish();

  if (!parser.start(msg, "TSTR") ||
      !parser.parse_dict_start() ||
      !parser.parse_dict_end() ||
      !parser.finished()) {
    PRINT_PARSING_FAILED(msg);
    return false;
  }

  // <TSTRXY"",{},[]>EB
  msg.start("TST", 'R', "XY");
  msg.append("");
  msg.append_dict_start();
  msg.append_dict_end();
  msg.append_list_start();
  msg.append_list_end();
  msg.finish();

  if (!parser.start(msg, "TSTR") ||
      !parser.parse_str(v_str, sizeof(v_str)) || strcmp(v_str, "") != 0 ||
      !parser.parse_dict_start() ||
      !parser.parse_dict_end() ||
      !parser.parse_list_start() ||
      !parser.parse_list_end() ||
      !parser.finished()) {
    PRINT_PARSING_FAILED(msg);
    return false;
  }

  // <TSTRXY"",{a={b={}},c={}},[]>DN
  msg.start("TST", 'R', "XY");
  msg.append("");
  msg.append_dict_start();
  msg.append_dict_key("a");
  msg.append_dict_start();
  msg.append_dict_key("b");
  msg.append_dict_start();
  msg.append_dict_end();
  msg.append_dict_end();
  msg.append_dict_key("c");
  msg.append_dict_start();
  msg.append_dict_end();
  msg.append_dict_end();
  msg.append_list_start();
  msg.append_list_end();
  msg.finish();

  if (!parser.start(msg, "TSTR") ||
      !parser.parse_str(v_str, sizeof(v_str)) ||
      strcmp(v_str, "") != 0 ||
      !parser.parse_dict_start() ||
      !parser.parse_dict_key(key, sizeof(key)) || strcmp(key, "a") != 0 ||
      !parser.parse_dict_start() ||
      !parser.parse_dict_key(key, sizeof(key)) || strcmp(key, "b") != 0 ||
      !parser.parse_dict_start() ||
      !parser.parse_dict_end() ||
      !parser.parse_dict_end() ||
      !parser.parse_dict_key(key, sizeof(key)) || strcmp(key, "c") != 0 ||
      !parser.parse_dict_start() ||
      !parser.parse_dict_end() ||
      !parser.parse_dict_end() ||
      !parser.parse_list_start() ||
      !parser.parse_list_end() ||
      !parser.finished()) {
    PRINT_PARSING_FAILED(msg);
    return false;
  }

  /* 2. complex nested dicts */

  // <XYZAzZ{int=-1,float=1.2,bool=T,str="asdf",bytes=0"123",list=[1,2,"hi"],none=N}>7m
  msg.start("XYZ", 'A', "zZ");
  msg.append_dict_start();
  msg.append_dict_key("int");
  msg.append(-1);
  msg.append_dict_key("float");
  msg.append(1.2);
  msg.append_dict_key("bool");
  msg.append(true);
  msg.append_dict_key("str");
  msg.append("asdf");
  msg.append_dict_key("bytes");
  msg.append((const uint8_t*)"123", 3);
  msg.append_dict_key("list");
  msg.append_list_start();
  msg.append(1);
  msg.append(2);
  msg.append("hi");
  msg.append_list_end();
  msg.append_dict_key("none");
  msg.append_none();
  msg.append_dict_end();
  msg.finish();

  if (!parser.start(msg, "XYZA") ||
      !parser.parse_dict_start() ||
      !parser.parse_dict_key(key, sizeof(key)) || strcmp(key, "int") != 0 ||
      !parser.parse_arg(&v_int) || v_int != -1 ||
      !parser.parse_dict_key(key, sizeof(key)) || strcmp(key, "float") != 0 ||
      !parser.parse_arg(&v_float) || abs(v_float-1.2f) > 0.0001 ||
      !parser.parse_dict_key(key, sizeof(key)) || strcmp(key, "bool") != 0 ||
      !parser.parse_arg(&v_bool) || !v_bool ||
      !parser.parse_dict_key(key, sizeof(key)) || strcmp(key, "str") != 0 ||
      !parser.parse_str(v_str, sizeof(v_str)) || strcmp(v_str, "asdf") != 0 ||
      !parser.parse_dict_key(key, sizeof(key)) || strcmp(key, "bytes") != 0 ||
      !parser.parse_bytes(v_bytes, sizeof(v_bytes), &n_bytes) ||
      strncmp(reinterpret_cast<char*>(v_bytes), "123", 3) != 0 ||
      !parser.parse_dict_key(key, sizeof(key)) || strcmp(key, "list") != 0 ||
      !parser.parse_list_start() ||
      !parser.parse_arg(&v_int) || v_int != 1 ||
      !parser.parse_arg(&v_int) || v_int != 2 ||
      !parser.parse_str(v_str, sizeof(v_str)) || strcmp(v_str, "hi") != 0 ||
      !parser.parse_list_end() ||
      !parser.parse_dict_key(key, sizeof(key)) || strcmp(key, "none") != 0 ||
      !parser.parse_null() ||
      !parser.parse_dict_end() ||
      !parser.finished()) {
    PRINT_PARSING_FAILED(msg);
    return false;
  }

  // Test appending key and value together

  msg.start("XYZ", 'A', "zZ");
  msg.append_dict_start();
  msg.append_dict_key_value("int", -1);
  msg.append_dict_key_value("float1", 1.2);
  msg.append_dict_key_value("float2", 1.23, 2);
  msg.append_dict_key_value("bool", true);
  msg.append_dict_key_value("str", "asdf");
  msg.append_dict_end();
  msg.finish();

  if (!parser.start(msg, "XYZA") ||
      !parser.parse_dict_start() ||
      !parser.parse_dict_key(key, sizeof(key)) || strcmp(key, "int") != 0 ||
      !parser.parse_arg(&v_int) || v_int != -1 ||
      !parser.parse_dict_key(key, sizeof(key)) || strcmp(key, "float1") != 0 ||
      !parser.parse_arg(&v_float) || abs(v_float-1.2f) > 0.0001 ||
      !parser.parse_dict_key(key, sizeof(key)) || strcmp(key, "float2") != 0 ||
      !parser.parse_arg(&v_float) || abs(v_float-1.2f) > 0.0001 ||
      !parser.parse_dict_key(key, sizeof(key)) || strcmp(key, "bool") != 0 ||
      !parser.parse_arg(&v_bool) || !v_bool ||
      !parser.parse_dict_key(key, sizeof(key)) || strcmp(key, "str") != 0 ||
      !parser.parse_str(v_str, sizeof(v_str)) || strcmp(v_str, "asdf") != 0 ||
      !parser.parse_dict_end() ||
      !parser.finished()) {
    PRINT_PARSING_FAILED(msg);
    return false;
  }

  return true;
}

/* Test that if we can't parse because we cannot store the result in the pointer
  passed, parsing succeeds when given the appropriate memory. */
bool test_parse_fails_and_recovers() {
  OatmealArgParser parser;
  uint8_t uint8val = 0, lst[4] = {0};
  int8_t int8val = 0;
  int32_t int32val = 0;
  size_t list_len = 0;
  char str[100];
  bool pass = true;

  // Parse 32bit int fails for uint8 then succeeds with int32
  pass &= _set_up_test_case(&parser, __func__, "123456") &&
          !parser.parse_arg(&uint8val) &&
          parser.parse_arg(&int32val) && int32val == 123456 &&
          parser.finished();

  // Parse negative int fails for unsigned int then succeeds with int
  pass &= _set_up_test_case(&parser, __func__, "-2") &&
          !parser.parse_arg(&uint8val) &&
          parser.parse_arg(&int8val) && int8val == -2 &&
          parser.finished();

  // Parse list fails when not enough space, then succeeds with more space
  pass &= _set_up_test_case(&parser, __func__, "[1,2,3,4]") &&
          !parser.parse_list(lst, &list_len, 3) &&
          parser.parse_list(lst, &list_len, 4) && list_len == 4 &&
          lst[0] == 1 && lst[1] == 2 && lst[2] == 3 && lst[3] == 4 &&
          parser.finished();

  // Parse string fails when not enough space, then succeeds with more space
  pass &= _set_up_test_case(&parser, __func__, "\"hello world!\"") &&
          !parser.parse_str(str, 5) &&
          parser.parse_str(str, sizeof(str)) &&
          strcmp(str, "hello world!") == 0 &&
          parser.finished();

  return pass;
}


bool compare_msgs(const char *exp_msg, const char *act_msg) {
  if (strcmp(exp_msg, act_msg) != 0) {
    fprintf(stderr, "Bad msg: '%s' vs '%s'\n", exp_msg, act_msg);
    return false;
  }
  return true;
}

bool test_checksum() {
  printf("Running %s()...\n", __func__);

  OatmealMsg msg;

  /* Construct examples from the README.md */

  msg.start("DIS", 'R', "XY");
  msg.finish();
  if (!compare_msgs(msg.frame(), "<DISRXY>i_")) {
    return false;
  }

  msg.start("RUN", 'R', "aa");
  msg.append(1.23, 3);
  msg.append(true);
  msg.append("Hi!");
  msg.append_list_start();
  msg.append(1);
  msg.append(2);
  msg.append_list_end();
  msg.finish();

  if (!compare_msgs(msg.frame(), "<RUNRaa1.23,T,\"Hi!\",[1,2]>-b")) {
    return false;
  }

  msg.start("XYZ", 'A', "zZ");
  msg.append(101);
  msg.append_list_start();
  msg.append(0);
  msg.append(42);
  msg.append_list_end();
  msg.finish();

  if (!compare_msgs(msg.frame(), "<XYZAzZ101,[0,42]>SH")) {
    return false;
  }

  msg.start("LOL", 'R', "Oh");
  msg.append(123);
  msg.append(true);
  msg.append(99.9, 3);
  msg.finish();

  if (!compare_msgs(msg.frame(), "<LOLROh123,T,99.9>SS")) {
    return false;
  }

  // Test long messages
  msg.start("HRT", 'B', "VU");
  msg.append_dict_start();
  msg.append_dict_key_value("a", 5.1);
  msg.append_dict_key_value("avail_kb", 247);
  msg.append_dict_key_value("b", "hi");
  msg.append_dict_key_value("loop_ms", 1);
  msg.append_dict_key_value("uptime", 16);
  msg.append_dict_end();
  msg.finish();

  if (!compare_msgs(msg.frame(),
      "<HRTBVU{a=5.1,avail_kb=247,b=\"hi\",loop_ms=1,uptime=16}>BH")) {
    return false;
  }

  msg.start("HRT", 'B', "0E");
  msg.append_dict_start();
  msg.append_dict_key_value("Itotal", 0.372172, 6);
  msg.append_dict_key_value("v1", false);
  // keys are sorted alphabetically when sending from python, so this is
  // the order in which we would receive these arguments
  msg.append_dict_key_value("v10", false);
  msg.append_dict_key_value("v2", false);
  msg.append_dict_key_value("v3", false);
  msg.append_dict_key_value("v4", false);
  msg.append_dict_key_value("v5", false);
  msg.append_dict_key_value("v6", false);
  msg.append_dict_key_value("v7", false);
  msg.append_dict_key_value("v8", false);
  msg.append_dict_key_value("v9", false);
  msg.append_dict_end();
  msg.finish();

  if (!compare_msgs(msg.frame(),
      "<HRTB0E{Itotal=0.372172,v1=F,v10=F,v2=F,v3=F,v4=F,v5=F,v6=F,v7=F,v8=F,v9=F}>yI")) {
    return false;
  }

  msg.start("DIS", 'A', "ea");
  msg.append("ValveCluster");
  msg.append(0);
  msg.append("0031FFFFFFFFFFFF4E45356740010017");
  msg.append("e5938cd");
  msg.finish();

  if (!compare_msgs(msg.frame(),
      "<DISAea\"ValveCluster\",0,\"0031FFFFFFFFFFFF4E45356740010017\","
      "\"e5938cd\">Hg")) {
    return false;
  }

  return true;
}

bool test_write_hex() {
  printf("Running %s()...\n", __func__);

  OatmealMsg msg;
  msg.start("TST", 'R', "ab");
  if (msg.write_hex(0x12345678) != 8) { return false; }
  if (msg.write_hex(0x90abcdef) != 8) { return false; }
  msg.finish();
  if (strncmp(msg.args(), "1234567890ABCDEF", msg.args_len()) != 0) {
    printf("'%s'\n", msg.args());
    return false;
  }

  msg.start("TST", 'R', "ab");
  if (msg.write_hex(0x123) != 8) { return false; }
  if (msg.write_hex(0xabc) != 8) { return false; }
  msg.finish();
  if (strncmp(msg.args(), "0000012300000ABC", msg.args_len()) != 0) {
    printf("'%s'\n", msg.args());
    return false;
  }

  // Test that we eventually fail to add test
  msg.start("TST", 'R', "ab");
  for (int i = 0; i < 200; i++) { msg.write_hex(0x1234abcd); }
  // check that we return 0 now
  if (msg.write_hex(0x1234abcd) != 0) { return false; }

  return true;
}

int main() {
  OatmealMsg msg;
  msg.start("TST", 'R', "ab");
  msg.append("hi");
  msg.finish();
  printf("'%s'\n", msg.frame());

  if (!test_format_ints()) { return EXIT_FAILURE; }
  if (!verbose_test()) { return EXIT_FAILURE; }
  if (!test_mixed_args()) { return EXIT_FAILURE; }
  if (!test_list_of_strs()) { return EXIT_FAILURE; }
  if (!test_complex_args()) { return EXIT_FAILURE; }
  if (!test_explicit_sep_parsing()) { return EXIT_FAILURE; }
  if (!test_parsing_none()) { return EXIT_FAILURE; }
  if (!test_parsing_fails()) { return EXIT_FAILURE; }
  if (!test_parse_fails_and_recovers()) { return EXIT_FAILURE; }
  if (!test_parse_dicts()) { return EXIT_FAILURE; }
  if (!test_write_hex()) { return EXIT_FAILURE; }
  if (!test_checksum()) { return EXIT_FAILURE; }
  printf("\n  Success!\n\n");
  return EXIT_SUCCESS;
}
