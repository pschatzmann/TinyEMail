// Desktop tests for logging, the limits, the connection checks, the MIME fixes
// and the text decoders.

#include <cstdint>
#include <iostream>
#include <string>
#include <utility>

#include <Arduino.h>

#include "MockClient.h"
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

// a Print that keeps what is written to it
class CapturePrint : public Print {
 public:
  std::string text;
  size_t write(uint8_t b) override {
    text += static_cast<char>(b);
    return 1;
  }
};

static const char* kLoginOk = "220 ready\r\n250 OK\r\n235 Accepted\r\n221 bye\r\n";

static bool contains(const std::string& text, const std::string& part) {
  return text.find(part) != std::string::npos;
}

// ---- logger -------------------------------------------------------------------

static void testLoggerOffByDefault() {
  TinyLogger::instance().end();
  CapturePrint capture;
  TinyLogger::instance().error("nothing");
  CHECK(capture.text.empty());
  CHECK(TinyLogger::instance().level() == TinyLogger::Level::None);
}

static void testLoggerLevelsFilter() {
  CapturePrint capture;
  TinyLogger::instance().begin(capture, TinyLogger::Level::Warn);
  TinyLogger::instance().error("e-text");
  TinyLogger::instance().warn("w-text");
  TinyLogger::instance().info("i-text");
  TinyLogger::instance().debug("d-text");
  TinyLogger::instance().end();
  CHECK(contains(capture.text, "E e-text\r\n"));
  CHECK(contains(capture.text, "W w-text\r\n"));
  CHECK(!contains(capture.text, "i-text"));
  CHECK(!contains(capture.text, "d-text"));
}

static void testLoggerFormat() {
  CapturePrint capture;
  TinyLogger::instance().begin(capture, TinyLogger::Level::Info);
  TinyLogger::instance().info("ready");
  TinyLogger::instance().end();
  CHECK(!capture.text.empty() && capture.text[0] == '[');
  CHECK(contains(capture.text, "] I ready\r\n"));
}

static void testLoginFailureIsLogged() {
  MockClient client;
  client.incoming = "220 ok\r\n250 ok\r\n535 bad credentials\r\n";
  TinyEMail mail(client);
  CapturePrint capture;
  TinyLogger::instance().begin(capture, TinyLogger::Level::Debug);
  CHECK(!mail.login("me@gmail.com", "secretpass123"));
  TinyLogger::instance().end();
  CHECK(contains(capture.text, "E login: AUTH PLAIN failed: 535 bad credentials"));
  CHECK(contains(capture.text, "E login failed"));
  // the password is never logged, not even in encoded form
  CHECK(!contains(capture.text, "secretpass123"));
  CHECK(!contains(capture.text, "AUTH PLAIN AG"));
  CHECK(contains(capture.text, "C: AUTH PLAIN ****"));
}

static void testInvalidReplyIsLogged() {
  MockClient client;
  client.incoming = "220 ok\r\nxyz not a reply\r\n";
  TinyEMail mail(client);
  CapturePrint capture;
  TinyLogger::instance().begin(capture, TinyLogger::Level::Error);
  CHECK(!mail.login("me@gmail.com", "pw"));
  TinyLogger::instance().end();
  CHECK(contains(capture.text, "E SMTP: invalid reply: xyz not a reply"));
}

static void testNoReplyIsLogged() {
  MockClient client;  // the server never answers
  TinyEMail mail(client);
  mail.setTimeout(20);
  CapturePrint capture;
  TinyLogger::instance().begin(capture, TinyLogger::Level::Error);
  CHECK(!mail.login("me@gmail.com", "pw"));
  TinyLogger::instance().end();
  CHECK(contains(capture.text, "E SMTP: no complete reply"));
  CHECK(contains(capture.text, "E login: greeting failed: no reply from the server"));
}

