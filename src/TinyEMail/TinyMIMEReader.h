/**
 * @file TinyMIMEReader.h
 * @brief Reads a received message as a stream: the parts of the mail come one
 * after the other, and their data is decoded while it is read. The message is
 * never held in RAM; only a line or two and the headers of the part are.
 *
 * The parts are forward only: they can be read once, in order.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <utility>  // std::move

#include <Arduino.h>  // String, Stream, Print

#include "TinyLogger.h"
#include "TinyMail.h"
#include "TinyText.h"

/// Reads the lines of a Stream one at a time, with a limit on the line length
class TinyLineSource {
 public:
  TinyLineSource(Stream& in, size_t maxLength) : in_(in), maxLength_(maxLength) {}

  /**
   * @brief The next line without its line break.
   * @param terminated true if a line break followed the line
   * @return false at the end of the data, or if the line is too long (see broken())
   */
  bool next(String& line, bool& terminated) {
    if (hasUnread_) {
      hasUnread_ = false;
      line = unread_;
      terminated = unreadTerminated_;
      return true;
    }
    line = "";
    terminated = false;
    while (true) {
      int c = in_.read();
      if (c < 0) {
        return line.length() > 0;
      }
      if (c == '\n') {
        terminated = true;
        if (line.endsWith("\r")) {
          line.remove(line.length() - 1);
        }
        return true;
      }
      if (line.length() >= maxLength_) {
        broken_ = true;
        TinyLogger::instance().error("MIME: a line of the message is longer than the limit");
        return false;
      }
      line += static_cast<char>(c);
    }
  }

  /// Gives back a line that was read: the next call to next() returns it again
  void unread(const String& line, bool terminated) {
    unread_ = line;
    unreadTerminated_ = terminated;
    hasUnread_ = true;
  }

  bool broken() const { return broken_; }

 private:
  Stream& in_;
  size_t maxLength_;
  bool broken_ = false;
  bool hasUnread_ = false;
  String unread_;
  bool unreadTerminated_ = false;
};

/**
 * @brief Walks the parts of a message in order. Used by TinyMIME::parse() and by
 * the parts of a received mail (TinyMailParts).
 */
class TinyMIMEReader {
 public:
  /// Transfer encoding of the part being read
  enum class Encoding : uint8_t { None, Base64, QuotedPrintable };

  explicit TinyMIMEReader(Stream& message, size_t maxLineLength = 2048)
      : lines_(message, maxLineLength) {}

  /**
   * @brief Reads the headers of the message. The parts come next, see nextPart().
   * @param headers receives the header lines (without the empty line)
   * @return false if the message cannot be read
   */
  bool begin(String& headers) {
    String first;
    bool terminated = false;
    headers = "";
    if (!lines_.next(first, terminated)) {
      return !lines_.broken();  // an empty message has no headers and one empty body
    }
    if (looksLikeHeader(first)) {
      headers = first;
      headers += "\r\n";
      String line;
      bool blank = false;
      while (lines_.next(line, terminated)) {
        if (line.length() == 0) {
          blank = true;
          break;
        }
        headers += line;
        headers += "\r\n";
      }
      if (!blank && !lines_.broken()) {
        TinyLogger::instance().warn("MIME: headers without an empty line, the message has no body");
      }
    } else {
      lines_.unread(first, terminated);  // no headers: the message is the body
    }
    if (lines_.broken()) {
      return false;
    }
    TinyMailPart info;
    String boundary;
    if (describe(headers, info, boundary)) {
      if (boundary.length() == 0) {
        TinyLogger::instance().warn("MIME: multipart without boundary, its parts are skipped");
      } else {
        boundaries_[depth_++] = boundary;
      }
    } else {
      leafPending_ = true;
      leafInfo_ = info;
    }
    return true;
  }

