//
//  COMPONENT:  UDPBasics
//  VERSION:    1.0
//  DATE:       2016-12-29
//  AUTHOR:     Gary Ebersole
//  DESCRIPTION:
//
//    This app implements basic UDP messaging functionality. It attempts to
//    handle three separate but related, sequential tasks.
//
//      -- CONNECT: Connects to the Wi-Fi access point (AP)
//      -- WAKEUP: Wakes up the component server running
//         on the Pi/AP. The response time of the network stack on the Pi
//         is variable and the first packet is often lost. We use the 'wakeup'
//         approach to ensure the Pi is properly handling UDP packets before
//         sending messages.
//      -- SEND: SendS the event message as a UDP packet
//
//    In each of these tasks, we iterate a defined number of times with
//    relatively short delays to complete the operation. When waking
//    up the component server and sending the event message, we wait for an ACK
//    from the server before proceeding. This permits us to use low-latency UDP
//    in a transactional manner to send messages somewhat more reliably than
//    fire-and-forget.
//
//    The configuration of this ESP client app and server listening app is optimized
//    for low latency balanced with basic reliability. The in-line comments
//    explain these optimizations.
//
//    NB: While this should work with most routers, this example has only been
//    tested with the service 'hostapd' running on a Raspberry Pi 3 using the
//    on-board Wi-Fi module configured on 'wlan0'. See the following article for
//    details on setting up this configuration.
//
//    https://gist.github.com/garyebersole/84b70af5ea7b9b46fcc2dc0bb21cdedc
//
//  REFERENCES:
//    https://github.com/esp8266/Arduino/blob/master/doc/esp8266wifi/station-class.md#config
//    The ESP8266 version of the Arduino Wifi lib which must be used.
//
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in all
//  copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//  SOFTWARE.
//
//  Copyright 2017 (c) Third Act Partners LLC
//
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <stdio.h>
#include <stdlib.h>
#include <ArduinoJson.h>
extern "C" {
  #include "user_interface.h"
}

const int POWER_SWITCH = 14;  // pin used to latch MOSFET power gate
const int LED = 13;  // pin used to latch LED MOSFET gate

// Raspberry Pi 3 router configuration
const char* SSID = "BrownBox-AP";
const char* PASSPHRASE = "bb_password";
IPAddress access_point_ip(172, 24, 1, 1);
IPAddress access_point_gateway(172, 24, 1, 1);
IPAddress access_point_subnet_mask(255,255,255,0);
const uint8_t ACCESS_POINT_MAC_ADDRESS[] = {0x74, 0xda, 0x38, 0x26, 0x07, 0xf5};
const int ACCESS_POINT_CHANNEL = 6;

// Component server IP and port (typically same as AP)
IPAddress component_server_ip(172, 24, 1, 1);  // Static IP for ESP client
const unsigned int COMPONENT_SERVER_PORT = 41234;

// Component ESP client IP address and port
IPAddress component_client_ip(172, 24, 1, 20);
const unsigned int COMPONENT_CLIENT_PORT = 41235;

// UDP Setup
WiFiUDP Udp;
char outgoing_udp_packet[100];
char incoming_udp_packet[255];
char packet_buffer[100];
int packet_size;

// CONTRAINTS
//  TODO: These values should be tested further for optimal values.
//  TODO: Possibly read these from config file in SPIFFS
//  TODO: Or move them to functions where they are used since they are not globals
// Wifi connection
int wifi_connection_attempts = 0;
const int WIFI_CONNECTION_MAX_WAIT_TRIES = 10;  // should connect in 200-250ms max
const int WIFI_CONNECTION_WAIT_DELAY_MS = 50;  // Seems to work better than 25ms or 100ms
// Component server ready
int component_server_ready_attempts = 0;
const int MAX_COMPONENT_SERVER_READY_ATTEMPTS = 5;
const int COMPONENT_SERVER_READY_RETRY_DELAY_MS = 10;
// Message sent
int message_sent_attempts = 0;
const int MAX_MESSAGE_SENT_ATTEMPTS = 5;
const int MESSAGE_SENT_RETRY_DELAY_MS = 10;
// Reply received
int reply_received_attempts = 0;
const int MAX_REPLY_RECEIVED_ATTEMPTS = 5;
const int REPLY_RECEIVED_RETRY_DELAY_MS = 20;

