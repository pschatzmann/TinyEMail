Receiving calls a callback once per mail; the parts are read in a loop.

`receive()` calls a callback once for each mail. The callback gets the mail,
and its parts are read in a range based `for` loop over `mail.parts()`. The text
body is a part too, and so are every HTML alternative and every attachment.
Return `true` from the callback to get the next mail, `false` to stop. The return
value is the number of mails read, or `-1` on error.

```cpp
mail.receive([](const TinyEMail::Mail& m) {
  Serial.println(m.subject);
  for (TinyPart& part : m.parts()) {
    if (part.info.attachment) {
      // part.data.writeTo(file);               // save the file, e.g. to an SD card
    } else if (part.info.contentType == "text/plain") {
      Serial.println(part.data.readString());  // the text body, as a String
    }
  }
  return true;
}, TinyEMail::Filter::Unread);
```

`receive()` takes a `TinyEMail::Filter`: `Filter::Unread` (the default),
`Filter::Read`, `Filter::All` or `Filter::Flagged`. The mails are never marked
as read.

The mail, in the callback:

| Member | Content |
|--------|---------|
| `uid` | the UID of the mail on the server |
| `from`, `subject` | the values of the headers (RFC 2047 decoded) |
| `headers`, `header("Name")` | the header lines, and one header value (case insensitive) |
| `parts()` | the parts of the mail, for `for (TinyPart& part : m.parts())` |
| `markAsRead()`, `markAsUnread()` | ask for `\Seen` to be set or removed on the server after the mail has been read |
| `markFlagged()`, `unmarkFlagged()` | ask for `\Flagged` (starred) to be set or removed |

Each `TinyPart` in the loop:

| Member | Content |
|--------|---------|
| `info.contentType` | the type, e.g. `"text/html"` |
| `info.filename` | the file name (RFC 2231 and RFC 2047 decoded), `""` if there is none |
| `info.attachment` | `true` if the mail marks the part as an attachment |
| `data` | the decoded content, a `Stream` |

`data` is decoded while it is read (base64 and quoted-printable). Use
`readString()` for text, or `writeTo(print)` to copy it to a `File` or any other
`Print`. Do not use `readBytes()`: at the end of the data it waits for the
timeout.

The message is read as a stream, line by line. Only the header block of the
mail, one line of the message and the part being read are kept in RAM, however
large the mail is. The parts come in order, and each part can be read once:
a second `for` loop over `m.parts()` gives no parts. The parts are only available
inside the callback. A message that cannot be read (for example a line longer
than 2048 characters) is skipped with an error, and the connection stays usable.

The callback runs while the IMAP connection is open, so do not call `send()` or
`login()` inside it.

## Marking mails

`receive()` does not change the flags of a mail. In the callback you can ask for
a change, which is sent to the server after the mail has been read:

```cpp
mail.receive([](const TinyEMail::Mail& m) {
  for (TinyPart& part : m.parts()) {
    // ... read the parts
  }
  m.markAsRead();      // \Seen is set on the server
  // m.markAsUnread(); // \Seen is removed
  // m.markFlagged();  // \Flagged (starred) is set
  // m.unmarkFlagged();
  return true;
}, TinyEMail::Filter::Unread);
```

The request is sent after the parts were read. A request that fails is logged as a
warning, and the mail is not changed.
