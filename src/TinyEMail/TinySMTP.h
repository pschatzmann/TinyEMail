/**
 * @file TinySMTP.h
 * @brief Header-only SMTP client for Gmail with an Arduino style API.
 *
 * The connection is a Stream (read) and a Print (write). On top of it, the
 * SMTP commands: EHLO, AUTH PLAIN, MAIL FROM, RCPT TO, DATA and QUIT. The
 * message of DATA can be plain text or MIME with base64 attachments.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <Arduino.h>  // Client, Stream, Print

#include "TinyMail.h"
#include "TinyLogger.h"
#include "TinyText.h"

/**
 * @brief SMTP connection with the commands for sending mail.
 *
 * Usage (sending a mail step by step):
 * @code
 * WiFiClientSecure secure;
 * TinySMTP smtp(secure);
 * if (smtp.begin() && smtp.readReply() == 220 &&     // smtp.gmail.com:465
 *     smtp.ehlo("esp32") &&
 *     smtp.authPlain("me@gmail.com", "abcdefghijklmnop") &&  // App Password
 *     smtp.mailFrom("me@gmail.com") &&
 *     smtp.rcptTo("you@example.com") &&
 *     smtp.data("Subject: Hello\r\n\r\nSent from an ESP32.\r\n")) {
 *   smtp.quit();
 * }
 * smtp.end();
 * @endcode
 *
 * Every command returns true if the server answered with the expected reply
 * code. The last reply is available with lastReplyCode() and lastReply().
 * For most uses TinyEMail is simpler.
 */
class TinySMTP : public Stream {
 public:
  /// Gmail SMTP server (submission over implicit TLS)
  static constexpr const char* gmailSmtpHost = "smtp.gmail.com";
  /// Port for SMTP over implicit TLS (SMTPS)
  static constexpr uint16_t gmailSmtpPort = 465;
  /// Largest user + password (before base64) accepted by authPlain()
  static constexpr size_t maxCredentialsSize = 128;

  /**
   * @brief Creates the connection on top of a client that is not connected yet.
   * @param client Arduino client with TLS (not owned, must outlive this object)
   */
  explicit TinySMTP(Client& client) : client_(&client) {
    setTimeout(defaultTimeoutMs);
  }

  /**
   * @brief Connects to the server.
   * @param host server name, by default the Gmail SMTP server
   * @param port server port, by default the Gmail SMTP port
   * @return true if the connection is established
   */
  bool begin(const char* host = gmailSmtpHost, uint16_t port = gmailSmtpPort) {
    if (client_ == nullptr || host == nullptr) {
      return false;
    }
    host_ = host;
    port_ = port;
    if (client_->connect(host, port) == 0) {
      TinyLogger::instance().error(String("SMTP: cannot connect to ") + host + ":" + String(static_cast<unsigned int>(port)));
      return false;
    }
    TinyLogger::instance().debug(String("SMTP: connected to ") + host + ":" + String(static_cast<unsigned int>(port)));
    return true;
  }

  /// Closes the connection. Safe to call when not connected.
  void end() {
    if (client_ != nullptr) {
      client_->stop();
    }
  }

  /// @return true while the connection to the server is open
  bool connected() { return client_ != nullptr && client_->connected(); }

  /// @return true while the connection to the server is open
  operator bool() { return connected(); }

  /// @return the host name given to begin()
  const char* host() const { return host_; }

  /// @return the port given to begin()
  uint16_t port() const { return port_; }

  // ---- Print: write data to the server ----------------------------------

  /// Writes one byte to the server
  size_t write(uint8_t b) override {
    return client_ != nullptr ? client_->write(b) : 0;
  }

  /// Writes a buffer to the server
  size_t write(const uint8_t* buffer, size_t size) override {
    return client_ != nullptr ? client_->write(buffer, size) : 0;
  }

  /// Sends the data that was buffered by the client
  void flush() override {
    if (client_ != nullptr) {
      client_->flush();
    }
  }

  // ---- Stream: read data from the server --------------------------------

  /// @return the number of bytes received and not read yet
  int available() override {
    return client_ != nullptr ? client_->available() : 0;
  }

