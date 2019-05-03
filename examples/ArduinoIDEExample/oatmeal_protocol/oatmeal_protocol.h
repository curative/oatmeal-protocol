/*
  oatmeal_protocol.h
  Copyright 2019, Shield Diagnostics and the Oatmeal Protocol contributors
  License: Apache 2.0

  Contains the OatmealPort class and methods to send OatmealMsg objects over
  UART.
*/
#ifndef OATMEAL_PROTOCOL_H_
#define OATMEAL_PROTOCOL_H_

#include <Arduino.h>

/*
Must import oatmeal_message.h as the calling code assumes including this file
also includes oatmeal_message.h. Otherwise we would need the client to always
need a redundant include to be future-proof.
*/
#include "oatmeal_message.h"

/* Set default values if the user did not set them */
#ifndef OATMEAL_HARDWARE_ID_STR
  #define OATMEAL_HARDWARE_ID_STR "UNDEF_ID"
#endif

#ifndef OATMEAL_VERSION_STR
  #define OATMEAL_VERSION_STR "UNDEF_VER"
#endif

#ifndef OATMEAL_INSTANCE_IDX
  #define OATMEAL_INSTANCE_IDX 0
#endif


class OatmealStats {
  /** Statistics about sending and receiving Oatmeal Protocol messages over
  UART. */

 public:
  /* statistics on UART messaging */
  size_t n_frame_too_short = 0;
  size_t n_frame_too_long = 0;
  size_t n_missing_start_byte = 0;
  size_t n_missing_end_byte = 0;
  size_t n_bad_checksums = 0;
  size_t n_illegal_character = 0;

  size_t n_bytes_read = 0;
  size_t n_good_frames = 0;
  size_t n_frames_written = 0;

  // stats updated by the user
  size_t n_unknown_opcode = 0;  /** unexpected opcode */
  size_t n_bad_messages = 0;  /** unexpected flag or args */

  /** Get the total number of errors encountered. */
  size_t get_n_errors() const {
    return n_frame_too_short +
           n_frame_too_long +
           n_missing_start_byte +
           n_missing_end_byte +
           n_bad_checksums +
           n_illegal_character +
           n_unknown_opcode +
           n_bad_messages;
  }

  /** Reset all statistics. */
  void reset() {
    *this = OatmealStats();
  }

  /**
  Add error stats to a message, only adds if there are any errors.
  Call reset() on this object after calling this method to reset the counters.
  @returns the number of bytes written
  */
  size_t format_stats(OatmealMsg *msg) const;
};


class OatmealPort {
 private:
  enum State : uint8_t {WaitingOnStart, WaitingOnEnd,
                        WaitingOnLength, WaitingOnChecksum};

  HardwareSerial *port;

  OatmealPort::State state = WaitingOnStart;

  /*
  Bytes read into `buf` from UART Serial port. Bytes `b_start..b_mid-1`
  inclusive have been processed and do not contain a complete message. Bytes
  `b_mid..b_end-1` inclusive have not been processed. Multiple messages can sit
  in the buffer, we parse them out one at a time into OatmealMsg objects with
  the recv() method.
  e.g. [x,x,x,x,H,e,l,l,o,x,x,x,x] b_start=4, b_end=9

  +8 is just padding when reading to make sure a whole message can fit in the
  buffer along with some some noises byte beforehand.
  */
  char buf[OatmealMsg::MAX_MSG_LEN + 8];
  size_t b_start = 0, b_mid, b_end = 0;

  /* Variables used in the discovery request */
  const char *role_str = nullptr;
  const char *hardware_id = nullptr;
  const char *version_str = nullptr;
  uint32_t instance_idx = 0;

  size_t token = 0;
  char token_str[OatmealMsg::TOKEN_LEN+1];

  bool send_logging = false;

  bool send_heartbeats = true;
  long last_heartbeat_ms = 0, heartbeats_period_ms = 0;

  /*
  Non-blocking read waiting bytes into the OatmealPort buffer.
  Shifts data in buffer back to the beginning to make space if needed.
  Returns True if there are unprocessed bytes to use in the port buffer
  */
  bool _read_uart_data();

