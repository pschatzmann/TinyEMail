// Desktop unit tests for TinyIMAP. Uses the Arduino-Emulator for the Arduino
// core headers and an in-memory client with canned server replies.

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

#include <Arduino.h>

#include "MockClient.h"
#include "TinyEMail/TinyIMAP.h"

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
  CHECK(std::strcmp(TinyIMAP::gmailImapHost, "imap.gmail.com") == 0);
  CHECK(TinyIMAP::gmailImapPort == 993);
}

static void testBeginConnectsToGmail() {
  MockClient client;
  TinyIMAP imap(client);

  CHECK(imap.begin());
  CHECK(client.connectedHost == "imap.gmail.com");
  CHECK(client.connectedPort == 993);
  CHECK(imap.connected());
}

static void testBeginFails() {
  MockClient client;
  client.connectResult = false;
  TinyIMAP imap(client);

  CHECK(!imap.begin());
  CHECK(!imap);
}

static void testGreeting() {
  MockClient client;
  client.incoming = "* OK Gimap ready for requests\r\n";
  TinyIMAP imap(client);
  imap.begin();

  CHECK(imap.readGreeting());
}

static void testGreetingRejected() {
  MockClient client;
  client.incoming = "* BYE too many connections\r\n";
  TinyIMAP imap(client);
  imap.begin();

  CHECK(!imap.readGreeting());
}

static void testLoginSendsQuotedCredentials() {
  MockClient client;
  client.incoming = "A001 OK LOGIN completed\r\n";
  TinyIMAP imap(client);
  imap.begin();

  CHECK(imap.login("me@gmail.com", "abcdefghijklmnop"));
  CHECK(client.written == "A001 LOGIN \"me@gmail.com\" \"abcdefghijklmnop\"\r\n");
}

static void testLoginEscapesQuotesAndBackslashes() {
  MockClient client;
  client.incoming = "A001 OK done\r\n";
  TinyIMAP imap(client);
  imap.begin();

  CHECK(imap.login("a\"b", "c\\d"));
  CHECK(client.written == "A001 LOGIN \"a\\\"b\" \"c\\\\d\"\r\n");
}

static void testLoginRejected() {
  MockClient client;
  client.incoming = "A001 NO [AUTHENTICATIONFAILED] Invalid credentials\r\n";
  TinyIMAP imap(client);
  imap.begin();

  CHECK(!imap.login("me@gmail.com", "wrong"));
  CHECK(imap.lastResponse().startsWith("A001 NO"));
}

static void testCommandCannotInjectLines() {
  MockClient client;
  TinyIMAP imap(client);
  imap.begin();

  CHECK(!imap.login("x\r\nA999 LOGOUT", "p"));
  CHECK(client.written.empty());
}

static void testTagsIncrease() {
  MockClient client;
  client.incoming = "A001 OK one\r\nA002 OK two\r\n";
  TinyIMAP imap(client);
  imap.begin();

  CHECK(imap.select("INBOX"));
  CHECK(imap.select("INBOX"));
  CHECK(client.written ==
        "A001 SELECT \"INBOX\"\r\nA002 SELECT \"INBOX\"\r\n");
}

static void testSelectReadsMessageCount() {
  MockClient client;
  client.incoming =
      "* 172 EXISTS\r\n"
      "* 0 RECENT\r\n"
      "A001 OK [READ-WRITE] SELECT completed\r\n";
  TinyIMAP imap(client);
  imap.begin();

  uint32_t exists = 0;
  CHECK(imap.select("INBOX", &exists));
  CHECK(exists == 172);
}

static void testSelectRejected() {
  MockClient client;
  client.incoming = "A001 NO [NONEXISTENT] Unknown mailbox\r\n";
  TinyIMAP imap(client);
  imap.begin();

  CHECK(!imap.select("Nope"));
}

static void testSearchReturnsUids() {
  MockClient client;
  client.incoming = "* SEARCH 2 5 9\r\nA001 OK SEARCH completed\r\n";
  TinyIMAP imap(client);
  imap.begin();

  String uids;
  CHECK(imap.searchList("ALL", uids));
  CHECK(uids == "2 5 9");
  CHECK(client.written == "A001 UID SEARCH ALL\r\n");
}

