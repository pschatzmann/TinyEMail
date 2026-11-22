# Text and encodings

How text, charsets and encoded words are handled.

- Text is returned as it is in the mail. Charsets UTF-8, US-ASCII and ISO-8859-1
  are converted to UTF-8; other charsets are kept as they are, with one warning
  per charset.
- `header()` and the `from`/`subject` fields are RFC 2047 decoded.
- File names are decoded from RFC 2231 (`filename*=`) or RFC 2047.
- The text of a sent mail is quoted-printable; the sent subject is written as
  it is given (UTF-8). RFC 2047 encoding of a sent subject is not done yet.