  /** Parse data from the input buffer.

  If successful, a new message is stored in `msg_in` and returns `true`

  @returns `true` on success and stores message in `msg_in`, `false` otherwise.
  */
  bool _consume_from_buffer();

  void send_discovery_ack(const char *token);

  /* ---------- Streaming output ---------- */

  size_t curr_msg_len = 0;
  uint8_t curr_msg_checksum = 0;
  char last_chr = '\0';

 public:
  /** Default baud rate (symbols-per-second) for the underlying serial port. */
  static const int32_t DEFAULT_BAUD_RATE = 115200;

  /** Statistics about this port */
  OatmealStats stats;

  /** Most recent message read in by this port.

  Valid immediately after `recv()` or `check_for_msgs()` have returned true.
  Reset each time `recv()` or `check_for_msgs()` is called.
  Invalid to access after `recv()` or `check_for_msgs()` have returned false.
  */
  OatmealMsgReadonly msg_in;

  /** Create a new OatmealPort.

  Any strings passed to the OatmealPort constructor MUST be stored by the
  caller as null terminated strings that can be accessed when calling any
  OatmealPort method. The safest way to do this is by passing a string literal:

      OatmealPort port = OatmealPort(&Serial1, "NAMEHERE")

  since string literals in C/C++ are guaranteed to be accessible for the life
  of the program.

  The caller must call .init() on this object before using it to send/receive
  messages.

  @param h_port: serial port to use
  @param _role_str: name representing the behaviour of this board.
  @param _instance_idx: integer to tell this board apart from others
  @param _hardware_id: string to identify this hardware. If not set, attempt to
                       use CPU identifier or fall back to OATMEAL_HARDWARE_ID
  @param _version_str: string representing the software being run. If not set,
                       falls back to using OATMEAL_VERSION_STR
  */
  OatmealPort(HardwareSerial *h_port,
              const char *_role_str,
              uint32_t _instance_idx = OATMEAL_INSTANCE_IDX,
              const char *_hardware_id = nullptr,
              const char *_version_str = nullptr) :
      port(h_port), msg_in(buf, 0) {
    strcpy(token_str, "aa");
    set_discovery_ptrs(_role_str, _instance_idx, _hardware_id, _version_str);
  }

  /** Set up the port */
  void init(int32_t baud_rate = DEFAULT_BAUD_RATE) {
    port->begin(baud_rate);
  }

  /**
  Set the values used to respond to a discovery request.

  Any strings passed to this method MUST be stored by the caller as
  null-terminated strings that can be accessed when calling any OatmealPort
  method. The safest way to do this is by passing a string literal e.g.

      OatmealPort port = OatmealPort(&Serial1, "NAMEHERE")

  since string literals in C/C++ are guaranteed to be accessible for the life
  of the program.

  @param _role_str: name representing the behaviour of this board.
  @param _instance_idx: integer to tell this board apart from others
  @param _hardware_id: string to identify this hardware. If not set, attempt to
                       use CPU identifier or fall back to OATMEAL_HARDWARE_ID
  @param _version_str: string representing the software being run. If not set,
                       falls back to using OATMEAL_VERSION_STR
  */
  void set_discovery_ptrs(const char *_role_str,
                          uint32_t _instance_idx = OATMEAL_INSTANCE_IDX,
                          const char *_hardware_id = nullptr,
                          const char *_version_str = nullptr) {
    role_str = _role_str;
    instance_idx = _instance_idx;
    hardware_id = _hardware_id;
    version_str = _version_str;
  }

  /** Send bytes directly over the underlying port serial port with a newline */
  void send(const char *buf, size_t n) {
    port->write((const uint8_t*)buf, n);
    port->write('\n');

    /*
    Uncommenting the flush() call here - which just blocks until the data
    has been sent over the serial port - could conceivably help prevent
    packet loss in cases where we're writing fast enough to overflow the
    board's output buffer. We've never witnessed this happening, but it's a
    thing we can try if we ever do.
    */
    // port->flush();

    stats.n_frames_written++;
  }

  /** Send a message over the port */
  void send(const OatmealMsgReadonly &msg) { send(msg.frame(), msg.length()); }

  /** Construct and send a message over the port */
  void send(const char *cmd, char flag, const char *token = nullptr) {
    if (token == nullptr) { token = next_token(); }
    start(cmd, flag, token);
    finish();
  }

