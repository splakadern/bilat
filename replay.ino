#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include <RF24.h>
#include <vector>
#include <ArduinoJson.h> // Include ArduinoJson for creating JSON responses

// Define AP credentials
const char* ap_ssid = "nothing";
const char* ap_password = "buymeacoffee";

// Define CE and CSN pins based on the provided pinout
#define CE_PIN  22
#define CSN_PIN 21

// Create an RF24 object
RF24 radio(CE_PIN, CSN_PIN);

// Address for communication
const byte addresses[][6] = {"1Node"};

// --- Packet Storage ---
struct CapturedPacket {
    uint8_t data[32];
    uint8_t size;
    uint8_t channel;
    unsigned long captureTime;
};

std::vector<CapturedPacket> capturedPackets;
const size_t MAX_PACKETS = 10; // Maximum number of packets to store

// State variables
volatile bool isCapturing = false;
volatile bool replayTriggered = false;
volatile int packetIndexToReplay = -1; // -1 indicates no packet selected for replay

// --- Scanning Variables ---
uint8_t currentChannel = 0;
unsigned long channelSwitchTime = 0;
const unsigned long CHANNEL_DWELL_TIME_MS = 10;
const unsigned long EXTENDED_DWELL_TIME_MS = 200;
bool packetCapturedRecently = false;


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

// --- Embedded Web Content (HTML, CSS, and JS) ---

// HTML Structure - Modified to use IDs for JS updates and include script tag
const char html_page[] PROGMEM = R"rawliteral(
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
      <p id="status"></p>
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
      <div id="packetList"></div>
    </div>

  </div>

  <script>
    // JavaScript for dynamic status and packet list updates

    const statusElement = document.getElementById('status');
    const packetListElement = document.getElementById('packetList');

    async function updateStatus() {
      try {
        const response = await fetch('/status_json');
        const data = await response.json();

        // Update Status
        statusElement.textContent = data.status;

        // Update Packet List
        let packetListHTML = '';
        if (data.packets.length === 0) {
          packetListHTML = '<p>No packets captured yet.</p>';
        } else {
          data.packets.forEach(packet => {
            // Use template literals for easier string building
            packetListHTML += `
              <div class="packet-item">
                <div class="packet-info">
                  <strong>Packet ${packet.index}:</strong> <span class="packet-size">Size: ${packet.size} bytes</span> <span class="packet-channel">Channel: ${packet.channel}</span><br>
                  <span class="packet-data-hex">${packet.data}</span>
                </div>
                <form action="/replay_packet" method="post" style="display:inline;">
                  <input type="hidden" name="index" value="${packet.index}">
                  <button type="submit">REPLAY</button>
                </form>
                <form action="/delete_packet" method="post" style="display:inline;">
                  <input type="hidden" name="index" value="${packet.index}">
                  <button type="submit" class="delete-button" onclick="return confirm('Are you sure you want to delete this packet?');">DELETE</button>
                </form>
              </div>
            `;
          });
        }
        packetListElement.innerHTML = packetListHTML;

      } catch (error) {
        console.error('Failed to fetch status:', error);
        statusElement.textContent = 'Error fetching status.';
        packetListElement.innerHTML = '<p>Error loading packets.</p>';
      }
    }

    // Update status and packet list every 2 seconds
    setInterval(updateStatus, 2000);

    // Initial update when the page loads
    updateStatus();

  </script>

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

/* Note: Removed specific start/stop button styles as they weren't in the HTML,
         but keeping them here just in case you add IDs later. */
/* #startCaptureButton { ... } */
/* #stopCaptureButton { ... } */