  /// @return the next byte from the server, or -1 if there is none
  int read() override { return client_ != nullptr ? client_->read() : -1; }

  /// @return the next byte from the server without removing it, or -1
  int peek() override { return client_ != nullptr ? client_->peek() : -1; }

  // ---- SMTP commands ------------------------------------------------------

  /**
   * @brief Reads one complete reply of the server (all lines of a multi-line
   * reply such as "250-..." followed by "250 ...").
   * @return the 3 digit reply code, or -1 on timeout or if the reply is invalid
   */
  int readReply() {
    lastCode_ = -1;
    lastReply_ = "";
    while (true) {
      String line;
      bool complete = TinyText::readLine(*this, line, maxReplyLength);
      if (!complete) {
        broken_ = true;  // timeout, or a line longer than maxReplyLength
        TinyLogger::instance().error("SMTP: no complete reply from the server (timeout or line too long)");
        return -1;
      }
      line.trim();
      if (line.length() < 3 || !isDigitChar(line.charAt(0)) ||
          !isDigitChar(line.charAt(1)) || !isDigitChar(line.charAt(2))) {
        lastReply_ = line;
        TinyLogger::instance().error(String("SMTP: invalid reply: ") + line);
        return -1;
      }
      TinyLogger::instance().debug(String("S: ") + line);
      if (lastReply_.length() > 0) {
        lastReply_ += '\n';
      }
      lastReply_ += line;
      lastCode_ = (line.charAt(0) - '0') * 100 + (line.charAt(1) - '0') * 10 +
                  (line.charAt(2) - '0');
      // "250 text" is the last line, "250-text" is followed by more lines
      if (line.length() == 3 || line.charAt(3) == ' ') {
        break;
      }
      if (line.charAt(3) != '-') {
        TinyLogger::instance().error(String("SMTP: invalid reply: ") + line);
        lastCode_ = -1;
        return -1;
      }
    }
    return lastCode_;
  }

  /// true if the connection can no longer be used (timeout or a too long reply). Close it.
  bool broken() const { return broken_; }

  /// @return the reply code of the last reply, -1 if there was none or it was invalid
  int lastReplyCode() const { return lastCode_; }

  /// @return the text of the last reply, all lines separated by '\n'
  String lastReply() const { return lastReply_; }

  /// Sends EHLO. Expects reply 250.
  bool ehlo(const char* domain = "localhost") {
    return command("EHLO ", domain, 250);
  }

  /**
   * @brief Logs in with AUTH PLAIN. Expects reply 235.
   *
   * Gmail requires an App Password here: a normal account password is refused.
   * @return false if the credentials are too long (see maxCredentialsSize)
   */
  bool authPlain(const char* user, const char* appPassword) {
    if (user == nullptr || appPassword == nullptr) {
      TinyLogger::instance().error("SMTP: AUTH PLAIN without user or password");
      return false;
    }
    size_t userLen = strlen(user);
    size_t passLen = strlen(appPassword);
    if (userLen + passLen + 2 > maxCredentialsSize) {
      TinyLogger::instance().error("SMTP: user and password are too long for AUTH PLAIN");
      return false;
    }
    // SASL PLAIN: authzid (empty) NUL authcid NUL passwd
    uint8_t raw[maxCredentialsSize];
    size_t n = 0;
    raw[n++] = 0;
    memcpy(raw + n, user, userLen);
    n += userLen;
    raw[n++] = 0;
    memcpy(raw + n, appPassword, passLen);
    n += passLen;

    char encoded[((maxCredentialsSize + 2) / 3) * 4 + 1];
    size_t len = TinyText::base64Encode(raw, n, encoded);
    encoded[len] = '\0';

    TinyLogger::instance().debug("C: AUTH PLAIN ****");  // the credentials are never logged
    print("AUTH PLAIN ");
    print(encoded);
    print("\r\n");
    flush();
    return expect(235);
  }

