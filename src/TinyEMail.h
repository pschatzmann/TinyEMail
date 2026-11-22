/**
 * @file TinyEMail.h
 * @brief Header-only e-mail client for Gmail with an Arduino style API.
 *
 * TinyEMail sends and receives mail with three methods: login(), send() and
 * receive(). It uses TinySMTP to send and TinyIMAP to read; both work on the
 * same Arduino Client (for example WiFiClientSecure), one connection at a time.
 *
 * The defaults are the Gmail servers. setSmtpServer() and setImapServer()
 * select other servers. Errors and warnings are written to the TinyLogger.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <Arduino.h>  // Client, String

#include "TinyEMail/TinyHtml.h"
#include "TinyEMail/TinyIMAP.h"
#include "TinyEMail/TinyLogger.h"
#include "TinyEMail/TinyMIME.h"
#include "TinyEMail/TinyMail.h"
#include "TinyEMail/TinySMTP.h"

/**
 * @brief Sends and receives mail with a simple API.
 *
 * @code
 * WiFiClientSecure secure;
 * TinyEMail mail(secure);
 *
 * mail.login("me@gmail.com", "abcdefghijklmnop");  // Gmail App Password
 * mail.send("you@example.com", "Hello", "Sent from an ESP32.");
 *
 * mail.receive([](const TinyEMail::Mail& m) {
 *   for (TinyPart& part : m.parts()) {
 *     if (part.info.contentType == "text/plain") Serial.println(part.data.readString());
 *   }
 *   return true;                                  // false stops
 * });
 * @endcode
 *
 * login(), send() and receive() return false (or -1) on failure. The reason is
 * written to the logger (see TinyLogger.h) and is also in lastReply() and
 * lastReplyCode().
 */
class TinyEMail {
 public:
  /// A mail: see TinyMail.h
  using Mail = TinyMail;
  /// An attachment of a mail: see TinyMail.h
  using Attachment = TinyMailAttachment;

  /// Which mails receive() returns
  enum class Filter : uint8_t {
    All,      ///< all mails of the inbox
    Unread,   ///< mails that are not read yet (default)
    Read,     ///< mails that were read
    Flagged,  ///< mails marked as important (starred)
  };

  /**
   * @brief Creates the mail client on top of a TLS client. The client is not owned.
   */
  /// The copy would share the SMTP connection of the original: copies are not allowed
  TinyEMail(const TinyEMail&) = delete;
  TinyEMail& operator=(const TinyEMail&) = delete;

  explicit TinyEMail(Client& client)
      : client_(&client),
        smtp_(client),
        body_(smtp_),
        smtpHost_(TinySMTP::gmailSmtpHost),
        smtpPort_(TinySMTP::gmailSmtpPort),
        imapHost_(TinyIMAP::gmailImapHost),
        imapPort_(TinyIMAP::gmailImapPort) {}

  /**
   * @brief Sets the SMTP server used by login() and send().
   * By default this is Gmail (smtp.gmail.com:465, implicit TLS).
   */
  void setSmtpServer(const char* host, uint16_t port) {
    smtpHost_ = host;
    smtpPort_ = port;
  }

  /**
   * @brief Sets the IMAP server used by receive().
   * By default this is Gmail (imap.gmail.com:993, implicit TLS).
   */
  void setImapServer(const char* host, uint16_t port) {
    imapHost_ = host;
    imapPort_ = port;
  }

  /**
   * @brief Largest message receive() reads. A larger mail is skipped with a
   * warning. 0 (the default) means no limit: messages are streamed, so their
   * size does not need RAM.
   */
  void setMaxMessageSize(size_t bytes) { maxMessageSize_ = bytes; }

  /// Time in milliseconds to wait for the server, 10 seconds by default
  void setTimeout(unsigned long milliseconds) { timeoutMs_ = milliseconds; }

