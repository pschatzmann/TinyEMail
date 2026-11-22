# Examples

The example sketches in the examples folder.

- `examples/Send/Send.ino`: logs in and sends a mail with a text body and an
  attachment.
- `examples/Receive/Receive.ino`: logs in and prints the unread mails. For each
  mail it shows the sender and the subject, the text and HTML parts as text, and
  the file name and type of each attachment.

Both compile for the ESP32 with the Arduino ESP32 core 4.0.0 and `arduino-cli`.
The library was also run on an ESP32 with a test sketch that logs in, sends,
receives and sets flags against a mail server on the local network. That sketch
is not part of this repository.