  /**
   * @brief The next part of the message. Its data is read with partByte().
   * @return false at the end of the message
   */
  bool nextPart(TinyMailPart& info) {
    if (leafPending_) {
      leafPending_ = false;
      info = leafInfo_;
      startBody();
      return true;
    }
    while (depth_ > 0) {
      Delim d;
      if (hasPending_) {
        hasPending_ = false;
        d = pending_;
      } else if (!scanDelimiter(d)) {
        if (!lines_.broken()) {
          TinyLogger::instance().warn("MIME: multipart without closing boundary");
        }
        depth_ = 0;
        return false;
      }
      depth_ = d.frame + 1;
      if (d.closing) {
        depth_ = d.frame;  // this multipart is finished: the parent goes on
        continue;
      }
      String head;
      Delim inner;
      bool hasInner = false;
      readHeaders(head, inner, hasInner);
      if (hasInner) {
        // a delimiter inside the headers: the part is empty
        info = TinyMailPart();
        encoding_ = Encoding::None;
        partOpen_ = false;
        pending_ = inner;
        hasPending_ = true;
        return true;
      }
      String boundary;
      if (describe(head, info, boundary)) {
        if (boundary.length() == 0) {
          TinyLogger::instance().warn("MIME: multipart without boundary, its parts are skipped");
          continue;
        }
        if (depth_ >= maxDepth) {
          TinyLogger::instance().warn("MIME: multipart nested too deep, its parts are skipped");
          continue;
        }
        boundaries_[depth_++] = boundary;
        continue;  // the parts of the new multipart come next
      }
      startBody();
      return true;
    }
    return false;
  }

  /// The next byte of the current part, -1 at its end
  int partByte() {
    for (;;) {
      if (!partOpen_) {
        return -1;
      }
      if (curPos_ < cur_.length()) {
        return static_cast<uint8_t>(cur_.charAt(curPos_++));
      }
      // the current line is done
      if (aheadIsDelim_) {
        // the line break before a delimiter is part of the delimiter, not of the data
        pending_ = aheadDelim_;
        hasPending_ = true;
        partOpen_ = false;
        return -1;
      }
      if (curTerm_ && termPos_ < 2) {
        return termPos_++ == 0 ? '\r' : '\n';
      }
      if (aheadEOF_) {
        partOpen_ = false;
        return -1;
      }
      cur_ = ahead_;
      curTerm_ = aheadTerm_;
      curPos_ = 0;
      termPos_ = 0;
      readAhead();
    }
  }

  /// Skips the rest of the current part
  void skipPart() {
    while (partByte() >= 0) {
    }
  }

  /// Encoding of the current part
  Encoding encoding() const { return encoding_; }

  /// true if the message could not be read completely
  bool broken() const { return lines_.broken(); }

 private:
  static const int maxDepth = 4;  // nested multipart levels

  struct Delim {
    int frame = 0;
    bool closing = false;
  };

  TinyLineSource lines_;
  String boundaries_[maxDepth];
  int depth_ = 0;

  bool leafPending_ = false;
  TinyMailPart leafInfo_;
  Encoding encoding_ = Encoding::None;

  bool hasPending_ = false;  // a delimiter was read before the data was asked for
  Delim pending_;

  bool partOpen_ = false;
  String cur_;
  bool curTerm_ = false;
  size_t curPos_ = 0;
  uint8_t termPos_ = 0;
  String ahead_;
  bool aheadTerm_ = false;
  bool aheadEOF_ = true;
  bool aheadIsDelim_ = false;
  Delim aheadDelim_;

  /// Starts the data of a part: the first line becomes the current line once the next is known
  void startBody() {
    cur_ = "";
    curTerm_ = false;  // an empty line that is not written
    curPos_ = 0;
    termPos_ = 0;
    partOpen_ = true;
    readAhead();
  }

  void readAhead() {
    aheadIsDelim_ = false;
    aheadEOF_ = !lines_.next(ahead_, aheadTerm_);
    if (!aheadEOF_ && isDelimiter(ahead_, aheadDelim_)) {
      aheadIsDelim_ = true;
    }
    if (aheadEOF_) {
      ahead_ = "";
    }
  }

  /// Reads the next line that is a delimiter of an open multipart
  bool scanDelimiter(Delim& d) {
    String line;
    bool terminated = false;
    while (lines_.next(line, terminated)) {
      if (isDelimiter(line, d)) {
        return true;
      }
    }
    return false;
  }

  /// Headers of a part, up to the empty line; a delimiter inside them ends the part
  void readHeaders(String& head, Delim& inner, bool& hasInner) {
    head = "";
    String line;
    bool terminated = false;
    while (lines_.next(line, terminated)) {
      if (line.length() == 0) {
        break;
      }
      if (isDelimiter(line, inner)) {
        hasInner = true;
        break;
      }
      head += line;
      head += "\r\n";
    }
  }