  /** Send an acknowledge packet, with a given flag */
  void send_response(const OatmealMsgReadonly &msg, char flag) {
    start(msg.opcode(), flag, msg.token());
    finish();
  }

  /** Send an 'ack' acknowledgement response to a given message */
  void send_ack(const OatmealMsgReadonly &msg) { send_response(msg, 'A'); }
  /** Send a 'done' response to a given message */
  void send_done(const OatmealMsgReadonly &msg) { send_response(msg, 'D'); }
  /** Send a 'failed' response to a given message */
  void send_failed(const OatmealMsgReadonly &msg) { send_response(msg, 'F'); }

  /** Increment the OatmealPort token and return a pointer to it.
  @returns A pointer to the next token to use. */
  const char* next_token();

  /** Read a message from the port into internal memory `msg_in`.
  Corrupted messages are dropped. Partial messages are left in the input buffer.
  Non-blocking.
  @returns `true` if a message was read into `msg_in`. */
  bool recv();

  /** Read a message from the port into `msg`.
  Corrupted messages are dropped. Partial messages are left in the input buffer.
  Non-blocking.
  @returns `true` if a message was read into `msg`
  */
  bool recv(OatmealMsgReadonly *msg) {
    if (recv()) { *msg = msg_in; return true; }
    return false;
  }

  /** Read a message from the port into `msg`.
  Corrupted messages are dropped. Partial messages are left in the input buffer.
  Non-blocking.
  @returns `true` if a message was read into `msg`
  */
  bool recv(OatmealMsg *msg) {
    if (recv()) { msg->copy_from(msg_in); return true; }
    return false;
  }

  /** Attempt to parse a built-in message.

  Built-in messages include a discovery request or toggling logging/heartbeats.
  If successful sends an ACK packet back.

  @returns `true` if parsed and ack'd successfully, `false` otherwise. */
  bool handle_msg(const OatmealMsgReadonly &msg);

  /** Read messages and reply to any built-in commands (DISR, HRTR, LOGR)
  @returns `true` if a message was read into `msg_in` for the user */
  bool check_for_msgs() {
    while (recv()) {
      if (!handle_msg(msg_in)) { return true; }
    }
    return false;
  }

  /** Read messages and reply to any built-in commands (DISR, HRTR, LOGR)
  Copies the read-only message into parameter `msg`.
  @returns `true` if a message was read into `msg_in` for the user */
  bool check_for_msgs(OatmealMsgReadonly *msg) {
    if (check_for_msgs()) { *msg = msg_in; return true; }
    return false;
  }

  /** Read messages and reply to any built-in commands (DISR, HRTR, LOGR)
  @returns `true` if a message was read into `msg` for the user. */
  bool check_for_msgs(OatmealMsg *msg) {
    if (check_for_msgs()) { msg->copy_from(msg_in); return true; }
    return false;
  }

  /* ---------- Logging ---------- */

  /** Turn logging on/off.
  If on, calls to log methods (e.g. `log(const char*, const char*)`) on this
  port will generate messages that are sent down this port. If off, calling log
  methods does nothing.
  @see `log(const char*, const char*) `*/
  void set_logging_on(bool status) { send_logging = status; }

  /** Send a log message.

  If logging is on, this will send an Oatmeal log message down this port.
  If logging is off, this method will do nothing.

  @see set_logging_on(bool)
  @see log_debug(const char*)
  @see log_info(const char*)
  @see log_warning(const char*)
  @see log_error(const char*) */
  void log(const char *level, const char *msg_text) {
    if (send_logging) {
      start("LOG", 'B', next_token());
      append(level);
      append(msg_text);
      finish();
    }
  }

  /** Send a log message with level `DEBUG` and message `txt`
  @see `log(const char*, const char*) `*/
  void log_debug(const char *txt) { log("DEBUG", txt); }
  /** Send a log message with level `INFO` and message `txt`
  @see `log(const char*, const char*) `*/
  void log_info(const char *txt) { log("INFO", txt); }
  /** Send a log message with level `WARNING` and message `txt`
  @see `log(const char*, const char*) `*/
  void log_warning(const char *txt) { log("WARNING", txt); }
  /** Send a log message with level `ERROR` and message `txt`
  @see `log(const char*, const char*) `*/
  void log_error(const char *txt) { log("ERROR", txt); }

