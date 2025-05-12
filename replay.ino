#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include <RF24.h>
#include <vector> // Include vector for dynamic array
// #include <LittleFS.h> // Removed include for LittleFS
// #include <ArduinoJson.h> // Removed include for ArduinoJson (as used for file ops)

// Define AP credentials
// This is the network the ESP32 will CREATE
const char* ap_ssid = "nothing"; // The name of the network the ESP32 will broadcast
const char* ap_password = "buymeacoffee"; // The password for the ESP32's network

// Define CE and CSN pins based on the provided pinout
#define CE_PIN  22 // Chip Enable pin connected to ESP32 GPIO22
#define CSN_PIN 21 // Chip Select Not pin connected to ESP32 GPIO21 (VSPI CS)

// Create an RF24 object
RF24 radio(CE_PIN, CSN_PIN);

// Address for communication (needs to match the target device's transmitting address)
// You might need to determine this address by sniffing traffic or from documentation.
// Note: While scanning, the radio listens on a single address across multiple channels.
// The target device must be transmitting on this address.
const byte addresses[][6] = {"1Node"}; // Example address, change as needed

// --- Packet Storage ---
struct CapturedPacket {
    uint8_t data[32]; // nRF24L01+ has a maximum payload size of 32 bytes
    uint8_t size;
    uint8_t channel; // Store the channel the packet was captured on
    unsigned long captureTime; // Optional: store capture time
};

std::vector<CapturedPacket> capturedPackets; // Use a vector to store packets
const size_t MAX_PACKETS = 10; // Maximum number of packets to store
// const char* PACKETS_FILE_PATH = "/packets.json"; // Removed file path

// State variables
volatile bool isCapturing = false;
volatile bool replayTriggered = false;
volatile int packetIndexToReplay = -1; // -1 indicates no packet selected for replay

// --- Scanning Variables ---
uint8_t currentChannel = 0; // Start scanning from channel 0
unsigned long channelSwitchTime = 0;
const unsigned long CHANNEL_DWELL_TIME_MS = 10; // DWELL TIME for quicker scan
const unsigned long EXTENDED_DWELL_TIME_MS = 200; // Longer dwell time after capture
bool packetCapturedRecently = false; // Flag for adaptive dwell


// Web Server
AsyncWebServer server(80);

// Function to convert byte array to hex string for web display
String bytesToHex(uint8_t* buffer, uint8_t bufferSize) {
  String hexString = "";
  for (int i = 0; i < bufferSize; i++) {
    char hexChar[3];
    sprintf(hexChar, "%02X", buffer[i]);
    hexString += hexChar;
    if (i < bufferSize - 1) {
      hexString += " ";
    }
  }
  return hexString;
}

// --- Persistence Functions (Removed) ---
// savePackets() and loadPackets() functions are removed.


// --- Embedded Web Content (HTML and CSS only) ---
// HTML (Dynamically generated, this is a template structure)
// The actual HTML content will be built as a String in the / handler
const char html_template_start[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Replay Module</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" href="/style.css">
</head>
<body>
  <div class="container">

    <div class="status-section">
      <h2>Status</h2>
      <p id="status">
)rawliteral";

const char html_template_status_end_controls_start[] PROGMEM = R"rawliteral(
      </p>
    </div>

    <div class="controls-section">
      <form action="/start_capture" method="post" style="display:inline;">
        <button type="submit">START CAPTURE</button>
      </form>
      <form action="/stop_capture" method="post" style="display:inline;">
        <button type="submit">STOP CAPTURE</button>
      </form>
      </div>

    <div class="packet-list-section">
      <h2>Captured Packets</h2>
      <div id="packetList">
)rawliteral";

const char html_template_packet_item_start[] PROGMEM = R"rawliteral(
        <div class="packet-item">
          <div class="packet-info">
            <strong>Packet %u:</strong> <span class="packet-size">Size: %u bytes</span> <span class="packet-channel">Channel: %u</span><br>
            <span class="packet-data-hex">%s</span>
          </div>
          <form action="/replay_packet" method="post" style="display:inline;">
            <input type="hidden" name="index" value="%u">
            <button type="submit">REPLAY</button>
          </form>
          <form action="/delete_packet" method="post" style="display:inline;">
            <input type="hidden" name="index" value="%u">
            <button type="submit" class="delete-button" onclick="return confirm('Are you sure you want to delete this packet?');">DELETE</button>
          </form>
        </div>
)rawliteral";