.delete-button, .delete-all-button { /* Added .delete-all-button style */
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


/* Mobile specific adjustments */
@media (max-width: 600px) {
  body {
    padding: 10px;
  }

  .container {
    padding: 20px;
    width: 100%; /* Use full width on smaller screens */
  }

  h2 {
    font-size: 1.1em;
  }

  button {
    padding: 10px 20px;
    font-size: 1em;
  }
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
  while (!Serial);

  Serial.println("ESP32 nRF24L01+ Replay Module (Access Point Mode - Dynamic Updates)");
  Serial.println("Using provided VSPI Pinout:");
  Serial.println("CE Pin: 22");
  Serial.println("CSN Pin: 21");
  Serial.println("SCK Pin: 18");
  Serial.println("MISO Pin: 19");
  Serial.println("MOSI Pin: 23");
  Serial.printf("Max packets to store in RAM: %u\n", MAX_PACKETS);


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
    // The web page status update will reflect this indirectly (no capture possible)
    // but a more explicit error message could be added to the JSON status.
    // For now, we proceed but functionality will be limited.
  } else {
    Serial.println("Radio initialized.");
     // Set data rate and PA level
    radio.setDataRate(RF24_250KBPS); // Match the target device's data rate
    radio.setPALevel(RF24_PA_LOW); // Adjust as needed

    // Open a reading pipe on the defined address
    radio.openReadingPipe(0, addresses[0]);
  }


  Serial.println("Access Point active. HTTP server starting.");


  // --- Web Server Handlers (with JSON endpoint for JS) ---

  // Handle for root URL (/) - Serves the main HTML page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", html_page);
  });

  // Handle for CSS file
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/css", style_css);
  });

  // NEW: Handle for JSON status and packet data
  server.on("/status_json", HTTP_GET, [](AsyncWebServerRequest *request){
    // Estimate the size needed for the JSON document
    // Status string ~64 bytes + for each packet: index(int) + size(int) + channel(int) + data(hex string up to 32*3 + spaces)
    // Rough estimate: 64 + MAX_PACKETS * (4 + 4 + 4 + 32*3 + 32) = 64 + 10 * (12 + 96 + 32) = 64 + 10 * 140 = 1464 bytes
    // Use a buffer slightly larger than the estimate
    const size_t capacity = JSON_ARRAY_SIZE(MAX_PACKETS) + MAX_PACKETS*JSON_OBJECT_SIZE(4) + JSON_OBJECT_SIZE(2); // JSON_OBJECT_SIZE(2) for status and packets array
    StaticJsonDocument<1600> doc; // Use static allocation on the stack

    // Add status
    if (!radio.isChipConnected()) {
        doc["status"] = "Radio Error - Check wiring";
    } else if (isCapturing) {
        char status_buffer[64];
        snprintf(status_buffer, sizeof(status_buffer), "Capturing (Scanning Channel %u)...", currentChannel);
        doc["status"] = status_buffer;
    } else {
        doc["status"] = "Capture Stopped";
    }


    // Add packets array
    JsonArray packets = doc.createNestedArray("packets");
    for (size_t i = 0; i < capturedPackets.size(); i++) {
        JsonObject packet = packets.createNestedObject();
        packet["index"] = i; // Use the index in the vector
        packet["size"] = capturedPackets[i].size;
        packet["channel"] = capturedPackets[i].channel;
        packet["data"] = bytesToHex(capturedPackets[i].data, capturedPackets[i].size); // Convert data to hex string
    }

    // Send the JSON response
    String jsonResponse;
    serializeJson(doc, jsonResponse);
    request->send(200, "application/json", jsonResponse);
  });


  // Handle for Start Capture action (POST request from form)
  server.on("/start_capture", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!radio.isChipConnected()) {
        Serial.println("Cannot start capture: Radio hardware not responding.");
         // Maybe redirect with an error parameter? For now, just redirect.
        request->redirect("/");
        return;
    }
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
    if (!radio.isChipConnected()) {
        Serial.println("Cannot replay: Radio hardware not responding.");
        request->redirect("/");
        return;
    }
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
        // Set the flag and index to be processed in the loop()
        packetIndexToReplay = requestedIndex;
        replayTriggered = true;
        Serial.printf("Replay triggered for packet index %d (via web)...\n", requestedIndex);
         // The replay logic happens in loop(), redirect immediately
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
    if (!radio.isChipConnected()) {
        Serial.println("Cannot delete packet: Radio hardware not responding.");
        request->redirect("/");
        return;
    }
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
        Serial.println("Packet deleted.");
      } else {
        Serial.printf("Invalid packet index requested for deletion (via web): %d\n", requestedIndex);
      }
    } else {
       Serial.println("No packet index provided for deletion (via web).");
    }
     request->redirect("/"); // Redirect back to the main page
  });

  // Start server
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  // The AsyncWebServer handles client connections and requests in the background

  // --- Multi-Channel Scanning and Capture ---
  if (isCapturing && radio.isChipConnected()) { // Only scan if radio is connected
    unsigned long currentTime = millis();

    // Check if it's time to switch channels
    unsigned long currentDwellTime = packetCapturedRecently ? EXTENDED_DWELL_TIME_MS : CHANNEL_DWELL_TIME_MS;

    if (currentTime - channelSwitchTime >= currentDwellTime) {
      // Switch to the next channel
      currentChannel++;
      if (currentChannel > 125) {
        currentChannel = 0;
      }

      radio.stopListening();
      radio.setChannel(currentChannel);
      radio.startListening();

      // Serial.printf("Scanning Channel %u...\n", currentChannel); // Removed frequent print
      channelSwitchTime = currentTime;
      packetCapturedRecently = false; // Reset flag on channel switch
    }

    // Check for incoming nRF24L01+ packets on the current channel
    if (radio.available()) {
      uint8_t currentPacketSize = radio.getDynamicPayloadSize();

      if (currentPacketSize > 0 && currentPacketSize <= 32) {
         // Read the packet data into a temporary buffer first
        uint8_t tempPacketData[32];
        radio.read(&tempPacketData, currentPacketSize);

        // Check if we have space to store the packet
        if (capturedPackets.size() < MAX_PACKETS) {
            CapturedPacket newPacket;
            memcpy(newPacket.data, tempPacketData, currentPacketSize);
            newPacket.size = currentPacketSize;
            newPacket.channel = currentChannel;
            newPacket.captureTime = millis();

            capturedPackets.push_back(newPacket);

            Serial.printf("Captured packet %u (Size: %u bytes, Channel: %u)\n", capturedPackets.size() - 1, currentPacketSize, newPacket.channel);
             packetCapturedRecently = true; // Set flag since a packet was just captured

        } else {
            // Only print when a packet is missed due to full storage
            static unsigned long lastFullMessage = 0;
            if (millis() - lastFullMessage > 5000) { // Print every 5 seconds
                 Serial.printf("Captured packet (Size: %u bytes, Channel: %u), but storage is full (max %u packets).\n", currentPacketSize, currentChannel, MAX_PACKETS);
                 lastFullMessage = millis();
            }
        }

      } else {
           // Serial.println("Received packet with unexpected payload size or no data while capturing."); // Removed noisy print
      }
    }
  }
  // --- End Multi-Channel Scanning and Capture ---


  // Check if replay is triggered (from web request) and radio is connected
  if (replayTriggered && radio.isChipConnected()) {
    // Ensure a valid packet index is set
    if (packetIndexToReplay >= 0 && packetIndexToReplay < capturedPackets.size()) {
        Serial.printf("Attempting to replay captured packet index %d (triggered via web)...\n", packetIndexToReplay);

        // Get the packet to replay from storage
        CapturedPacket packetToReplay = capturedPackets[packetIndexToReplay];

        // --- Prepare for transmission ---
        // Stop listening if currently capturing or idle listening
        radio.stopListening();
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
  }

  // Small delay to prevent watchdog timer issues in tight loops
  delay(1);
}