static void testSearchNoResults() {
  MockClient client;
  client.incoming = "* SEARCH\r\nA001 OK SEARCH completed\r\n";
  TinyIMAP imap(client);
  imap.begin();

  String uids = "stale";
  CHECK(imap.searchList("UNSEEN", uids));
  CHECK(uids.length() == 0);
}

static void testSearchLongList() {
  // a long answer is one line of UIDs: it is not cut at any buffer size
  std::string answer = "* SEARCH";
  for (int i = 1; i <= 2000; i++) answer += " " + std::to_string(i);
  MockClient client;
  client.incoming = answer + "\r\nA001 OK SEARCH completed\r\n";
  TinyIMAP imap(client);
  imap.begin();

  String uids;
  CHECK(imap.searchList("ALL", uids));
  CHECK(uids.length() > 8000);
  CHECK(uids.startsWith("1 2 3 "));
}

static void testFetchMessageReadsLiteral() {
  const std::string message = "Subject: hi\r\n\r\nbody\r\n";
  MockClient client;
  client.incoming =
      "* 5 FETCH (UID 5 BODY[] {" + std::to_string(message.size()) + "}\r\n" +
      message + ")\r\nA001 OK FETCH completed\r\n";
  TinyIMAP imap(client);
  imap.begin();

  String out;
  CHECK(imap.fetchMessage(5, out));
  CHECK(out == message.c_str());
  CHECK(client.written == "A001 UID FETCH 5 BODY.PEEK[]\r\n");
}

static void testLiteralIsNotParsedAsResponse() {
  // the literal contains a line that looks like the tagged response
  const std::string message = "A001 OK fake\r\nreal\r\n";
  MockClient client;
  client.incoming =
      "* 7 FETCH (UID 7 BODY[] {" + std::to_string(message.size()) + "}\r\n" +
      message + ")\r\nA001 OK FETCH completed\r\n";
  TinyIMAP imap(client);
  imap.begin();

  String out;
  CHECK(imap.fetchMessage(7, out));
  CHECK(out == message.c_str());
  CHECK(client.available() == 0);
}

static void testFetchHeaderUsesPeekHeader() {
  const std::string header = "From: a@b.c\r\nSubject: hi\r\n";
  MockClient client;
  client.incoming =
      "* 3 FETCH (UID 3 BODY[HEADER] {" + std::to_string(header.size()) +
      "}\r\n" + header + ")\r\nA001 OK FETCH completed\r\n";
  TinyIMAP imap(client);
  imap.begin();

  String out;
  CHECK(imap.fetchHeader(3, out));
  CHECK(out == header.c_str());
  CHECK(client.written == "A001 UID FETCH 3 BODY.PEEK[HEADER]\r\n");
}

static void testFetchWithoutLiteralFails() {
  MockClient client;
  client.incoming = "A001 OK FETCH completed\r\n";  // no message data
  TinyIMAP imap(client);
  imap.begin();

  String out;
  CHECK(!imap.fetchMessage(5, out));
}

static void testFetchRejected() {
  MockClient client;
  client.incoming = "A001 NO no such message\r\n";
  TinyIMAP imap(client);
  imap.begin();

  String out;
  CHECK(!imap.fetchMessage(99, out));
}

static void testTimeoutFailsCommand() {
  MockClient client;  // server never answers
  TinyIMAP imap(client);
  imap.begin();
  imap.setTimeout(20);

  CHECK(!imap.login("me@gmail.com", "pw"));
}

static void testLogout() {
  MockClient client;
  client.incoming = "* BYE logging out\r\nA001 OK LOGOUT completed\r\n";
  TinyIMAP imap(client);
  imap.begin();

  CHECK(imap.logout());
  CHECK(client.written == "A001 LOGOUT\r\n");
  imap.end();
  CHECK(!imap.connected());
}

