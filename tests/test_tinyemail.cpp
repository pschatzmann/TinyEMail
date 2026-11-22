// Desktop unit tests for TinyEMail. Uses the Arduino-Emulator for the Arduino
// core headers (Client, Stream, Print) and an in-memory client instead of a
// network connection. No external test framework needed.

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include <Arduino.h>

#include "MockClient.h"
#include "TinyEMail.h"
#include "TinyEMail/TinySMTP.h"
#include "TinyEMail/TinyVector.h"

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

static void testDefaultsAreGmail() {
  CHECK(std::strcmp(TinySMTP::gmailSmtpHost, "smtp.gmail.com") == 0);
  CHECK(TinySMTP::gmailSmtpPort == 465);
  CHECK(std::strcmp(TinyIMAP::gmailImapHost, "imap.gmail.com") == 0);
  CHECK(TinyIMAP::gmailImapPort == 993);
}

static void testBeginDefaultsToGmailSmtp() {
  MockClient client;
  TinySMTP mail(client);

  CHECK(!client.open);  // the connection is closed after login
  CHECK(mail.begin());
  CHECK(client.connectCalls == 1);
  CHECK(client.connectedHost == "smtp.gmail.com");
  CHECK(client.connectedPort == 465);
  CHECK(mail.connected());
  CHECK(static_cast<bool>(mail));
  CHECK(std::strcmp(mail.host(), "smtp.gmail.com") == 0);
  CHECK(mail.port() == 465);
}

static void testBeginWithImap() {
  MockClient client;
  TinySMTP mail(client);

  CHECK(mail.begin(TinyIMAP::gmailImapHost, TinyIMAP::gmailImapPort));
  CHECK(client.connectedHost == "imap.gmail.com");
  CHECK(client.connectedPort == 993);
}

static void testBeginFails() {
  MockClient client;
  client.connectResult = false;
  TinySMTP mail(client);

  CHECK(!mail.begin());
  CHECK(!client.open);  // the connection is closed after login
  CHECK(!static_cast<bool>(mail));
}

static void testBeginWithNullHostFails() {
  MockClient client;
  TinySMTP mail(client);

  CHECK(!mail.begin(nullptr));
  CHECK(client.connectCalls == 0);
}

static void testEndClosesConnection() {
  MockClient client;
  TinySMTP mail(client);
  mail.begin();

  mail.end();
  CHECK(client.stopCalls == 1);
  CHECK(!client.open);  // the connection is closed after login

  // ending twice is harmless
  mail.end();
  CHECK(client.stopCalls == 2);
}

static void testPrintWritesToServer() {
  MockClient client;
  TinySMTP mail(client);
  mail.begin();

  mail.print("EHLO ");
  mail.println("esp32");
  mail.write('Q');
  const uint8_t bytes[] = {'U', 'I', 'T', '\r', '\n'};
  mail.write(bytes, sizeof(bytes));

  CHECK(client.written == "EHLO esp32\r\nQUIT\r\n");
}

static void testPrintWithoutConnectionWritesNothing() {
  MockClient client;
  TinySMTP mail(client);

  CHECK(mail.print("EHLO") == 0);
  CHECK(client.written.empty());
}

static void testStreamReadsFromServer() {
  MockClient client;
  client.incoming = "220 smtp.gmail.com ESMTP\r\n250 OK";
  TinySMTP mail(client);
  mail.begin();
  mail.setTimeout(20);  // readString() waits for the timeout at the end of the data

  CHECK(mail.available() == static_cast<int>(client.incoming.size()));
  CHECK(mail.peek() == '2');
  CHECK(mail.peek() == '2');  // peek does not consume
  CHECK(mail.read() == '2');

  String line = mail.readStringUntil('\n');
  CHECK(line == "20 smtp.gmail.com ESMTP\r");

  String rest = mail.readString();
  CHECK(rest == "250 OK");
  CHECK(mail.available() == 0);
  CHECK(mail.read() == -1);
  CHECK(mail.peek() == -1);
}

static void testStreamWithoutConnectionReadsNothing() {
  // a client that was never connected has no data to deliver
  MockClient client;
  TinySMTP mail(client);

  CHECK(mail.available() == 0);
  CHECK(mail.read() == -1);
  CHECK(mail.peek() == -1);
}

static void testEchoRoundTrip() {
  MockClient client;
  client.incoming = "250 pong\r\n";
  TinySMTP mail(client);
  mail.begin();

  mail.print("PING\r\n");
  String reply = mail.readStringUntil('\n');

  CHECK(client.written == "PING\r\n");
  CHECK(reply == "250 pong\r");
}

// ---- SMTP ------------------------------------------------------------------

