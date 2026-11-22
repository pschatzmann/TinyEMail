// Reads the unread mails of the inbox with TinyEMail (IMAP). The example uses Gmail.
// The logger writes the steps and any error to Serial.
//
// Needs a board with TLS (for example ESP32). For Gmail, create an App Password:
// https://support.google.com/accounts/answer/185833
//
// Every mail is passed to the callback once; its parts are read in a for loop.
// The mails are read without marking them as read.

#include <TinyEMail.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

const char* WIFI_SSID = "your-wifi";
const char* WIFI_PASSWORD = "your-wifi-password";

const char* MAIL_USER = "you@gmail.com";
const char* MAIL_APP_PASSWORD = "abcdefghijklmnop";  // 16 characters, no spaces

WiFiClientSecure secure;
TinyEMail mail(secure);  // Gmail is the default server

void setup() {
  Serial.begin(115200);
  // errors, warnings and the main steps go to Serial; use Level::Debug for the protocol lines
  Logger.begin(Serial, TinyLogger::Level::Info);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  // Skips certificate checking to keep the example short.
  // For a real device, use secure.setCACert() with the server's root certificate.
  secure.setInsecure();

  if (!mail.login(MAIL_USER, MAIL_APP_PASSWORD)) {
    Serial.print("Login failed: ");
    Serial.println(mail.lastReply());
    return;
  }

  // called once for each unread mail; return false to stop
  int count = mail.receive([](const TinyEMail::Mail& m) {
    Serial.print("From: ");
    Serial.print(m.from);
    Serial.print("  Subject: ");
    Serial.println(m.subject);

    // every part of the mail: the text body, HTML alternatives and attachments
    for (TinyPart& part : m.parts()) {
      if (part.info.attachment) {
        Serial.print("  attachment ");
        Serial.print(part.info.filename);
        Serial.print(" (");
        Serial.print(part.info.contentType);
        Serial.println(")");
        // part.data.writeTo(file);             // save it, e.g. to an SD card
      } else if (part.info.contentType.startsWith("text/")) {
        Serial.println(part.data.readString());  // text body or HTML part
      }
    }
    return true;
  });
  if (count < 0) {
    Serial.print("Receiving failed: ");
    Serial.println(mail.lastReply());
    return;
  }
  Serial.print(count);
  Serial.println(" unread mails");
}

void loop() {}
