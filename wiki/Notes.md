What the library does not do yet.

- TLS only (SMTPS on port 465, IMAPS on port 993). STARTTLS and OAuth2 are not
  supported.
- Authentication with `AUTH PLAIN` (SMTP) and `LOGIN` (IMAP).
- Charsets other than UTF-8, US-ASCII and ISO-8859-1 are not converted.
- Received parts are forward only: a part is read once, in order.
- A line of a received message is limited to 2048 characters.
- Several To and Bcc recipients are supported; there is no Cc header.
- A received mail is streamed, but its parts can be read only once, in order.
- The password is kept in memory after `login()`.
- The examples turn off certificate checking (`setInsecure()`) to keep them
  short. Use `setCACert()` on real devices.
