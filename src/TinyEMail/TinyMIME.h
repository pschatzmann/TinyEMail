/**
 * @file TinyMIME.h
 * @brief Splits a received message into its parts. Every part is delivered to a
 * visitor with its data as a stream. The message is read as a stream, see
 * TinyMIMEReader.h.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <Arduino.h>  // String, Stream

#include "TinyMIMEReader.h"
#include "TinyMail.h"

/**
 * @brief Splits a received message into its parts.
 *
 * TinyMIME::parse() calls visit(const TinyMailPart&, TinyMailData&) for every
 * part of the message, in order. The text body is a part too, and so is every
 * attachment. Return false from visit to stop.
 */
class TinyMIME {
 public:
  /// Calls visit for each part of the message read from a stream
  template <typename Visit>
  static void parse(Stream& message, Visit visit) {
    TinyMIMEReader reader(message);
    String headers;
    if (!reader.begin(headers)) {
      return;
    }
    TinyMailPart info;
    while (reader.nextPart(info)) {
      TinyMailData data(&reader);
      bool more = visit(info, data);
      reader.skipPart();
      if (!more) {
        break;
      }
    }
  }

  /// Calls visit for each part of a complete message
  template <typename Visit>
  static void parse(const String& message, Visit visit) {
    TinyBufferStream stream(reinterpret_cast<const uint8_t*>(message.c_str()), message.length());
    parse(static_cast<Stream&>(stream), visit);
  }

  /// The header lines of a complete message, without the body
  static String headerBlock(const String& message) {
    TinyBufferStream stream(reinterpret_cast<const uint8_t*>(message.c_str()), message.length());
    TinyMIMEReader reader(stream);
    String headers;
    reader.begin(headers);
    if (headers.endsWith("\r\n")) {
      headers.remove(headers.length() - 2);  // the last line has no line break
    }
    return headers;
  }
};
