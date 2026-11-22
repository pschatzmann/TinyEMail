# Limits and timeouts

Limits on what a server may send, and the timeouts.

| Setting | Default | Meaning |
|---------|---------|---------|
| `setTimeout(ms)` | 10 s | how long to wait for the server |
| `setMaxMessageSize(bytes)` | 0 (no limit) | a larger mail is skipped with a warning. Messages are streamed, so their size needs no RAM; the limit only saves time |

Lines from the server are limited as well: 1000 characters for SMTP replies,
and 32 KB for IMAP lines (a `SEARCH` answer with many UIDs is one long line).
A line of a received message is limited to 2048 characters: such a mail is
skipped with an error. When the connection breaks or the server times out,
`receive()` stops and logs an error instead of waiting for each remaining mail.
