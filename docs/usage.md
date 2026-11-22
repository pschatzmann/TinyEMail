# Usage

Login, send, receive and the servers the client talks to.

## Quick start

```cpp
#include <TinyEMail.h>
#include <WiFiClientSecure.h>

WiFiClientSecure secure;
TinyEMail mail(secure);  // Gmail by default

void setup() {
  Serial.begin(115200);
  Logger.begin(Serial, TinyLogger::Level::Info);

  // Gmail App Password: https://support.google.com/accounts/answer/185833
  mail.login("me@gmail.com", "abcdefghijklmnop");

  mail.send("you@example.com", "Hello", "Sent from an ESP32.");

  mail.receive([](const TinyEMail::Mail& m) {
    for (TinyPart& part : m.parts()) {
      if (part.info.contentType == "text/plain") Serial.println(part.data.readString());
    }
    return true;                          // false stops
  });
}
```

`login()`, `send()` and `receive()` return `false` (or `-1`) on failure. The
reason is written to the logger, and it is also in `mail.lastReply()` and
`mail.lastReplyCode()`.

## Other mail servers

```cpp
TinyEMail mail(secure);
mail.setSmtpServer("smtp.example.org", 465);  // implicit TLS
mail.setImapServer("imap.example.org", 993);  // implicit TLS
mail.login("me@example.org", "password");
```

The server has to accept `AUTH PLAIN` over TLS on these ports. STARTTLS
(ports 587 and 143) is not supported yet.