  /* ---------- Heartbeats ---------- */

  /** Set whether or not the user should be sending heartbeat message. */
  void set_heartbeats_on(bool status) { send_heartbeats = status; }

  /** Set the minimum time between heartbeat messages.
  @param period_ms: minimum time between heartbeat messages in milliseconds */
  void set_heartbeats_period(long period_ms) {
    heartbeats_period_ms = period_ms;
  }

  /** Construct a heartbeat message with general statistics in it. */
  void build_status_heartbeat(OatmealMsg *resp, uint32_t max_loop_ms);

  /** Whether or not to send a heartbeat message.

  Will return False if heartbeats have been turned off with
  `OatmealPort.set_heartbeats_on(false)` or if it is too soon since this method
  last returned `true`. The time period between heartbeats is set with
  `OatmealPort.set_heartbeats_period()`

  @returns: Whether the caller should send a heartbeat message now.
  */
  bool send_heartbeat_now(long now_ms = 0) {
    if (!now_ms) { now_ms = millis(); }
    if (send_heartbeats &&
        now_ms - last_heartbeat_ms >= heartbeats_period_ms) {
      last_heartbeat_ms = now_ms;
      return true;
    }
    return false;
  }

  /* ---------- Streaming output messages ---------- */

  /** Write out a single raw character
  @returns the number of bytes written (1)
  @see OatmealMsg::write(const char) */
  size_t write(const char c) {
    curr_msg_checksum = (curr_msg_checksum + c) * OATMEAL_CHECKSUM_COEFF;
    curr_msg_len++;
    last_chr = c;
    return port->write(c);
  }

  /** Write out bytes from a null-terminated string
  @returns the number of bytes written
  @see OatmealMsg::write(const char*) */
  size_t write(const char *b) {
    return write(b, strlen(b));
  }

  /** Write out `n` bytes pointed to by `ptr`.
  @returns the number of bytes written
  @see OatmealMsg::write(const char*, size_t) */
  size_t write(const char *b, size_t n) {
    if (!n) { return 0; }
    for (size_t i = 0; i < n; i++) {
      curr_msg_checksum = (curr_msg_checksum + b[i]) * OATMEAL_CHECKSUM_COEFF;
    }
    curr_msg_len += n;
    last_chr = b[n-1];
    return port->write(b, n);
  }

  /** Encode and write a byte to this frame as part of a str/data message argument.
  @returns the number of bytes written (1)
  @see OatmealMsg::write_encoded(const char c) */
  size_t write_encoded(const char c) {
    if (c == '\\') { return write('\\') + write('\\'); }
    else if (c == '"') { return write('\\') + write('"'); }
    else if (c == '<') { return write('\\') + write('('); }
    else if (c == '>') { return write('\\') + write(')'); }
    else if (c == '\n') { return write('\\') + write('n'); }
    else if (c == '\r') { return write('\\') + write('r'); }
    else if (c == '\0') { return write('\\') + write('0'); }
    else { return write(c); }
  }

  /** Encode and write `n` bytes pointed to by `ptr`.
  @returns the number of bytes written.
  @see OatmealMsg::write_encoded(const char*, size_t) */
  size_t write_encoded(const char *b, size_t n) {
    size_t n_sent = 0;
    for (size_t i = 0; i < n; i++) { n_sent += write_encoded(b[i]); }
    return n_sent;
  }

  /** Append a value as hex.
  @returns Number of frame bytes written out.
  @see OatmealMsg::write_hex(uint32_t) */
  size_t write_hex(uint32_t val) {
    char hex[9];
    OatmealFmt::uint32_to_hex(hex, val);
    return write(hex, 8);
  }