  /**
   * @brief Checks the account and remembers it for send() and receive().
   *
   * Connects to the SMTP server and logs in with AUTH PLAIN, then closes the
   * connection. The password is kept in memory. A failed login forgets the
   * account given before.
   * @param user the e-mail address
   * @param appPassword the password (for Gmail: the 16 character App Password)
   * @return true if the server accepted the login
   */
  bool login(const char* user, const char* appPassword) {
    if (!TinySMTP::isAddress(user)) {
      TinyLogger::instance().error("login: the user must be an e-mail address without spaces");
      forget();
      return false;
    }
    if (!TinySMTP::isSafe(appPassword)) {
      TinyLogger::instance().error("login: the password is empty or has a line break");
      forget();
      return false;
    }
    TinySMTP smtp(*client_);
    smtp.setTimeout(timeoutMs_);
    bool ok = smtp.begin(smtpHost_, smtpPort_) &&
              check(smtp.readReply() == 220, smtp, "login: greeting") &&
              check(smtp.ehlo(), smtp, "login: EHLO") &&
              check(smtp.authPlain(user, appPassword), smtp, "login: AUTH PLAIN");
    if (ok && !smtp.quit()) {
      TinyLogger::instance().warn("login: the server did not confirm QUIT");
    }
    remember(smtp);
    smtp.end();
    if (!ok) {
      forget();
      TinyLogger::instance().error("login failed, the account is not logged in");
      return false;
    }
    user_ = user;
    password_ = appPassword;
    TinyLogger::instance().info("login accepted");
    return true;
  }

  /// @return true after a successful login()
  bool loggedIn() const { return user_.length() > 0; }

  /**
   * @brief Sends a mail from the logged in account.
   *
   * Uses mail.to, mail.subject and mail.attachments. The first attachment is
   * the text body (a text type, e.g. text/plain or text/html). Each further attachment
   * is a file, base64 encoded while it is sent. A mail with only the text is
   * plain text; with files it is MIME multipart.
   * @return true if the server accepted the mail
   */
  bool send(const Mail& mail) {
    if (!loggedIn()) {
      TinyLogger::instance().error("send: not logged in, call login() first");
      return false;
    }
    if (!TinySMTP::isValidMail(mail)) {
      TinyLogger::instance().error("send: invalid mail: recipient or attachment");
      return false;
    }
    TinySMTP smtp(*client_);
    smtp.setTimeout(timeoutMs_);
    bool ok = smtp.begin(smtpHost_, smtpPort_) &&
              check(smtp.readReply() == 220, smtp, "send: greeting") &&
              check(smtp.ehlo(), smtp, "send: EHLO") &&
              check(smtp.authPlain(user_.c_str(), password_.c_str()), smtp, "send: AUTH PLAIN") &&
              check(smtp.mailFrom(user_.c_str()), smtp, "send: MAIL FROM") &&
              recipients(smtp, mail) &&
              check(smtp.beginData(), smtp, "send: DATA") &&
              check(smtp.writeMail(user_.c_str(), mail), smtp, "send: message") &&
              check(smtp.endData(), smtp, "send: end of message");
    if (ok && !smtp.quit()) {
      TinyLogger::instance().warn("send: the server did not confirm QUIT, the mail was accepted");
    }
    remember(smtp);
    smtp.end();
    if (ok) {
      TinyLogger::instance().info("send: mail accepted by the server");
    }
    return ok;
  }

  /**
   * @brief Sends a mail whose body is written by writer(Print& body), e.g.
   * [](Print& body) { TinyHtml html(body); html.paragraph("Hi"); }
   * @return true if the server accepted the mail
   */
  template <typename Writer>
  bool sendWith(const Mail& mail, const char* contentType, Writer writer) {
    if (!beginSend(mail, contentType)) {
      return false;
    }
    writer(body_);
    return endSend();
  }

  template <typename Writer>
  bool sendWith(const char* to, const char* subject, const char* contentType, Writer writer) {
    if (to == nullptr || subject == nullptr) {
      TinyLogger::instance().error("send: recipient and subject are required");
      return false;
    }
    Mail mail;
    mail.to.push_back(String(to));
    mail.subject = subject;
    return sendWith(mail, contentType, writer);
  }

