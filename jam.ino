#include <SPI.h>
#include <RF24.h>
#include <string.h> // Required for memset (used for data filling)

// **************************************************
// Select Mode: Uncomment ONE of the following lines
// **************************************************
#define IS_TRANSMITTER    // Uncomment this line for the Transmitter ESP32
//#define IS_RECEIVER     // Uncomment this line for the Receiver ESP32
// **************************************************
// Make sure only ONE of the above lines is uncommented!
// **************************************************


// --- User Specified Pin Definitions ---

// HSPI PINS (Typically for Receiver in this setup - NOT MODIFIED IN THIS VERSION)
#define HSPI_SCK   14
#define HSPI_MISO  12
#define HSPI_MOSI  13
#define HSPI_CSN   15 // User specified HSPI CSN (HCS)
#define HSPI_CE    16 // User specified HSPI CE

// VSPI PINS (Typically for Transmitter in this setup - PINS USED)
#define VSPI_SCK   18
#define VSPI_MISO  19
#define VSPI_MOSI  23
#define VSPI_CSN   21 // User specified VSPI CS
#define VSPI_CE    22 // User specified VSPI CE


// --- Radio Initialization ---

#ifdef IS_TRANSMITTER
  // Transmitter uses VSPI pins and the VSPI instance
  // This uses the standard constructor form (will work with updated ESP32 cores)
  RF24 radio(VSPI_CE, VSPI_CSN, VSPI); // Radio object: CE, CSN, SPI Instance
  const byte address[6] = "SPEED"; // Address for communication (Writing Pipe)

  // Payload structure sized to the maximum fixed payload (32 bytes) without CRC
  struct Payload {
    unsigned long packet_count;     // 4 bytes
    unsigned long timestamp;        // 4 bytes
    byte channel_in_payload;       // 1 byte (Indicates channel in payload data)
    byte data[23];                 // 23 bytes (Remaining bytes to fill 32-byte payload)
  }; // Total size = 4 + 4 + 1 + 23 = 32 bytes

  Payload myData; // Instance of the payload structure
  unsigned long packetCounter = 0; // Global packet counter

  // --- Channel Hopping Settings (Transmitter - MAX HOPPING) ---
  // Use ALL 128 possible RF24 Channels (0 to 127)
  const int totalChannels = 128;
  // Pushing limitation: Attempt to fill TX FIFO (3 packets) before hopping
  // This maximizes the rate of packets sent per channel visit before switching
  const int TX_BURST_SIZE = 3;

#elif defined IS_RECEIVER
  // Receiver uses HSPI pins and the HSPI instance (NOT MODIFIED)
  RF24 radio(HSPI_CE, HSPI_CSN, HSPI); // Radio object: CE, CSN, SPI Instance
  const byte address[6] = "SPEED"; // Address for communication (MUST match transmitter)
  struct Payload { // Data structure for reception (MUST match transmitter)
    unsigned long packet_count;
    unsigned long timestamp;
    byte channel_in_payload;
    byte data[23];
  };
  Payload receivedData;

  // --- Channel Hopping Settings (Receiver - NOT MODIFIED) ---
  // MUST match transmitter's channel list or scan strategy (transmitter is now 0-127)
   const byte channels[] = {
    10, 15, 20, 25, 30, 35, 40, 45, 50, 55,
    60, 65, 70, 75, 80, 85, 90, 95, 100, 105, 110
  };
  const int numChannels = sizeof(channels) / sizeof(channels[0]);
  int currentChannelIndex = 0;
  unsigned long lastChannelSwitchTime = 0;
  const unsigned long CHANNEL_LISTEN_DURATION_MS = 0;


#else
  #error "Please uncomment either IS_TRANSMITTER or IS_RECEIVER"
#endif


