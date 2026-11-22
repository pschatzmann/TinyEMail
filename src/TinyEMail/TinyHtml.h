/**
 * @file TinyHtml.h
 * @brief Composes the HTML of a mail. The markup is written straight to a Print,
 * for example the body of a mail (TinyEMail::body()), so no markup is kept in RAM.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <Arduino.h>  // Print

/**
 * @brief Writes HTML to a Print. Text is escaped, so user input cannot add tags.
 * The calls can be chained:
 *
 * @code
 * mail.sendWith("you@example.com", "Report", "text/html", [](Print& body) {
 *   TinyHtml html(body);
 *   html.heading(1, "Report").paragraph("Temperature: 21.5 C")
 *       .link("https://example.com", "details").lineBreak()
 *       .beginList().item("first").item("second").endList();
 * });
 * @endcode
 */
class TinyHtml : public Print {
 public:
  explicit TinyHtml(Print& out) : out_(out) {}

  size_t write(uint8_t c) override { return out_.write(c); }

  /// Writes text, escaped: & < > " ' become entities
  TinyHtml& text(const char* text) {
    if (text == nullptr) {
      return *this;
    }
    for (const char* p = text; *p != '\0'; ++p) {
      switch (*p) {
        case '&': print("&amp;"); break;
        case '<': print("&lt;"); break;
        case '>': print("&gt;"); break;
        case '"': print("&quot;"); break;
        case '\'': print("&#39;"); break;
        default: write(static_cast<uint8_t>(*p));
      }
    }
    return *this;
  }

  /// Writes markup as it is (no escaping). Only use markup you have written yourself.
  TinyHtml& raw(const char* markup) {
    print(markup);
    return *this;
  }

  /// <tag>
  TinyHtml& open(const char* tag) {
    print('<');
    print(tag);
    print('>');
    return *this;
  }

  /// </tag>
  TinyHtml& close(const char* tag) {
    print("</");
    print(tag);
    print('>');
    return *this;
  }

  /// <tag>escaped text</tag>
  TinyHtml& element(const char* tag, const char* text) {
    open(tag);
    this->text(text);
    return close(tag);
  }

  /// <h1> to <h6>; the level is limited to 1..6
  TinyHtml& heading(uint8_t level, const char* text) {
    if (level < 1) level = 1;
    if (level > 6) level = 6;
    char tag[3] = {'h', static_cast<char>('0' + level), '\0'};
    return element(tag, text);
  }

  TinyHtml& paragraph(const char* text) { return element("p", text); }
  TinyHtml& bold(const char* text) { return element("b", text); }
  TinyHtml& italic(const char* text) { return element("i", text); }

  /// <a href="href">text</a>
  TinyHtml& link(const char* href, const char* text) {
    print("<a href=\"");
    attribute(href);
    print("\">");
    this->text(text);
    return close("a");
  }

  /// <img src="src" alt="alt">
  TinyHtml& image(const char* src, const char* alt) {
    print("<img src=\"");
    attribute(src);
    print("\" alt=\"");
    attribute(alt);
    print("\">");
    return *this;
  }

  /// <br>
  TinyHtml& lineBreak() { return raw("<br>"); }

  /// <ul>
  TinyHtml& beginList() { return open("ul"); }
  /// </ul>
  TinyHtml& endList() { return close("ul"); }
  /// <li>escaped text</li>
  TinyHtml& item(const char* text) { return element("li", text); }

 private:
  Print& out_;

  /// Text inside a quoted attribute value: the same escaping as text()
  void attribute(const char* value) { text(value); }
};