  /// Sends MAIL FROM. Expects reply 250.
  bool mailFrom(const char* address) {
    if (!isAddress(address)) {
      TinyLogger::instance().error("SMTP: invalid sender address");
      return false;
    }
    return command("MAIL FROM:<", address, ">", 250);
  }

  /// Sends RCPT TO. Expects reply 250 (or 251 if the server forwards it).
  bool rcptTo(const char* address) {
    if (!isAddress(address)) {
      TinyLogger::instance().error("SMTP: invalid recipient address");
      return false;
    }
    return command("RCPT TO:<", address, ">", 250, 251);
  }

  /// Sends DATA and expects reply 354. Then write the message with dataText() or writeMail(), and call endData().
  bool beginData() {
    dataLineStart_ = true;
    return command("DATA", 354);
  }

  /// Ends the message sent after beginData() with "." and expects reply 250.
  bool endData() {
    flushDataCR();
    if (!dataLineStart_) {
      print("\r\n");
    }
    print(".\r\n");
    flush();
    dataLineStart_ = true;
    return expect(250);
  }

  /**
   * @brief Writes text of the message: CRLF line endings and dot stuffing.
   * Call it between beginData() and endData().
   */
  void dataText(const char* text) {
    size_t n = strlen(text);
    for (size_t i = 0; i < n; i++) {
      dataChar(text[i]);
    }
    flushDataCR();
  }

  /**
   * @brief Sends a complete message with DATA. Expects replies 354 and 250.
   *
   * The message uses CRLF or LF line endings; both are sent as CRLF.
   * @param message the complete message: headers, an empty line, then the body
   */
  bool data(const char* message) {
    if (message == nullptr || !beginData()) {
      return false;
    }
    dataText(message);
    return endData();
  }

  /**
   * @brief Writes a mail between beginData() and endData().
   *
   * The first attachment is the text body (a text type, e.g. text/plain
   * or text/html); the others are files. A mail with only the text body is sent
   * as that text. With files it is MIME multipart, with the text as the first part.
   * @param from the sender address
   */
  bool writeMail(const char* from, const TinyMail& mail) {
    String subject = singleLine(mail.subject.c_str());
    dataText("From: ");
    dataText(from);
    dataText("\r\nTo: ");
    writeRecipients(mail.to);
    dataText("\r\nSubject: ");
    dataText(subject.c_str());
    dataText("\r\nMIME-Version: 1.0\r\n");

    const TinyMailAttachment* body = mail.attachments.empty() ? nullptr : &mail.attachments[0];
    if (mail.attachments.size() <= 1) {
      dataText("Content-Type: ");
      dataText(body != nullptr ? body->contentType : "text/plain");
      dataText("; charset=utf-8\r\nContent-Transfer-Encoding: quoted-printable\r\n\r\n");
      if (body != nullptr) {
        writeBody(*body);
      }
      return true;
    }

    dataText("Content-Type: multipart/mixed; boundary=\"");
    dataText(kBoundary);
    dataText("\"\r\n\r\n--");
    dataText(kBoundary);
    dataText("\r\nContent-Type: ");
    dataText(body->contentType);
    dataText("; charset=utf-8\r\nContent-Transfer-Encoding: quoted-printable\r\n\r\n");
    writeBody(*body);
    dataText("\r\n");

    for (size_t i = 1; i < mail.attachments.size(); i++) {
      const TinyMailAttachment& a = mail.attachments[i];
      dataText("--");
      dataText(kBoundary);
      dataText("\r\nContent-Type: ");
      dataText(a.contentType);
      dataText("; name=\"");
      dataText(a.filename);
      dataText("\"\r\nContent-Disposition: attachment; filename=\"");
      dataText(a.filename);
      dataText("\"\r\nContent-Transfer-Encoding: base64\r\n\r\n");
      writeBase64(a);
    }
    dataText("--");
    dataText(kBoundary);
    dataText("--\r\n");
    return true;
  }