static void testReadReplySingleLine() {
  MockClient client;
  client.incoming = "220 smtp.gmail.com ESMTP ready\r\n";
  TinySMTP mail(client);
  mail.begin();

  CHECK(mail.readReply() == 220);
  CHECK(mail.lastReplyCode() == 220);
  CHECK(mail.lastReply() == "220 smtp.gmail.com ESMTP ready");
}

static void testReadReplyMultiLine() {
  MockClient client;
  client.incoming =
      "250-smtp.gmail.com at your service\r\n250-AUTH PLAIN LOGIN\r\n250 8BITMIME\r\n";
  TinySMTP mail(client);
  mail.begin();

  CHECK(mail.readReply() == 250);
  CHECK(mail.lastReply() ==
        "250-smtp.gmail.com at your service\n250-AUTH PLAIN LOGIN\n250 8BITMIME");
  CHECK(mail.available() == 0);
}

static void testReadReplyInvalid() {
  MockClient client;
  client.incoming = "hello there\r\n";
  TinySMTP mail(client);
  mail.begin();

  CHECK(mail.readReply() == -1);
  CHECK(mail.lastReplyCode() == -1);
}

static void testReadReplyTimeout() {
  MockClient client;  // the server never answers
  TinySMTP mail(client);
  mail.begin();
  mail.setTimeout(20);

  CHECK(mail.readReply() == -1);
}

static void testEhloSendsCommand() {
  MockClient client;
  client.incoming = "250 OK\r\n";
  TinySMTP mail(client);
  mail.begin();

  CHECK(mail.ehlo("esp32"));
  CHECK(client.written == "EHLO esp32\r\n");
}

static void testEhloRejected() {
  MockClient client;
  client.incoming = "502 not implemented\r\n";
  TinySMTP mail(client);
  mail.begin();

  CHECK(!mail.ehlo("esp32"));
  CHECK(mail.lastReplyCode() == 502);
}

static void testAuthPlainSendsBase64() {
  // base64 of: NUL user@gmail.com NUL abcdefghijklmnop
  MockClient client;
  client.incoming = "235 2.7.0 Accepted\r\n";
  TinySMTP mail(client);
  mail.begin();

  CHECK(mail.authPlain("user@gmail.com", "abcdefghijklmnop"));
  CHECK(client.written ==
        "AUTH PLAIN AHVzZXJAZ21haWwuY29tAGFiY2RlZmdoaWprbG1ub3A=\r\n");
}

static void testAuthPlainShortCredentials() {
  // base64 of: NUL a NUL b (with padding)
  MockClient client;
  client.incoming = "235 ok\r\n";
  TinySMTP mail(client);
  mail.begin();

  CHECK(mail.authPlain("a", "b"));
  CHECK(client.written == "AUTH PLAIN AGEAYg==\r\n");
}

static void testAuthPlainRejected() {
  MockClient client;
  client.incoming = "535 5.7.8 Username and Password not accepted\r\n";
  TinySMTP mail(client);
  mail.begin();

  CHECK(!mail.authPlain("user@gmail.com", "wrong"));
  CHECK(mail.lastReplyCode() == 535);
}

static void testAuthPlainTooLong() {
  MockClient client;
  TinySMTP mail(client);
  mail.begin();
  std::string longPassword(TinySMTP::maxCredentialsSize, 'x');

  CHECK(!mail.authPlain("user@gmail.com", longPassword.c_str()));
  CHECK(client.written.empty());
}

static void testMailFromAndRcptTo() {
  MockClient client;
  client.incoming = "250 OK\r\n250 OK\r\n";
  TinySMTP mail(client);
  mail.begin();

  CHECK(mail.mailFrom("me@gmail.com"));
  CHECK(mail.rcptTo("you@example.com"));
  CHECK(client.written ==
        "MAIL FROM:<me@gmail.com>\r\nRCPT TO:<you@example.com>\r\n");
}

static void testRcptToForwarded() {
  MockClient client;
  client.incoming = "251 User not local; will forward\r\n";
  TinySMTP mail(client);
  mail.begin();

  CHECK(mail.rcptTo("you@example.com"));
}

static void testDataEscapesDotsAndUsesCrlf() {
  MockClient client;
  client.incoming = "354 go ahead\r\n250 queued\r\n";
  TinySMTP mail(client);
  mail.begin();

  CHECK(mail.data("Subject: hi\n\n.dot line\r\nlast"));
  CHECK(client.written ==
        "DATA\r\nSubject: hi\r\n\r\n..dot line\r\nlast\r\n.\r\n");
}

