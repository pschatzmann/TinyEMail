# TinyEMail

[![Arduino Library](https://img.shields.io/badge/Arduino-Library-blue.svg)](https://www.arduino.cc/reference/en/libraries/)
[![CMake](https://img.shields.io/badge/CMake-Supported-blue.svg)](https://github.com/pschatzmann/TinyEMail/blob/main/CMakeLists.txt)
[![License: Apache](https://img.shields.io/badge/License-Apache-yellow.svg)](https://opensource.org/licenses/Apache-2.0)

A generic e-mail client for Arduino: it sends and receives mail through SMTP and
IMAP servers over TLS. Gmail is the default configuration. The library is
header-only and works with any Arduino `Client` that supports TLS, e.g.
`WiFiClientSecure` on the ESP32.

```cpp
#include <TinyEMail.h>
#include <WiFiClientSecure.h>

WiFiClientSecure secure;
TinyEMail mail(secure);

void setup() {
  mail.login("me@gmail.com", "abcdefghijklmnop");   // Gmail App Password
  mail.send("you@example.com", "Hello", "Sent from an ESP32.");

  mail.receive([](const TinyEMail::Mail& m) {       // unread mails
    for (TinyPart& part : m.parts()) {
      if (part.info.contentType == "text/plain") Serial.println(part.data.readString());
    }
    return true;                                    // false stops
  });
}
```

## Status

- Desktop: unit tests with a mock client, run with AddressSanitizer and UBSan.
- Mail server: login, send, receive and flags tested against GreenMail, over plain TCP and TLS.
- ESP32: a test sketch logs in, sends, receives and sets flags over TLS against a mail server on the local network.
- Not tested yet: a live Gmail account, certificate checking, and STARTTLS (not implemented).

## Documentation

- [Usage](docs/usage.md): login, send, receive, and the servers the client talks to
- [Sending mails](docs/sending.md): recipients, attachments, streams and streamed bodies
- [Receiving mails](docs/receiving.md): the callback, the parts of a mail and flags
- [Logging](docs/logging.md): log levels and what is logged
- [Limits and timeouts](docs/limits.md): size limits and timeouts
- [Text and encodings](docs/encodings.md): charsets, base64, quoted-printable, encoded words
- [Classes](docs/classes.md): the classes of the library
- [Examples](docs/examples.md): the example sketches
- [Desktop build](docs/desktop-build.md): CMake, unit tests and the mail server test
- [Notes](docs/notes.md): limitations
- [Class documentation (Doxygen)](https://pschatzmann.github.io/TinyEMail/annotated.html): API reference for all classes

## Installation in Arduino

You can download the library as zip and call include Library -> zip library. Or you can git clone this project into the Arduino libraries folder e.g. with

```
cd  ~/Documents/Arduino/libraries
git clone https://github.com/pschatzmann/TinyEMail.git
```

I recommend to use git because you can easily update to the latest version just by executing the ```git pull``` command in the project folder.

The emulator and the test mail server are only needed for the desktop build, see [Desktop build](docs/desktop-build.md).

## License

Apache-2.0, see [LICENSE](LICENSE).