  /**
   * @brief Starts a message: DATA, then the headers. The body is then written
   * with bodyByte() (quoted-printable), and the message ends with endMessage().
   * Nothing is kept in RAM.
   * @param contentType a text type, e.g. "text/plain" or "text/html"
   */
  bool beginMessage(const char* from, const TinyVector<String>& to, const char* subject,
                    const char* contentType) {
    if (!beginData()) {
      return false;
    }
    String line = singleLine(subject);
    dataText("From: ");
    dataText(from);
    dataText("\r\nTo: ");
    writeRecipients(to);
    dataText("\r\nSubject: ");
    dataText(line.c_str());
    dataText("\r\nMIME-Version: 1.0\r\nContent-Type: ");
    dataText(contentType);
    dataText("; charset=utf-8\r\nContent-Transfer-Encoding: quoted-printable\r\n\r\n");
    qpColumn_ = 0;
    qpPendingCount_ = 0;
    qpCR_ = false;
    return true;
  }

  /// One byte of the body of a message started with beginMessage()
  void bodyByte(uint8_t b) { qpByte(b); }

  /// Ends the body and the message: expects reply 250
  bool endMessage() {
    if (qpCR_) {
      qpCR_ = false;
      qpHardBreak();
    }
    qpEncodePending();  // trailing spaces must be encoded
    return endData();
  }

  /// Sends QUIT. Expects reply 221. Close the connection with end() afterwards.
  bool quit() { return command("QUIT", 221); }

  /**
   * @brief true if text is an e-mail address that can be written in a command:
   * not empty, and without spaces, angle brackets, quotes or control characters.
   */
  static bool isAddress(const char* text) {
    if (text == nullptr || text[0] == '\0') {
      return false;
    }
    for (const char* p = text; *p != '\0'; ++p) {
      unsigned char c = static_cast<unsigned char>(*p);
      if (c <= ' ' || c == '<' || c == '>' || c == '"' || c == 0x7F) {
        return false;
      }
    }
    return true;
  }

  /// false for null or for text with line breaks (it would inject protocol lines)
  static bool isSafe(const char* text) {
    return text != nullptr && strpbrk(text, "\r\n") == nullptr;
  }

  /// false if the text has a line break
  static bool isSafeText(const String& text) {
    return text.indexOf('\r') < 0 && text.indexOf('\n') < 0;
  }

  /**
   * @brief true if the mail can be sent: a recipient, and attachments that can be
   * written safely. The first attachment must be text (a text type), the others need a file name.
   */
  static bool isValidMail(const TinyMail& mail) {
    if (mail.to.size() + mail.bcc.size() == 0) {
      return false;  // at least one recipient
    }
    for (const String& address : mail.to) {
      if (!isAddress(address.c_str())) {
        return false;
      }
    }
    for (const String& address : mail.bcc) {
      if (!isAddress(address.c_str())) {
        return false;
      }
    }
    for (size_t i = 0; i < mail.attachments.size(); i++) {
      const TinyMailAttachment& a = mail.attachments[i];
      // the content type is a header value: no line breaks, parameters or quotes
      if (!isSafe(a.contentType) || strpbrk(a.contentType, "; \\\"") != nullptr ||
          a.stream == nullptr) {
        return false;
      }
      if (i == 0) {
        if (strncmp(a.contentType, "text/", 5) != 0) {
          return false;  // the text body
        }
        continue;
      }
      if (a.filename == nullptr || !isSafe(a.filename) ||
          strpbrk(a.filename, "\"\\") != nullptr) {
        return false;
      }
    }
    return true;
  }

 private:
  static constexpr uint32_t defaultTimeoutMs = 10000;
  static constexpr size_t maxReplyLength = 1000;  // RFC 5321 allows 512 characters
  static constexpr const char* kBoundary = "TinyEMailBoundary";

  Client* client_ = nullptr;
  const char* host_ = nullptr;
  uint16_t port_ = 0;
  int lastCode_ = -1;
  String lastReply_;
  bool broken_ = false;
  bool dataLineStart_ = true;  // for dot stuffing in DATA
  bool pendingCR_ = false;     // a CR seen, waiting for the LF
  bool qpCR_ = false;          // quoted-printable: a CR seen, waiting for the LF
  size_t qpColumn_ = 0;        // quoted-printable: characters on the current line
  static constexpr uint8_t qpPendingMax = 64;
  uint8_t qpPending_[qpPendingMax];
  uint8_t qpPendingCount_ = 0;
  uint8_t b64Group_[3] = {0, 0, 0};
  uint8_t b64Count_ = 0;
  uint8_t b64Column_ = 0;