static void testDataRejectedBeforeBody() {
  MockClient client;
  client.incoming = "554 no recipients\r\n";
  TinySMTP mail(client);
  mail.begin();

  CHECK(!mail.data("Subject: hi\r\n\r\nbody\r\n"));
  CHECK(client.written == "DATA\r\n");  // the body is not sent
}

static void testQuit() {
  MockClient client;
  client.incoming = "221 bye\r\n";
  TinySMTP mail(client);
  mail.begin();

  CHECK(mail.quit());
  CHECK(client.written == "QUIT\r\n");
}

static void testFullSession() {
  MockClient client;
  client.incoming =
      "220 smtp.gmail.com ESMTP ready\r\n"
      "250-smtp.gmail.com at your service\r\n"
      "250-AUTH PLAIN LOGIN\r\n"
      "250 8BITMIME\r\n"
      "235 2.7.0 Accepted\r\n"
      "250 2.1.0 OK\r\n"
      "250 2.1.5 OK\r\n"
      "354 Go ahead\r\n"
      "250 2.0.0 OK queued\r\n"
      "221 2.0.0 closing connection\r\n";
  TinySMTP mail(client);

  CHECK(mail.begin());
  CHECK(mail.readReply() == 220);
  CHECK(mail.ehlo("esp32"));
  CHECK(mail.authPlain("me@gmail.com", "abcdefghijklmnop"));
  CHECK(mail.mailFrom("me@gmail.com"));
  CHECK(mail.rcptTo("you@example.com"));
  CHECK(mail.data("Subject: Hello\r\n\r\nSent from an ESP32.\r\n"));
  CHECK(mail.quit());
  mail.end();

  CHECK(!client.open);  // the connection is closed after login
  CHECK(client.written ==
        "EHLO esp32\r\n"
        "AUTH PLAIN AG1lQGdtYWlsLmNvbQBhYmNkZWZnaGlqa2xtbm9w\r\n"
        "MAIL FROM:<me@gmail.com>\r\n"
        "RCPT TO:<you@example.com>\r\n"
        "DATA\r\n"
        "Subject: Hello\r\n\r\nSent from an ESP32.\r\n.\r\n"
        "QUIT\r\n");
}

// ---- simple API: login, send, receive ---------------------------------------

static const char* kLoginWire =
    "EHLO localhost\r\n"
    "AUTH PLAIN AG1lQGdtYWlsLmNvbQBhYmNkZWZnaGlqa2xtbm9w\r\n"
    "QUIT\r\n";
static const char* kLoginReplies =
    "220 smtp.gmail.com ready\r\n250 OK\r\n235 Accepted\r\n221 bye\r\n";

static void testLoginStoresCredentials() {
  MockClient client;
  client.incoming = kLoginReplies;
  TinyEMail mail(client);

  CHECK(!mail.loggedIn());
  CHECK(mail.login("me@gmail.com", "abcdefghijklmnop"));
  CHECK(mail.loggedIn());
  CHECK(client.written == kLoginWire);
  CHECK(!client.open);  // the connection is closed after login
}

static void testLoginRejected() {
  MockClient client;
  client.incoming =
      "220 smtp.gmail.com ready\r\n535 5.7.8 Username and Password not accepted\r\n";
  TinyEMail mail(client);

  CHECK(!mail.login("me@gmail.com", "wrong"));
  CHECK(!mail.loggedIn());
  CHECK(mail.lastReplyCode() == 535);
}

static void testLoginRejectsLineBreaks() {
  MockClient client;
  TinyEMail mail(client);

  CHECK(!mail.login("me@gmail.com\r\nQUIT", "pw"));
  CHECK(client.written.empty());
}

static void testSendWithoutLoginFails() {
  MockClient client;
  TinyEMail mail(client);

  CHECK(!mail.send("you@example.com", "Hello", "body"));
  CHECK(client.written.empty());
}

static void testSendTranscript() {
  MockClient client;
  client.incoming = std::string(kLoginReplies) +
      "220 smtp.gmail.com ready\r\n250 OK\r\n235 Accepted\r\n250 OK\r\n"
      "250 OK\r\n354 go ahead\r\n250 queued\r\n221 bye\r\n";
  TinyEMail mail(client);
  CHECK(mail.login("me@gmail.com", "abcdefghijklmnop"));
  client.written.clear();

  CHECK(mail.send("you@example.com", "Hello", "Hi there"));
  CHECK(client.written ==
        std::string("EHLO localhost\r\n"
                    "AUTH PLAIN AG1lQGdtYWlsLmNvbQBhYmNkZWZnaGlqa2xtbm9w\r\n"
                    "MAIL FROM:<me@gmail.com>\r\n"
                    "RCPT TO:<you@example.com>\r\n"
                    "DATA\r\n"
                    "From: me@gmail.com\r\n"
                    "To: you@example.com\r\n"
                    "Subject: Hello\r\n"
                    "MIME-Version: 1.0\r\n"
                    "Content-Type: text/plain; charset=utf-8\r\n"
                    "Content-Transfer-Encoding: quoted-printable\r\n\r\n"
                    "Hi there\r\n"
                    ".\r\n"
                    "QUIT\r\n"));
}

