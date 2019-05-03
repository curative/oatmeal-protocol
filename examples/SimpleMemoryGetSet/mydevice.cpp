/*
  mydevice.cpp
  Copyright 2019, Shield Diagnostics and the Oatmeal Protocol contributors
  License: Apache 2.0
*/

// Core Libraries
#include <Arduino.h>

#include "oatmeal_protocol.h"

// Create an OatmealPort on Serial1 and identify yourself as 'MyDevice'
// Replace Serial1 with Serial for Arduino boards with a single serial port
OatmealPort port = OatmealPort(&Serial1, "MyDevice");

unsigned long prev_loop_start, max_loop_ms = 0;

// In this example we store three ints on the board: x, y & z
int32_t x, y, z;


void send_heartbeat() {
  OatmealMsg hb_msg;

  hb_msg.start("HRT", 'B', port.next_token());
  port.build_status_heartbeat(&hb_msg, max_loop_ms);
  hb_msg.append_dict_key_value("a", 5.1);
  hb_msg.append_dict_key_value("b", "hi");
  hb_msg.finish();
  port.send(hb_msg);

  // Zero the max loop time now that it has been reported by
  // build_status_heartbeat()
  max_loop_ms = 0;
}

void check_UART() {
  OatmealMsg msg;
  OatmealArgParser parser;

  char var_name[10];
  int32_t int32_val = 0;

  // Read any new messages
  while (port.check_for_msgs(&msg)) {
    if (msg.is_opcode("HALR")) {
      /* HALt Request; args: None */
      port.send_ack(msg);
    }
    /* Custom opcodes */
    else if (parser.start(msg, "SETR") &&
             parser.parse_str(var_name, sizeof(var_name)) &&
             parser.parse_arg(&int32_val) &&
             parser.finished()) {
      /* Set a variable value. Args: <var_name:str>;<value:int32_t> */
      bool known_var_name = true;
      if (strcmp(var_name, "x") == 0) { x = int32_val; }
      else if (strcmp(var_name, "y") == 0) { y = int32_val; }
      else if (strcmp(var_name, "z") == 0) { z = int32_val; }
      port.send_response(msg, known_var_name ? 'A' : 'F');
    }
    else if (parser.start(msg, "GETR") &&
             parser.parse_str(var_name, sizeof(var_name)) &&
             parser.finished()) {
      /* get a variable value. Args: <var_name:str> */
      bool known_var_name = true;
      if (strcmp(var_name, "x") == 0) { int32_val = x; }
      else if (strcmp(var_name, "y") == 0) { int32_val = y; }
      else if (strcmp(var_name, "z") == 0) { int32_val = z; }
      if (known_var_name) {
        /* Respond with an ack with the same token */
        port.start("GET", 'A', msg.token());
        port.append(int32_val);
        port.finish();
      } else {
        port.send_failed(msg); /* unknown variable name */
      }
    } else if (msg.is_opcode("FETR")) {
      // Respond with all values
      port.start("FET", 'A', msg.token());
      port.append(x);
      port.append(y);
      port.append(z);
      port.finish();
    } else {
      /* Unknown command */
      port.stats.n_unknown_opcode++;
    }
  }
}

void setup() {
  // Must initialize the port to set up UART
  port.init();

  // Set up heartbeat timer to send a heartbeat every 500 milliseconds
  port.set_heartbeats_period(500);
  port.set_heartbeats_on(true);

  prev_loop_start = millis();
}

void loop() {
  // Track how long it takes to complete the main loop
  unsigned long now_ms = millis(), loop_ms = now_ms-prev_loop_start;
  max_loop_ms = loop_ms > max_loop_ms ? loop_ms : max_loop_ms;

  check_UART();

  if (port.send_heartbeat_now(now_ms)) {
    send_heartbeat();
  }

  prev_loop_start = now_ms;
}