  static bool isDigitChar(char c) { return c >= '0' && c <= '9'; }

  /// Replaces line breaks with spaces, so that a header cannot be split
  static String singleLine(const char* text) {
    String out = text;
    for (unsigned int i = 0; i < out.length(); i++) {
      if (out.charAt(i) == '\r' || out.charAt(i) == '\n') {
        out.setCharAt(i, ' ');
      }
    }
    return out;
  }

  /// Reads a reply and returns true if its code is the expected one
  bool expect(int expectedCode) {
    int code = readReply();
    if (code == expectedCode) {
      return true;
    }
    if (code >= 0) {
      TinyLogger::instance().warn(String("SMTP: expected reply ") + String(expectedCode) + ", got: " + lastReply_);
    }
    return false;
  }

  bool command(const char* a, const char* b, int expectedCode) {
    return command(a, b, "", expectedCode, -1);
  }

  bool command(const char* a, const char* b, const char* c, int expectedCode,
               int alternativeCode = -1) {
    TinyLogger::instance().debug(String("C: ") + a + b + c);
    print(a);
    print(b);
    print(c);
    print("\r\n");
    flush();
    int code = readReply();
    return code == expectedCode || (alternativeCode >= 0 && code == alternativeCode);
  }

  bool command(const char* text, int expectedCode) {
    TinyLogger::instance().debug(String("C: ") + text);
    print(text);
    print("\r\n");
    flush();
    return expect(expectedCode);
  }

  /// Writes one character of the DATA text: CRLF line endings and dot stuffing
  void dataChar(char c) {
    if (pendingCR_) {
      pendingCR_ = false;
      print("\r\n");  // the held back CR is a line break
      dataLineStart_ = true;
      if (c == '\n') {
        return;  // this LF completes that CRLF
      }
    }
    if (c == '\r') {
      pendingCR_ = true;  // wait for the LF: CRLF is written once
      return;
    }
    if (c == '\n') {
      print("\r\n");
      dataLineStart_ = true;
      return;
    }
    if (dataLineStart_ && c == '.') {
      write(static_cast<uint8_t>('.'));  // dot stuffing
    }
    write(static_cast<uint8_t>(c));
    dataLineStart_ = false;
  }

  /// Writes a CR that is still waiting at the end of a text
  void flushDataCR() {
    if (pendingCR_) {
      pendingCR_ = false;
      print("\r\n");
      dataLineStart_ = true;
    }
  }

  /// The To header: the addresses separated by commas. Without addresses the
  /// recipients are not disclosed (only bcc recipients).
  void writeRecipients(const TinyVector<String>& to) {
    if (to.empty()) {
      dataText("undisclosed-recipients:;");
      return;
    }
    bool first = true;
    for (const String& address : to) {
      if (!first) {
        dataText(", ");
      }
      dataText(address.c_str());
      first = false;
    }
  }

  /**
   * @brief Writes the text body as quoted-printable (RFC 2045): lines are at most
   * 76 characters, long lines get soft breaks, and CR, LF, CRLF all become CRLF.
   */
  void writeBody(const TinyMailAttachment& a) {
    qpColumn_ = 0;
    qpPendingCount_ = 0;
    qpCR_ = false;
    int c;
    while ((c = a.stream->read()) >= 0) {
      qpByte(static_cast<uint8_t>(c));
    }
    if (qpCR_) {
      qpCR_ = false;
      qpHardBreak();
    }
    qpEncodePending();  // trailing spaces must be encoded
    flushDataCR();
  }