static void testSendKeepsSubjectOnOneLine() {
  MockClient client;
  client.incoming = std::string(kLoginReplies) +
      "220 ready\r\n250 OK\r\n235 OK\r\n250 OK\r\n250 OK\r\n354 go\r\n250 OK\r\n221 bye\r\n";
  TinyEMail mail(client);
  CHECK(mail.login("me@gmail.com", "abcdefghijklmnop"));
  client.written.clear();

  CHECK(mail.send("you@example.com", "a\r\nBcc: spy@example.com", "x"));
  CHECK(client.written.find("Subject: a  Bcc: spy@example.com\r\n") != std::string::npos);
  CHECK(client.written.find("\r\nBcc:") == std::string::npos);
}

static void testSendRejectsBadRecipient() {
  MockClient client;
  client.incoming = kLoginReplies;
  TinyEMail mail(client);
  CHECK(mail.login("me@gmail.com", "abcdefghijklmnop"));
  client.written.clear();

  CHECK(!mail.send("you@example.com\r\nRCPT TO:<spy@example.com>", "Hi", "x"));
  CHECK(client.written.empty());
}

static void testCustomServerIsUsed() {
  MockClient client;
  client.incoming = kLoginReplies;
  TinyEMail mail(client);
  mail.setSmtpServer("mail.example.org", 587);

  CHECK(mail.login("me@example.org", "secret"));
  CHECK(client.connectedHost == "mail.example.org");
  CHECK(client.connectedPort == 587);
}

// ---- attachments ----------------------------------------------------------------

// A Stream over a string: read() returns -1 at the end, like a File
class MemoryStream : public Stream {
 public:
  explicit MemoryStream(const std::string& text) : text_(text) {}
  int available() override { return static_cast<int>(text_.size() - pos_); }
  int read() override {
    return pos_ < text_.size() ? static_cast<uint8_t>(text_[pos_++]) : -1;
  }
  int peek() override {
    return pos_ < text_.size() ? static_cast<uint8_t>(text_[pos_]) : -1;
  }
  size_t write(uint8_t) override { return 0; }

 private:
  std::string text_;
  size_t pos_ = 0;
};

static const char* kSendPrefix =
    "EHLO localhost\r\n"
    "AUTH PLAIN AG1lQGdtYWlsLmNvbQBhYmNkZWZnaGlqa2xtbm9w\r\n"
    "MAIL FROM:<me@gmail.com>\r\n"
    "RCPT TO:<you@example.com>\r\n";

static const char* kAttachmentHead =
    "DATA\r\nFrom: me@gmail.com\r\nTo: you@example.com\r\nSubject: Report\r\n"
    "MIME-Version: 1.0\r\n"
    "Content-Type: multipart/mixed; boundary=\"TinyEMailBoundary\"\r\n\r\n"
    "--TinyEMailBoundary\r\n"
    "Content-Type: text/plain; charset=utf-8\r\n"
    "Content-Transfer-Encoding: quoted-printable\r\n\r\n"
    "See attached\r\n";

static void testSendAttachmentFromBuffer() {
  MockClient client;
  client.incoming = std::string(kLoginReplies) +
      "220 ready\r\n250 OK\r\n235 OK\r\n250 OK\r\n250 OK\r\n354 go\r\n250 OK\r\n221 bye\r\n";
  TinyEMail mail(client);
  CHECK(mail.login("me@gmail.com", "abcdefghijklmnop"));
  client.written.clear();

  TinyTextStream bodyText("See attached");
  const uint8_t content[] = {'H', 'e', 'l', 'l', 'o'};
  TinyBufferStream contentStream(content, sizeof(content));
  TinyEMail::Attachment files[2];
  files[0].contentType = "text/plain";  // the text body
  files[0].stream = &bodyText;
  files[1].filename = "hello.txt";
  files[1].contentType = "text/plain";
  files[1].stream = &contentStream;

  TinyEMail::Mail out;
  out.to.push_back(String("you@example.com"));
  out.subject = "Report";
  out.attachments.push_back(files[0]);
  out.attachments.push_back(files[1]);

  CHECK(mail.send(out));
  CHECK(client.written ==
        std::string(kSendPrefix) + kAttachmentHead +
        "--TinyEMailBoundary\r\n"
        "Content-Type: text/plain; name=\"hello.txt\"\r\n"
        "Content-Disposition: attachment; filename=\"hello.txt\"\r\n"
        "Content-Transfer-Encoding: base64\r\n\r\n"
        "SGVsbG8=\r\n"
        "--TinyEMailBoundary--\r\n"
        ".\r\n"
        "QUIT\r\n");
}

