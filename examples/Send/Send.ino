// Sends a mail with TinyEMail (SMTP): a text body and an attachment.
// The example uses Gmail. The logger writes the steps and any error to Serial.
//
// Needs a board with TLS (for example ESP32). For Gmail, create an App Password:
// https://support.google.com/accounts/answer/185833

#include <TinyEMail.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

const char* WIFI_SSID = "your-wifi";
const char* WIFI_PASSWORD = "your-wifi-password";

const char* MAIL_USER = "you@gmail.com";
const char* MAIL_APP_PASSWORD = "abcdefghijklmnop";  // 16 characters, no spaces
const char* RECIPIENT = "someone@example.com";

const char* BODY = "The report is attached. This mail was sent by TinyEMail.";
const char* REPORT = "time,temperature\r\n08:00,21.5\r\n09:00,22.0\r\n";

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

  // The attachments: the first one is the text body, the others are files.
  // Every attachment is read from a Stream: TinyTextStream for text, TinyBufferStream
  // for binary data in memory, or an opened File.
  TinyTextStream bodyText(BODY);
  TinyTextStream reportText(REPORT);
  TinyVector<TinyEMail::Attachment> parts(2);

  parts[0].contentType = "text/plain";  // or "text/html" for an HTML body
  parts[0].stream = &bodyText;

  parts[1].filename = "report.csv";  // a file from memory
  parts[1].contentType = "text/csv";
  parts[1].stream = &reportText;
  // A file from an SD card is a stream too, e.g. (add a third entry to parts):
  //   File photo = SD.open("/photo.jpg");
  //   parts[2].filename = "photo.jpg";
  //   parts[2].contentType = "image/jpeg";
  //   parts[2].stream = &photo;                 // read until its end

  TinyEMail::Mail message;
  message.to.push_back(String(RECIPIENT));
  // more recipients: message.to.push_back(String("second@example.com"));
  // blind copies, not shown in the headers: message.bcc.push_back(String("archive@example.com"));
  message.subject = "Report from an Arduino";
  message.attachments = parts;

  if (mail.send(message)) {
    Serial.println("Mail sent");
  } else {
    Serial.print("Sending failed: ");
    Serial.println(mail.lastReply());
  }
}

void loop() {}