const char html_template_no_packets[] PROGMEM = R"rawliteral(
        <p>No packets captured yet.</p>
)rawliteral";


const char html_template_end[] PROGMEM = R"rawliteral(
      </div>
    </div>

  </div>

</body>
</html>
)rawliteral";


// CSS (Adjusted styling for "chill expensive" - same as previous version)
const char style_css[] PROGMEM = R"rawliteral(
body {
  font-family: 'Arial', sans-serif;
  background: radial-gradient(ellipse at bottom, #400000 0%, #1a0000 50%, #000000 100%); /* Red/Black gradient */
  color: #c0c0c0; /* Muted text color */
  margin: 0;
  padding: 20px;
  display: flex;
  justify-content: center;
  align-items: center;
  min-height: 100vh;
}

.container {
  background-color: rgba(30, 30, 30, 0.9); /* Slightly lighter transparent dark background */
  padding: 30px;
  border-radius: 15px;
  box-shadow: 0 8px 20px rgba(0, 0, 0, 0.7); /* More pronounced shadow */
  text-align: center;
  max-width: 600px; /* Increased max-width for packet list */
  width: 95%; /* Responsive width */
  border: 1px solid #505050; /* More visible subtle border */
}

h1 { /* Removed h1 from HTML, so this style is not used but kept for completeness */
  color: #00ccff; /* Accent color */
  margin-bottom: 20px;
  font-size: 1.8em;
  text-shadow: 0 0 5px rgba(0, 204, 255, 0.5);
}

h2 {
  color: #a0a0a0; /* Muted heading color */
  margin-top: 15px;
  margin-bottom: 10px;
  font-size: 1.2em;
}

.status-section, .controls-section, .packet-list-section {
  margin-bottom: 20px;
  padding: 15px;
  background-color: rgba(50, 50, 50, 0.7); /* Slightly lighter background for sections */
  border-radius: 8px;
  border: 1px solid #606060; /* More visible border for sections */
}

#packetList {
  text-align: left;
  max-height: 250px; /* Slightly increased height */
  overflow-y: auto;
  padding-right: 15px; /* Space for scrollbar */
}

.packet-item {
    background-color: rgba(20, 20, 20, 0.9); /* Darker background for items */
    border: 1px solid #707070; /* More visible border for items */
    border-radius: 5px;
    padding: 10px;
    margin-bottom: 10px;
    word-break: break-all;
    font-family: 'Courier New', Courier, monospace;
    font-size: 0.9em;
    display: flex; /* Use flexbox for layout */
    justify-content: space-between; /* Space between packet info and button */
    align-items: center; /* Vertically align items */
}

.packet-info {
    flex-grow: 1; /* Allow packet info to take available space */
    margin-right: 10px; /* Space between info and button */
}

.packet-data-hex {
    display: block; /* Ensure hex data is on its own line */
    margin-top: 5px;
    color: #00e000; /* Slightly muted green for data */
}

.packet-size {
    font-size: 0.8em;
    color: #808080; /* More muted size color */
}

.packet-channel { /* Style for channel indicator */
    font-size: 0.8em;
    color: #808080;
    margin-left: 10px;
}


button {
  background-color: #606060; /* Muted button color */
  color: #e0e0e0;
  border: none;
  padding: 10px 20px; /* Slightly larger padding for main buttons */
  border-radius: 5px;
  cursor: pointer;
  font-size: 1em; /* Slightly smaller font for main buttons */
  transition: background-color 0.3s ease, transform 0.1s ease;
  box-shadow: 0 2px 5px rgba(0, 0, 0, 0.3);
  margin: 5px; /* Consistent spacing */
}

button:hover {
  background-color: #707070; /* Slightly lighter on hover */
  transform: translateY(-1px); /* Subtle lift effect */
}

button:active {
  transform: translateY(0);
  box-shadow: 0 1px 3px rgba(0, 0, 0, 0.3);
}

#startCaptureButton {
    background-color: #408040; /* Muted green */
    color: white;
}
#startCaptureButton:hover {
    background-color: #509050;
}

#stopCaptureButton {
    background-color: #a04040; /* Muted red */
    color: white;
}
#stopCaptureButton:hover {
    background-color: #b05050;
}

.delete-button, .delete-all-button {
    background-color: #c03030; /* Muted delete red */
    color: white;
}
.delete-button:hover, .delete-all-button:hover {
     background-color: #d04040;
}


