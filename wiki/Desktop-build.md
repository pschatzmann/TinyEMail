Build the library and its tests on the desktop, and test against a mail server.

The desktop build uses [Arduino-Emulator](https://github.com/pschatzmann/Arduino-Emulator),
which CMake fetches when the tests are built.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The test suites are `test_tinyemail` (SMTP and the facade), `test_tinyimap`,
`test_tinymime`, `test_tinyvector` and `test_robustness` (logging, limits,
connection checks, MIME fixes and decoders).

## Integration test (GreenMail)

`tests/integration/test_greenmail.cpp` runs login, send (plain and HTML), receive,
flags and the filters against a real mail server. It is not built by default.
The server is GreenMail, started with Docker:

```sh
docker run -d --name greenmail-test -p 3025:3025 -p 3143:3143 -p 8080:8080 \
  -e GREENMAIL_OPTS='-Dgreenmail.setup.test.smtp -Dgreenmail.setup.test.imap -Dgreenmail.hostname=0.0.0.0' \
  greenmail/standalone:2.1.2
curl -X POST -H 'Content-Type: application/json' \
  -d '{"email":"me@localhost","login":"me@localhost","password":"secret"}' \
  http://127.0.0.1:8080/api/user
cmake -S . -B build -DTINYEMAIL_INTEGRATION=ON
cmake --build build --target test_greenmail
curl -X POST http://127.0.0.1:8080/api/service/reset   # empty mailboxes before each run
./build/tests/test_greenmail
```

The test uses plain TCP (`WiFiClient`), and settings come from the environment:
`GREENMAIL_HOST` (127.0.0.1), `GREENMAIL_SMTP` (3025), `GREENMAIL_IMAP` (3143).
GreenMail accepts any password, so a wrong password cannot be tested against it.

TLS (`WiFiClientSecure`, SMTPS 3465 and IMAPS 3993) is built with `-DTINYEMAIL_TLS=ON`
as `test_greenmail_tls`. It uses the TinyTLS client (TLS 1.3) from the local
TinyTLS checkout (`-DFETCHCONTENT_SOURCE_DIR_TINYTLS=/path/to/TinyTLS`; without it
TinyTLS is fetched from GitHub). The container needs the SMTPS and IMAPS ports
published (`-p 3465:3465 -p 3993:3993`). The test uses `setInsecure()`, because
GreenMail has a self-signed certificate. Certificate checking is therefore not
tested.

| Option | Default | Meaning |
|--------|---------|---------|
| `TINYEMAIL_BUILD_TESTS` | ON (top level) | Unit tests (`ctest`) |
| `TINYEMAIL_BUILD_DOCS` | ON (top level) | `docs` target that runs Doxygen |
| `FETCHCONTENT_SOURCE_DIR_ARDUINO_EMULATOR` | fetched | Local Arduino-Emulator checkout |
| `TINYEMAIL_TLS` | OFF | Build the emulator with TLS and the `test_greenmail_tls` test |
| `TINYEMAIL_INTEGRATION` | OFF | Build the mail server tests (`test_greenmail`, and `test_greenmail_tls` with TLS) |

Doxygen HTML is written to `docs/html`:

```sh
cmake --build build --target docs
```

On the desktop, a program that includes `TinyEMail.h` needs an Arduino core,
so link `arduino_emulator` as well:

```cmake
find_package(TinyEMail 0.1 REQUIRED)
target_link_libraries(myapp PRIVATE TinyEMail::TinyEMail arduino_emulator)
```

On Arduino boards the core provides the Arduino API, so only
`TinyEMail::TinyEMail` is needed.
