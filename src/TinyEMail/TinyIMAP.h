/**
 * @file TinyIMAP.h
 * @brief Header-only IMAP client for Gmail with an Arduino style API.
 *
 * Reads mail from Gmail (imap.gmail.com:993, implicit TLS). Implemented
 * commands: LOGIN, SELECT, UID SEARCH, UID FETCH (BODY.PEEK, so the messages
 * are not marked as read) and LOGOUT.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Arduino.h>  // Client, Stream, Print

#include "TinyLogger.h"
#include "TinyText.h"

/**
 * @brief IMAP client for Gmail: transport plus IMAP commands.
 *
 * Usage (reading the mail of the inbox):
 * @code
 * WiFiClientSecure secure;
 * TinyIMAP imap(secure);
 * if (imap.begin() && imap.readGreeting() &&
 *     imap.login("me@gmail.com", "abcdefghijklmnop") && // App Password
 *     imap.select("INBOX")) {
 *   String uids;                       // "4 7 12", the UIDs of the unread mails
 *   if (imap.searchList("UNSEEN", uids)) {
 *     // parse the numbers of uids, and for each uid:
 *     String message;
 *     if (imap.fetchMessage(4, message)) {
 *       // message holds the complete RFC 822 text
 *     }
 *   }
 *   imap.logout();
 * }
 * imap.end();
 * @endcode
 *
 * Every command returns true if the server answered with a tagged OK. The
 * server's last tagged response is available with lastResponse().
 * Message data is returned as a String, so binary data with NUL bytes is not
 * supported.
 */
class TinyIMAP : public Stream {
 public:

  /// Receives the data of a literal (e.g. a message) as a stream
  class LiteralHandler {
   public:
    virtual ~LiteralHandler() {}
    /// Reads the data from the stream; what is not read is skipped. false if the data is not usable.
    virtual bool handle(Stream& data, size_t size) = 0;
  };

  /// Gmail IMAP server (implicit TLS)
  static constexpr const char* gmailImapHost = "imap.gmail.com";
  /// Port for IMAP over implicit TLS
  static constexpr uint16_t gmailImapPort = 993;

  /**
   * @brief Creates the client on top of a TLS client that is not connected yet.
   * @param client Arduino client with TLS (not owned, must outlive this object)
   */
  explicit TinyIMAP(Client& client) : client_(&client) {
    setTimeout(defaultTimeoutMs);
    tag_[0] = '\0';
  }

  /**
   * @brief Connects to the server. Call readGreeting() afterwards.
   * @param host server name, by default the Gmail IMAP server
   * @param port server port, by default the Gmail IMAP port
   * @return true if the connection is established
   */
  bool begin(const char* host = gmailImapHost, uint16_t port = gmailImapPort) {
    if (client_ == nullptr || host == nullptr) {
      return false;
    }
    host_ = host;
    port_ = port;
    if (client_->connect(host, port) == 0) {
      TinyLogger::instance().error(String("IMAP: cannot connect to ") + host + ":" + String(static_cast<unsigned int>(port)));
      return false;
    }
    TinyLogger::instance().debug(String("IMAP: connected to ") + host + ":" + String(static_cast<unsigned int>(port)));
    return true;
  }

  /// Closes the connection without LOGOUT. Use logout() first.
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

  // ---- IMAP ----------------------------------------------------------------

  /// Reads the server greeting ("* OK ..."). Call it right after begin().
  bool readGreeting() {
    String line;
    if (!readLine(line)) {
      TinyLogger::instance().error("IMAP: no greeting from the server");
      return false;
    }
    TinyLogger::instance().debug(String("S: ") + line);
    lastResponse_ = line;
    if (line.startsWith("* OK") || line.startsWith("* PREAUTH")) {
      return true;
    }
    TinyLogger::instance().error(String("IMAP: unexpected greeting: ") + line);
    return false;
  }

  /**
   * @brief Logs in with LOGIN. Gmail requires an App Password here.
   * @param user the Gmail address
   * @param password the 16 character App Password
   */
  bool login(const char* user, const char* password) {
    if (user == nullptr || password == nullptr) {
      return false;
    }
    String command = "LOGIN ";
    command += quote(user);
    command += ' ';
    command += quote(password);
    return run(command.c_str());
  }