  void qpByte(uint8_t b) {
    if (qpCR_) {
      qpCR_ = false;
      qpHardBreak();
      if (b == '\n') {
        return;  // this LF completes the CRLF
      }
    }
    if (b == '\r') {
      qpCR_ = true;  // wait for the LF
      return;
    }
    if (b == '\n') {
      qpHardBreak();
      return;
    }
    if (b == ' ' || b == '\t') {
      if (qpPendingCount_ < qpPendingMax) {
        qpPending_[qpPendingCount_++] = b;  // written only if no line break follows
      } else {
        qpEncodePending();
        qpEncodeByte(b);
      }
      return;
    }
    qpFlushPending();  // a space before a visible character is written as it is
    if (b >= 33 && b <= 126 && b != '=') {
      char c = static_cast<char>(b);
      qpToken(&c, 1);
    } else {
      qpEncodeByte(b);
    }
  }

  /// A line break: pending spaces before it are encoded, then CRLF
  void qpHardBreak() {
    qpEncodePending();
    dataChar('\r');
    dataChar('\n');
    qpColumn_ = 0;
  }

  void qpFlushPending() {
    for (uint8_t i = 0; i < qpPendingCount_; i++) {
      char c = static_cast<char>(qpPending_[i]);
      qpToken(&c, 1);
    }
    qpPendingCount_ = 0;
  }

  void qpEncodePending() {
    for (uint8_t i = 0; i < qpPendingCount_; i++) {
      qpEncodeByte(qpPending_[i]);
    }
    qpPendingCount_ = 0;
  }

  void qpEncodeByte(uint8_t b) {
    static const char hex[] = "0123456789ABCDEF";
    char token[3] = {'=', hex[b >> 4], hex[b & 0x0F]};
    qpToken(token, 3);
  }

  /// Writes a token; a soft line break first if it would make the line longer than 75 characters
  void qpToken(const char* token, size_t length) {
    if (qpColumn_ + length > 75) {
      dataChar('=');
      dataChar('\r');
      dataChar('\n');
      qpColumn_ = 0;
    }
    for (size_t i = 0; i < length; i++) {
      dataChar(token[i]);
    }
    qpColumn_ += length;
  }

  /// Writes the content of an attachment base64 encoded, in lines of 76 characters
  void writeBase64(const TinyMailAttachment& a) {
    b64Count_ = 0;
    b64Column_ = 0;
    int c;
    while ((c = a.stream->read()) >= 0) {
      base64Put(static_cast<uint8_t>(c));
    }
    if (b64Count_ > 0) {
      base64Quad(b64Group_, b64Count_);
    }
    if (b64Column_ > 0) {
      print("\r\n");
    }
    b64Count_ = 0;
    b64Column_ = 0;
    dataLineStart_ = true;
  }

  void base64Put(uint8_t b) {
    b64Group_[b64Count_++] = b;
    if (b64Count_ == 3) {
      base64Quad(b64Group_, 3);
      b64Count_ = 0;
    }
  }

  /// Writes up to 3 bytes as 4 base64 characters, with a line break every 76 characters
  void base64Quad(const uint8_t* group, uint8_t n) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t triple = static_cast<uint32_t>(group[0]) << 16;
    if (n > 1) {
      triple |= static_cast<uint32_t>(group[1]) << 8;
    }
    if (n > 2) {
      triple |= group[2];
    }
    char out[4] = {alphabet[(triple >> 18) & 0x3F], alphabet[(triple >> 12) & 0x3F],
                   n > 1 ? alphabet[(triple >> 6) & 0x3F] : '=',
                   n > 2 ? alphabet[triple & 0x3F] : '='};
    for (int i = 0; i < 4; i++) {
      write(static_cast<uint8_t>(out[i]));
    }
    b64Column_ += 4;
    if (b64Column_ >= 76) {
      print("\r\n");
      b64Column_ = 0;
    }
  }

};

/**
 * @brief A Print that writes the body of a message started with
 * TinySMTP::beginMessage(). The bytes are quoted-printable encoded and sent.
 */
class TinySMTPBody : public Print {
 public:
  explicit TinySMTPBody(TinySMTP& smtp) : smtp_(smtp) {}

  size_t write(uint8_t b) override {
    smtp_.bodyByte(b);
    return 1;
  }

 private:
  TinySMTP& smtp_;
};
