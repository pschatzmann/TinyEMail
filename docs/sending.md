# Sending mails

Recipients, attachments, streams and streamed bodies.

A mail to send is a `TinyEMail::Mail` (`TinyMail`). Its `to` is a list of
recipient addresses (the `To` header), its `bcc` a list of blind copy addresses
(sent to the server, but not in the headers), its `subject` the subject, and its
`attachments` a `TinyVector` of `TinyEMail::Attachment`. The **first attachment is
the text body** and the others are files. The body's `contentType` decides how it is sent: `text/plain`
(the default for `send(to, subject, body)`) or `text/html`.

Every attachment is read from a `Stream`:

| Stream | Use |
|--------|-----|
| `TinyTextStream` | a text, e.g. `TinyTextStream body("Hello");` |
| `TinyBufferStream` | binary data in memory, e.g. `TinyBufferStream data(bytes, size);` |
| `File` (SD card) | a file on disk; it is read until its end |

```cpp
#include <SD.h>

TinyTextStream bodyText("The photo is attached.");
TinyBufferStream note(reinterpret_cast<const uint8_t*>("hello"), 5);
File photo = SD.open("/photo.jpg");

TinyVector<TinyEMail::Attachment> files(3);
files[0].contentType = "text/plain";             // the text body comes first
files[0].stream = &bodyText;
files[1].filename = "hello.txt";                 // a file from memory
files[1].contentType = "text/plain";
files[1].stream = &note;
files[2].filename = "photo.jpg";                 // a file from an SD card
files[2].contentType = "image/jpeg";
files[2].stream = &photo;

TinyEMail::Mail mail_out;
mail_out.to.push_back(String("you@example.com"));
mail_out.subject = "Photo and notes";
mail_out.attachments = files;

mail.send(mail_out);
photo.close();
```

For an HTML body, set `files[0].contentType = "text/html";` and write HTML as
the text.

Without files the mail is plain text. With files it is MIME `multipart/mixed`.
The text body is sent as quoted-printable, so long lines are split and the
characters that SMTP cannot carry are encoded. Files are base64 encoded. The
name of a file must not contain `"` or line breaks, and the first attachment
must be `text/*`. A stream is read until it returns -1: a stream that returns -1
before its data is finished ends the file early.

## Several recipients and Bcc

```cpp
TinyEMail::Mail mail_out;
mail_out.to.push_back(String("a@example.com"));    // To: a@example.com, b@example.com
mail_out.to.push_back(String("b@example.com"));
mail_out.bcc.push_back(String("archive@example.com")); // not shown in the headers
mail_out.subject = "Team";
// ... attachments as above
mail.send(mail_out);
```

A mail with only Bcc recipients gets `To: undisclosed-recipients:;`.

## Sending from a stream

`send(to, subject, stream, contentType)` copies the body from any `Stream`,
for example a file on an SD card, while it is sent:

```cpp
File page = SD.open("/report.html");
mail.send("you@example.com", "Report", page, "text/html");
page.close();
```

## Writing the body while it is sent

`sendWith()` sends the headers, then calls your writer with the body as a
`Print`. Whatever you write goes straight to the server (quoted-printable,
dot-stuffed), so no part of the mail is kept in RAM. `TinyHtml` writes escaped
HTML to any `Print`:

```cpp
mail.sendWith("you@example.com", "Report", "text/html", [](Print& body) {
  TinyHtml html(body);
  html.heading(1, "Report").paragraph("Temperature: 21.5 C")
      .link("https://example.com", "details").lineBreak()
      .beginList().item("first").item("second").endList();
});
```

`sendWith()` also takes a `TinyEMail::Mail` with the recipients (`to` and `bcc`)
and a content type: `sendWith(mail_out, "text/html", writer)`. Such a mail must not
have attachments: the body is the writer's output.
