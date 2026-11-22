// Integration test against a real mail server (GreenMail in Docker, see README).
// Uses plain TCP (no TLS): the desktop build has no TLS client.
// Settings from the environment: GREENMAIL_HOST (127.0.0.1), GREENMAIL_SMTP (3025), GREENMAIL_IMAP (3143)

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <Arduino.h>

// the same test over plain TCP, or over TLS with WiFiClientSecure (the TLS test
// is built with TINYEMAIL_GREENMAIL_TLS=1; GreenMail uses a self-signed certificate)
#if defined(TINYEMAIL_GREENMAIL_TLS) && TINYEMAIL_GREENMAIL_TLS
#include <WiFiClientSecure.h>
using TestClient = WiFiClientSecure;
static const uint16_t kDefaultSmtp = 3465;  // SMTPS
static const uint16_t kDefaultImap = 3993;  // IMAPS
#else
#include <WiFiClient.h>
using TestClient = WiFiClient;
static const uint16_t kDefaultSmtp = 3025;
static const uint16_t kDefaultImap = 3143;
#endif

#include "TinyEMail.h"

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    ++g_checks;                                                              \
    if (!(cond)) {                                                           \
      ++g_failures;                                                          \
      std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond  \
                << "\n";                                                     \
    }                                                                        \
  } while (0)

// the mail server and the test account (created by the container, see README)
static const char* kUser = "me@localhost";
static const char* kPassword = "secret";

int main() {
  const char* envHost = getenv("GREENMAIL_HOST");
  const char* envSmtp = getenv("GREENMAIL_SMTP");
  const char* envImap = getenv("GREENMAIL_IMAP");
  std::string host = envHost != nullptr ? envHost : "127.0.0.1";
  uint16_t smtpPort = static_cast<uint16_t>(envSmtp != nullptr ? atoi(envSmtp) : kDefaultSmtp);
  uint16_t imapPort = static_cast<uint16_t>(envImap != nullptr ? atoi(envImap) : kDefaultImap);

  TinyLogger::instance().begin(Serial, TinyLogger::Level::Debug);  // the protocol lines
  TestClient client;
#if defined(TINYEMAIL_GREENMAIL_TLS) && TINYEMAIL_GREENMAIL_TLS
  client.setInsecure();  // the self-signed certificate of the test server
#endif
  TinyEMail mail(client);
  mail.setSmtpServer(host.c_str(), smtpPort);
  mail.setImapServer(host.c_str(), imapPort);
  mail.setTimeout(5000);

  // GreenMail accepts any password, so a wrong password cannot be checked here
  CHECK(mail.login(kUser, kPassword));

  // a plain text mail
  CHECK(mail.send(kUser, "integration plain", "hello from TinyEMail"));

  // an HTML mail, written while it is sent
  CHECK(mail.sendWith(kUser, "integration html", "text/html", [](Print& body) {
    body.print("<p>Hi <b>there</b> &amp; welcome</p>");
  }));

  // both mails are received; the plain one is marked read
  std::vector<std::string> subjects;
  std::vector<std::string> bodies;
  int count = mail.receive([&](const TinyEMail::Mail& m) {
    subjects.push_back(std::string(m.subject.c_str()));
    for (TinyPart& part : m.parts()) {
      if (part.info.contentType == "text/plain") {
        bodies.push_back(std::string(part.data.readString().c_str()));
      }
    }
    if (m.subject == "integration plain") {
      m.markAsRead();
    }
    return true;
  });
  CHECK(count == 2);
  CHECK(subjects.size() == 2);
  CHECK(bodies.size() >= 1 && bodies[0].find("hello from TinyEMail") != std::string::npos);

  // after the flag was set, only the HTML mail is unread
  int unread = mail.receive([&](const TinyEMail::Mail& m) {
    CHECK(m.subject == "integration html");
    return true;
  }, TinyEMail::Filter::Unread);
  CHECK(unread == 1);

  int read = mail.receive([](const TinyEMail::Mail& m) {
    CHECK(m.subject == "integration plain");
    return true;
  }, TinyEMail::Filter::Read);
  CHECK(read == 1);

  std::cout << g_checks << " checks, " << g_failures << " failures\n";
  return g_failures == 0 ? 0 : 1;
}