  /**
   * @brief Selects a mailbox, e.g. "INBOX" or "[Gmail]/Sent Mail".
   * @param mailbox name of the mailbox
   * @param exists if not null, receives the number of messages in the mailbox
   */
  bool select(const char* mailbox = "INBOX", uint32_t* exists = nullptr) {
    if (mailbox == nullptr) {
      return false;
    }
    String command = "SELECT ";
    command += quote(mailbox);
    if (!run(command.c_str())) {
      return false;
    }
    if (exists != nullptr) {
      *exists = 0;
      int pos = 0;
      String line;
      while (nextLine(lastUntagged_, pos, line)) {
        if (line.startsWith("* ") && line.endsWith(" EXISTS")) {
          *exists = strtoul(line.c_str() + 2, nullptr, 10);
        }
      }
    }
    return true;
  }

  /**
   * @brief Searches the selected mailbox with UID SEARCH and returns the UIDs as
   * text, separated by spaces (empty if nothing was found). There is no limit
   * on the number of UIDs.
   * @param criteria IMAP search keys, e.g. "ALL" or "UNSEEN"
   * @param uids receives the UIDs, e.g. "4 7 12"
   */
  bool searchList(const char* criteria, String& uids) {
    if (criteria == nullptr) {
      TinyLogger::instance().error("IMAP: search without criteria");
      return false;
    }
    String command = "UID SEARCH ";
    command += criteria;
    uids = "";
    if (!run(command.c_str())) {
      return false;
    }
    int pos = 0;
    String line;
    while (nextLine(lastUntagged_, pos, line)) {
      if (line.startsWith("* SEARCH")) {
        String found = line.substring(8);
        found.trim();
        if (found.length() > 0) {
          if (uids.length() > 0) {
            uids += ' ';
          }
          uids += found;
        }
      }
    }
    return true;
  }

  /**
   * @brief Fetches the complete message (headers and body) and marks nothing as read.
   * @param uid UID of the message, from searchList()
   * @param message receives the RFC 822 text of the message
   */
  bool fetchMessage(uint32_t uid, String& message) {
    return fetchItem(uid, "BODY.PEEK[]", message);
  }

  /**
   * @brief Fetches only the headers of a message.
   * @param uid UID of the message, from searchList()
   * @param header receives the header lines of the message
   */
  bool fetchHeader(uint32_t uid, String& header) {
    return fetchItem(uid, "BODY.PEEK[HEADER]", header);
  }

  /// Sends LOGOUT and reads the reply. Close the connection with end() afterwards.
  bool logout() { return run("LOGOUT"); }

  /**
   * @brief Adds (on = true) or removes a flag of a message, e.g. "\\Seen" or "\\Flagged".
   * UID STORE is sent, so the message is marked on the server.
   */
  bool setFlag(uint32_t uid, const char* flag, bool on) {
    String command = "UID STORE ";
    command += String(static_cast<unsigned long>(uid));
    command += on ? " +FLAGS (" : " -FLAGS (";
    command += flag;
    command += ')';
    return run(command.c_str());
  }

  /**
   * @brief Fetches one item of a message (e.g. "BODY.PEEK[]") and gives its data to
   * the handler as a stream. The message is never held in RAM.
   * @return false if the handler or the server did not accept the data; check
   * broken() to see whether the connection is still usable
   */
  bool fetchStreamed(uint32_t uid, const char* item, LiteralHandler& handler) {
    String command = "UID FETCH ";
    command += String(static_cast<unsigned long>(uid));
    command += ' ';
    command += item;
    literalSize_ = -1;
    if (!run(command.c_str(), nullptr, &handler)) {
      return false;
    }
    if (literalSize_ < 0) {
      TinyLogger::instance().warn(String("IMAP: no message data in the answer to ") + command);
      return false;
    }
    return true;
  }

  /// @return the last tagged response of the server, e.g. "A001 NO [AUTHENTICATIONFAILED] ..."
  String lastResponse() const { return lastResponse_; }

  /// @return the untagged responses of the last command, one per line
  String lastUntagged() const { return lastUntagged_; }