static void testUnsafeLoginIsLogged() {
  MockClient client;
  TinyEMail mail(client);
  CapturePrint capture;
  TinyLogger::instance().begin(capture, TinyLogger::Level::Error);
  CHECK(!mail.login("me@gmail.com\r\nQUIT", "pw"));
  CHECK(!mail.login("me@gmail.com", "bad\r\npassword"));
  TinyLogger::instance().end();
  CHECK(contains(capture.text, "E login: the user must be an e-mail address"));
  CHECK(contains(capture.text, "E login: the password is empty or has a line break"));
  CHECK(client.written.empty());
}

static void testSendNotLoggedInIsLogged() {
  MockClient client;
  TinyEMail mail(client);
  CapturePrint capture;
  TinyLogger::instance().begin(capture, TinyLogger::Level::Error);
  CHECK(!mail.send("you@example.com", "s", "b"));
  TinyLogger::instance().end();
  CHECK(contains(capture.text, "E send: not logged in"));
}

static void testFailedLoginForgetsAccount() {
  MockClient client;
  client.incoming = std::string(kLoginOk) + "220 ready\r\n535 no\r\n";
  TinyEMail mail(client);
  CHECK(mail.login("me@gmail.com", "abcdefghijklmnop"));
  CHECK(mail.loggedIn());
  CHECK(!mail.login("me@gmail.com", "wrong"));
  CHECK(!mail.loggedIn());  // the old account is not used any more
}

static void testReceiveStopsOnDroppedConnection() {
  const std::string m1 = "Subject: first\r\n\r\nhi\r\n";
  MockClient client;
  client.incoming = std::string(kLoginOk) +
      "* OK ready\r\nA001 OK LOGIN\r\n* 2 EXISTS\r\nA002 OK SELECT\r\n"
      "* SEARCH 4 7\r\nA003 OK SEARCH\r\n"
      "* 1 FETCH (UID 4 BODY[] {" + std::to_string(m1.size()) + "}\r\n" + m1 + ")\r\n"
      "A004 OK FETCH\r\n";  // then the server stops answering
  TinyEMail mail(client);
  mail.setTimeout(20);
  CHECK(mail.login("me@gmail.com", "abcdefghijklmnop"));

  CapturePrint capture;
  TinyLogger::instance().begin(capture, TinyLogger::Level::Warn);
  int calls = 0;
  int count = mail.receive([&](const TinyEMail::Mail&) {
    calls++;
    return true;
  });
  TinyLogger::instance().end();
  CHECK(count == 1);  // the first mail was read; the second one timed out
  CHECK(calls == 1);
  CHECK(contains(capture.text, "E receive: connection lost after 1 mails"));
}

static void testReceiveMessageOverLimit() {
  const std::string big(100, 'x');
  MockClient client;
  client.incoming = std::string(kLoginOk) +
      "* OK ready\r\nA001 OK LOGIN\r\n* 1 EXISTS\r\nA002 OK SELECT\r\n"
      "* SEARCH 4\r\nA003 OK SEARCH\r\n"
      "* 1 FETCH (UID 4 BODY[] {100}\r\n" + big + ")\r\nA004 OK FETCH\r\n";
  TinyEMail mail(client);
  mail.setMaxMessageSize(10);
  CHECK(mail.login("me@gmail.com", "abcdefghijklmnop"));

  CapturePrint capture;
  TinyLogger::instance().begin(capture, TinyLogger::Level::Warn);
  int count = mail.receive([](const TinyEMail::Mail&) {
    return true;
  });
  TinyLogger::instance().end();
  CHECK(count == 0);  // the mail is skipped, and the connection stays usable
  CHECK(contains(capture.text, "W IMAP: message of 100 bytes is over the limit of 10, skipped"));
}

// ---- MIME warnings and fixes ---------------------------------------------------

static std::string parseWarnings(const std::string& raw) {
  CapturePrint capture;
  TinyLogger::instance().begin(capture, TinyLogger::Level::Warn);
  TinyMIME::parse(String(raw.c_str()), [](const TinyMailPart&, TinyMailData& d) {
    d.readString();
    return true;
  });
  TinyLogger::instance().end();
  return capture.text;
}