 private:
  /** Write out an integer / float / string / boolean value
  @param sig_figs: number of significant figures, max is 14.
  @returns the number of bytes written out */
  template<typename T>
  size_t _write_val(T val, int sig_figs = OatmealFmt::DEFAULT_SIG_FIGS) {
    /*
    Max character limits without null-terminating byte:
    - 64 bit ints, -9223372036854775808 .. 9223372036854775807 => 20 chars max.
    - bool T/F => 1 char max.
    - None: N => 1 char max.
    - float: 1.17549e-38 .. 3.40282e+38 => 5+sig_figs
    - double: 2.22507e-308 .. 1.79769e+308 => 6+sig_figs
    */
    /* limit format length to 20 chars, which gives us 20-6 = 14 sig figs max */
    const int SCI_NOTATION_OVERHEAD = 6;
    sig_figs = min(sig_figs, 20-SCI_NOTATION_OVERHEAD);
    /* +1 => null-terminating byte, +5 => safety margin */
    char tmp[20+1+5];
    size_t n = OatmealFmt::format(tmp, sizeof(tmp), val, sig_figs);
    return write(tmp, n);
  }

 public:
  /** Write out a representation of a double
  @returns the number of bytes written (no nul-byte) or 0 on failure.
  @see OatmealMsg::write(double, int) */
  size_t write(double val, int sig_figs = OatmealFmt::DEFAULT_SIG_FIGS) {
    return _write_val(val, sig_figs);
  }

  /** Write out a representation of a float
  @returns the number of bytes written (no nul-byte) or 0 on failure.
  @see OatmealMsg::write(float, int) */
  size_t write(float val, int sig_figs = OatmealFmt::DEFAULT_SIG_FIGS) {
    return _write_val(val, sig_figs);
  }

  /** Append a value as (part of) an argument (int, float, double, bool, string)
  @returns The number of bytes written out
  @see OatmealMsg::write(T) */
  template<typename T>
  size_t write(T val) {
    return _write_val(val);
  }

  /** Construct a message with a given command, flag and token
  @returns Number of frame bytes written out
  @see OatmealMsg::start(const char*, char, const char*) */
  size_t start(const char *cmd, char flag, const char *token) {
    curr_msg_len = curr_msg_checksum = 0;
    return write(OatmealFmt::START_BYTE) +
           write(cmd, OatmealMsg::CMD_LEN) +
           write(flag) +
           write(token, OatmealMsg::TOKEN_LEN);
  }

  /* Argument construction */

  /** Append an arg separator onto the message.
  @returns Number of frame bytes written out
  @see OatmealMsg::separator() */
  size_t separator() {
    return write(OatmealFmt::ARG_SEP);
  }

  /** Append an arg separator onto the message only if needed.
  @returns The number of bytes written out
  @see OatmealMsg::separator_if_needed() */
  size_t separator_if_needed() {
    if (curr_msg_len > OatmealMsg::ARGS_OFFSET &&
        last_chr != OatmealFmt::LIST_START &&
        last_chr != OatmealFmt::DICT_START &&
        last_chr != OatmealFmt::DICT_KV_SEP &&
        last_chr != OatmealFmt::ARG_SEP) {
      return write(OatmealFmt::ARG_SEP);
    } else {
      return 0;
    }
  }

  /** Append a null terminated string argument.
  @returns Number of frame bytes written out
  @see OatmealMsg::append(const char*) */
  size_t append(const char *str) {
    size_t n = separator_if_needed() + write('"');
    for (; *str; str++) { n += write_encoded(*str); }
    n += write('"');
    return n;
  }

  /** Append a data bytes argument to the message.
  @returns Number of frame bytes written out
  @see OatmealMsg::append(const uint8_t*, size_t) */
  size_t append(const uint8_t *data, size_t n_bytes) {
    size_t i, n = separator_if_needed() + write("0\"", 2);
    for (i = 0; i < n_bytes; i++) { n += write_encoded(data[i]); }
    n += write('"');
    return n;
  }

  /** Append an integer or string to the list of arguments.
  @returns Number of frame bytes written out
  @see OatmealMsg::append(T) */
  template<typename T>
  size_t append(T val) {
    size_t n = separator_if_needed() + write(val);
    return n;
  }

  /** Append a double to the list of arguemnts.
  @returns Number of frame bytes written out
  @see OatmealMsg::append(double, int) */
  size_t append(double val, int sig_figs = OatmealFmt::DEFAULT_SIG_FIGS) {
    size_t n = separator_if_needed() + write(val, sig_figs);
    return n;
  }