  /**
   * @brief Sends a mail whose body is copied from a Stream, e.g. a File on an SD
   * card. The content is copied while it is sent; nothing is kept in RAM.
   * @param content the text of the body, read until it returns -1
   * @param contentType "text/plain" or "text/html"
   * @return true if the server accepted the mail
   */
  bool send(const char* to, const char* subject, Stream& content, const char* contentType = "text/plain") {
    return sendWith(to, subject, contentType, [&content](Print& body) {
      int c;
      while ((c = content.read()) >= 0) {
        body.write(static_cast<uint8_t>(c));
      }
    });
  }

  /**
   * @brief Sends a plain text mail (no attachments).
   * @param to the recipient address
   * @param subject the subject (line breaks are replaced with spaces)
   * @param body the text of the mail (plain text)
   * @return true if the server accepted the mail
   */
  bool send(const char* to, const char* subject, const char* body) {
    if (to == nullptr || subject == nullptr || body == nullptr) {
      TinyLogger::instance().error("send: recipient, subject and body are required");
      return false;
    }
    TinyTextStream bodyText(body);
    Attachment text;
    text.contentType = "text/plain";
    text.stream = &bodyText;
    Mail mail;
    mail.to.push_back(String(to));
    mail.subject = subject;
    if (!mail.attachments.push_back(text)) {
      return false;  // the error is logged by TinyVector
    }
    return send(mail);
  }

  /**
   * @brief Calls callback(mail) for each mail in the INBOX.
   *
   * The mail has the uid, from, subject and headers. Its parts (the text body,
   * HTML alternatives and attachments) are read with a range based for loop:
   * for (TinyPart& part : mail.parts()). Return true to get the next mail,
   * false to stop. The mails are read without marking them as read.
   *
   * The IMAP connection is open while the callback runs, so do not call
   * send() or login() from it.
   * @code
   * mail.receive([](const TinyEMail::Mail& m) {
   *   for (TinyPart& part : m.parts()) {
   *     if (part.info.contentType == "text/plain") Serial.println(part.data.readString());
   *   }
   *   return true;
   * }, TinyEMail::Filter::Unread);
   * @endcode
   * @param callback called for each mail, e.g. a lambda
   * @param filter which mails to read, by default the unread ones
   * @return the number of mails read, -1 if the mails could not be read
   */
  template <typename Callback>
  int receive(Callback callback, Filter filter = Filter::Unread) {
    if (!loggedIn()) {
      TinyLogger::instance().error("receive: not logged in, call login() first");
      return -1;
    }

    TinyIMAP imap(*client_);
    imap.setTimeout(timeoutMs_);
    imap.setLimits(32768, maxMessageSize_);
    bool session = imap.begin(imapHost_, imapPort_) && imap.readGreeting();
    if (session && !imap.login(user_.c_str(), password_.c_str())) {
      TinyLogger::instance().error(String("receive: login failed: ") + reason(imap));
      session = false;
    }
    if (session && !imap.select("INBOX")) {
      TinyLogger::instance().error(String("receive: cannot open INBOX: ") + reason(imap));
      session = false;
    }

    int count = -1;
    if (session) {
      String uids;
      if (!imap.searchList(searchKey(filter), uids)) {
        TinyLogger::instance().error(String("receive: search failed: ") + reason(imap));
      } else {
        count = 0;
        Receiver<Callback> receiver(callback);
        const char* p = uids.c_str();
        while (receiver.keepGoing) {
          char* end = nullptr;
          unsigned long uid = strtoul(p, &end, 10);
          if (end == p) {
            break;  // no more UIDs
          }
          p = end;
          receiver.uid = static_cast<uint32_t>(uid);
          receiver.flag = Mail::FlagChange::None;
          if (!imap.fetchStreamed(receiver.uid, "BODY.PEEK[]", receiver)) {
            if (imap.broken() || !imap.connected()) {
              TinyLogger::instance().error(String("receive: connection lost after ") +
                                           String(receiver.count) + " mails");
              break;
            }
            TinyLogger::instance().warn(String("receive: mail ") + String(static_cast<unsigned long>(uid)) +
                                        " could not be read, skipped");
          }
          // the message has been read to its end: now the requested flag can be sent
          if (receiver.flag != Mail::FlagChange::None && imap.connected() && !imap.broken()) {
            bool ok = true;
            switch (receiver.flag) {
              case Mail::FlagChange::Read:
                ok = imap.setFlag(receiver.uid, "\\Seen", true);
                break;
              case Mail::FlagChange::Unread:
                ok = imap.setFlag(receiver.uid, "\\Seen", false);
                break;
              case Mail::FlagChange::Flagged:
                ok = imap.setFlag(receiver.uid, "\\Flagged", true);
                break;
              case Mail::FlagChange::Unflagged:
                ok = imap.setFlag(receiver.uid, "\\Flagged", false);
                break;
              default:
                break;
            }
            if (!ok) {
              TinyLogger::instance().warn("receive: the flag of a mail could not be set on the server");
            }
          }
        }
        count = receiver.count;
      }
    }
    lastCode_ = -1;
    lastReply_ = imap.lastResponse();
    if (session && imap.connected() && !imap.broken() && !imap.logout()) {
      TinyLogger::instance().warn("receive: the server did not confirm LOGOUT");
    }
    imap.end();
    if (count >= 0) {
      TinyLogger::instance().info(String("receive: ") + String(count) + " mails read");
    }
    return count;
  }