void setup() {
  Serial.begin(115200);

  // Seed the random number generator (used for initial channel or other randomness)
  randomSeed(analogRead(0)); // Use an unconnected analog pin for some entropy

  // --- Initialize SPI and Radio based on selected mode ---
#ifdef IS_TRANSMITTER
  Serial.println("--- Configuring as RF24 ABSOLUTE EXTREME TRANSMITTER - MAX POWER (VSPI) ---");
  // Explicitly initialize VSPI using user-specified pins and frequency
  SPI.begin(VSPI_SCK, VSPI_MISO, VSPI_MOSI, VSPI_CSN);
  SPI.setFrequency(10000000); // Set SPI speed to 10 MHz - Max for nRF24L01+
  Serial.println("VSPI initialized at 10 MHz.");

#elif defined IS_RECEIVER
  // HSPI Initialization (NOT MODIFIED)
  Serial.println("--- Configuring as RF24 ABSOLUTE EXTREME RECEIVER - MAX SCAN (HSPI) ---");
  SPI.begin(HSPI_SCK, HSPI_MISO, HSPI_MOSI, HSPI_CSN);
  SPI.setFrequency(10000000); // Set SPI speed to 10 MHz
  Serial.println("HSPI initialized at 10 MHz.");

#endif

  Serial.println("Targeting E01-ML01DP5 / nRF24L01+ compatible modules.");


  // *** Initialize Radio ***
  if (!radio.begin()) {
    Serial.println("Radio hardware is NOT responding!! Check wiring and power.");
    while (1); // Stop here if initialization fails
  } else {
    // --- Configure Radio (ABSOLUTE EXTREME SETTINGS - Pushing Limits) ---
    radio.setPALevel(RF24_PA_MAX); // Maximum transmit power (Required for pushing limits/range)
    radio.setDataRate(RF24_2MBPS); // Set Data Rate to 2MBPS (Max speed)
    radio.setPayloadSize(sizeof(Payload)); // Set the static payload size (MUST match, 32 bytes)
    radio.setCRCLength(RF24_CRC_DISABLED); // CRC Disabled for maximum raw throughput (MUST match)

    // Disable Auto-Acknowledgement and Retries for MAXIMUM new packet rate
    radio.setAutoAck(false); // Disable auto-acknowledgement for speed
    radio.setRetries(0, 0);   // No auto-retries for speed

    // Set initial channel (randomly selected from all possible channels)
    radio.setChannel(random(totalChannels)); // Start on a random channel from 0-127

#ifdef IS_TRANSMITTER
    radio.stopListening();    // Set radio to transmit mode
    radio.openWritingPipe(address);
    Serial.println("\n*** ABSOLUTE EXTREME TRANSMITTER: HIGHER POWER THAN PREVIOUS VERSIONS ***");
    Serial.print("Pushing limits with: 2Mbps, MAX PA, 32-byte payload (No CRC), No ACK/Retries, ");
    Serial.print("MAX SPI speed, Rapid Bursts (filling TX FIFO), and All 128 Channels Hopping.");

#elif defined IS_RECEIVER
    radio.openReadingPipe(0, address); // Open a reading pipe on the address
    radio.startListening();    // Start listening for packets
    Serial.println("\n*** ABSOLUTE EXTREME RECEIVER: Designed to rapidly scan channels ***");
    // Note: Receiver code is NOT modified in this version to scan all 128 channels.
    // It would need significant changes to match the transmitter's hopping strategy.

#endif

    radio.printDetails(); // Print final radio configuration (Minimal detail)


    // *** EXTREME PERIL WARNING - YOU ARE OPERATING WITH UTTER DISREGARD ***
    Serial.println("\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
    Serial.println("!!! YOU ARE CONFIGURING FOR **MAXIMUM BROADBAND DISRUPTION** AND        !!!");
    Serial.println("!!! ABSOLUTE MINIMAL COMPLIANCE.                                        !!!");
    Serial.println("!!!                                                                     !!!");
    Serial.println("!!! THIS CONFIGURATION BLASTS HIGH-POWER, HIGH-SPEED SIGNALS RANDOMLY !!!");
    Serial.println("!!! ACROSS THE **ENTIRE** 2.4 GHz SPECTRUM (CHANNELS 0-127), INCLUDING !!!");
    Serial.println("!!! WI-FI, BLUETOOTH, AND MANY PROPRIETARY FREQUENCIES.                 !!!");
    Serial.println("!!!                                                                     !!!");
    Serial.println("!!! EXPECT TO CAUSE **CATASTROPHIC, PROLONGED, AND WIDESPREAD** !!!");
    Serial.println("!!! INTERFERENCE TO **VIRTUALLY ALL** DEVICES IN THE 2.4 GHz BAND.      !!!");
    Serial.println("!!!                                                                     !!!");
    Serial.println("!!! THIS IS **HIGHLY ILLEGAL, HARMFUL, AND EXTREMELY IRRESPONSIBLE**. !!!");
    Serial.println("!!!                                                                     !!!");
    Serial.println("!!! **DO NOT POWER THIS ON UNLESS YOU ARE IN a CERTIFIED, SECURELY** !!!");
    Serial.println("!!! **SHIELDED ROOM (Faraday Cage) WITH NO OTHER 2.4 GHz ACTIVITY.** !!!");
    Serial.println("!!! **OR IN a REMOTE LOCATION WHERE INTERFERENCE IS ABSOLUTELY** !!!");
    Serial.println("!!! **IMPOSSIBLE.** !!!");
    Serial.println("!!!                                                                     !!!");
    Serial.println("!!! BY PROCEEDING, YOU ACCEPT FULL AND SOLE RESPONSIBILITY FOR ALL    !!!");
    Serial.println("!!! CONSEQUENCES, INCLUDING CRIMINAL PROSECUTION, FINES, AND HARM.      !!!");
    Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
    Serial.println("\nEngaging ABSOLUTE EXTREME, WIDESPREAD, potentially illegal operation...");

  }
}

void loop() {
  unsigned long currentMillis = millis();

#ifdef IS_TRANSMITTER
  // --- Transmitter Loop Logic (ABSOLUTE EXTREME - MAX THROUGHPUT & HOPPING) ---

  // Attempt to fill the TX FIFO (3 packets) before hopping channels.
  // This sends small, rapid bursts of data on each frequency visited.
  for (int burst = 0; burst < TX_BURST_SIZE; burst++) {
    myData.packet_count = packetCounter; // Assign current counter value
    myData.timestamp = currentMillis;   // Include current timestamp

    // Indicate the channel number within the payload data (for informational purposes)
    // Use the channel the radio is currently set to transmit on
    myData.channel_in_payload = radio.getChannel();

    // Fill the remaining data payload bytes with a simple pattern or 0xFF
    // Using 0xFF fills with the maximum byte value
    memset(myData.data, 0xFF, sizeof(myData.data));
    // Or use your original pattern:
    // for(int i=0; i<sizeof(myData.data); i++){
    //   myData.data[i] = (byte)packetCounter + i;
    // }


    // Use writeFast for non-blocking transmit attempt - prioritizes MAX speed.
    // It attempts to add the packet to the transmit FIFO.
    // If the FIFO is full (3 packets), subsequent writeFast calls might return false
    // until space is available. We increment counter regardless of writeFast return,
    // as we are just tracking attempted packets.
    radio.writeFast(&myData, sizeof(myData));

    packetCounter++; // Increment packet counter for each packet attempted
  }

  // --- Channel Switching Logic (ABSOLUTE EXTREME Transmitter - MAX HOPPING) ---
  // Hop to the next channel (0-127) after attempting the burst.
  // The channel is determined by the total packets attempted.
  // This ensures sequential hopping through all 128 channels after every burst.
  byte next_channel = (packetCounter / TX_BURST_SIZE) % totalChannels;
  radio.setChannel(next_channel); // Change the actual transmission frequency

  // No delay here maximizes the hop speed between bursts.


#elif defined IS_RECEIVER
  // --- Receiver Loop Logic (NOT MODIFIED) ---

  // Check for incoming packets as fast as possible
  if (radio.available()) {
    // Reading packet - attempting to read any available data immediately
    radio.read(&receivedData, sizeof(receivedData));
    // Do not increment counter or print here to minimize ANY delay

    // --- You would typically process receivedData here ---
    // Example (be mindful of performance if processing a lot):
    // Serial.print("Received Packet: ");
    // Serial.print(receivedData.packet_count);
    // Serial.print(" Time: ");
    // Serial.print(receivedData.timestamp);
    // Serial.print(" Channel(in payload): ");
    // Serial.println(receivedData.channel_in_payload);
    // Note: Actual channel received on would need radio.getChannel() after radio.available()

  }

  // --- Channel Switching Logic (NOT MODIFIED) ---
  // This logic is for the receiver's scan strategy and needs adjustment
  // if you want it to try and follow the transmitter's new 0-127 hopping.
  if (currentMillis - lastChannelSwitchTime >= CHANNEL_LISTEN_DURATION_MS) {
      // The original channel list (21 channels) is used here.
      // This part needs significant modification to scan all 128 channels.
      currentChannelIndex = random(numChannels);
      radio.setChannel(channels[currentChannelIndex]);
      radio.startListening(); // Must call startListening() again after setChannel()
      lastChannelSwitchTime = currentMillis;
  }

#endif
  // No Serial print or other heavy operations in the main loop to maximize performance
}
