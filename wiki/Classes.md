The classes of the library.

| Class | What it is |
|-------|------------|
| `TinyEMail` | The main class: `login()`, `send()`, `receive()`. It is not a `Stream`. |
| `TinySMTP` | SMTP connection: a `Stream` and `Print` with the SMTP commands. |
| `TinyIMAP` | IMAP connection: a `Stream` and `Print` with `LOGIN`, `SELECT`, `UID SEARCH`, `UID FETCH`, `UID STORE` (`setFlag()`), `LOGOUT`. |
| `TinyMIME` | Splits a received message into its parts, for a stream or a string. |
| `TinyMIMEReader`, `TinyLineSource` | Reads the parts of a message as a stream, line by line (used by `receive()`). |
| `TinyHtml` | Writes escaped HTML to a `Print`, e.g. the body of a mail. |
| `TinyMail`, `TinyMailAttachment` | The mail and the attachment (`TinyEMail::Mail` and `TinyEMail::Attachment` are aliases). |
| `TinyPart`, `TinyMailParts` | A part of a received mail, and the parts of a mail for the `for` loop (forward only). |
| `TinyMailPart`, `TinyMailData` | The metadata of a part, and its decoded data (a `Stream`). |
| `TinyTextStream`, `TinyBufferStream` | Read only streams over a text and over bytes in memory. |
| `TinyVector` | A small vector; its memory is taken from PSRAM when the board has it. |
| `TinyLogger` | The logger, with `Logger` as the static object. |

`TinyEMail.h` includes everything. The classes are in `src/TinyEMail/`, so
`TinySMTP` is `#include <TinyEMail/TinySMTP.h>` when used alone.

`TinySMTP` and `TinyIMAP` are `Stream`s and `Print`s, so they can also be used
for other commands. For example, with `TinySMTP`:

```cpp
TinySMTP smtp(secure);
if (smtp.begin() && smtp.readReply() == 220 && smtp.ehlo("esp32")) {
  smtp.quit();
}
smtp.end();
```

`TinyVector` takes its memory from PSRAM when the board has it (ESP32 boards
with PSRAM enabled), and from the normal heap otherwise. Define
`TINY_EMAIL_USE_PSRAM` to use PSRAM in any case.