  /// @return the text of the last server reply of the last command
  String lastReply() const { return lastReply_; }

  /// @return the SMTP reply code of the last command, -1 if there was none
  int lastReplyCode() const { return lastCode_; }

 private:
  /**
   * @brief Starts a mail: sends the headers now, and the body is written with
   * body(). Finish it with endSend(). The body is streamed to the server, so no
   * part of it is kept in RAM.
   * @param contentType a text type: "text/plain" or "text/html"
   * @return true if the server accepted the recipient and is ready for the body
   */
  bool beginSend(const Mail& mail, const char* contentType) {
    if (!loggedIn()) {
      TinyLogger::instance().error("send: not logged in, call login() first");
      return false;
    }
    if (sending_) {
      TinyLogger::instance().error("send: a mail is already open, call endSend() first");
      return false;
    }
    if (contentType == nullptr || strncmp(contentType, "text/", 5) != 0 ||
        strpbrk(contentType, "; \"\\") != nullptr || !mail.attachments.empty() ||
        !TinySMTP::isValidMail(mail)) {
      TinyLogger::instance().error("send: invalid recipient or content type");
      return false;
    }
    smtp_.setTimeout(timeoutMs_);
    bool ok = smtp_.begin(smtpHost_, smtpPort_) &&
              check(smtp_.readReply() == 220, smtp_, "send: greeting") &&
              check(smtp_.ehlo(), smtp_, "send: EHLO") &&
              check(smtp_.authPlain(user_.c_str(), password_.c_str()), smtp_, "send: AUTH PLAIN") &&
              check(smtp_.mailFrom(user_.c_str()), smtp_, "send: MAIL FROM") &&
              recipients(smtp_, mail) &&
              check(smtp_.beginMessage(user_.c_str(), mail.to, mail.subject.c_str(), contentType),
                    smtp_, "send: DATA");
    if (!ok) {
      remember(smtp_);
      smtp_.end();
      return false;
    }
    sending_ = true;
    return true;
  }

  /// The body of the mail started with beginSend(): write text to it (e.g. a TinyHtml)
  Print& body() { return body_; }