static void testFullSession() {
  const std::string message = "Subject: Hello\r\n\r\nSent from an ESP32.\r\n";
  MockClient client;
  client.incoming =
      "* OK Gimap ready\r\n"
      "A001 OK LOGIN completed\r\n"
      "* 172 EXISTS\r\n"
      "A002 OK [READ-WRITE] SELECT completed\r\n"
      "* SEARCH 170 171\r\n"
      "A003 OK SEARCH completed\r\n"
      "* 1 FETCH (UID 171 BODY[] {" + std::to_string(message.size()) + "}\r\n" +
      message + ")\r\n"
      "A004 OK FETCH completed\r\n"
      "* BYE\r\n"
      "A005 OK LOGOUT completed\r\n";
  TinyIMAP imap(client);

  CHECK(imap.begin());
  CHECK(imap.readGreeting());
  CHECK(imap.login("me@gmail.com", "abcdefghijklmnop"));
  uint32_t exists = 0;
  CHECK(imap.select("INBOX", &exists));
  CHECK(exists == 172);
  String uids;
  CHECK(imap.searchList("ALL", uids));
  CHECK(uids == "170 171");
  String out;
  CHECK(imap.fetchMessage(171, out));
  CHECK(out == message.c_str());
  CHECK(imap.logout());
  imap.end();

  CHECK(client.written ==
        "A001 LOGIN \"me@gmail.com\" \"abcdefghijklmnop\"\r\n"
        "A002 SELECT \"INBOX\"\r\n"
        "A003 UID SEARCH ALL\r\n"
        "A004 UID FETCH 171 BODY.PEEK[]\r\n"
        "A005 LOGOUT\r\n");
}

static void testStreamReadsRawData() {
  MockClient client;
  client.incoming = "* OK hello\r\n";
  TinyIMAP imap(client);
  imap.begin();

  CHECK(imap.peek() == '*');
  CHECK(imap.read() == '*');
  CHECK(imap.available() == static_cast<int>(client.incoming.size()) - 1);
}

static void testPrintWritesRawData() {
  MockClient client;
  TinyIMAP imap(client);
  imap.begin();

  imap.print("A001 NOOP\r\n");
  CHECK(client.written == "A001 NOOP\r\n");
}

static void testSearchListJoinsResults() {
  MockClient client;
  client.incoming = "* SEARCH 2 5\r\n* SEARCH 9\r\nA001 OK SEARCH completed\r\n";
  TinyIMAP imap(client);
  imap.begin();

  String uids;
  CHECK(imap.searchList("ALL", uids));
  CHECK(uids == "2 5 9");
  CHECK(client.written == "A001 UID SEARCH ALL\r\n");
}

static void testSearchListEmpty() {
  MockClient client;
  client.incoming = "* SEARCH\r\nA001 OK SEARCH completed\r\n";
  TinyIMAP imap(client);
  imap.begin();

  String uids = "stale";
  CHECK(imap.searchList("UNSEEN", uids));
  CHECK(uids.length() == 0);
}

int main() {
  testDefaultsAreGmail();
  testBeginConnectsToGmail();
  testBeginFails();
  testGreeting();
  testGreetingRejected();
  testLoginSendsQuotedCredentials();
  testLoginEscapesQuotesAndBackslashes();
  testLoginRejected();
  testCommandCannotInjectLines();
  testTagsIncrease();
  testSelectReadsMessageCount();
  testSelectRejected();
  testSearchReturnsUids();
  testSearchNoResults();
  testSearchLongList();
  testFetchMessageReadsLiteral();
  testLiteralIsNotParsedAsResponse();
  testFetchHeaderUsesPeekHeader();
  testFetchWithoutLiteralFails();
  testFetchRejected();
  testTimeoutFailsCommand();
  testLogout();
  testFullSession();
  testStreamReadsRawData();
  testPrintWritesRawData();
  testSearchListJoinsResults();
  testSearchListEmpty();

  std::cout << g_checks << " checks, " << g_failures << " failures\n";
  return g_failures == 0 ? 0 : 1;
}