  /// true if line is a delimiter of an open multipart: "--boundary" or "--boundary--"
  bool isDelimiter(const String& line, Delim& d) const {
    for (int i = depth_ - 1; i >= 0; i--) {
      String prefix = "--";
      prefix += boundaries_[i];
      if (!line.startsWith(prefix)) {
        continue;
      }
      String rest = line.substring(prefix.length());
      rest.trim();
      if (rest.length() == 0) {
        d.frame = i;
        d.closing = false;
        return true;
      }
      if (rest == "--") {
        d.frame = i;
        d.closing = true;
        return true;
      }
    }
    return false;
  }

  /// true if the first line has the form "Name: value"
  static bool looksLikeHeader(const String& line) {
    for (unsigned int i = 0; i < line.length(); i++) {
      char c = line.charAt(i);
      if (c == ':') {
        return i > 0;
      }
      if (c == ' ' || c == '\t') {
        return false;
      }
    }
    return false;
  }

  /**
   * @brief Fills info from the headers of a part. Returns true if the part is a
   * multipart; its boundary is then in boundary.
   */
  bool describe(const String& headers, TinyMailPart& info, String& boundary) {
    String contentType = TinyMail::headerValue(headers, "Content-Type");
    String type = TinyText::lowerCopy(contentType);
    int semicolon = type.indexOf(';');
    if (semicolon >= 0) {
      type = type.substring(0, semicolon);
    }
    type.trim();
    if (type.length() == 0) {
      type = "text/plain";  // default when the header is missing
    }
    if (type.startsWith("multipart/")) {
      boundary = paramValue(contentType, "boundary");
      return true;
    }

    String disposition = TinyMail::headerValue(headers, "Content-Disposition");
    info.contentType = type;
    info.attachment = TinyText::lowerCopy(disposition).startsWith("attachment");
    info.filename = fileName(disposition, contentType);

    String encodingText = TinyText::lowerCopy(TinyMail::headerValue(headers, "Content-Transfer-Encoding"));
    encodingText.trim();
    encoding_ = Encoding::None;
    if (encodingText == "base64") {
      encoding_ = Encoding::Base64;
    } else if (encodingText == "quoted-printable") {
      encoding_ = Encoding::QuotedPrintable;
    } else if (encodingText.length() > 0 && encodingText != "7bit" && encodingText != "8bit" &&
               encodingText != "binary") {
      TinyLogger::instance().warn(String("MIME: transfer encoding not supported, data passed as is: ") +
                                  encodingText);
    }
    return false;
  }

  /// File name of a part: RFC 2231 filename*, else filename or name, with RFC 2047 words decoded
  static String fileName(const String& disposition, const String& contentType) {
    String name = paramValue(disposition, "filename*");
    if (name.length() > 0) {
      // RFC 2231: charset'language'percent-encoded value
      int q1 = name.indexOf('\'');
      int q2 = q1 >= 0 ? name.indexOf('\'', q1 + 1) : -1;
      if (q1 >= 0 && q2 >= 0) {
        return TinyText::toUtf8(name.substring(0, q1), TinyText::percentDecode(name.substring(q2 + 1)));
      }
      return TinyText::percentDecode(name);
    }
    name = paramValue(disposition, "filename");
    if (name.length() == 0) {
      name = paramValue(contentType, "name");
    }
    return TinyText::decodeWords(name);
  }

  /// Value of a parameter in a header value, e.g. boundary in "multipart/mixed; boundary=\"x\""
  static String paramValue(const String& value, const char* param) {
    String lower = TinyText::lowerCopy(value);
    String key = param;
    key += '=';
    int start = -1;
    int from = 0;
    while (true) {
      int k = lower.indexOf(key, from);
      if (k < 0) {
        break;
      }
      // a parameter starts after ';' or a space: "name=" is not part of "filename="
      if (k == 0 || lower.charAt(k - 1) == ';' || lower.charAt(k - 1) == ' ' ||
          lower.charAt(k - 1) == '\t') {
        start = k;
        break;
      }
      from = k + 1;
    }
    if (start < 0) {
      return "";
    }
    start += key.length();
    if (start < static_cast<int>(value.length()) && value.charAt(start) == '"') {
      int close = value.indexOf('"', start + 1);
      if (close < 0) {
        close = value.length();
      }
      return value.substring(start + 1, close);
    }
    int end = value.indexOf(';', start);
    if (end < 0) {
      end = value.length();
    }
    String out = value.substring(start, end);
    out.trim();
    return out;
  }
};