static void testSendAttachmentFromStream() {
  MockClient client;
  client.incoming = std::string(kLoginReplies) +
      "220 ready\r\n250 OK\r\n235 OK\r\n250 OK\r\n250 OK\r\n354 go\r\n250 OK\r\n221 bye\r\n";
  TinyEMail mail(client);
  CHECK(mail.login("me@gmail.com", "abcdefghijklmnop"));
  client.written.clear();

  MemoryStream content("Hello");
  TinyTextStream bodyText("See attached");
  TinyEMail::Attachment files[2];
  files[0].contentType = "text/plain";
  files[0].stream = &bodyText;
  files[1].filename = "hello.txt";
  files[1].contentType = "text/plain";
  files[1].stream = &content;

  TinyEMail::Mail out;
  out.to.push_back(String("you@example.com"));
  out.subject = "Report";
  out.attachments.push_back(files[0]);
  out.attachments.push_back(files[1]);

  CHECK(mail.send(out));
  CHECK(client.written.find("\r\n\r\nSGVsbG8=\r\n--TinyEMailBoundary--\r\n.\r\n") !=
        std::string::npos);
}

static void testBase64LinesAreWrappedAt76() {
  // 60 bytes of 'A' encode to 20 groups "QUFB"; the first line holds 76 characters
  std::string content(60, 'A');
  MockClient client;
  client.incoming = std::string(kLoginReplies) +
      "220 ready\r\n250 OK\r\n235 OK\r\n250 OK\r\n250 OK\r\n354 go\r\n250 OK\r\n221 bye\r\n";
  TinyEMail mail(client);
  CHECK(mail.login("me@gmail.com", "abcdefghijklmnop"));
  client.written.clear();

  TinyTextStream emptyBody("");
  MemoryStream file(content);
  TinyEMail::Attachment files[2];
  files[0].contentType = "text/plain";  // empty text body
  files[0].stream = &emptyBody;
  files[1].filename = "a.bin";
  files[1].stream = &file;
  TinyEMail::Mail out;
  out.to.push_back(String("you@example.com"));
  out.subject = "Lines";
  out.attachments.push_back(files[0]);
  out.attachments.push_back(files[1]);

  CHECK(mail.send(out));
  std::string first;
  for (int i = 0; i < 19; i++) first += "QUFB";
  CHECK(client.written.find("base64\r\n\r\n" + first + "\r\nQUFB\r\n--TinyEMailBoundary--") !=
        std::string::npos);
}

static void testSendRejectsUnsafeAttachmentName() {
  MockClient client;
  client.incoming = kLoginReplies;
  TinyEMail mail(client);
  CHECK(mail.login("me@gmail.com", "abcdefghijklmnop"));
  client.written.clear();

  TinyTextStream bodyText("text");
  MemoryStream content(std::string("\x01\x02\x03", 3));
  TinyEMail::Attachment files[2];
  files[0].contentType = "text/plain";
  files[0].stream = &bodyText;
  files[1].filename = "evil\".txt";  // a quote would break the header
  files[1].stream = &content;
  TinyEMail::Mail out;
  out.to.push_back(String("you@example.com"));
  out.attachments.push_back(files[0]);
  out.attachments.push_back(files[1]);

  CHECK(!mail.send(out));
  CHECK(client.written.empty());
}

static void testSendRejectsNonTextBody() {
  MockClient client;
  client.incoming = kLoginReplies;
  TinyEMail mail(client);
  CHECK(mail.login("me@gmail.com", "abcdefghijklmnop"));
  client.written.clear();

  MemoryStream content(std::string("\x01\x02\x03", 3));
  TinyEMail::Attachment files[1];  // the first attachment must be text
  files[0].contentType = "application/octet-stream";
  files[0].stream = &content;
  TinyEMail::Mail out;
  out.to.push_back(String("you@example.com"));
  out.attachments.push_back(files[0]);

  CHECK(!mail.send(out));
  CHECK(client.written.empty());
}

static void testSendRejectsEmptyRecipient() {
  MockClient client;
  client.incoming = kLoginReplies;
  TinyEMail mail(client);
  CHECK(mail.login("me@gmail.com", "abcdefghijklmnop"));
  client.written.clear();

  TinyEMail::Mail out;
  out.subject = "no recipient";
  CHECK(!mail.send(out));
  CHECK(client.written.empty());
}

