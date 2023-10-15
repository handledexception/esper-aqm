# esper-aqm

Open-source air-quality monitor built upon the LilyGo ESP32 T-Display-S3.
- Utilizes the Sensirion Sen55 air quality sensor
- Optional support for the PMS5003 air quality sensor.
- Optional support for the MCP9808 temperature sensor.
- Optional support for the HiLetGo (or compatible) HD44780 IIC I2C1602 LCD Display

# Prerequisites
1. Install esp-idf-v4.4.4.
2. Connect components to the LilyGo ESP32 T-Display-S3 as shown in the diagram.

# WiFi Configuration
1. Install esp-idf-v4.4.4.
2. Open an esp-idf command line.
3. Run `idf.py menuconfig`.
4. Select `Component Config`.
5. Select `Example Configuration`.
6. Set the Wi-Fi SSID, Password and authentication type according to your Wi-Fi access point settings.

# Building
1. Open an esp-idf command line
2. Run `idf.py build`
3. Connect the LilyGo T-Display-S3 to USB
4. Run `idf.py flash`, with optional specification of the COM port and baud rate

# VSCode ESP-IDF Terminal
1. Ensure esp-idf v4.4.4 is installed in C:\Espressif\frameworks\esp-idf-v4.4.4
2. Open esper-aqm as a folder in VSCode
3. Open the Terminal window

If ESP-IDF is installed in a different location, edit the path to "C:/Espressif/idf_cmd_init.bat" in .vscode/settings.json file. This batch file is responsible for configuring the Terminal as an ESP-IDF terminal.