/**
 * @brief The data of one part of a mail, decoded while it is read (base64 and
 * quoted-printable). A Stream: read() gives the next byte, -1 at the end.
 *
 * Use writeTo() to copy the data to a File or another Print, or readString() for
 * text. Do not use readBytes(): at the end of the data it waits for the timeout.
 * The data can be read once, and only while its part is current.
 */
class TinyMailData : public Stream {
 public:
  using Encoding = TinyMIMEReader::Encoding;

  explicit TinyMailData(TinyMIMEReader* reader) : reader_(reader) {
    if (reader_ != nullptr) {
      encoding_ = reader_->encoding();
    }
  }

  TinyMailData(const TinyMailData&) = delete;
  TinyMailData& operator=(const TinyMailData&) = delete;
  TinyMailData(TinyMailData&&) = default;

  /// Starts the data of the current part of reader
  void attach(TinyMIMEReader& reader) {
    reader_ = &reader;
    encoding_ = reader.encoding();
    peeked_ = -1;
    hasPeek_ = false;
    outLen_ = 0;
    outPos_ = 0;
    queue_ = "";
    queuePos_ = 0;
    whitespace_ = "";
    pushedCount_ = 0;
  }

  /// 1 if there is more data, 0 at the end of the part
  int available() override { return peek() >= 0 ? 1 : 0; }

  /// @return the next decoded byte, or -1 at the end of the part
  int read() override {
    if (hasPeek_) {
      hasPeek_ = false;
      return peeked_;
    }
    return next();
  }

  /// @return the next decoded byte without removing it, or -1 at the end
  int peek() override {
    if (!hasPeek_) {
      peeked_ = next();
      hasPeek_ = true;
    }
    return peeked_;
  }

  /// The data is read only
  size_t write(uint8_t) override { return 0; }

  /// Copies the rest of the part to out. Returns the number of bytes written.
  size_t writeTo(Print& out) {
    size_t n = 0;
    int c;
    while ((c = read()) >= 0) {
      out.write(static_cast<uint8_t>(c));
      n++;
    }
    return n;
  }

  /// The rest of the part as a String. Use it for text; binary data may contain NUL bytes.
  String readString() {
    String text;
    int c;
    while ((c = read()) >= 0) {
      text += static_cast<char>(c);
    }
    return text;
  }

 private:
  TinyMIMEReader* reader_;
  Encoding encoding_ = Encoding::None;
  int peeked_ = -1;
  bool hasPeek_ = false;
  // base64: the bytes of the last group
  uint8_t out_[3] = {0, 0, 0};
  uint8_t outLen_ = 0;
  uint8_t outPos_ = 0;
  // quoted-printable: bytes to give out, whitespace not yet known to be kept, pushed back bytes
  String queue_;
  size_t queuePos_ = 0;
  String whitespace_;
  int pushed_[4] = {-1, -1, -1, -1};
  uint8_t pushedCount_ = 0;

  int raw() {
    if (reader_ == nullptr) {
      return -1;
    }
    return reader_->partByte();
  }

  int next() {
    if (reader_ == nullptr) {
      return -1;
    }
    switch (encoding_) {
      case Encoding::Base64:
        return nextBase64();
      case Encoding::QuotedPrintable:
        return nextQuotedPrintable();
      default:
        return raw();
    }
  }

  int nextBase64() {
    while (outPos_ >= outLen_) {
      uint32_t value = 0;
      int n = 0;
      bool eof = false;
      while (n < 4) {
        int c = raw();
        if (c < 0) {
          eof = true;
          break;
        }
        if (c == '=') {
          if (n == 0) {
            continue;  // padding of an earlier group
          }
          break;  // padding ends this group
        }
        int d = TinyText::base64Value(static_cast<char>(c));
        if (d >= 0) {
          value = (value << 6) | static_cast<uint32_t>(d);
          n++;
        }
      }
      if (n < 2) {
        if (eof) {
          return -1;
        }
        continue;
      }
      value <<= 6 * (4 - n);
      out_[0] = static_cast<uint8_t>(value >> 16);
      out_[1] = static_cast<uint8_t>(value >> 8);
      out_[2] = static_cast<uint8_t>(value);
      outLen_ = static_cast<uint8_t>(n - 1);
      outPos_ = 0;
    }
    return out_[outPos_++];
  }