  /// Finishes the mail started with beginSend(): reads the reply of the server
  bool endSend() {
    if (!sending_) {
      TinyLogger::instance().error("send: no mail is open, call beginSend() first");
      return false;
    }
    sending_ = false;
    bool ok = check(smtp_.endMessage(), smtp_, "send: end of message");
    if (ok && !smtp_.quit()) {
      TinyLogger::instance().warn("send: the server did not confirm QUIT, the mail was accepted");
    }
    remember(smtp_);
    smtp_.end();
    if (ok) {
      TinyLogger::instance().info("send: mail accepted by the server");
    }
    return ok;
  }

  /// Hands the message of each mail to the callback. The message is read as a stream.
  template <typename Callback>
  class Receiver : public TinyIMAP::LiteralHandler {
   public:
    explicit Receiver(Callback& callback) : callback_(callback) {}

    uint32_t uid = 0;
    int count = 0;
    bool keepGoing = true;
    Mail::FlagChange flag = Mail::FlagChange::None;  // requested by the callback

    bool handle(Stream& raw, size_t) override {
      TinyMIMEReader reader(raw, mimeLineLength);
      Mail mail;
      mail.uid = uid;
      if (!reader.begin(mail.headers)) {
        TinyLogger::instance().warn("receive: the message cannot be read, skipped");
        return false;
      }
      mail.from = mail.header("From");
      mail.subject = mail.header("Subject");
      count++;
      mail.reader_ = &reader;  // the parts are read from the stream while the callback runs
      keepGoing = callback_(static_cast<const Mail&>(mail));
      flag = mail.flagChange();
      mail.reader_ = nullptr;
      return !reader.broken();
    }

   private:
    Callback& callback_;
  };

  static constexpr size_t mimeLineLength = 2048;  // longest line of a message that is read

  Client* client_;
  TinySMTP smtp_;      // the connection of a mail started with beginSend()
  TinySMTPBody body_;  // the body of that mail
  bool sending_ = false;
  const char* smtpHost_;
  uint16_t smtpPort_;
  const char* imapHost_;
  uint16_t imapPort_;
  size_t maxMessageSize_ = 0;  // 0: no check
  unsigned long timeoutMs_ = 10000;
  String user_;      // set by login()
  String password_;  // set by login()
  String lastReply_;
  int lastCode_ = -1;

  /// Keeps the reply of an SMTP session for lastReply() and lastReplyCode()
  void remember(const TinySMTP& smtp) {
    lastReply_ = smtp.lastReply();
    lastCode_ = smtp.lastReplyCode();
  }

  /// Forgets the account (the password stays out of memory)
  void forget() {
    user_ = "";
    password_ = "";
  }

  /// RCPT TO for every address: the To and the bcc recipients. false on the first refusal.
  static bool recipients(TinySMTP& smtp, const Mail& mail) {
    for (const String& address : mail.to) {
      if (!check(smtp.rcptTo(address.c_str()), smtp, "send: RCPT TO")) {
        return false;
      }
    }
    for (const String& address : mail.bcc) {
      if (!check(smtp.rcptTo(address.c_str()), smtp, "send: RCPT TO (bcc)")) {
        return false;
      }
    }
    return true;
  }

  /// Logs the failed step with the reply of the server. Returns ok.
  static bool check(bool ok, const TinySMTP& smtp, const char* step) {
    if (!ok) {
      String reply = smtp.lastReply();
      TinyLogger::instance().error(String(step) + " failed: " +
                                   (reply.length() > 0 ? reply : String("no reply from the server")));
    }
    return ok;
  }

  static String reason(const TinyIMAP& imap) {
    String reply = imap.lastResponse();
    return reply.length() > 0 ? reply : String("no reply from the server");
  }

  /// The IMAP search key for a filter
  static const char* searchKey(Filter filter) {
    switch (filter) {
      case Filter::All:
        return "ALL";
      case Filter::Read:
        return "SEEN";
      case Filter::Flagged:
        return "FLAGGED";
      case Filter::Unread:
      default:
        return "UNSEEN";
    }
  }
};
