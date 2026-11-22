/**
 * @file TinyText.h
 * @brief Text helpers shared by the mail classes: limited line reading, case,
 * base64, percent decoding and the encoded words of mail headers
 * (RFC 2047 and RFC 2231).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <Arduino.h>  // String, Stream

#include "TinyLogger.h"

struct TinyText {
  /// Lower case of an ASCII letter
  static char lower(char c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

  /// Lower case copy of a string (ASCII letters only)
  static String lowerCopy(const String& text) {
    String out = text;
    for (unsigned int i = 0; i < out.length(); i++) {
      out.setCharAt(i, lower(out.charAt(i)));
    }
    return out;
  }

  /**
   * @brief Reads one line without the CRLF from a stream.
   *
   * Fails on a timeout, if the line is longer than maxLength characters, or if
   * the line is empty. A line that is too long is not read to its end: the
   * stream is then out of step and the caller must close the connection.
   */
  static bool readLine(Stream& stream, String& line, size_t maxLength) {
    line = "";
    unsigned long start = millis();
    unsigned long timeout = stream.getTimeout();
    while (true) {
      int c = stream.read();
      if (c < 0) {
        if (millis() - start > timeout) {
          return false;
        }
        continue;
      }
      if (c == '\n') {
        break;
      }
      if (line.length() >= maxLength) {
        return false;
      }
      line += static_cast<char>(c);
    }
    if (line.endsWith("\r")) {
      line.remove(line.length() - 1);
    }
    return line.length() > 0;
  }

  /// Value of a base64 character, -1 if it is not in the alphabet
  static int base64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  }

  /// Base64 encoding (RFC 4648). out needs ((n + 2) / 3) * 4 bytes, returns the length
  static size_t base64Encode(const uint8_t* in, size_t n, char* out) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
      uint32_t b0 = in[i];
      uint32_t b1 = (i + 1 < n) ? in[i + 1] : 0;
      uint32_t b2 = (i + 2 < n) ? in[i + 2] : 0;
      uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
      out[o++] = alphabet[(triple >> 18) & 0x3F];
      out[o++] = alphabet[(triple >> 12) & 0x3F];
      out[o++] = (i + 1 < n) ? alphabet[(triple >> 6) & 0x3F] : '=';
      out[o++] = (i + 2 < n) ? alphabet[triple & 0x3F] : '=';
    }
    return o;
  }

  /**
   * @brief Decodes base64 text into out. Characters outside the alphabet are
   * skipped; a padding '=' ends a group, and a new group starts after it.
   */
  static void base64Decode(const char* in, size_t n, String& out) {
    uint32_t value = 0;
    int count = 0;
    for (size_t i = 0; i < n; i++) {
      if (in[i] == '=') {
        // end of a group: flush the bits we have
        if (count >= 2) {
          emitGroup(value, count, out);
        }
        value = 0;
        count = 0;
        continue;
      }
      int d = base64Value(in[i]);
      if (d < 0) {
        continue;
      }
      value = (value << 6) | static_cast<uint32_t>(d);
      count++;
      if (count == 4) {
        emitGroup(value, count, out);
        value = 0;
        count = 0;
      }
    }
    if (count >= 2) {
      emitGroup(value, count, out);
    }
  }

  /// Percent decoding (RFC 3986), used for RFC 2231 values: %XX is a byte
  static String percentDecode(const String& text) {
    String out;
    for (unsigned int i = 0; i < text.length(); i++) {
      char c = text.charAt(i);
      if (c == '%' && i + 2 < text.length() && hexValue(text.charAt(i + 1)) >= 0 &&
          hexValue(text.charAt(i + 2)) >= 0) {
        out += static_cast<char>(hexValue(text.charAt(i + 1)) * 16 +
                                 hexValue(text.charAt(i + 2)));
        i += 2;
      } else {
        out += c;
      }
    }
    return out;
  }

  /**
   * @brief Converts bytes of a charset to UTF-8. UTF-8 and US-ASCII are kept;
   * ISO-8859-1 (and its close relatives) is converted; other charsets are kept
   * as they are.
   */
  static String toUtf8(const String& charset, const String& bytes) {
    String cs = lowerCopy(charset);
    if (cs == "utf-8" || cs == "us-ascii" || cs == "ascii") {
      return bytes;
    }
    if (cs == "iso-8859-1" || cs == "latin1" || cs == "iso-8859-15" || cs == "windows-1252") {
      String out;
      for (unsigned int i = 0; i < bytes.length(); i++) {
        uint8_t b = static_cast<uint8_t>(bytes.charAt(i));
        if (b < 0x80) {
          out += static_cast<char>(b);
        } else {
          out += static_cast<char>(0xC0 | (b >> 6));
          out += static_cast<char>(0x80 | (b & 0x3F));
        }
      }
      return out;
    }
    // one warning per charset: a mail can have many encoded words in the same charset
    static String warned;
    if (warned.indexOf(cs + ";") < 0) {
      warned += cs + ";";
      TinyLogger::instance().warn(String("MIME: charset not converted: ") + charset);
    }
    return bytes;
  }

  /**
   * @brief Decodes the RFC 2047 encoded words of a header value, e.g.
   * "=?UTF-8?B?w7Y=?=" becomes UTF-8 text. Text without encoded words is returned
   * unchanged. Spaces between two adjacent encoded words are removed.
   */
  static String decodeWords(const String& text) {
    String out;
    bool afterWord = false;
    unsigned int i = 0;
    while (i < text.length()) {
      char c = text.charAt(i);
      if (c == '=' && i + 1 < text.length() && text.charAt(i + 1) == '?') {
        int end = text.indexOf("?=", i + 2);
        if (end >= 0) {
          String word = text.substring(i + 2, end);  // charset?encoding?data
          int q1 = word.indexOf('?');
          int q2 = q1 >= 0 ? word.indexOf('?', q1 + 1) : -1;
          if (q1 > 0 && q2 == q1 + 2) {
            String charset = word.substring(0, q1);
            char encoding = word.charAt(q1 + 1);
            String data = word.substring(q2 + 1);
            String bytes = decodeWordData(encoding, data);
            out += toUtf8(charset, bytes);
            afterWord = true;
            i = end + 2;
            continue;
          }
        }
      }
      if ((c == ' ' || c == '\t') && afterWord) {
        // a space between two encoded words is not part of the text
        unsigned int next = i;
        while (next < text.length() && (text.charAt(next) == ' ' || text.charAt(next) == '\t')) {
          next++;
        }
        if (next + 1 < text.length() && text.charAt(next) == '=' && text.charAt(next + 1) == '?') {
          i = next;
          continue;
        }
      }
      out += c;
      afterWord = false;
      i++;
    }
    return out;
  }

 private:
  static void emitGroup(uint32_t value, int count, String& out) {
    // count characters carry count * 6 bits; the bytes are the top 8-bit groups
    uint32_t bits = value << (6 * (4 - count));
    int bytes = count - 1;
    if (bytes >= 1) out += static_cast<char>((bits >> 16) & 0xFF);
    if (bytes >= 2) out += static_cast<char>((bits >> 8) & 0xFF);
    if (bytes >= 3) out += static_cast<char>(bits & 0xFF);
  }

  static int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  }

  /// The data of an encoded word: B is base64, Q is quoted-printable with '_' as space
  static String decodeWordData(char encoding, const String& data) {
    if (encoding == 'B' || encoding == 'b') {
      String out;
      base64Decode(data.c_str(), data.length(), out);
      return out;
    }
    String out;
    for (unsigned int i = 0; i < data.length(); i++) {
      char c = data.charAt(i);
      if (c == '_') {
        out += ' ';
      } else if (c == '=' && i + 2 < data.length() && hexValue(data.charAt(i + 1)) >= 0 &&
                 hexValue(data.charAt(i + 2)) >= 0) {
        out += static_cast<char>(hexValue(data.charAt(i + 1)) * 16 + hexValue(data.charAt(i + 2)));
        i += 2;
      } else {
        out += c;
      }
    }
    return out;
  }
};