//  FUNCTION PROTOTYPES
void ledAlert(int led_on_ms, int led_off_ms, int led_flash_iterations); // Connect to the WiFi Access point
bool wifiConnected();
bool messageSent(char message_packet[100]);
bool replyReceived();
bool componentServerReady();
bool eventMessageSent();

//  SETUP
void setup() {
  //pinMode(POWER_SWITCH, OUTPUT);
  //digitalWrite(POWER_SWITCH, LOW);  // Latch the gate to stay on while sending message
  //pinMode(LED, OUTPUT);
  //digitalWrite(LED, HIGH);  // turn off the LED
  Serial.begin(115200);
  Serial.printf("\n=========================\n");
  Serial.printf("Awake at %lims\n", millis());
  if (wifiConnected()) {
    // Uncomment these lines if you are having problems with the connection
    //Serial.printf("\nConnection status:\n");
    //WiFi.printDiag(Serial);
    Serial.printf("\nConnected to AP at %lims\n", millis());
    if (Udp.begin(COMPONENT_CLIENT_PORT) == 1) {  // Initialize UDP on local port
      Serial.printf("Listening at IP %s:%d\n", WiFi.localIP().toString().c_str(), COMPONENT_CLIENT_PORT);
      if (componentServerReady()) {
        Serial.printf("Component server is ready...\n");
        if (eventMessageSent()) {
          Serial.printf("Event message sent...\n");
          //ledAlert(250, 250, 10);  // Integrate the alert library and use 'success' level alert
        } else {
          //ledAlert(250, 250, 10);  // Integrate the alert library and use 'hard_fail' level alert
        }
      } else {
        //ledAlert(250, 250, 10);  // Integrate the alert library and use 'hard_fail' level alert
      }
    } else {
      Serial.printf("Failed to initialize UDP...\n");
      //ledAlert(250, 250, 10);  // Integrate the alert library and use 'hard_fail' level alert
    }
  }
  //  Uncomment when using with MOSFET power-on/off circuit
  //digitalWrite(LED, HIGH);  // Open the gate to turn off the LED
  //digitalWrite(POWER_SWITCH, HIGH);  // Open the gate to power down the unit
  // DONE...
}

//  LOOP
void loop() {}

