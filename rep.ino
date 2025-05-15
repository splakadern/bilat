#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include <RF24.h>
#include <vector>
#include <ArduinoJson.h> // Include ArduinoJson for creating JSON responses

// Define AP credentials
const char* ap_ssid = "RF24_Replay"; // Changed to be more descriptive
const char* ap_password = "buymeacoffee"; // Keep your password :)

// Define CE and CSN pins based on the provided pinout for E01-ML01DP5
// Standard ESP32 VSPI pins: SCK=18, MISO=19, MOSI=23
#define CE_PIN  22 // User-defined for E01-ML01DP5
#define CSN_PIN 21 // User-defined for E01-ML01DP5

// Create an RF24 object
RF24 radio(CE_PIN, CSN_PIN);

// Address for communication (This is just a placeholder, actual packets will have various source/dest, but we use one for opening pipes)
const byte addresses[][6] = {"1Node"}; // Using a single address for simplification

// --- Packet Storage ---
struct CapturedPacket {
    uint8_t data[32]; // Max payload size
    uint8_t size;
    uint8_t channel;
    unsigned long captureTime;
};

std::vector<CapturedPacket> capturedPackets;
const size_t MAX_PACKETS = 10; // Maximum number of packets to store in RAM

// State variables
volatile bool isCapturing = false;
volatile bool replayTriggered = false;
volatile int packetIndexToReplay = -1; // -1 indicates no packet selected for replay

// --- Scanning Variables ---
uint8_t currentChannel = 0; // Start scanning from channel 0
unsigned long channelSwitchTime = 0;
const unsigned long CHANNEL_DWELL_TIME_MS = 10; // How long to stay on a channel normally
const unsigned long EXTENDED_DWELL_TIME_MS = 200; // Longer dwell time if a packet was just seen
bool packetCapturedRecently = false; // Flag to trigger extended dwell

// Web Server
AsyncWebServer server(80);

