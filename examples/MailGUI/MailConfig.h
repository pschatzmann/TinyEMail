/**
 * @file MailConfig.h
 * @brief Settings of the MailGUI example. Replace the placeholders with your
 * own values. Do not commit real credentials.
 */
#pragma once

// WiFi (ESP32 only; the desktop build uses the network of the computer)
#define MAILGUI_WIFI_SSID "your-ssid"
#define MAILGUI_WIFI_PASSWORD "your-password"

// Mail account used by the Fetch button (Gmail: use an App Password)
#define MAILGUI_MAIL_ADDRESS "me@gmail.com"
#define MAILGUI_MAIL_PASSWORD "abcdefghijklmnop"

// Chip select of the SD card used for the mail folders (ESP32 only).
// Adjust it to the SD slot of your board.
#define MAILGUI_SD_CS 10
