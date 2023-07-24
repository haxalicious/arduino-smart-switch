#include <EEPROM.h>
#include <Ethernet.h>
#include <SPI.h>
#include <Wire.h>
#define REQ_BUF_SZ   60
int addr; // EEPROM address of values
byte wcount = 0; // Write cycle count
byte states;
byte resetstates = 0; // Pending reset states
byte mac[] = {0xA8, 0x61, 0x0A, 0xAE, 0x96, 0xC0}; // MAC address
byte ip[] = {10, 42, 0, 2}; // IP addr
byte gateway[] = {10, 42, 0, 1}; // Internet access via router
byte subnet[] = {255, 255, 255, 0};
EthernetServer server(80); // HTTP server vars
char linebuf[80];
int charcount=0;

void applyStates() {
  states ^= resetstates; // Apply pending resets
  Wire.beginTransmission(0x3f);
  Wire.write(states);
  Wire.endTransmission();
  if (resetstates != 0) { // If any relays were reset, wait 5s and turn them back on
    delay(5000);
    states ^= resetstates;
    Wire.beginTransmission(0x3f);
    Wire.write(states);
    Wire.endTransmission();
    resetstates = 0; 
  }
  Serial.println("Applied states");
}

void writeStates() {
  if (states != EEPROM.read(addr)) { // Don't waste write cycles
    EEPROM.write(addr, states);
    wcount++;
    if (wcount == 50) { // Wear leveling
      if (addr == EEPROM.length() - 1) {
        EEPROM.update(2, states);
        addr = 2;
      }
      else {
        EEPROM.update(addr + 1, states); // Avoid race condition
        addr++;
      }
      EEPROM.put(0, addr); // Distribute write cycles
    }
    Serial.print("Wrote states to addr ");
    Serial.println(addr);
    Serial.print("Write cycle count: ");
    Serial.println(wcount);
  }
}

void setup() {
  EEPROM.get(0, addr);
  EEPROM.get(addr, states); // Load states from EEPROM
  Wire.begin();
  Wire.beginTransmission(0x3f);
  Wire.write(states);
  Wire.endTransmission();
  Serial.begin(115200);
  Serial.print("Loaded settings from EEPROM addr ");
  Serial.println(addr);
  Ethernet.begin(mac, ip, gateway, gateway, subnet);
  server.begin(); // Start Ethernet server
  Serial.print("Server address: ");
  Serial.println(Ethernet.localIP());
}

void dashboardPage(EthernetClient &client) { // HTML header
  client.println("<!DOCTYPE HTML><html><head>");
  client.println("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"></head><body>");                                                             
  client.println("<h3>Relay Control | <a href=\"/at\">Test Changes</a> | <a href=\"/aa\">Apply Changes</a> | <a href=\"/ar\">Revert Changes</a></h3>");
  byte curstates = EEPROM.read(addr); // Saved states
  for (byte i = 0; i < 8; i++) { // Generate HTML for each relay
    byte state = bitRead(states, 7 - i);
    client.print("<h4>Relay ");
    client.print(i + 1);
    client.print(" - State: ");
    if (bitRead(resetstates, 7 - i) == 1) { // Pending reset state
      client.println("Pending Reset</h4>");
      client.print("<a href=\"/rr");
      client.print(i);
      client.println("\"><button>CANCEL</button></a>");
    }
    else {
      if (state != bitRead(curstates, 7 - i)) { // Check if state is modified
        client.print("Pending ");
      }
      if (state == 1) { // Relay off
        client.println("Off</h4>");
        client.print("<a href=\"/rh");
      }
      else { // Relay on
        client.println("On</h4>");
        client.print("<a href=\"/rl");
      }
      client.print(i);
      client.print("\"><button>POWER</button></a>");
      if (state == 0) { // Only show reset button if relay is powered
        client.print("<a href=\"/rr");
        client.print(i);
        client.println("\"><button>RESET</button></a>");
      }
    }
  }
  client.println("</body></html>");
}

void loop() {
  // Listen for clients
  EthernetClient client = server.available();
  if (client) {
    Serial.println("Client connected");
    memset(linebuf, 0, sizeof(linebuf));
    charcount = 0;
    // An http request ends with a blank line
    boolean currentLineIsBlank = true;
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        // Read char by char HTTP request
        linebuf[charcount] = c;
        if (charcount<sizeof(linebuf)-1) charcount++;
        /* If you've gotten to the end of the line (received a newline
        character) and the line is blank, the http request has ended,
        so you can send a reply */
        if (c == '\n' && currentLineIsBlank) {
          Serial.println("Sent main page");
          dashboardPage(client);
          break;
        }
        if (c == '\n') {
          //Serial.println(linebuf);
          char *ptr = strstr(linebuf, "GET /r"); // Relay operation
          if (ptr != NULL) {
            ptr += 6;
            switch (*ptr) {
              case 'h': // Set relay to HIGH state
                ptr++;
                bitClear(states, 7 - atoi(ptr));
                Serial.print("Powered on relay ");
                Serial.println(atoi(ptr) + 1);
                break;
              case 'l': // Set relay to LOW state
                ptr++;
                bitSet(states, 7 - atoi(ptr));
                Serial.print("Powered off relay ");
                Serial.println(atoi(ptr) + 1);
                break;
              case 'r': // Reset relay
                ptr++;
                resetstates ^= (1 << 7 - atoi(ptr));
                Serial.print("Reset relay ");
                Serial.println(atoi(ptr));
                break;
            }
          }
          else {
            ptr = strstr(linebuf,"GET /a"); // Apply operation
            if (ptr != NULL) {
              ptr += 6;
              switch (*ptr) { // Test config
                case 't':
                  applyStates();
                  break;
                case 'a': // Apply config
                  writeStates(); // Avoid race condition
                  applyStates();
                  break;
                case 'r': // Revert config
                  resetstates = 0;
                  EEPROM.get(addr, states);
                  break;
              }
            }
          }
          // You're starting a new line
          currentLineIsBlank = true;
          memset(linebuf, 0, sizeof(linebuf));
          charcount = 0;
        } 
        else if (c != '\r') {
          // You've gotten a character on the current line
          currentLineIsBlank = false;
        }
      }
    }
    // give the web browser time to receive the data
    delay(50);
    // close the connection:
    client.stop();
    Serial.println("Client disconnected");
  }
}