  /**
   * @brief Sets the largest line and the largest literal (message data) the
   * server may send. A server that sends more is treated as broken.
   */
  void setLimits(size_t maxLineLength, size_t maxLiteralSize) {
    maxLineLength_ = maxLineLength;
    maxLiteralSize_ = maxLiteralSize;
  }

  /**
   * @brief true if the connection can no longer be used: a line or literal was
   * too long, or the server did not answer in time. Close it.
   */
  bool broken() const { return broken_; }

 private:
  static constexpr uint32_t defaultTimeoutMs = 10000;

  Client* client_ = nullptr;
  const char* host_ = nullptr;
  uint16_t port_ = 0;
  uint16_t tagCounter_ = 0;
  char tag_[8];
  String lastUntagged_;
  String lastResponse_;
  int literalSize_ = -1;

  /// The data of a literal of exactly size bytes, read from the connection
  class LiteralStream : public Stream {
   public:
    LiteralStream(TinyIMAP& imap, size_t size) : imap_(imap), left_(size) {}

    int available() override { return left_ > 0 ? 1 : 0; }
    int read() override {
      if (left_ == 0 || timedOut_) {
        return -1;
      }
      unsigned long start = millis();
      unsigned long timeout = imap_.getTimeout();
      while (true) {
        int c = imap_.read();
        if (c >= 0) {
          left_--;
          return c;
        }
        if (millis() - start > timeout) {
          timedOut_ = true;
          return -1;
        }
      }
    }
    int peek() override { return -1; }  // not needed: the data is read in order
    size_t write(uint8_t) override { return 0; }

    bool timedOut() const { return timedOut_; }

   private:
    TinyIMAP& imap_;
    size_t left_;
    bool timedOut_ = false;
  };
  size_t maxLineLength_ = 32768;   // SEARCH answers with many UIDs are long lines
  size_t maxLiteralSize_ = 0;  // largest message the server may send; 0 means no check
  bool broken_ = false;

  /// Reads a line without the CRLF. false on timeout or if the line is too long.
  bool readLine(String& line) {
    if (!TinyText::readLine(*this, line, maxLineLength_)) {
      broken_ = true;
      TinyLogger::instance().error("IMAP: timeout, or a line longer than the limit");
      return false;
    }
    return true;
  }

  /// Quotes a string for IMAP (escapes backslash and double quote)
  static String quote(const char* text) {
    String out = "\"";
    for (const char* p = text; *p != '\0'; ++p) {
      if (*p == '"' || *p == '\\') {
        out += '\\';
      }
      out += *p;
    }
    out += '"';
    return out;
  }

  /// Returns the next line of text (lines are separated by '\n')
  static bool nextLine(const String& text, int& pos, String& line) {
    if (pos < 0 || pos >= static_cast<int>(text.length())) {
      return false;
    }
    int end = text.indexOf('\n', pos);
    if (end < 0) {
      end = text.length();
    }
    line = text.substring(pos, end);
    pos = end + 1;
    return true;
  }

  /// If the line ends with a literal marker "{n}", returns n, otherwise -1
  static int literalSizeAt(const String& line) {
    if (!line.endsWith("}")) {
      return -1;
    }
    int open = line.lastIndexOf('{');
    if (open < 0 || open >= static_cast<int>(line.length()) - 2) {
      return -1;
    }
    const char* digits = line.c_str() + open + 1;
    char* end = nullptr;
    unsigned long size = strtoul(digits, &end, 10);
    if (end != line.c_str() + line.length() - 1 || end == digits) {
      return -1;
    }
    return static_cast<int>(size);
  }

