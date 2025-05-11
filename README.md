# ✨ Simple ESP32 Wi-Fi Deauther (Web Interface) ✨

---

This is a straightforward program for your ESP32 board that lets you control basic Wi-Fi deauthentication attacks right from a web page on your phone or computer.

---

## 🔥 EXTREMELY IMPORTANT WARNINGS 🔥

* **🚨 LEGAL & ETHICAL:** Using this code on Wi-Fi networks you **do not own or have explicit permission** to test is **HIGHLY ILLEGAL** and unethical. You are fully responsible for your actions. **ONLY use this on your own equipment** for learning or testing purposes.
* **⚠️ MAY NOT WORK:** This attack **will not work** on all Wi-Fi networks. Newer and more secure networks (like many using WPA3 or updated WPA2) have built-in protection that stops this type of attack. Also, depending on the software version on your ESP32, you might see "unsupport" errors (check Serial Monitor!) and the attack messages won't even be sent by the ESP32.

---

## What It Can Do ✨

* 📶 Create its own Wi-Fi network (Access Point) for you to connect to.
* 🌐 Show you a web page when you connect to its network.
* 🔍 Scan for other Wi-Fi networks nearby and list them.
* 💥 Send special messages to disconnect devices from a network you pick (Single Target attack).
* 🌪️ Try to disconnect devices from many networks by switching channels (Deauth All attack).
* 💡 Flash an LED or print messages to help you see what's happening (if you set it up).

---

## How to Get It Running ▶️

1.  **➡️ Get the Code:** Copy the complete code block from the "Code" section below.
2.  **➡️ Open in Arduino IDE:** Paste the code into a new sketch in the standard Arduino IDE on your computer.
3.  **➡️ Choose ESP32:** Make sure you have the correct ESP32 board selected under the `Tools > Board` menu.
    * *(Helpful Tip: If you have problems, make sure you've updated your ESP32 board definitions in the IDE's Boards Manager!)*
4.  **➡️ Upload:** Connect your ESP32 board to your computer and upload the code using the IDE.

---

## How to Use the Web Page 📱💻

1.  **🔌 Power On:** Connect your ESP32 to power.
2.  **🌐 Connect to Its Wi-Fi:** On your phone or computer, find and connect to the Wi-Fi network named **"don't mind me"** (password: **"@suckmydickplease"**).
3.  **➡️ Go to Web Page:** Open a web browser and go to `http://192.168.4.1/`.
4.  **🔍 Scan:** Click the **"Rescan Networks"** button on the page to see networks nearby.
5.  **🎯 Attack:** Choose a network number and reason code on the page to start an attack (Single Target), or click "Deauth All".
6.  **🛑 Stop:** Click the **"Stop Deauth Attack"** button to end any active attack and make the web page work normally again.

---

## Code (`deauth.ino`) 📄