static void testMimeWarnings() {
  CHECK(contains(parseWarnings("Content-Type: multipart/mixed\r\n\r\nbody"),
                 "W MIME: multipart without boundary"));
  CHECK(contains(parseWarnings("Content-Type: multipart/mixed; boundary=\"x\"\r\n\r\n"
                               "--x\r\nContent-Type: text/plain\r\n\r\nhello\r\n"),
                 "W MIME: multipart without closing boundary"));
  CHECK(contains(parseWarnings("Content-Type: text/plain\r\n"
                               "Content-Transfer-Encoding: uuencode\r\n\r\nxx"),
                 "W MIME: transfer encoding not supported"));
}

static void testMimeBase64OnSeveralLines() {
  std::string text;
  TinyMIME::parse(String("Content-Type: text/html\r\nContent-Transfer-Encoding: base64\r\n\r\n"
                         "PGI+SGk8\r\nL2I+\r\n"),
                  [&](const TinyMailPart&, TinyMailData& d) {
                    text = std::string(d.readString().c_str());
                    return true;
                  });
  CHECK(text == "<b>Hi</b>");
}

static void testBase64PaddingStartsANewGroup() {
  String out;
  TinyText::base64Decode("YQ==YWI=", 8, out);
  CHECK(out == "aab");
}

static void testQuotedPrintableRemovesTrailingSpaces() {
  std::string text;
  TinyMIME::parse(String("Content-Type: text/plain\r\nContent-Transfer-Encoding: quoted-printable\r\n\r\n"
                         "abc  \r\ndef"),
                  [&](const TinyMailPart&, TinyMailData& d) {
                    text = std::string(d.readString().c_str());
                    return true;
                  });
  CHECK(text == "abc\r\ndef");
}

static void testBoundaryMustStartALine() {
  int parts = 0;
  std::string first;
  TinyMIME::parse(String("Content-Type: multipart/mixed; boundary=\"b\"\r\n\r\n"
                         "--b\r\n\r\na --b c\r\n--b--\r\n"),
                  [&](const TinyMailPart&, TinyMailData& d) {
                    if (parts++ == 0) first = std::string(d.readString().c_str());
                    return true;
                  });
  CHECK(parts == 1);
  CHECK(first == "a --b c");
}

static void testPartWithoutHeadersIsBody() {
  std::string text;
  TinyMIME::parse(String("Hello there world"), [&](const TinyMailPart& p, TinyMailData& d) {
    CHECK(p.contentType == "text/plain");
    text = std::string(d.readString().c_str());
    return true;
  });
  CHECK(text == "Hello there world");
}

static void testFileNamesAreDecoded() {
  TinyMailPart info;
  TinyMIME::parse(String("Content-Type: application/pdf\r\n"
                         "Content-Disposition: attachment; filename*=UTF-8''%C3%B6.pdf\r\n\r\nxx"),
                  [&](const TinyMailPart& p, TinyMailData&) {
                    info = p;
                    return true;
                  });
  CHECK(info.filename == "\xC3\xB6.pdf");  // ö.pdf (RFC 2231)
  CHECK(info.attachment);

  TinyMIME::parse(String("Content-Type: text/plain\r\n"
                         "Content-Disposition: attachment; filename=\"=?UTF-8?B?w7Y=?=.txt\"\r\n\r\nxx"),
                  [&](const TinyMailPart& p, TinyMailData&) {
                    info = p;
                    return true;
                  });
  CHECK(info.filename == "\xC3\xB6.txt");  // ö.txt (RFC 2047)

  // "name=" inside another parameter name is not the name
  TinyMIME::parse(String("Content-Type: application/x; xname=\"no\"\r\n\r\nxx"),
                  [&](const TinyMailPart& p, TinyMailData&) {
                    info = p;
                    return true;
                  });
  CHECK(info.filename.length() == 0);
}

static void testEncodedWords() {
  CHECK(TinyText::decodeWords("=?UTF-8?B?w7Y=?=") == "\xC3\xB6");
  CHECK(TinyText::decodeWords("=?ISO-8859-1?Q?Gr=FC=DFe?=") == "Gr\xC3\xBC\xC3\x9F" "e");
  CHECK(TinyText::decodeWords("=?UTF-8?Q?a?= =?UTF-8?Q?b?=") == "ab");
  CHECK(TinyText::decodeWords("hello =?UTF-8?Q?x?=") == "hello x");
  CHECK(TinyText::decodeWords("plain text") == "plain text");
  CHECK(TinyText::decodeWords("=?UTF-8?Q?a_b?=") == "a b");
}