static void testReceiveReadsUnreadMails() {
  const std::string m1 =
      "From: Bob <bob@example.com>\r\nsubject: Lunch?\r\n"
      "Message-ID: <m1@example.com>\r\n\r\nNoon?\r\n";
  const std::string m2 = "FROM: alice@example.com\r\nSubject: Hi\r\n\r\nbody\r\n";
  MockClient client;
  client.incoming = std::string(kLoginReplies) +
      "* OK Gimap ready\r\n"
      "A001 OK LOGIN completed\r\n"
      "* 2 EXISTS\r\n"
      "A002 OK SELECT completed\r\n"
      "* SEARCH 4 7\r\n"
      "A003 OK SEARCH completed\r\n"
      "* 1 FETCH (UID 4 BODY[] {" + std::to_string(m1.size()) + "}\r\n" + m1 + ")\r\n"
      "A004 OK FETCH completed\r\n"
      "* 2 FETCH (UID 7 BODY[] {" + std::to_string(m2.size()) + "}\r\n" + m2 + ")\r\n"
      "A005 OK FETCH completed\r\n"
      "* BYE\r\n"
      "A006 OK LOGOUT completed\r\n";
  TinyEMail mail(client);
  CHECK(mail.login("me@gmail.com", "abcdefghijklmnop"));
  client.written.clear();

  std::vector<std::string> subjects;
  std::vector<std::string> froms;
  std::vector<uint32_t> uids;
  int count = mail.receive([&](const TinyEMail::Mail& m) {
    subjects.push_back(std::string(m.subject.c_str()));
    froms.push_back(std::string(m.from.c_str()));
    uids.push_back(m.uid);
    if (uids.size() == 1) {
      for (TinyPart& part : m.parts()) {
        CHECK(part.data.readString() == "Noon?\r\n");  // no Content-Type: plain text
      }
      CHECK(m.header("message-id") == "<m1@example.com>");  // any case
      CHECK(m.header("X-Missing").length() == 0);
    }
    return true;
  });
  CHECK(count == 2);
  CHECK(uids.size() == 2 && uids[0] == 4 && uids[1] == 7);
  CHECK(subjects.size() == 2 && subjects[0] == "Lunch?" && subjects[1] == "Hi");
  CHECK(froms.size() == 2 && froms[0] == "Bob <bob@example.com>");  // header name in any case
  CHECK(froms.size() == 2 && froms[1] == "alice@example.com");
  CHECK(client.written ==
        "A001 LOGIN \"me@gmail.com\" \"abcdefghijklmnop\"\r\n"
        "A002 SELECT \"INBOX\"\r\n"
        "A003 UID SEARCH UNSEEN\r\n"
        "A004 UID FETCH 4 BODY.PEEK[]\r\n"
        "A005 UID FETCH 7 BODY.PEEK[]\r\n"
        "A006 LOGOUT\r\n");
}

static void testReceiveStopsWhenCallbackReturnsFalse() {
  const std::string m1 = "Subject: First\r\n\r\nx\r\n";
  MockClient client;
  client.incoming = std::string(kLoginReplies) +
      "* OK ready\r\n"
      "A001 OK LOGIN completed\r\n"
      "* 2 EXISTS\r\n"
      "A002 OK SELECT completed\r\n"
      "* SEARCH 4 7\r\n"
      "A003 OK SEARCH completed\r\n"
      "* 1 FETCH (UID 4 BODY[] {" + std::to_string(m1.size()) + "}\r\n" + m1 + ")\r\n"
      "A004 OK FETCH completed\r\n"
      "A005 OK LOGOUT completed\r\n";
  TinyEMail mail(client);
  CHECK(mail.login("me@gmail.com", "abcdefghijklmnop"));
  client.written.clear();

  int calls = 0;
  int count = mail.receive([&](const TinyEMail::Mail& m) {
    calls++;
    CHECK(m.subject == "First");
    return false;  // stop after the first mail
  });
  CHECK(count == 1);
  CHECK(calls == 1);
  CHECK(client.written ==
        "A001 LOGIN \"me@gmail.com\" \"abcdefghijklmnop\"\r\n"
        "A002 SELECT \"INBOX\"\r\n"
        "A003 UID SEARCH UNSEEN\r\n"
        "A004 UID FETCH 4 BODY.PEEK[]\r\n"
        "A005 LOGOUT\r\n");
}

static void testReceiveWithoutLoginFails() {
  MockClient client;
  TinyEMail mail(client);

  CHECK(mail.receive([](const TinyEMail::Mail&) { return true; }) == -1);
  CHECK(client.written.empty());
}

