// Desktop unit tests for TinyMIME: splitting a received mail into parts.

#include <iostream>
#include <string>
#include <vector>

#include <Arduino.h>

#include "TinyEMail/TinyMIME.h"

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

struct Part {
  TinyMailPart info;
  std::string data;
};

// splits the raw message into parts; the data is read with readString()
static std::vector<Part> parts(const std::string& raw) {
  std::vector<Part> out;
  TinyMIME::parse(String(raw.c_str()), [&](const TinyMailPart& p, TinyMailData& d) {
    Part part;
    part.info = p;
    part.data = std::string(d.readString().c_str());
    out.push_back(part);
    return true;
  });
  return out;
}

static void testPlainText() {
  auto p = parts("Subject: x\r\nContent-Type: text/plain; charset=utf-8\r\n\r\nHello");
  CHECK(p.size() == 1);
  CHECK(p[0].info.contentType == "text/plain");
  CHECK(p[0].data == "Hello");
  CHECK(!p[0].info.attachment);
  CHECK(p[0].info.filename.length() == 0);
}

static void testNoContentTypeIsPlainText() {
  auto p = parts("Subject: x\r\n\r\nNoon?\r\n");
  CHECK(p.size() == 1);
  CHECK(p[0].info.contentType == "text/plain");
  CHECK(p[0].data == "Noon?\r\n");
}

static void testHtmlOnly() {
  auto p = parts("Content-Type: text/html\r\n\r\n<b>Hi</b>");
  CHECK(p.size() == 1);
  CHECK(p[0].info.contentType == "text/html");
  CHECK(p[0].data == "<b>Hi</b>");
}

static void testAlternativeHasTextAndHtml() {
  auto p = parts(
      "Content-Type: multipart/alternative; boundary=\"b1\"\r\n\r\n"
      "--b1\r\nContent-Type: text/plain\r\n\r\nplain text\r\n"
      "--b1\r\nContent-Type: text/html\r\n\r\n<p>html</p>\r\n"
      "--b1--\r\n");
  CHECK(p.size() == 2);
  CHECK(p[0].info.contentType == "text/plain" && p[0].data == "plain text");
  CHECK(p[1].info.contentType == "text/html" && p[1].data == "<p>html</p>");
}

static void testBase64Html() {
  // base64 of "<b>Hi</b>"
  auto p = parts(
      "Content-Type: text/html\r\nContent-Transfer-Encoding: base64\r\n\r\n"
      "PGI+SGk8L2I+\r\n");
  CHECK(p.size() == 1 && p[0].data == "<b>Hi</b>");
}

static void testBase64OnePaddingByte() {
  // base64 of "a" is "YQ==": a short last group
  auto p = parts("Content-Type: text/plain\r\nContent-Transfer-Encoding: base64\r\n\r\nYQ==\r\n");
  CHECK(p.size() == 1 && p[0].data == "a");
}

static void testBase64TwoPaddingBytes() {
  // base64 of "ab" is "YWI="
  auto p = parts("Content-Type: text/plain\r\nContent-Transfer-Encoding: base64\r\n\r\nYWI=\r\n");
  CHECK(p.size() == 1 && p[0].data == "ab");
}

static void testQuotedPrintable() {
  auto p = parts(
      "Content-Type: text/plain\r\nContent-Transfer-Encoding: quoted-printable\r\n\r\n"
      "abc=\r\ndef=3Dx\r\n");
  CHECK(p.size() == 1 && p[0].data == "abcdef=x\r\n");  // soft break removed, =3D is '='
}

static void testAttachmentWithFoldedBoundary() {
  // the folded boundary header is joined onto one line
  auto p = parts(
      "Content-Type: multipart/mixed;\r\n boundary=\"mixb\"\r\n\r\n"
      "--mixb\r\nContent-Type: text/html\r\n\r\n<i>see</i>\r\n"
      "--mixb\r\nContent-Type: application/pdf; name=\"a.pdf\"\r\n"
      "Content-Disposition: attachment; filename=\"a.pdf\"\r\n"
      "Content-Transfer-Encoding: base64\r\n\r\nJVBERg==\r\n"
      "--mixb--\r\n");
  CHECK(p.size() == 2);
  CHECK(p[0].info.contentType == "text/html" && p[0].data == "<i>see</i>");
  CHECK(!p[0].info.attachment);
  CHECK(p[1].info.contentType == "application/pdf");
  CHECK(p[1].info.attachment);
  CHECK(p[1].info.filename == "a.pdf");
  CHECK(p[1].data == "%PDF");  // base64 of "%PDF"
}