  /// Sends a tagged command and collects the responses until the tagged one.
  /// Literal data goes to literal, or to handler as a stream; otherwise it is skipped.
  bool run(const char* text, String* literal = nullptr, LiteralHandler* handler = nullptr) {
    if (text == nullptr || strpbrk(text, "\r\n") != nullptr) {
      TinyLogger::instance().error("IMAP: command with a line break refused");
      return false;  // no line breaks: they would inject commands
    }
    lastUntagged_ = "";
    lastResponse_ = "";
    literalSize_ = -1;

    tagCounter_ = (tagCounter_ % 999) + 1;
    snprintf(tag_, sizeof(tag_), "A%03u", static_cast<unsigned>(tagCounter_));
    size_t tagLen = strlen(tag_);

    // the password of LOGIN is never logged
    bool secret = strncmp(text, "LOGIN ", 6) == 0;
    TinyLogger::instance().debug(String("C: ") + tag_ + " " + (secret ? "LOGIN ****" : text));
    print(tag_);
    print(' ');
    print(text);
    print("\r\n");
    flush();

    bool dataOk = true;  // false if the handler did not accept the literal
    while (true) {
      String line;
      if (!readLine(line)) {
        return false;  // timeout or too long
      }
      if (line.startsWith(tag_) && line.charAt(tagLen) == ' ') {
        lastResponse_ = line;
        bool ok = strncmp(line.c_str() + tagLen + 1, "OK", 2) == 0 && dataOk;
        if (!ok) {
          TinyLogger::instance().warn(String("IMAP: command failed: ") + line);
        }
        return ok;
      }
      TinyLogger::instance().debug(String("S: ") + line);
      if (lastUntagged_.length() + line.length() > maxLineLength_ * 4) {
        broken_ = true;
        TinyLogger::instance().error("IMAP: responses too long, connection dropped");
        return false;
      }
      lastUntagged_ += line;
      lastUntagged_ += '\n';

      int size = literalSizeAt(line);
      if (size >= 0) {
        if (handler != nullptr) {
          if (!readHandled(static_cast<size_t>(size), *handler)) {
            if (broken_) {
              return false;
            }
            dataOk = false;  // the answer is still read, to stay in step
          }
        } else if (!readLiteral(static_cast<size_t>(size), literal)) {
          return false;
        }
        literalSize_ = size;
      }
    }
  }

  /**
   * @brief Gives a literal to a handler as a stream of exactly size bytes. What the
   * handler does not read is skipped. A literal over the limit is skipped too.
   */
  bool readHandled(size_t size, LiteralHandler& handler) {
    if (maxLiteralSize_ > 0 && size > maxLiteralSize_) {
      TinyLogger::instance().warn(String("IMAP: message of ") + String(static_cast<unsigned long>(size)) +
                                  " bytes is over the limit of " +
                                  String(static_cast<unsigned long>(maxLiteralSize_)) + ", skipped");
      LiteralStream rest(*this, size);
      while (rest.read() >= 0) {
      }
      if (rest.timedOut()) {
        broken_ = true;
        TinyLogger::instance().error("IMAP: timeout while skipping message data");
      }
      return false;
    }
    LiteralStream data(*this, size);
    bool ok = handler.handle(data, size);
    while (data.read() >= 0) {
    }
    if (data.timedOut()) {
      broken_ = true;
      TinyLogger::instance().error("IMAP: timeout while reading message data");
      return false;
    }
    return ok;
  }

  /// Reads exactly size bytes of a literal
  bool readLiteral(size_t size, String* target) {
    if (maxLiteralSize_ > 0 && size > maxLiteralSize_) {
      broken_ = true;  // the data is not read: the stream is out of step
      TinyLogger::instance().error(String("IMAP: message of ") + String(static_cast<unsigned long>(size)) +
                   " bytes is over the limit of " + String(static_cast<unsigned long>(maxLiteralSize_)));
      return false;
    }
    if (target != nullptr) {
      *target = "";
      target->reserve(size);
    }
    char buffer[64];
    size_t left = size;
    while (left > 0) {
      size_t chunk = left < sizeof(buffer) ? left : sizeof(buffer);
      size_t n = readBytes(buffer, chunk);
      if (n != chunk) {
        broken_ = true;
        TinyLogger::instance().error("IMAP: timeout while reading message data");
        return false;
      }
      if (target != nullptr) {
        for (size_t i = 0; i < n; i++) {
          *target += buffer[i];
        }
      }
      left -= n;
    }
    return true;
  }

  /// Runs UID FETCH for one item; the item must come back as a literal
  bool fetchItem(uint32_t uid, const char* item, String& out) {
    String command = "UID FETCH ";
    command += String(static_cast<unsigned long>(uid));
    command += ' ';
    command += item;
    out = "";
    if (!run(command.c_str(), &out)) {
      return false;
    }
    if (literalSize_ < 0) {
      TinyLogger::instance().warn(String("IMAP: no message data in the answer to ") + command);
      return false;
    }
    return true;
  }
};