static void testReceiveFailsOnBadLogin() {
  MockClient client;
  client.incoming = std::string(kLoginReplies) +
      "* OK ready\r\n"
      "A001 NO [AUTHENTICATIONFAILED] Invalid credentials\r\n";
  TinyEMail mail(client);
  CHECK(mail.login("me@gmail.com", "abcdefghijklmnop"));
  client.written.clear();

  CHECK(mail.receive([](const TinyEMail::Mail&) { return true; }) == -1);
  CHECK(mail.lastReply().startsWith("A001 NO"));
}

static void testReceiveFilterSendsSearchKey() {
  // every receive() is its own IMAP session, so the tags start at A001 again
  const std::string session =
      "* OK ready\r\nA001 OK LOGIN\r\n* 0 EXISTS\r\nA002 OK SELECT\r\n"
      "* SEARCH\r\nA003 OK SEARCH\r\nA004 OK LOGOUT\r\n";
  MockClient client;
  client.incoming = std::string(kLoginReplies) + session + session;
  TinyEMail mail(client);
  CHECK(mail.login("me@gmail.com", "abcdefghijklmnop"));
  client.written.clear();

  auto nothing = [](const TinyEMail::Mail&) { return true; };
  CHECK(mail.receive(nothing, TinyEMail::Filter::Read) == 0);
  CHECK(mail.receive(nothing, TinyEMail::Filter::All) == 0);
  CHECK(client.written.find("A003 UID SEARCH SEEN\r\n") != std::string::npos);
  CHECK(client.written.find("A003 UID SEARCH ALL\r\n") != std::string::npos);
}

static void testSendNormalizesLineEndings() {
  MockClient client;
  client.incoming = std::string(kLoginReplies) +
      "220 ready\r\n250 OK\r\n235 OK\r\n250 OK\r\n250 OK\r\n354 go\r\n250 OK\r\n221 bye\r\n";
  TinyEMail mail(client);
  CHECK(mail.login("me@gmail.com", "abcdefghijklmnop"));
  client.written.clear();

  // CRLF, lone CR and LF must each become one CRLF, also a CRLF split in two
  CHECK(mail.send("you@example.com", "Lines", "a\r\nb\rc\nd\r\n.e"));
  CHECK(client.written.find("\r\n\r\na\r\nb\r\nc\r\nd\r\n..e\r\n.\r\n") !=
        std::string::npos);
}

static void testReceivePartsLoop() {
  // a mail with a text body, an HTML alternative and a file: one callback, three parts
  const std::string raw =
      "Subject: parts\r\nContent-Type: multipart/mixed; boundary=\"m\"\r\n\r\n"
      "--m\r\nContent-Type: multipart/alternative; boundary=\"a\"\r\n\r\n"
      "--a\r\nContent-Type: text/plain\r\n\r\nhello\r\n"
      "--a\r\nContent-Type: text/html\r\n\r\n<p>hello</p>\r\n"
      "--a--\r\n"
      "--m\r\nContent-Type: application/pdf; name=\"doc.pdf\"\r\n"
      "Content-Disposition: attachment; filename=\"doc.pdf\"\r\n"
      "Content-Transfer-Encoding: base64\r\n\r\nJVBERg==\r\n"
      "--m--\r\n";
  MockClient client;
  client.incoming = std::string(kLoginReplies) +
      "* OK ready\r\nA001 OK LOGIN\r\n* 1 EXISTS\r\nA002 OK SELECT\r\n"
      "* SEARCH 4\r\nA003 OK SEARCH\r\n"
      "* 1 FETCH (UID 4 BODY[] {" + std::to_string(raw.size()) + "}\r\n" + raw + ")\r\n"
      "A004 OK FETCH\r\nA005 OK LOGOUT\r\n";
  TinyEMail mail(client);
  CHECK(mail.login("me@gmail.com", "abcdefghijklmnop"));

  int callbacks = 0;
  std::vector<std::string> types;
  std::vector<std::string> names;
  std::string text;
  std::string pdf;
  int count = mail.receive([&](const TinyEMail::Mail& m) {
    callbacks++;
    CHECK(m.subject == "parts");
    for (TinyPart& part : m.parts()) {
      types.push_back(std::string(part.info.contentType.c_str()));
      names.push_back(std::string(part.info.filename.c_str()));
      if (part.info.contentType == "text/plain") {
        text = std::string(part.data.readString().c_str());
      } else if (part.info.attachment) {
        int c;
        while ((c = part.data.read()) >= 0) pdf += static_cast<char>(c);
      }
    }
    return true;
  });
  CHECK(count == 1);
  CHECK(callbacks == 1);  // one callback for the mail
  CHECK(types.size() == 3);
  CHECK(types.size() == 3 && types[0] == "text/plain" && types[1] == "text/html");
  CHECK(types.size() == 3 && types[2] == "application/pdf");
  CHECK(names.size() == 3 && names[2] == "doc.pdf");
  CHECK(text == "hello");
  CHECK(pdf == "%PDF");  // base64 of the file, decoded while it is read
}