static void testReadLineLimit() {
  MockClient client;
  client.incoming = "0123456789abcdef\r\nok\r\n";
  TinyIMAP imap(client);
  imap.begin();
  imap.setTimeout(20);
  imap.setLimits(10, 100);
  // readGreeting reads the first line: it is too long for the limit
  CHECK(!imap.readGreeting());
  CHECK(imap.broken());
}

// ---- quoted-printable output of the body -----------------------------------------

static std::string bodyWritten(const std::string& body) {
  MockClient client;
  client.incoming = std::string(kLoginOk) +
      "220 ready\r\n250 OK\r\n235 OK\r\n250 OK\r\n250 OK\r\n354 go\r\n250 OK\r\n221 bye\r\n";
  TinyEMail mail(client);
  CHECK(mail.login("me@gmail.com", "abcdefghijklmnop"));
  client.written.clear();
  CHECK(mail.send("you@example.com", "Body", body.c_str()));
  return client.written;
}

static void testBodyQuotedPrintable() {
  CHECK(contains(bodyWritten("x \r\ny"), "\r\n\r\nx=20\r\ny\r\n.\r\n"));  // trailing space
  CHECK(contains(bodyWritten("a=b"), "a=3Db\r\n"));                        // '=' is escaped
  CHECK(contains(bodyWritten("\xC3\xA9"), "=C3=A9\r\n"));                  // UTF-8 bytes
  const std::string longLine(100, 'a');
  CHECK(contains(bodyWritten(longLine), std::string(75, 'a') + "=\r\n" + std::string(25, 'a') + "\r\n"));
  CHECK(contains(bodyWritten("Content-Transfer-Encoding: x"), "quoted-printable"));
}

static void testVectorAtAndMove() {
  TinyVector<int> v;
  v.push_back(1);
  v.push_back(2);
  CHECK(v.at(1) != nullptr && *v.at(1) == 2);
  CHECK(v.at(2) == nullptr);
  TinyVector<int> w(std::move(v));
  CHECK(w.size() == 2 && v.size() == 0);
}

static void testHtmlComposition() {
  CapturePrint out;
  TinyHtml html(out);
  html.text("a<b & \"c\" 'd'");
  CHECK(out.text == "a&lt;b &amp; &quot;c&quot; &#39;d&#39;");  // user text is escaped

  CapturePrint page;
  TinyHtml page_html(page);
  page_html.heading(1, "Report").paragraph("Temperature: 21.5").lineBreak()
      .link("https://example.com/?a=1&b=2", "details")
      .beginList().item("first").item("second").endList()
      .image("pic.png", "a \"picture\"").raw("<em>x</em>");
  CHECK(page.text ==
        "<h1>Report</h1><p>Temperature: 21.5</p><br>"
        "<a href=\"https://example.com/?a=1&amp;b=2\">details</a>"
        "<ul><li>first</li><li>second</li></ul>"
        "<img src=\"pic.png\" alt=\"a &quot;picture&quot;\"><em>x</em>");

  CapturePrint levels;
  TinyHtml level_html(levels);
  level_html.heading(0, "low").heading(9, "high");
  CHECK(levels.text == "<h1>low</h1><h6>high</h6>");  // level limited to 1..6
}