static void testTextAttachment() {
  auto p = parts(
      "Content-Type: multipart/mixed; boundary=\"m\"\r\n\r\n"
      "--m\r\nContent-Type: text/plain\r\nContent-Disposition: attachment; filename=\"n.txt\"\r\n\r\n"
      "not the body\r\n"
      "--m\r\nContent-Type: text/html\r\n\r\n<p>body</p>\r\n"
      "--m--\r\n");
  CHECK(p.size() == 2);
  CHECK(p[0].info.attachment && p[0].info.filename == "n.txt");
  CHECK(p[0].data == "not the body");
  CHECK(p[1].info.contentType == "text/html" && !p[1].info.attachment);
}

static void testNestedMultipart() {
  auto p = parts(
      "Content-Type: multipart/mixed; boundary=\"outer\"\r\n\r\n"
      "--outer\r\n"
      "Content-Type: multipart/alternative; boundary=\"inner\"\r\n\r\n"
      "--inner\r\nContent-Type: text/plain\r\n\r\nhello\r\n"
      "--inner\r\nContent-Type: text/html\r\n\r\n<p>hello</p>\r\n"
      "--inner--\r\n"
      "--outer\r\nContent-Type: application/zip\r\nContent-Disposition: attachment\r\n\r\nZZ\r\n"
      "--outer--\r\n");
  CHECK(p.size() == 3);
  CHECK(p[0].data == "hello");
  CHECK(p[1].data == "<p>hello</p>");
  CHECK(p[2].info.contentType == "application/zip" && p[2].info.attachment);
}

static void testBinaryPartIsDecoded() {
  // base64 of 89 50 4E 47 0D 0A (the start of a PNG file), with bytes that are not text
  std::string raw =
      "Content-Type: multipart/mixed; boundary=\"x\"\r\n\r\n"
      "--x\r\nContent-Type: image/png\r\nContent-Transfer-Encoding: base64\r\n\r\n"
      "iVBORw0K\r\n"
      "--x--\r\n";
  std::vector<uint8_t> bytes;
  TinyMIME::parse(String(raw.c_str()), [&](const TinyMailPart& p, TinyMailData& d) {
    CHECK(p.contentType == "image/png");
    int c;
    while ((c = d.read()) >= 0) bytes.push_back(static_cast<uint8_t>(c));
    return true;
  });
  CHECK(bytes.size() == 6);
  CHECK(bytes.size() == 6 && bytes[0] == 0x89 && bytes[1] == 'P' && bytes[5] == 0x0A);
}

static void testWriteToPrint() {
  // a Print that counts the bytes, like a File would receive them
  struct Counter : public Print {
    size_t n = 0;
    size_t write(uint8_t) override { n++; return 1; }
  };
  Counter counter;
  TinyMIME::parse(String("Content-Type: text/plain\r\nContent-Transfer-Encoding: base64\r\n\r\n"
                         "PGI+SGk8L2I+\r\n"),
                  [&](const TinyMailPart&, TinyMailData& d) {
                    CHECK(d.writeTo(counter) == 9);
                    return true;
                  });
  CHECK(counter.n == 9);
}

static void testStopEarly() {
  int calls = 0;
  TinyMIME::parse(String("Content-Type: multipart/mixed; boundary=\"s\"\r\n\r\n"
                         "--s\r\n\r\none\r\n--s\r\n\r\ntwo\r\n--s--\r\n"),
                  [&](const TinyMailPart&, TinyMailData&) {
                    calls++;
                    return false;
                  });
  CHECK(calls == 1);
}

static void testHeaderBlockStopsAtBody() {
  CHECK(TinyMIME::headerBlock(String("A: 1\r\n\r\nbody")) == "A: 1");
}

int main() {
  testPlainText();
  testNoContentTypeIsPlainText();
  testHtmlOnly();
  testAlternativeHasTextAndHtml();
  testBase64Html();
  testBase64OnePaddingByte();
  testBase64TwoPaddingBytes();
  testQuotedPrintable();
  testAttachmentWithFoldedBoundary();
  testTextAttachment();
  testNestedMultipart();
  testBinaryPartIsDecoded();
  testWriteToPrint();
  testStopEarly();
  testHeaderBlockStopsAtBody();

  std::cout << g_checks << " checks, " << g_failures << " failures\n";
  return g_failures == 0 ? 0 : 1;
}