static void testSeveralRecipientsAndBcc() {
  MockClient client;
  client.incoming = std::string(kLoginReplies) +
      "220 ready\r\n250 OK\r\n235 OK\r\n250 OK\r\n250 OK\r\n250 OK\r\n250 OK\r\n354 go\r\n250 OK\r\n221 bye\r\n";
  TinyEMail mail(client);
  CHECK(mail.login("me@gmail.com", "abcdefghijklmnop"));
  client.written.clear();

  TinyTextStream body("hi");
  TinyEMail::Attachment text;
  text.contentType = "text/plain";
  text.stream = &body;
  TinyEMail::Mail out;
  out.to.push_back(String("a@example.com"));
  out.to.push_back(String("b@example.com"));
  out.bcc.push_back(String("hidden@example.com"));
  out.subject = "Team";
  out.attachments.push_back(text);
  CHECK(mail.send(out));
  // To lists both addresses; the bcc address is only in the envelope
  CHECK(client.written.find("To: a@example.com, b@example.com\r\n") != std::string::npos);
  CHECK(client.written.find("RCPT TO:<a@example.com>\r\nRCPT TO:<b@example.com>\r\n"
                            "RCPT TO:<hidden@example.com>\r\n") != std::string::npos);
  CHECK(client.written.find("hidden@example.com") == client.written.rfind("hidden@example.com"));  // only in RCPT TO
  CHECK(client.written.find("Bcc") == std::string::npos);
}

static void testBccOnlyIsUndisclosed() {
  MockClient client;
  client.incoming = std::string(kLoginReplies) +
      "220 ready\r\n250 OK\r\n235 OK\r\n250 OK\r\n250 OK\r\n354 go\r\n250 OK\r\n221 bye\r\n";
  TinyEMail mail(client);
  CHECK(mail.login("me@gmail.com", "abcdefghijklmnop"));
  client.written.clear();

  TinyEMail::Mail out;
  out.bcc.push_back(String("hidden@example.com"));
  out.subject = "Hidden";
  TinyTextStream body("hi");
  TinyEMail::Attachment text;
  text.contentType = "text/plain";
  text.stream = &body;
  out.attachments.push_back(text);
  CHECK(mail.send(out));
  CHECK(client.written.find("To: undisclosed-recipients:;\r\n") != std::string::npos);
}

int main() {
  testDefaultsAreGmail();
  testBeginDefaultsToGmailSmtp();
  testBeginWithImap();
  testBeginFails();
  testBeginWithNullHostFails();
  testEndClosesConnection();
  testPrintWritesToServer();
  testPrintWithoutConnectionWritesNothing();
  testStreamReadsFromServer();
  testStreamWithoutConnectionReadsNothing();
  testEchoRoundTrip();

  testReadReplySingleLine();
  testReadReplyMultiLine();
  testReadReplyInvalid();
  testReadReplyTimeout();
  testEhloSendsCommand();
  testEhloRejected();
  testAuthPlainSendsBase64();
  testAuthPlainShortCredentials();
  testAuthPlainRejected();
  testAuthPlainTooLong();
  testMailFromAndRcptTo();
  testRcptToForwarded();
  testDataEscapesDotsAndUsesCrlf();
  testDataRejectedBeforeBody();
  testQuit();
  testFullSession();

  testLoginStoresCredentials();
  testLoginRejected();
  testLoginRejectsLineBreaks();
  testSendWithoutLoginFails();
  testSendTranscript();
  testSendKeepsSubjectOnOneLine();
  testSendRejectsBadRecipient();
  testCustomServerIsUsed();
  testReceiveReadsUnreadMails();
  testReceiveStopsWhenCallbackReturnsFalse();
  testReceiveWithoutLoginFails();
  testReceiveFailsOnBadLogin();
  testReceiveFilterSendsSearchKey();
  testReceivePartsLoop();
  testSeveralRecipientsAndBcc();
  testBccOnlyIsUndisclosed();
  testSendAttachmentFromBuffer();
  testSendAttachmentFromStream();
  testBase64LinesAreWrappedAt76();
  testSendRejectsUnsafeAttachmentName();
  testSendRejectsNonTextBody();
  testSendNormalizesLineEndings();
  testSendRejectsEmptyRecipient();

  std::cout << g_checks << " checks, " << g_failures << " failures\n";
  return g_failures == 0 ? 0 : 1;
}