// Function to convert byte array to hex string for web display
String bytesToHex(uint8_t* buffer, uint8_t bufferSize) {
  String hexString = "";
  for (int i = 0; i < bufferSize; i++) {
    char hexChar[3];
    sprintf(hexChar, "%02X", buffer[i]); // Format byte as two hex characters
    hexString += hexChar;
    if (i < bufferSize - 1) {
      hexString += " "; // Add space between bytes
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
  <title>nRF24 Replay Module</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" href="/style.css">
</head>
<body>
  <div class="container">
    <h1>nRF24 Replay Module</h1>

    <div class="status-section">
      <h2>Status</h2>
      <p id="status">Initializing...</p>
    </div>

    <div class="controls-section">
      <h2>Controls</h2>
      <form action="/start_capture" method="post" style="display:inline;">
        <button type="submit" id="startCaptureButton">START CAPTURE</button>
      </form>
      <form action="/stop_capture" method="post" style="display:inline;">
        <button type="submit" id="stopCaptureButton">STOP CAPTURE</button>
      </form>
      <form action="/delete_all_packets" method="post" style="display:inline;">
         <button type="submit" class="delete-all-button" onclick="return confirm('Are you sure you want to delete ALL captured packets?');">DELETE ALL</button>
      </form>
    </div>

    <div class="packet-list-section">
      <h2>Captured Packets (<span id="packetCount">0</span>/<span id="maxPacketCount">10</span>)</h2>
      <div id="packetList">
          <p>Loading packets...</p>
      </div>
    </div>

  </div>

  <script>
    // JavaScript for dynamic status and packet list updates

    const statusElement = document.getElementById('status');
    const packetListElement = document.getElementById('packetList');
    const packetCountElement = document.getElementById('packetCount');
    const maxPacketCountElement = document.getElementById('maxPacketCount');


    async function updateStatus() {
      try {
        const response = await fetch('/status_json');
        const data = await response.json();

        // Update Status
        statusElement.textContent = data.status;

        // Update Max Packet Count (should be static, but good practice to fetch)
        maxPacketCountElement.textContent = data.maxPackets;


        // Update Packet List
        packetCountElement.textContent = data.packets.length;
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
                  <button type="submit" class="replay-button">REPLAY</button>
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
        packetCountElement.textContent = '?';
        maxPacketCountElement.textContent = '?';
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


// CSS (Adjusted styling for "chill expensive" - same as previous version, added replay button style, mobile-friendly)
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

h1 {
    color: #e0e0e0; /* Light grey for main title */
    font-size: 1.8em;
    margin-bottom: 20px;
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
    flex-wrap: wrap; /* Allow items to wrap on smaller screens */
}

.packet-info {
    flex-grow: 1; /* Allow packet info to take available space */
    margin-right: 10px; /* Space between info and button */
    min-width: 150px; /* Ensure info block doesn't shrink too much */
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

/* Specific button styles (using IDs as in HTML) */
#startCaptureButton {
    background-color: #208020; /* Muted green */
}
#startCaptureButton:hover {
    background-color: #309030;
}

#stopCaptureButton {
    background-color: #802020; /* Muted red */
}
#stopCaptureButton:hover {
    background-color: #903030;
}


.delete-button, .delete-all-button { /* Added .delete-all-button style */
    background-color: #c03030; /* Muted delete red */
    color: white;
    padding: 8px 15px; /* Slightly smaller for list/all delete */
    font-size: 0.9em;
}
.delete-button:hover, .delete-all-button:hover {
     background-color: #d04040;
}


/* Style for the replay button in the list */
.replay-button { /* Changed from .packet-item button for clarity */
    padding: 8px 15px; /* Smaller padding for inline buttons */
    font-size: 0.9em; /* Smaller font size */
    background-color: #806020; /* Muted gold/yellow */
    color: #e0e0e0;
}
.replay-button:hover {
     background-color: #907030;
}

/* Ensure forms for buttons are inline */
.packet-item form {
    display: inline-block;
    margin: 2px; /* Small margin between buttons */
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

  h1 {
      font-size: 1.5em;
  }

  h2 {
    font-size: 1.1em;
  }

  button {
    padding: 10px 15px; /* Adjust button padding */
    font-size: 1em;
  }
  .packet-item {
      flex-direction: column; /* Stack items vertically on small screens */
      align-items: flex-start; /* Align items to the start */
  }
  .packet-info {
      margin-right: 0;
      margin-bottom: 5px; /* Space below info */
      min-width: unset; /* Remove min-width constraint */
      width: 100%; /* Allow info block to take full width */
  }
  .packet-item form {
      display: block; /* Stack buttons vertically on small screens */
      width: 100%;
      margin: 5px 0;
  }
  .packet-item button {
      width: 100%; /* Make buttons full width */
      padding: 10px;
  }

}
)rawliteral";


void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 5000); // Wait for Serial, but not forever

  Serial.println("\n--- ESP32 nRF24L01+ Replay Module ---");
  Serial.println("Using provided VSPI Pinout for E01-ML01DP5:");
  Serial.println("CE Pin: 22");
  Serial.println("CSN Pin: 21");
  Serial.println("SCK Pin: 18 (VSPI)");
  Serial.println("MISO Pin: 19 (VSPI)");
  Serial.println("MOSI Pin: 23 (VSPI)");
   Serial.printf("Max packets to store in RAM: %u\n", MAX_PACKETS);


  // --- Configure ESP32 as a WiFi Access Point (AP) ---
  Serial.printf("Starting WiFi Access Point '%s' with password '%s'\n", ap_ssid, ap_password);
  WiFi.softAP(ap_ssid, ap_password);

  IPAddress apIP = WiFi.softAPIP();
  Serial.print("Access Point IP address: ");
  Serial.println(apIP);
  // --- WiFi AP Setup Complete ---

  // Initialize the radio
  Serial.print("Initializing RF24 radio...");
  // Use the hardware SPI bus instance VSPI, configured via Arduino core
  // SPI.begin(SCK, MISO, MOSI, SS); SS is often CSN, but we manage CSN manually with radio(CE, CSN) constructor
  SPI.begin(18, 19, 23, -1); // SS (Chip Select for SPI.begin) not strictly needed when CSN is handled by RF24 library constructor

  if (!radio.begin()) {
    Serial.println(" FAILED");
    Serial.println("Radio hardware not responding! Check wiring.");
    // The web page status update will reflect this.
    // Proceeding, but radio functionality will be unavailable.
  } else {
    Serial.println(" OK");
     // Set data rate and PA level (these MUST match the target device)
    // Common rates: RF24_250KBPS, RF24_1MBPS, RF24_2MBPS
    radio.setDataRate(RF24_250KBPS); // *** IMPORTANT: Set this to match your target device ***
    radio.setPALevel(RF24_PA_LOW); // Adjust PA level as needed (LOW, HIGH, MAX). E01-ML01DP5 has internal PA/LNA. Try different levels.
    radio.setCRCLength(RF24_CRC_16); // Set CRC length (8, 16, or disabled). MUST match target.
    radio.disableDynamicPayloads(); // Disable dynamic payloads for simpler packet handling (optional, but common).
                                   // If target uses dynamic payloads, you must enable them here!
                                   // radio.enableDynamicPayloads();
                                   // radio.setAutoAck(true); // AutoACK is often enabled and expected by sender

    // Set the address width (usually 5 bytes, common with NRF24)
    radio.setAddressWidth(5); // Most devices use 5 bytes. Set to match target.

    // For receiving, we need to open a reading pipe. We'll use pipe 0 and the base address.
    // The library handles filtering packets not addressed to this pipe/address when listening.
    radio.openReadingPipe(0, addresses[0]);

    // Note: We don't start listening until capture is initiated via the web interface.
    // We don't open a writing pipe until replay is triggered.
     Serial.printf("RF24 Configuration: Data Rate=%s, PA Level=%s, CRC=%s, Address Width=%u\n",
                  (radio.getDataRate() == RF24_250KBPS ? "250KBPS" : (radio.getDataRate() == RF24_1MBPS ? "1MBPS" : "2MBPS")),
                  (radio.getPALevel() == RF24_PA_LOW ? "LOW" : (radio.getPALevel() == RF24_PA_HIGH ? "HIGH" : "MAX")),
                  (radio.getCRCLength() == RF24_CRC_8 ? "8-bit" : (radio.getCRCLength() == RF24_CRC_16 ? "16-bit" : "Disabled")),
                  radio.getAddressWidth()
                  );

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
    // Status string (~64 bytes) + maxPackets(int) + for each packet: index(int) + size(int) + channel(int) + data(hex string up to 32*3 + spaces)
    // Rough estimate: JSON_OBJECT_SIZE(3) (status, packets, maxPackets) + JSON_ARRAY_SIZE(MAX_PACKETS) + MAX_PACKETS * JSON_OBJECT_SIZE(4) (index, size, channel, data)
    // Max data hex string size: 32 bytes * 3 chars/byte (XX ) - 1 space = 95 chars. Let's round up to 100.
    // StaticJsonDocument size: 256 (base) + 10 * (4 + 4 + 4 + 100) = 256 + 10 * 112 = 256 + 1120 = ~1376
    // Let's use 1600 to be safe, or even 2048
    const size_t capacity = JSON_ARRAY_SIZE(MAX_PACKETS) + MAX_PACKETS*JSON_OBJECT_SIZE(4) + JSON_OBJECT_SIZE(3);
    StaticJsonDocument<2048> doc; // Use static allocation on the stack with a safe size

    // Add status
    if (!radio.isChipConnected()) {
        doc["status"] = "Radio Error - Check wiring";
    } else if (isCapturing) {
        char status_buffer[64]; // Use a small buffer for the dynamic part
        snprintf(status_buffer, sizeof(status_buffer), "Capturing (Scanning Channel %u)...", currentChannel);
        doc["status"] = status_buffer;
    } else {
        doc["status"] = "Capture Stopped";
    }

    // Add max packets limit
    doc["maxPackets"] = MAX_PACKETS;

    // Add packets array
    JsonArray packets = doc.createNestedArray("packets");
    for (size_t i = 0; i < capturedPackets.size(); i++) {
        JsonObject packet = packets.createNestedObject();
        packet["index"] = i; // Use the index in the vector for web reference
        packet["size"] = capturedPackets[i].size;
        packet["channel"] = capturedPackets[i].channel;
        // Convert data to hex string only when needed for web display
        packet["data"] = bytesToHex(capturedPackets[i].data, capturedPackets[i].size);
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
         // Redirect back, status_json will show the error
        request->redirect("/");
        return;
    }
    if (!isCapturing) {
      isCapturing = true;
      currentChannel = 0; // Start scanning from channel 0
      packetCapturedRecently = false; // Reset flag
      radio.stopListening(); // Ensure not listening before configuring
      // Open the reading pipe. We assume the target uses the base address for receiving.
      radio.openReadingPipe(0, addresses[0]);
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
      Serial.println("Cannot replay while capturing (via web). Stop capture first.");
      request->redirect("/"); // Redirect back with status
      return;
    }
    if (replayTriggered) {
       Serial.println("Replay is already in progress. Please wait.");
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
      Serial.println("Cannot delete packet while capturing (via web). Stop capture first.");
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

    // Handle for Delete ALL Packets action (POST request from form)
  server.on("/delete_all_packets", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!radio.isChipConnected()) {
        Serial.println("Cannot delete packets: Radio hardware not responding.");
        request->redirect("/");
        return;
    }
     if (isCapturing) {
      Serial.println("Cannot delete packets while capturing (via web). Stop capture first.");
      request->redirect("/"); // Redirect back with status
      return;
    }

    Serial.println("Deleting all captured packets (via web)...");
    capturedPackets.clear(); // Clear the vector
    Serial.println("All packets deleted.");
    request->redirect("/"); // Redirect back to the main page
  });


  // Start server
  server.begin();
  Serial.println("HTTP server started");
  Serial.println("Ready. Connect to the AP 'RF24_Replay' and navigate to 192.168.4.1");
}