//  FUNCTION IMPLEMENTATIONS
//  LED alert...used if a test LED is in the circuit
//  This is a placeholder for the alert module which will use red and
//  green LEDs (probably a dual red-green SMD 1206 module). Needs more design but
//  essential to provide user feedback on success or failure of event. Power cost
//  needs to be computed/
void ledAlert(int led_on_ms, int led_off_ms, int led_flash_iterations) {
  for (int i = 1; i <= led_flash_iterations; i++) {
    digitalWrite(LED, HIGH);
    delay(led_on_ms);
    digitalWrite(LED, LOW);
    delay(led_off_ms);
  }
}
// Connect to the WiFi Access point
bool wifiConnected() {
  // Configure the client to reduce connection time
  //    -- Use mode 'g' on router since ESP has known problems with 'n'
  wifi_set_phy_mode(PHY_MODE_11G);
  //    -- Set static IP address
  WiFi.config(component_client_ip, access_point_gateway, access_point_subnet_mask);
  //    -- Tell the client to auto-connect/reconnect on boot up
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);
  //    -- Set the ESP to STA mode
  WiFi.mode(WIFI_STA);
  // If WiFi values have already been persisted in NVM, skip rewriting to
  // minimize NVM cell wear from excessive writing
  // TODO: Need to check all persisted values for change since last write
  if (WiFi.SSID() != SSID) {
    WiFi.persistent(false);
  } else {
    WiFi.persistent(true);
    Serial.println("Using saved WiFi settings");
  }
  // Pass MAC address and channel to minimize scanning and negotiation with router
  WiFi.begin(SSID, PASSPHRASE, ACCESS_POINT_CHANNEL, ACCESS_POINT_MAC_ADDRESS);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    if (wifi_connection_attempts++ < WIFI_CONNECTION_MAX_WAIT_TRIES) {
      delay(WIFI_CONNECTION_WAIT_DELAY_MS);
      Serial.print(".");
    } else {
      Serial.printf("\nFailed to connect after %i of %i max attempts\n", wifi_connection_attempts, WIFI_CONNECTION_MAX_WAIT_TRIES);
    }
  }
  //  This doesn't seem right but to avoid a power_off hack, I need to test
  //  WiFi.status() again so I can power_off cleanly without redundant code
  //  or other hacks
  if (WiFi.status() == WL_CONNECTED) {
    return true;  // Connected
  } else {
    return false;  // Failed to connect
  }
}
// Send JSON-formatted message packet to the component server
bool messageSent(char message_packet[100]) {
  for (int i = 1; i <= MAX_MESSAGE_SENT_ATTEMPTS; i++) {
    delay(MESSAGE_SENT_RETRY_DELAY_MS);  // Wait for a bit
    if (Udp.beginPacket(component_server_ip, COMPONENT_SERVER_PORT) == 1) {
      if (Udp.write(message_packet) > 0) {
        if (Udp.endPacket() == 1) {
          yield();  // ESP Wi-Fi time
          Serial.printf("Sent packet of %u bytes at %lims\n", (unsigned)strlen(message_packet), millis());
          return true;
        } else {
          Serial.printf("Failed to send message...\n"); // Failed to send packet to remote
        }
      } else {
        Serial.printf("Empty message packet...\n"); // Zero-length packet
      }
    } else {
      Serial.printf("Failed to initialize message packet...\n");  // Failed to initialize packet
    }
    return false;
  }
}
//  Wait for reply from component server
bool replyReceived() {
  reply_received_attempts = 0;
  packet_size = 0;
  while (!packet_size) {
    packet_size = Udp.parsePacket();
    if (++reply_received_attempts > MAX_REPLY_RECEIVED_ATTEMPTS) {
      Serial.printf("Failed to receive ACK from server after %i attempts\n", MAX_REPLY_RECEIVED_ATTEMPTS);
      return false;
    }
  }
  // Process incoming UDP packet
  Serial.printf("Received %d bytes from %s, port %d at %lims\n", packet_size, Udp.remoteIP().toString().c_str(), Udp.remotePort(), millis());
  int len = Udp.read(incoming_udp_packet, 255);
  if (len > 0) {
    incoming_udp_packet[len] = 0;
  }
  Serial.printf("Reply from server: %s\n", incoming_udp_packet);
  return true;  // TODO return JSON message
}
// Check if the component server is ready before sending event message. The first
// packet or two is usually dropped.
bool componentServerReady() {
  //  Create a JSON "server-ready" message
  StaticJsonBuffer<200> json_buffer;
  JsonObject& data = json_buffer.createObject();  //  Create a JSON buffer object
  for (int i = 1; i <= MAX_COMPONENT_SERVER_READY_ATTEMPTS; i++) {
    sprintf(packet_buffer, "Component server-ready check at %lims", millis());
    Serial.printf("Component server-ready check sent at %lims\n", millis());
    data["body"] = packet_buffer;  //
    data.printTo(outgoing_udp_packet, 100);  // Convert the JSON object to a char array
    if (messageSent(outgoing_udp_packet)) {
      if (replyReceived()) {
        return true;  // Server is ready
      } else {
        Serial.printf("Component server is not ready...\n");
      }
    } else {
      Serial.printf("Component server-ready message not sent...\n");
    }
    delay(COMPONENT_SERVER_READY_RETRY_DELAY_MS);  // Wait for a few ms
  }
  return false;  // Server not ready
}
// Send event message
bool eventMessageSent() {
  //  Create JSON event message
  StaticJsonBuffer<200> json_buffer;
  JsonObject& data = json_buffer.createObject();  //  Create a JSON buffer object
  sprintf(packet_buffer, "Elapsed time: %lims", millis());
  Serial.printf("Elapsed time: %lims\n", millis());
  data["body"] = packet_buffer;  //
  data.printTo(outgoing_udp_packet, 100);  // Convert the JSON object to a char array
  if (messageSent(outgoing_udp_packet)) {
    Serial.printf("Event message sent at %lims\n", millis());
    if (replyReceived()) {
      return true;  // Event message sent
    } else {
      Serial.printf("Failed to receive event message ACK...\n");
    }
  } else {
    Serial.printf("Failed to send event message...\n");
  }
  return false;  // Event message not sent
}