  /** Append a float to the list of arguemnts.
  @returns Number of frame bytes written out
  @see OatmealMsg::append(float, int) */
  size_t append(float val, int sig_figs = OatmealFmt::DEFAULT_SIG_FIGS) {
    size_t n = separator_if_needed() + write(val, sig_figs);
    return n;
  }

  /** Append a list start character.
  @returns Number of frame bytes written out
  @see OatmealMsg::append_list_start() */
  size_t append_list_start() {
    size_t n = separator_if_needed() + write(OatmealFmt::LIST_START);
    return n;
  }

  /** Append a list end character.
  @returns Number of frame bytes written out (1)
  @see OatmealMsg::append_list_end() */
  size_t append_list_end() {
    return write(OatmealFmt::LIST_END);
  }

  /** Append a dict start character.
  @returns The number of bytes written out
  @see OatmealMsg::append_dict_start() */
  size_t append_dict_start() {
    return separator_if_needed() + write(OatmealFmt::DICT_START);
  }

  /** Append a dict end character.
  @returns The number of bytes written out (1)
  @see OatmealMsg::append_dict_end() */
  size_t append_dict_end() {
    return write(OatmealFmt::DICT_END);
  }

  /** Append (separator if needed then) a dictionary key and equals sign.
  Call e.g. `append(val)` after this method to append a value for the key.
  @returns The number of bytes written out
  @see OatmealMsg::append_dict_key(const char*) */
  size_t append_dict_key(const char *key) {
    return write(key) + write(OatmealFmt::DICT_KV_SEP);
  }

  /** Append a key=value pair to a dictionary for a float value.
  @returns The number of bytes written out
  @see OatmealMsg::append_dict_key_value(const char*, float, int) */
  template<typename T>
  size_t append_dict_key_value(const char *key, float val,
                               int sig_figs = OatmealFmt::DEFAULT_SIG_FIGS) {
    return separator_if_needed() + append_dict_key(key) + append(val, sig_figs);
  }

  /** Append a key=value pair to a dictionary for a double value.
  @returns The number of bytes written out
  @see OatmealMsg::append_dict_key_value(const char*, T) */
  template<typename T>
  size_t append_dict_key_value(const char *key, double val,
                               int sig_figs = OatmealFmt::DEFAULT_SIG_FIGS) {
    return separator_if_needed() + append_dict_key(key) + append(val, sig_figs);
  }

  /** Append a key=value pair to a dictionary for an integer or string value.
  @returns The number of bytes written out
  @see OatmealMsg::append_dict_key_value(const char*, T) */
  template<typename T>
  size_t append_dict_key_value(const char *key, T val) {
    return separator_if_needed() + append_dict_key(key) + append(val);
  }

  /** Append a key=value pair to a dictionary for a bytes value.
  @param key: null termintated string to use as the key
  @param data: bytes to use as the value
  @param len: number of bytes to represent in the value
  @returns The number of bytes written out
  @see OatmealMsg::append_dict_key_value(const char*, const uint8_t*, size_t) */
  size_t append_dict_key_value(const char *key,
                               const uint8_t *data, size_t len) {
    return separator_if_needed() + append_dict_key(key) + append(data, len);
  }

  /** Append a None/NULL/nil value
  @returns Number of frame bytes written out.
  @see OatmealMsg::append_none() */
  size_t append_none() {
    size_t n = separator_if_needed() + write('N');
    return n;
  }

  /** End a message with a frame end byte and checksum bytes to a message frame.
  After calling this method you cannot add any more arguments.
  @returns the number of bytes written (3).
  @see OatmealMsg::finish() */
  size_t finish() {
    // +3 for the last three bytes: '>', checklen, checksum
    uint16_t checklen_byte = (curr_msg_len+3) * OATMEAL_CHECKLEN_COEFF;
    // _stream_write updates curr_msg_len and curr_msg_checksum
    write(OatmealFmt::END_BYTE);
    write(OatmealMsg::checkbyte_uint16_to_ascii(checklen_byte));
    write(OatmealMsg::checkbyte_uint16_to_ascii(curr_msg_checksum));
    port->write('\n');
    return 3; /* Don't include the newline (not part of the frame) */
  }
};

#endif /* OATMEAL_PROTOCOL_H_ */