static void testSendHtmlTranscript() {
  MockClient client;
  client.incoming = std::string(kLoginOk) +
      "220 ready\r\n250 OK\r\n235 OK\r\n250 OK\r\n250 OK\r\n354 go\r\n250 OK\r\n221 bye\r\n";
  TinyEMail mail(client);
  CHECK(mail.login("me@gmail.com", "abcdefghijklmnop"));
  client.written.clear();

  CHECK(mail.sendWith("you@example.com", "Greeting", "text/html", [](Print& body) {
    TinyHtml html(body);
    html.paragraph("Hi <you>");
  }));
  CHECK(contains(client.written,
                 "DATA\r\nFrom: me@gmail.com\r\nTo: you@example.com\r\nSubject: Greeting\r\n"
                 "MIME-Version: 1.0\r\nContent-Type: text/html; charset=utf-8\r\n"
                 "Content-Transfer-Encoding: quoted-printable\r\n\r\n"));
  CHECK(contains(client.written, "<p>Hi &lt;you&gt;</p>\r\n.\r\nQUIT\r\n"));
}

static void testStreamedBodyIsQuotedPrintable() {
  MockClient client;
  client.incoming = std::string(kLoginOk) +
      "220 ready\r\n250 OK\r\n235 OK\r\n250 OK\r\n250 OK\r\n354 go\r\n250 OK\r\n221 bye\r\n";
  TinyEMail mail(client);
  CHECK(mail.login("me@gmail.com", "abcdefghijklmnop"));
  client.written.clear();

  // the body is written in pieces, and a long line gets soft line breaks as it is written
  CHECK(mail.sendWith("you@example.com", "Parts", "text/plain", [](Print& body) {
    body.print("x ");
    body.print(std::string(80, 'b').c_str());
  }));
  // "x " is not trailing (a letter follows), so the space stays; the first line
  // holds 75 characters including the soft break "="
  CHECK(contains(client.written, "x " + std::string(73, 'b') + "=\r\n" + std::string(7, 'b') + "\r\n.\r\n"));
}

static void testSendWithInvalidRecipientFails() {
  MockClient client;
  client.incoming = std::string(kLoginOk) + "220 ready\r\n250 OK\r\n235 OK\r\n250 OK\r\n250 OK\r\n";
  TinyEMail mail(client);
  CHECK(mail.login("me@gmail.com", "abcdefghijklmnop"));
  client.written.clear();
  CapturePrint log;
  TinyLogger::instance().begin(log, TinyLogger::Level::Error);
  CHECK(!mail.sendWith("you@example.com\r\nRCPT", "s", "text/plain", [](Print&) {}));
  TinyLogger::instance().end();
  CHECK(contains(log.text, "E send: invalid recipient or content type"));
  CHECK(client.written.empty());
}

static void testSendFromStream() {
  MockClient client;
  client.incoming = std::string(kLoginOk) +
      "220 ready\r\n250 OK\r\n235 OK\r\n250 OK\r\n250 OK\r\n354 go\r\n250 OK\r\n221 bye\r\n";
  TinyEMail mail(client);
  CHECK(mail.login("me@gmail.com", "abcdefghijklmnop"));
  client.written.clear();

  TinyTextStream content("copied from a stream");
  CHECK(mail.send("you@example.com", "From stream", content, "text/html"));
  CHECK(contains(client.written, "Content-Type: text/html; charset=utf-8\r\n"));
  CHECK(contains(client.written, "\r\n\r\ncopied from a stream\r\n.\r\nQUIT\r\n"));
}

static void testLargeMessageIsStreamed() {
  // 200 KB in 1000 lines: more than the old limit, and read line by line
  std::string body;
  for (int i = 0; i < 1000; i++) body += std::string(199, 'a') + "\r\n";
  std::string raw = "Subject: big\r\nContent-Type: text/plain\r\n\r\n" + body;
  MockClient client;
  client.incoming = std::string(kLoginOk) +
      "* OK ready\r\nA001 OK LOGIN\r\n* 1 EXISTS\r\nA002 OK SELECT\r\n"
      "* SEARCH 4\r\nA003 OK SEARCH\r\n"
      "* 1 FETCH (UID 4 BODY[] {" + std::to_string(raw.size()) + "}\r\n" + raw + ")\r\n"
      "A004 OK FETCH\r\nA005 OK LOGOUT\r\n";
  TinyEMail mail(client);
  CHECK(mail.login("me@gmail.com", "abcdefghijklmnop"));

  size_t received = 0;
  std::string head;
  int count = mail.receive([&](const TinyEMail::Mail& m) {
    CHECK(m.subject == "big");
    for (TinyPart& part : m.parts()) {
      int c;
      while ((c = part.data.read()) >= 0) {
        if (received < 8) head += static_cast<char>(c);
        received++;
      }
    }
    return true;
  });
  CHECK(count == 1);
  CHECK(received == body.size());
  CHECK(head == "aaaaaaaa");
}

