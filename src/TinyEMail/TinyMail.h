/**
 * @file TinyMail.h
 * @brief The mail and attachment structures used by TinyEMail and TinySMTP.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <Arduino.h>  // String, Stream

#include <utility>  // std::move

#include "TinyText.h"
#include "TinyVector.h"

class TinyMIMEReader;  // the parts of a received message (TinyMIMEReader.h)
class TinyMailParts;

/**
 * @brief A file or the text body of a mail that is sent. Its content is read
 * from a Stream, e.g. a TinyTextStream, a TinyBufferStream or an opened File.
 *
 * The stream is read until read() returns -1, which is the end of a File.
 */
struct TinyMailAttachment {
  const char* filename = nullptr;  ///< name shown to the recipient, e.g. "photo.jpg"; null for the text body
  const char* contentType = "application/octet-stream";
  Stream* stream = nullptr;        ///< the content; required
};

/**
 * @brief A read only Stream over a text, e.g. the body of a mail.
 *
 * @code
 * TinyTextStream body("Hello");
 * attachment.stream = &body;
 * @endcode
 */
class TinyTextStream : public Stream {
 public:
  explicit TinyTextStream(const char* text)
      : text_(text), length_(text == nullptr ? 0 : strlen(text)) {}

  int available() override { return static_cast<int>(length_ - pos_); }
  int read() override { return pos_ < length_ ? static_cast<uint8_t>(text_[pos_++]) : -1; }
  int peek() override { return pos_ < length_ ? static_cast<uint8_t>(text_[pos_]) : -1; }
  size_t write(uint8_t) override { return 0; }  // read only

 private:
  const char* text_;
  size_t length_;
  size_t pos_ = 0;
};

/**
 * @brief A read only Stream over bytes in memory, e.g. binary data for an attachment.
 *
 * @code
 * const uint8_t image[] = {0x89, 'P', 'N', 'G'};
 * TinyBufferStream data(image, sizeof(image));
 * @endcode
 */
class TinyBufferStream : public Stream {
 public:
  TinyBufferStream(const uint8_t* data, size_t size) : data_(data), size_(size) {}

  int available() override { return static_cast<int>(size_ - pos_); }
  int read() override { return pos_ < size_ ? data_[pos_++] : -1; }
  int peek() override { return pos_ < size_ ? data_[pos_] : -1; }
  size_t write(uint8_t) override { return 0; }  // read only

 private:
  const uint8_t* data_;
  size_t size_;
  size_t pos_ = 0;
};

/**
 * @brief One part of a received mail, e.g. the text body, an HTML alternative
 * or an attachment. The data is delivered separately, as a stream.
 */
struct TinyMailPart {
  String contentType;  ///< lower case type without parameters, e.g. "text/plain"
  String filename;     ///< file name of the part, "" if it has none
  bool attachment = false;  ///< true if the mail marks it as an attachment
};

/**
 * @brief A mail. TinyEMail::receive() fills the received fields; send() uses
 * the sent fields.
 */
struct TinyMail {
  // received mails
  uint32_t uid = 0;  ///< UID of the mail on the server
  String from;       ///< value of the From header
  String subject;    ///< value of the Subject header
  String headers;    ///< header lines of the mail, without the body (see header())

  // mails to send: the first attachment is the text body, the others are files
  TinyVector<String> to;   ///< recipient addresses: the To header and RCPT TO
  TinyVector<String> bcc;  ///< blind copy addresses: RCPT TO only, not in the headers
  TinyVector<TinyMailAttachment> attachments;  ///< the text body first, then the files

  // received mails: the reader of the message, set by receive() while the callback runs
  TinyMIMEReader* reader_ = nullptr;

  /// A change of the flags of a received mail, sent to the server after the mail was read
  enum class FlagChange : uint8_t { None, Read, Unread, Flagged, Unflagged };
  mutable FlagChange flagChange_ = FlagChange::None;

  /// Asks for the mail to be marked read on the server, once it has been read
  void markAsRead() const { flagChange_ = FlagChange::Read; }
  /// Asks for the mail to be marked unread on the server, once it has been read
  void markAsUnread() const { flagChange_ = FlagChange::Unread; }
  /// Asks for the mail to be flagged (starred) on the server, once it has been read
  void markFlagged() const { flagChange_ = FlagChange::Flagged; }
  /// Asks for the flag to be removed from the mail on the server, once it has been read
  void unmarkFlagged() const { flagChange_ = FlagChange::Unflagged; }

  /// The request made in the callback (see markAsRead())
  FlagChange flagChange() const { return flagChange_; }

  /**
   * @brief The parts of a received mail: the text body, HTML alternatives and
   * attachments. Empty for a mail that is not received. Use it in a range
   * based for loop, see TinyMailParts. The parts can be read once, in order.
   */
  TinyMailParts parts() const;

  /**
   * @brief Value of a header of a received mail, e.g. header("Message-ID").
   * The name is case insensitive. Returns "" if the mail has no such header.
   */
  String header(const char* name) const {
    return TinyText::decodeWords(headerValue(headers, name));
  }

  /**
   * @brief Value of a header in a block of header lines. Folded lines (starting
   * with a space or tab) are joined to the value.
   */
  static String headerValue(const String& block, const char* name) {
    size_t nameLen = strlen(name);
    String value;
    bool found = false;
    int pos = 0;
    while (pos < static_cast<int>(block.length())) {
      int end = block.indexOf('\n', pos);
      if (end < 0) {
        end = block.length();
      }
      String line = block.substring(pos, end);
      if (line.endsWith("\r")) {
        line.remove(line.length() - 1);
      }
      bool continuation =
          line.length() > 0 && (line.charAt(0) == ' ' || line.charAt(0) == '\t');
      if (continuation && found) {
        line.trim();
        value += ' ';
        value += line;
      } else if (found) {
        break;  // the header ended
      } else if (line.length() > nameLen && line.charAt(nameLen) == ':' &&
                 sameName(line, name, nameLen)) {
        found = true;
        value = line.substring(nameLen + 1);
        value.trim();
      }
      pos = end + 1;
    }
    return value;
  }

  /// true if the first n characters of line equal name, ignoring case
  static bool sameName(const String& line, const char* name, size_t n) {
    for (size_t i = 0; i < n; i++) {
      char a = line.charAt(i);
      char b = name[i];
      if (a >= 'A' && a <= 'Z') a = a + 32;
      if (b >= 'A' && b <= 'Z') b = b + 32;
      if (a != b) {
        return false;
      }
    }
    return true;
  }
};
