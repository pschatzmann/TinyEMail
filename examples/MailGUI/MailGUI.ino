/**
 * @file MailGUI.ino
 * @brief E-mail GUI client, built on TinyEMailGUI (src/TinyEMailGUI.h).
 *
 * The mails are stored in folders on the SD card and rendered from there. The
 * menu has Fetch (download the unread mails) and Exit. A failed login opens
 * the login screen.
 *
 * Runs on the desktop (SDL2 window, via TinyMaterialDesign) and on the
 * Hosyond ESP32-S3 2.8" board (ILI9341, 240x320). Set the account and WiFi
 * in MailConfig.h.
 */
#include <TinyEMailGUI.h>

#include "MailConfig.h"
#if !defined(ESP32)
#include "TestData.h"
#endif

// ---- board ------------------------------------------------------------------
#if defined(ESP32)
tinygpu::LCDBoardESP32S3_2_8Display board;
#else
tinygpu::LCDBoardDesktopSDL board(240, 320);
#endif

App<RGB565> app(board);
// the mails are stored by the GUI: on the SD card (ESP32) or in a disk image file (desktop)
TinyEMailGUI gui(app, TinyEMailGUI::Config{MAILGUI_MAIL_ADDRESS, MAILGUI_MAIL_PASSWORD,
                                           MAILGUI_SD_CS, "mails.img"});

void setup() {
  Serial.begin(115200);
  app.begin();
  gui.begin();
#if !defined(ESP32)
  seedTestMails(gui.store());  // desktop only: test mails if the store is empty
  gui.refresh();               // the list was built before the test mails were added
#endif
}

void loop() {
  gui.tick();
  app.update();
}