static void testMarkAsReadSendsStore() {
  const std::string raw = "Subject: s\r\n\r\nhi\r\n";
  MockClient client;
  client.incoming = std::string(kLoginOk) +
      "* OK ready\r\nA001 OK LOGIN\r\n* 1 EXISTS\r\nA002 OK SELECT\r\n"
      "* SEARCH 4\r\nA003 OK SEARCH\r\n"
      "* 1 FETCH (UID 4 BODY[] {" + std::to_string(raw.size()) + "}\r\n" + raw + ")\r\n"
      "A004 OK FETCH\r\nA005 OK STORE\r\nA006 OK LOGOUT\r\n";
  TinyEMail mail(client);
  CHECK(mail.login("me@gmail.com", "abcdefghijklmnop"));
  client.written.clear();

  int count = mail.receive([](const TinyEMail::Mail& m) {
    m.markAsRead();  // the change is sent after the mail was read
    return true;
  });
  CHECK(count == 1);
  CHECK(client.written ==
        "A001 LOGIN \"me@gmail.com\" \"abcdefghijklmnop\"\r\n"
        "A002 SELECT \"INBOX\"\r\n"
        "A003 UID SEARCH UNSEEN\r\n"
        "A004 UID FETCH 4 BODY.PEEK[]\r\n"
        "A005 UID STORE 4 +FLAGS (\\Seen)\r\n"
        "A006 LOGOUT\r\n");
}

static void testNoFlagWithoutRequest() {
  const std::string raw = "Subject: s\r\n\r\nhi\r\n";
  MockClient client;
  client.incoming = std::string(kLoginOk) +
      "* OK ready\r\nA001 OK LOGIN\r\n* 1 EXISTS\r\nA002 OK SELECT\r\n"
      "* SEARCH 4\r\nA003 OK SEARCH\r\n"
      "* 1 FETCH (UID 4 BODY[] {" + std::to_string(raw.size()) + "}\r\n" + raw + ")\r\n"
      "A004 OK FETCH\r\nA005 OK LOGOUT\r\n";
  TinyEMail mail(client);
  CHECK(mail.login("me@gmail.com", "abcdefghijklmnop"));
  client.written.clear();
  mail.receive([](const TinyEMail::Mail&) { return true; });
  CHECK(client.written.find("STORE") == std::string::npos);  // BODY.PEEK keeps the mail unread
}

int main() {
  testLoggerOffByDefault();
  testLoggerLevelsFilter();
  testLoggerFormat();
  testLoginFailureIsLogged();
  testInvalidReplyIsLogged();
  testNoReplyIsLogged();
  testUnsafeLoginIsLogged();
  testSendNotLoggedInIsLogged();
  testFailedLoginForgetsAccount();
  testReceiveStopsOnDroppedConnection();
  testReceiveMessageOverLimit();
  testMimeWarnings();
  testMimeBase64OnSeveralLines();
  testBase64PaddingStartsANewGroup();
  testQuotedPrintableRemovesTrailingSpaces();
  testBoundaryMustStartALine();
  testPartWithoutHeadersIsBody();
  testFileNamesAreDecoded();
  testEncodedWords();
  testReadLineLimit();
  testBodyQuotedPrintable();
  testVectorAtAndMove();
  testHtmlComposition();
  testSendHtmlTranscript();
  testStreamedBodyIsQuotedPrintable();
  testSendFromStream();
  testLargeMessageIsStreamed();
  testMarkAsReadSendsStore();
  testNoFlagWithoutRequest();
  testSendWithInvalidRecipientFails();

  std::cout << g_checks << " checks, " << g_failures << " failures\n";
  return g_failures == 0 ? 0 : 1;
}
