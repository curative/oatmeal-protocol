/*
  oatmeal_protocol.cpp
  Copyright 2019, Shield Diagnostics and the Oatmeal Protocol contributors
  License: Apache 2.0

  Contains the OatmealPort class and methods to send OatmealMsg objects over
  UART.
*/

#include "oatmeal_protocol.h"

#define _QUOTE(str) #str
#define _EXPAND_AND_QUOTE(str) _QUOTE(str)


size_t OatmealStats::format_stats(OatmealMsg *msg) const {
  size_t n_oatmeal_errs = n_frame_too_short +
                          n_frame_too_long +
                          n_missing_start_byte +
                          n_missing_end_byte +
                          n_bad_checksums +
                          n_illegal_character +
                          n_unknown_opcode +
                          n_bad_messages;
  size_t orig_msg_len = msg->length();

  if (n_oatmeal_errs) {
    msg->append("oatmeal_errs=");
    msg->append(n_oatmeal_errs);

    if (n_frame_too_short)   { msg->append(",sh="); msg->append(n_frame_too_short); }
    if (n_frame_too_long)    { msg->append(",lg="); msg->append(n_frame_too_long); }
    if (n_missing_start_byte){ msg->append(",ms="); msg->append(n_missing_start_byte); }
    if (n_missing_end_byte)  { msg->append(",me="); msg->append(n_missing_end_byte); }
    if (n_bad_checksums)     { msg->append(",bc="); msg->append(n_bad_checksums); }
    if (n_illegal_character) { msg->append(",bb="); msg->append(n_illegal_character); }
    if (n_unknown_opcode)    { msg->append(",uo="); msg->append(n_unknown_opcode); }
    if (n_bad_messages)      { msg->append(",bm="); msg->append(n_bad_messages); }
  }

  return msg->length() - orig_msg_len;
}

/*
Increment the OatmealPort token and return a pointer to it.
Not threadsafe.
*/
const char* OatmealPort::next_token() {
  // Increment token and convert to string
  const size_t n_chars = OatmealFmt::N_TOKEN_CHARS;
  token = (token+1) % (n_chars*n_chars);
  token_str[1] = OatmealFmt::TOKEN_CHARS[token % n_chars];
  token_str[0] = OatmealFmt::TOKEN_CHARS[token / n_chars];
  token_str[2] = '\0';
  return token_str;
}


bool OatmealPort::_read_uart_data() {
  // If we've read the start of a frame and it's already at max length and not
  // a complete message, reset buffer
  if (b_mid - b_start >= OatmealMsg::MAX_MSG_LEN) {
    b_start = b_mid;
    state = WaitingOnStart;
  }
  // Shift buffer start back to zero if needed
  if (b_start == b_end) {
    b_start = b_mid = b_end = 0;
  } else if (b_start > 0) {
    /* Shift waiting data to the start of the input buffer */
    memmove(buf, buf+b_start, b_end-b_start);
    b_mid -= b_start;
    b_end -= b_start;
    b_start = 0;
  }
  // Get the number of bytes waiting from the serial port
  size_t nbytes_avail = port->available();
  // Get space remaining in the input buffer
  size_t nbuf_rem = sizeof(buf) - b_end;
  // Get the min of the two above numbers
  size_t n = nbuf_rem < nbytes_avail ? nbuf_rem : nbytes_avail;
  b_end += port->readBytes(buf+b_end, n);
  stats.n_bytes_read += n;
  return b_mid < b_end;
}


/** Parse data from the input buffer.

If successful, a new message is stored in `msg_in` and returns `true`

@returns `true` on success and stores message in `msg_in`, `false` otherwise */
bool OatmealPort::_consume_from_buffer() {
  const char *msg_buf;
  size_t n;

  /*
  We assemble a complete frame before validating it to check that the length and
  checksum check bytes are consistent, it's a valid length and that the frame
  start and end bytes are `<` and `>` respectively.

  If a frame is invalid, we throw it out.

  A frame is considered to start at any `<` byte.
  */

  // Iterate to find start or end byte
  for (; b_mid < b_end; b_mid++) {
    if (buf[b_mid] == 0) {
      // Invalid byte - reset the parser state and record the error
      b_start = b_mid;
      state = WaitingOnStart;
      stats.n_illegal_character++;
    } else if (buf[b_mid] == OatmealFmt::START_BYTE) {
      // A start byte means a packet is now starting, regardless of the state
      // we were in.
      stats.n_missing_end_byte += (state != WaitingOnStart);
      b_start = b_mid;
      state = WaitingOnEnd;
    } else if (state == WaitingOnStart) {
      // Don't need to do anything here, start bytes are handled above
      // Just ignore non-frame-start bytes by resetting frame start (b_start).
      b_start = b_mid;
      stats.n_missing_start_byte += (buf[b_mid] == OatmealFmt::END_BYTE);
    } else if (state == WaitingOnEnd) {
      // < => frame start, > => frame end, other => add to frame
      if (buf[b_mid] == OatmealFmt::END_BYTE) {
        state = WaitingOnLength;
      }
    } else if (state == WaitingOnLength) {
      // Now have a length-checksum byte
      // < => frame start, accept any other byte as length checksum
      state = WaitingOnChecksum;
    } else if (state == WaitingOnChecksum) {
      // Now have a checksum byte
      msg_buf = buf+b_start;
      n = b_mid+1-b_start;
      b_start = b_mid+1;
      state = WaitingOnStart;
      if (n < OatmealMsg::MIN_MSG_LEN) {
        stats.n_frame_too_short++;
      } else if (n > OatmealMsg::MAX_MSG_LEN) {
        stats.n_frame_too_long++;
      } else if (!OatmealMsg::validate_frame(msg_buf, n)) {
        stats.n_bad_checksums++;
      } else {
        msg_in = OatmealMsgReadonly(msg_buf, n);
        stats.n_good_frames++;
        b_mid++;
        return true;
      }
    }
  }
  return false;
}