  /// A byte of the encoded data, including the bytes pushed back
  int pull() {
    if (pushedCount_ > 0) {
      return pushed_[--pushedCount_];
    }
    return raw();
  }

  void push(int c) {
    if (c >= 0 && pushedCount_ < 4) {
      pushed_[pushedCount_++] = c;
    }
  }

  int nextQuotedPrintable() {
    for (;;) {
      if (queuePos_ < queue_.length()) {
        return static_cast<uint8_t>(queue_.charAt(queuePos_++));
      }
      queue_ = "";
      queuePos_ = 0;
      int c = pull();
      if (c < 0) {
        whitespace_ = "";  // whitespace at the end of the data is removed
        return -1;
      }
      if (c == ' ' || c == '\t') {
        whitespace_ += static_cast<char>(c);
        continue;
      }
      if (c == '\r' || c == '\n') {
        whitespace_ = "";  // whitespace before a line break is removed (RFC 2045)
        return c;
      }
      if (whitespace_.length() > 0) {
        // a visible byte follows: the whitespace is part of the data
        push(c);
        queue_ = whitespace_;
        whitespace_ = "";
        continue;
      }
      if (c != '=') {
        return c;
      }
      int a = pull();
      if (a < 0) {
        return '=';
      }
      if (a == '\n') {
        continue;  // soft line break
      }
      if (a == '\r') {
        int b = pull();
        if (b == '\n') {
          continue;  // soft line break
        }
        push(b);
        push(a);
        return '=';
      }
      int b = pull();
      if (TinyMailData::hexValue(static_cast<char>(a)) >= 0 &&
          TinyMailData::hexValue(static_cast<char>(b)) >= 0) {
        return TinyMailData::hexValue(static_cast<char>(a)) * 16 +
               TinyMailData::hexValue(static_cast<char>(b));
      }
      push(b);
      push(a);
      return '=';  // not an escape: keep it
    }
  }

  static int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  }
};

/// One part of a received mail, as given by a for loop over mail.parts()
struct TinyPart {
  TinyMailPart info;  ///< content type, file name and attachment flag
  TinyMailData data;  ///< the decoded content, read as a Stream

  explicit TinyPart(TinyMIMEReader* reader) : data(reader) {}
};

/**
 * @brief The parts of a received mail. Use it in a range based for loop:
 *
 * @code
 * for (TinyPart& part : mail.parts()) {
 *   if (part.info.attachment) {
 *     part.data.writeTo(file);   // or part.data.readString()
 *   }
 * }
 * @endcode
 *
 * The parts are read from the message as the loop goes: each part is read in
 * order, once. Only the part that is current is kept in RAM.
 */
class TinyMailParts {
 public:
  /// Iterator over the parts; dereferencing gives the current TinyPart
  class Iterator {
   public:
    Iterator(TinyMIMEReader* reader, bool atEnd) : reader_(reader), valid_(false), current_(reader) {
      if (!atEnd) {
        advance();
      }
    }
    Iterator(Iterator&& other)
        : reader_(other.reader_), valid_(other.valid_), current_(std::move(other.current_)) {}

    TinyPart& operator*() { return current_; }
    Iterator& operator++() {
      if (valid_) {
        reader_->skipPart();  // the rest of the part the loop did not read
      }
      advance();
      return *this;
    }
    bool operator!=(const Iterator& other) const { return valid_ != other.valid_; }

   private:
    TinyMIMEReader* reader_;
    bool valid_;
    TinyPart current_;

    void advance() {
      valid_ = reader_ != nullptr && reader_->nextPart(current_.info);
      if (valid_) {
        current_.data.attach(*reader_);
      }
    }
  };

  explicit TinyMailParts(TinyMIMEReader* reader) : reader_(reader) {}
  TinyMailParts(const TinyMailParts&) = delete;
  TinyMailParts& operator=(const TinyMailParts&) = delete;
  TinyMailParts(TinyMailParts&&) = default;
  TinyMailParts& operator=(TinyMailParts&&) = default;

  Iterator begin() { return Iterator(reader_, false); }
  Iterator end() { return Iterator(reader_, true); }

 private:
  TinyMIMEReader* reader_;
};

inline TinyMailParts TinyMail::parts() const { return TinyMailParts(reader_); }
