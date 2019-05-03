/*
  mydevice.cpp
  Copyright 2019, Shield Diagnostics and the Oatmeal Protocol contributors
  License: Apache 2.0
*/

// Core Libraries
#include <Arduino.h>

#include "oatmeal_protocol.h"

// Pin assignment
#define PIN_LED   13
#define PIN_RELAY 1
#define PIN_ADC0  A0
#define PIN_ADC1  A1

#define BAUD_RATE 57600

// Create an OatmealPort on Serial and identify yourself as "MyDevice"
OatmealPort port = OatmealPort(&Serial, "MyDevice");


void configure_pins() {
  // LED and Relay are outputs
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_RELAY, OUTPUT);
  // ADC pins are inputs
  pinMode(PIN_ADC0, INPUT);
  pinMode(PIN_ADC1, INPUT);
}

void setup() {
  configure_pins();  // Set pins directions (INPUT or OUTPUT)
  port.init(BAUD_RATE);  // Must initialize the port to set up UART
}

void loop() {
  OatmealMsg msg;
  OatmealArgParser parser;

  bool led_state;

  // Read any new messages
  while (port.check_for_msgs(&msg)) {
    if (msg.is_opcode("HALR")) {
      /* HALt Request; args: None */
      analogWrite(PIN_LED, 0);        // Set LED PWM to 0
      digitalWrite(PIN_RELAY, LOW);   // Turn off relay
      port.send_ack(msg);             // Ack message
    }
    /* Custom opcodes */
    else if (msg.is_opcode("LEDR")) {
      /* LED Request: Set the LED state (ON or OFF).
         Args: <led_state:bool> */
      if (parser.init(msg) &&
          parser.parse_arg(&led_state) &&
          parser.finished()) {
        digitalWrite(PIN_LED, led_state);     // Set LED state
        port.send_ack(msg);                   // Ack message
      }
    } else if (msg.is_opcode("ADCR")) {
      /* ADC Request: Read the ADC pins.
         Args: None
         Returns: <ADC0_value: uint16_t, ADC1_value: uint16_t> */
      port.start("ADC", 'A', msg.token());  // Start an ack message
      port.append(analogRead(PIN_ADC0));    // Append 10-bit ADC0 value to ack message
      port.separator();                     // Add a ','
      port.append(analogRead(PIN_ADC1));    // Append 10-bit ADC1 value to ack message
      port.finish();                        // Complete the message
    } else {
      /* Unknown command */
      port.stats.n_unknown_opcode++;
    }
  }
}