void loop() {
  // The AsyncWebServer handles client connections and requests in the background

  // --- Multi-Channel Scanning and Capture ---
  // Perform radio operations ONLY if the chip is connected
  if (radio.isChipConnected()) {
    if (isCapturing) {
      unsigned long currentTime = millis();

      // Determine dwell time based on recent packet capture
      unsigned long currentDwellTime = packetCapturedRecently ? EXTENDED_DWELL_TIME_MS : CHANNEL_DWELL_TIME_MS;

      // Check if it's time to switch channels
      if (currentTime - channelSwitchTime >= currentDwellTime) {
        // Switch to the next channel (0-125)
        currentChannel++;
        if (currentChannel > 125) {
          currentChannel = 0;
        }

        // Stop listening, change channel, start listening
        radio.stopListening();
        radio.setChannel(currentChannel);
        radio.startListening();

        // Serial.printf("Scanning Channel %u...\n", currentChannel); // Can be noisy, uncomment if needed
        channelSwitchTime = currentTime;
        packetCapturedRecently = false; // Reset flag on channel switch
      }

      // Check for incoming nRF24L01+ packets on the current channel
      if (radio.available()) {
        // Use getDynamicPayloadSize if dynamic payloads are ENABLED on the target.
        // If using fixed payloads (disableDynamicPayloads), use radio.getPayloadSize()
        // after setting it with radio.setPayloadSize().
        // This code assumes dynamic payloads MIGHT be used or is configured for it.

        uint8_t currentPacketSize = radio.getDynamicPayloadSize();

        // If getDynamicPayloadSize returns 0, it could mean no packet or a packet with size 0.
        // radio.available() already confirmed a packet is in the FIFO.
        // If dynamic payloads are DISABLED, getDynamicPayloadSize() will return 0 even for a valid packet.
        // In that fixed-payload case, you MUST use radio.getPayloadSize() and radio.setPayloadSize()
        // to define the expected packet size.
        // Let's adjust to handle the likely scenario where dynamic payloads are disabled (common for simple devices).
        // If dynamic payloads are disabled, radio.getPayloadSize() gives the configured size.
        // If enabled, getDynamicPayloadSize() is preferred.

        // Let's assume dynamic payloads are disabled as in the current config:
         if (!radio.getDynamicPayloadsEnabled()) {
             currentPacketSize = radio.getPayloadSize(); // Use static size if dynamic is off
             if (currentPacketSize == 0) {
                // This shouldn't happen if radio.available() is true and static size is set,
                // but as a fallback, maybe try a common size or max size?
                // Or assume a protocol-specific size if known.
                // For robustness, let's just read max 32 bytes if static size is not set or is 0
                // though this is less reliable than knowing the actual size.
                 Serial.println("Warning: Dynamic payloads disabled but static size is 0. Reading max 32 bytes.");
                 currentPacketSize = 32; // Fallback to max size
             }
         } else {
             // Dynamic payloads are enabled, get the actual size
             if (currentPacketSize == 0) {
                // This could be a valid packet of size 0 if allowed by protocol,
                // or an issue. Read and process if size 0 is valid, otherwise discard.
                // Assuming valid packets have size > 0 for storing.
                 Serial.println("Warning: Received dynamic packet with reported size 0. Skipping storage.");
                 uint8_t dummy[1]; // Read at least 1 byte to clear FIFO if needed
                 radio.read(&dummy, 1); // Attempt to read 1 byte to clear FIFO item
                 return; // Skip storing
             }
         }


        if (currentPacketSize > 0 && currentPacketSize <= 32) {
           // Read the packet data into a temporary buffer first
          uint8_t tempPacketData[32];
          radio.read(&tempPacketData, currentPacketSize); // Read the actual data

          // Check if we have space to store the packet
          if (capturedPackets.size() < MAX_PACKETS) {
              CapturedPacket newPacket;
              memcpy(newPacket.data, tempPacketData, currentPacketSize); // Copy data
              newPacket.size = currentPacketSize;
              newPacket.channel = currentChannel;
              newPacket.captureTime = millis(); // Record capture time

              capturedPackets.push_back(newPacket); // Add to vector

              Serial.printf("Captured packet %u (Size: %u bytes, Channel: %u)\n", capturedPackets.size() - 1, currentPacketSize, newPacket.channel);
              packetCapturedRecently = true; // Set flag since a packet was just captured

          } else {
              // Only print when a packet is missed due to full storage
              static unsigned long lastFullMessage = 0;
              if (millis() - lastFullMessage > 5000) { // Print every 5 seconds
                   Serial.printf("Captured packet (Size: %u bytes, Channel: %u), but storage is full (max %u packets).\n", currentPacketSize, currentChannel, MAX_PACKETS);
                   lastFullMessage = millis();
              }
              // Need to read the packet from the FIFO even if we don't store it
              // radio.read(&tempPacketData, currentPacketSize); // Already read above
          }

        } else {
             // Handle packets with size > 32
             Serial.printf("Received packet with invalid size: %u bytes (max 32). Clearing FIFO.\n", currentPacketSize);
             // Attempt to read/clear whatever is there to prevent FIFO getting stuck
             uint8_t dummy[32];
             // Read the reported size if reasonable, otherwise read max 32
             radio.read(&dummy, (currentPacketSize > 0 && currentPacketSize <= 32) ? currentPacketSize : 32);
        }
      }
    }

    // --- Packet Replay ---
    // Check if replay is triggered (from web request) and radio is connected
    if (replayTriggered) { // Flag is set by the web handler
      // Ensure a valid packet index is set
      if (packetIndexToReplay >= 0 && packetIndexToReplay < capturedPackets.size()) {
          Serial.printf("Attempting to replay captured packet index %d (triggered via web)...\n", packetIndexToReplay);

          // Get the packet to replay from storage
          CapturedPacket packetToReplay = capturedPackets[packetIndexToReplay];

          // --- Prepare for transmission ---
          // Stop listening if currently doing so
          radio.stopListening();

          // Replay on the channel the packet was captured on
          radio.setChannel(packetToToReplay.channel);

          // Open a writing pipe. We use the same address for sending as receiving for simplicity,
          // assuming the target device listens on this address.
          // If the target expects to receive on a different address than it sends from,
          // you would need to configure that specific TX address here.
          radio.openWritingPipe(addresses[0]); // Assuming target receives on pipe 0's address

          // Write the captured packet
          Serial.printf("Replaying %u bytes on channel %u...\n", packetToReplay.size, packetToReplay.channel);
          unsigned long tx_start_time = millis();
          // radio.write returns true immediately if packet is loaded into TX FIFO.
          // It doesn't wait for successful transmission/ACK.
          // For confirmation, you might need to check radio.txStandBy() or radio.txStandBy(timeout).
          bool write_success = radio.write(packetToReplay.data, packetToReplay.size);
          unsigned long tx_end_time = millis();

          if (write_success) {
             Serial.printf("Packet sent to TX FIFO in %lu ms.\n", tx_end_time - tx_start_time);
             // Optional: Wait for transmission complete (blocking)
             // radio.txStandBy(); // or radio.txStandBy(timeout)
             // Check radio.failureDetected if using auto-ACK and txStandBy
             // if (radio.txStandBy()) {
             //     Serial.println("Transmission successful (or FIFO empty).");
             // } else {
             //      Serial.println("Transmission failed or timed out.");
             //      if (radio.failureDetected) Serial.println("ACK not received.");
             // }
          } else {
            Serial.println("Packet write to TX FIFO failed (maybe FIFO full?).");
          }


          // --- Return to appropriate mode ---
          // After replay, if capturing was active *before* replay, resume listening on the *current scanning channel*.
          // Otherwise, stay in idle/standby.
          // IMPORTANT: Need a small delay here after write() to allow the radio to finish transmitting
          // before switching modes or channels, especially on the E01 modules which might need more time.
          delay(5); // Small delay (experiment if needed)

          if (isCapturing) {
              radio.stopListening(); // Ensure out of TX mode before opening reading pipe
              radio.openReadingPipe(0, addresses[0]); // Re-open the reading pipe
              radio.setChannel(currentChannel); // Return to the current scanning channel
              radio.startListening(); // Resume listening for capture
              Serial.printf("Switched back to listening mode (resumed capture on channel %u).\n", currentChannel);
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
    } // End if(replayTriggered)
  } // End if(radio.isChipConnected())
  else {
      // Radio not connected, ensure capturing is off and print error periodically
      if (isCapturing) {
          isCapturing = false; // Force capture off
          Serial.println("Capture forced off due to radio disconnection.");
          // No need to radio.stopListening() if chip is not connected
      }
       static unsigned long lastRadioError = 0;
       if (millis() - lastRadioError > 10000) { // Print every 10 seconds
           Serial.println("Radio hardware not responding. Check wiring.");
           lastRadioError = millis();
       }
  }


  // Small delay to prevent watchdog timer issues in tight loops,
  // although AsyncWebServer and delay(1) generally handle this well.
  delay(1);
}
