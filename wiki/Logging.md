The logger shows the steps of the library and what fails.

`Logger` is the logger of the library. Call `begin()` to choose where the
messages go and how much is written:

```cpp
Logger.begin(Serial, TinyLogger::Level::Info);  // Serial and Info are the defaults
```

| Level | Written for |
|-------|-------------|
| `Error` | a step failed: no connection, a rejected login, a broken connection, a message that cannot be read |
| `Warn` | something unexpected that the library handled: a server reply that was not expected, a mail skipped because it is over the size limit, a MIME part that is not supported |
| `Info` | the main steps: login accepted, mail accepted, number of mails read |
| `Debug` | the protocol lines and the reply of the server |

Until `begin()` is called nothing is written. Passwords are never written, not
even base64 encoded, and the content of mails is never written. Messages look
like `[1234] E send: RCPT TO failed: 550 ...`.

On the desktop the Arduino-Emulator also has a `Logger` object, so write
`TinyLogger::instance()` there instead. The library itself always uses
`TinyLogger::instance()`.