/*
Read a message from the port into `msg`. Returns true if a complete message
was read. Messages with invalid checksums are dropped. Non-blocking.
*/
bool OatmealPort::recv() {
  // Reset msg_in
  msg_in = OatmealMsgReadonly(buf, 0);

  // Attempt to read from the existing buffer
  if (_consume_from_buffer()) { return true; }

  // Read into the buffer and parse any messages
  while (_read_uart_data()) {
    if (_consume_from_buffer()) { return true; }
  }

  return false;
}


bool OatmealPort::handle_msg(const OatmealMsgReadonly &msg) {
  OatmealArgParser parser;
  bool bool_arg = false;

  if (msg.is_opcode("DISR")) {
    /* Discovery Request doesn't have any parameters - no need to check */
    send_discovery_ack(msg.token());
    return true;
  } else if (msg.is_opcode("HRTR")) {
    /* Heartbeat toggle request; args: <status:bool> */
    if (parser.init(msg) &&
        parser.parse_arg(&bool_arg) &&
        parser.finished()) {
      set_heartbeats_on(bool_arg);
      send_ack(msg);
      return true;
    }
  } else if (msg.is_opcode("LOGR")) {
    /* Logging toggle request; args: <status:bool> */
    if (parser.init(msg) &&
        parser.parse_arg(&bool_arg) &&
        parser.finished()) {
      set_logging_on(bool_arg);
      send_ack(msg);
      return true;
    }
  }

  return false;
}


#ifdef TEENSY36
static const time_t start_time = Teensy3Clock.get();
#endif


static inline int32_t get_free_ram_bytes() {
  // brkval is a pointer the top of the heap (grows up)
  // &v is a pointer to the end of the stack (grows down)
  // free_ram ~= end_of_stack - start_of_heap
  extern int *__brkval;
  char v;
  return &v - reinterpret_cast<char*>(__brkval);
}


void OatmealPort::build_status_heartbeat(OatmealMsg *resp,
                                         uint32_t max_loop_ms) {
  // Oatmeal errors
  if (stats.format_stats(resp)) { resp->append(','); }
  stats.reset();
  // Max loop period (milliseconds)
  resp->append_dict_key_value("loop_ms", max_loop_ms);
  // Free RAM
  int32_t avail_kb = get_free_ram_bytes() / 1024;
  resp->append_dict_key_value("avail_kb", avail_kb);
  // Uptime, if we have a real time clock (RTC)
  #ifdef TEENSY36
    uint32_t uptime_mins = (Teensy3Clock.get() - start_time) / 60;
    resp->append_dict_key_value("uptime", uptime_mins);
  #endif
}

void OatmealPort::send_discovery_ack(const char *token) {
  /*
  Report <role>,<instance_idx>,<hardware_id>,<version>
    - role (str): board type
    - instance_idx (int): index of the board (to tell apart different boards
      with same role. Use jumpers or a selector switch to set this.)
    - hardware_id (str): string uniquely identifying the board
    - version (str): version of the code / board
  */
  start("DIS", 'A', token);
  // Append role and instance index
  append(role_str);
  append(instance_idx);

  if (hardware_id != nullptr) {
    append(hardware_id);
  } else {
    #ifdef TEENSY36
      // Append UUID from Teensy CPU (freescale MK66 CPU), defined for
      // teensy3 in kinetis.h and converted to hex
      separator();
      write('"');
      write_hex(SIM_UIDH);
      write_hex(SIM_UIDMH);
      write_hex(SIM_UIDML);
      write_hex(SIM_UIDL);
      write('"');
    #else
      // TODO(Isaac): find unique IDs for other CPUs
      // Use random string defined in Makefile
      append(_EXPAND_AND_QUOTE(OATMEAL_HARDWARE_ID_STR));
    #endif
  }

  if (version_str != nullptr) {
    append(version_str);
  } else {
    append(_EXPAND_AND_QUOTE(OATMEAL_VERSION_STR));
  }
  finish();
}