/* Style for the replay button in the list */
.packet-item button {
    padding: 5px 10px; /* Smaller padding for inline buttons */
    font-size: 0.8em; /* Smaller font size */
    background-color: #806020; /* Muted gold/yellow */
    color: #e0e0e0;
}
.packet-item button:hover {
     background-color: #907030;
}


.footer { /* Removed footer from HTML, so this style is not used */
  margin-top: 30px;
  font-size: 0.8em;
  color: #666;
}

/* Mobile specific adjustments */
@media (max-width: 600px) {\n  body {\n    padding: 10px;\n  }\n\n  .container {\n    padding: 20px;\n    width: 100%; /* Use full width on smaller screens */\n  }\n\n  h2 {\n    font-size: 1.1em;\n  }\n\n  button {\n    padding: 10px 20px;\n    font-size: 1em;\n  }
  .packet-item {
      flex-direction: column; /* Stack items vertically on small screens */
      align-items: flex-start; /* Align items to the start */
  }
  .packet-info {
      margin-right: 0;
      margin-bottom: 5px; /* Space below info */
  }
}
)rawliteral";


void setup() {
  Serial.begin(115200);
  while (!Serial); // Wait for serial port to connect

  Serial.println("ESP32 nRF24L01+ Replay Module (Access Point Mode - No JS - Multi-Channel Scan - No Persistence)"); // Updated print
  Serial.println("Using provided VSPI Pinout:");
  Serial.println("CE Pin: 22");
  Serial.println("CSN Pin: 21");
  Serial.println("SCK Pin: 18");
  Serial.println("MISO Pin: 19");
  Serial.println("MOSI Pin: 23");
  Serial.printf("Max packets to store in RAM: %u\n", MAX_PACKETS); // Updated print


  // --- Filesystem Initialization (Removed) ---
  // LittleFS.begin() is removed
  // Serial.println("LittleFS initialized."); // Removed print

  // --- Load captured packets from flash memory (Removed) ---
  // loadPackets() call is removed

  // --- Configure ESP32 as a WiFi Access Point (AP) ---
  Serial.printf("Starting WiFi Access Point '%s' with password '%s'\n", ap_ssid, ap_password);
  WiFi.softAP(ap_ssid, ap_password);

  IPAddress apIP = WiFi.softAPIP();
  Serial.print("Access Point IP address: ");
  Serial.println(apIP);
  // --- WiFi AP Setup Complete ---

  // Initialize the radio
  if (!radio.begin()) {
    Serial.println("Radio hardware not responding!");
    // Consider a way to indicate this on the web page
    return; // Exit setup if radio fails
  }

  // Set data rate and PA level (These apply across all scanned channels)
  radio.setDataRate(RF24_250KBPS); // Match the target device's data rate
  radio.setPALevel(RF24_PA_LOW); // Adjust as needed

  // Open a reading pipe on the defined address (This address will be used on each scanned channel)
  radio.openReadingPipe(0, addresses[0]);


  // Radio is initialized but not listening yet, will start when Capture is enabled

  Serial.println("Radio initialized. Access Point active.");
  Serial.println("Connect to the network above to access the web interface.");


  // --- Web Server Handlers (No JS) ---

  // Handle for root URL (/) - Serves the main HTML page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = ""; // Build the HTML string
    html += html_template_start;

    // Add status text
    if (!radio.isChipConnected()) {
        html += "Radio Error";
    } else if (isCapturing) {
        char status_buffer[64]; // Buffer for status text with channel
        snprintf(status_buffer, sizeof(status_buffer), "Capturing (Scanning Channel %u)...", currentChannel);
        html += status_buffer;
    } else {
        html += "Capture Stopped"; // Changed indicator text here
    }

    html += html_template_status_end_controls_start;

    // Add captured packets list
    if (capturedPackets.empty()) {
        html += html_template_no_packets;
    } else {
        char packet_item_buffer[512]; // Buffer to build each packet item HTML
        for (size_t i = 0; i < capturedPackets.size(); i++) {
            String hexData = bytesToHex(capturedPackets[i].data, capturedPackets[i].size);
             // Use snprintf to safely format the string, including channel and indices for buttons
            snprintf(packet_item_buffer, sizeof(packet_item_buffer),
                     html_template_packet_item_start,
                     i, capturedPackets[i].size, capturedPackets[i].channel, hexData.c_str(), i, i); // Added i twice for replay and delete forms
            html += packet_item_buffer;
        }
    }


    html += html_template_end;

    request->send(200, "text/html", html); // Send the generated HTML
  });

  // Handle for CSS file
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/css", style_css); // Serve the embedded CSS
  });

  // Handle for Start Capture action (POST request from form)
  server.on("/start_capture", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!isCapturing) {
      isCapturing = true;
      currentChannel = 0; // Start scanning from channel 0
      radio.stopListening(); // Ensure not listening before starting
      radio.openReadingPipe(0, addresses[0]); // Open the reading pipe
      radio.setChannel(currentChannel); // Set initial channel
      radio.startListening(); // Start listening for capture
      channelSwitchTime = millis(); // Reset channel switch timer
      Serial.println("Capture started (via web).");
    } else {
      Serial.println("Capture is already in progress (via web).");
    }
    request->redirect("/"); // Redirect back to the main page to show updated status/list
  });

  // Handle for Stop Capture action (POST request from form)
  server.on("/stop_capture", HTTP_POST, [](AsyncWebServerRequest *request){
    if (isCapturing) {
      isCapturing = false;
      radio.stopListening(); // Stop listening
      Serial.println("Capture stopped (via web).");
    } else {
      Serial.println("Capture is not in progress (via web).");
    }
    request->redirect("/"); // Redirect back to the main page
  });

  // Handle for Replay Packet action (POST request from form)
  server.on("/replay_packet", HTTP_POST, [](AsyncWebServerRequest *request){
    if (isCapturing) {
      Serial.println("Cannot replay while capturing (via web).");
      request->redirect("/"); // Redirect back with status
      return;
    }

    // Check if index parameter is provided in form data
    if (request->hasParam("index", true)) {
      int requestedIndex = request->getParam("index", true)->value().toInt();

      // Validate the index
      if (requestedIndex >= 0 && requestedIndex < capturedPackets.size()) {
        packetIndexToReplay = requestedIndex; // Set the index of the packet to replay
        replayTriggered = true; // Set the flag
        Serial.printf("Replay triggered for packet index %d (via web)...\n", requestedIndex);
      } else {
        Serial.printf("Invalid packet index requested for replay (via web): %d\n", requestedIndex);
      }
    } else {
       Serial.println("No packet index provided for replay (via web).");
    }
     request->redirect("/"); // Redirect back to the main page after triggering replay
  });

    // Handle for Delete Packet action (POST request from form)
  server.on("/delete_packet", HTTP_POST, [](AsyncWebServerRequest *request){
    if (isCapturing) {
      Serial.println("Cannot delete packet while capturing (via web).");
      request->redirect("/"); // Redirect back with status
      return;
    }

    // Check if index parameter is provided in form data
    if (request->hasParam("index", true)) {
      int requestedIndex = request->getParam("index", true)->value().toInt();

      // Validate the index
      if (requestedIndex >= 0 && requestedIndex < capturedPackets.size()) {
        Serial.printf("Deleting packet index %d (via web)...\n", requestedIndex);
        capturedPackets.erase(capturedPackets.begin() + requestedIndex); // Remove the packet

        // savePackets(); // Removed save call

        Serial.println("Packet deleted."); // Updated print
      } else {
        Serial.printf("Invalid packet index requested for deletion (via web): %d\n", requestedIndex);
      }
    } else {
       Serial.println("No packet index provided for deletion (via web).");
    }
     request->redirect("/"); // Redirect back to the main page
  });

    // Handle for Delete All Packets action (Removed) ---
    // server.on("/delete_all_packets", HTTP_POST, [](AsyncWebServerRequest *request){...}); is removed.


  // Start server
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  // The AsyncWebServer handles client connections and requests in the background

  // --- Multi-Channel Scanning and Capture ---
  if (isCapturing) {
    unsigned long currentTime = millis();

    // Check if it's time to switch channels
    // Use adaptive dwell time based on recent capture
    unsigned long currentDwellTime = packetCapturedRecently ? EXTENDED_DWELL_TIME_MS : CHANNEL_DWELL_TIME_MS;

    if (currentTime - channelSwitchTime >= currentDwellTime) {
      // Switch to the next channel
      currentChannel++;
      if (currentChannel > 125) { // Channels are typically 0-125
        currentChannel = 0;
      }

      radio.stopListening(); // Stop listening before changing channel
      radio.setChannel(currentChannel); // Set the new channel
      radio.startListening(); // Start listening on the new channel

      Serial.printf("Scanning Channel %u...\n", currentChannel);
      channelSwitchTime = currentTime; // Reset timer
      packetCapturedRecently = false; // Reset recent capture flag on channel switch
    }

    // Check for incoming nRF24L01+ packets on the current channel
    if (radio.available()) {
      uint8_t pipeNum;
      // Read the payload size
      uint8_t currentPacketSize = radio.getDynamicPayloadSize();

      if (currentPacketSize > 0 && currentPacketSize <= 32) {
         // Read the packet data into a temporary buffer first
        uint8_t tempPacketData[32];
        radio.read(&tempPacketData, currentPacketSize);

        // Check if we have space to store the packet
        if (capturedPackets.size() < MAX_PACKETS) {
            CapturedPacket newPacket;
            memcpy(newPacket.data, tempPacketData, currentPacketSize); // Copy data
            newPacket.size = currentPacketSize;
            newPacket.channel = currentChannel; // Store the channel it was captured on
            newPacket.captureTime = millis(); // Store capture time

            capturedPackets.push_back(newPacket); // Add to the vector

            Serial.printf("Captured packet %u (Size: %u bytes, Channel: %u): ", capturedPackets.size() - 1, currentPacketSize, newPacket.channel);
            for (int i = 0; i < currentPacketSize; i++) {
              Serial.print(tempPacketData[i], HEX);
              Serial.print(" ");
            }
            Serial.println();

             // savePackets(); // Removed save call
             packetCapturedRecently = true; // Set flag since a packet was just captured


        } else {
            Serial.printf("Captured packet (Size: %u bytes, Channel: %u), but storage is full (max %u packets).\\n", currentPacketSize, currentChannel, MAX_PACKETS);
             // Optional: Overwrite the oldest packet instead of stopping capture
             // For now, we just ignore new packets when full.
        }

      } else {
           Serial.println("Received packet with unexpected payload size or no data while capturing.");
      }
    }
  }
  // --- End Multi-Channel Scanning and Capture ---


  // Check if replay is triggered (from web request)
  if (replayTriggered) {
    // Ensure a valid packet index is set
    if (packetIndexToReplay >= 0 && packetIndexToReplay < capturedPackets.size()) {
        Serial.printf("Attempting to replay captured packet index %d (triggered via web)...\\n", packetIndexToReplay);

        // Get the packet to replay from storage
        CapturedPacket packetToReplay = capturedPackets[packetIndexToReplay];

        // --- Prepare for transmission ---
        // Stop listening if currently capturing
        if (isCapturing) {
             radio.stopListening();
        } else {
            // If not capturing, ensure listening is off before transmitting
            radio.stopListening();
        }
        // Replay on the channel the packet was captured on
        radio.setChannel(packetToReplay.channel);
        radio.openWritingPipe(addresses[0]); // Use the defined address for transmission

        // Write the captured packet
        if (radio.write(packetToReplay.data, packetToReplay.size)) {
          Serial.println("Packet replayed successfully.");
        } else {
          Serial.println("Packet replay failed.");
        }

        // --- Return to appropriate mode ---
        // Close the writing pipe (implicitly handled by setting reading pipe or stopping)
        // radio.closeWritingPipe(); // Not in v1.5.0

        // After replay, if capturing was active, resume listening on the *current scanning channel*. Otherwise, stay idle.
        if (isCapturing) {
            radio.stopListening(); // Ensure out of TX mode
            radio.openReadingPipe(0, addresses[0]); // Re-open the reading pipe
            radio.setChannel(currentChannel); // Return to the current scanning channel
            radio.startListening(); // Resume listening for capture
            Serial.println("Switched back to listening mode (resumed capture).");
        } else {
            // If not capturing, stay in idle/standby after transmit
            radio.stopListening(); // Ensure out of TX mode and listening is off
            Serial.println("Switched back to idle mode (capture not active).");
        }

    } else {
      Serial.println("Replay triggered but invalid packet index or no packets captured.");
    }

    replayTriggered = false; // Reset the flag
    packetIndexToReplay = -1; // Reset packet index

    // Note: The web page will be redirected and reloaded after replay,
    // showing the updated status/list.

  }

  // Small delay to prevent watchdog timer issues in tight loops
  delay(1); // Keep the loop from running too fast
}
