/**
 * @file TinyHtmlView.h
 * @brief Simple streaming HTML renderer drawn with TinyFonts (TinyMaterialDesign widget).
 *
 * The content is never loaded into memory: the renderer reads the mail file
 * through the File API one byte at a time, lays out words and draws each word
 * at its position. Scrolling is done by the Container that holds the view.
 *
 * Supported: paragraphs, headings (h1-h6), line breaks, list items, entities
 * (&amp; &lt; &gt; &quot; &#NN; ...). Script, style, head and comments are
 * skipped. Links, images, tables and fonts are shown as their text only.
 * The text is UTF-8; charsets other than UTF-8 are not converted.
 */
#pragma once

#include <fatfs.h>
#include <initializer_list>
#include <stdlib.h>
#include <string.h>

#include <TinyFontsGPU.h>
#include <TinyMaterialDesign.h>

#include "../TinyEMail.h"  // TinyTextStream

class TinyHtmlView : public Widget<RGB565> {
 public:
  static constexpr int kPadding = 6;

  /**
   * @brief Sets the content: a plain text header followed by the file at path.
   * plain = true shows the file as text ('\n' breaks lines), otherwise as HTML.
   * The height of the view is set to the height of the content.
   */
  void setSource(const String& header, fatfs::SDClass& fs, const String& path, bool plain) {
    header_ = header;
    fs_ = &fs;
    path_ = path;
    plain_ = plain;
    layout();
  }

  /// Height of the content in pixels, after layout()
  int contentHeight() const { return contentHeight_; }

  /// Text in the smallest font (Lato8), e.g. for the header of a mail
  void setSmallText(bool small) { small_ = small; }

  /// Colour behind the text (default: the background of the theme)
  void setBackgroundColor(RGB565 color) {
    backgroundColor_ = color;
    hasBackgroundColor_ = true;
  }

  /// Measures the content (without drawing) and sets the height of the view
  void layout() {
    render(nullptr);
    bounds.h = contentHeight_;  // includes the padding
  }

  void draw(tinygpu::ISurface<RGB565>& target) override { render(&target); }

 private:
  String header_;
  bool small_ = false;
  RGB565 backgroundColor_{};
  bool hasBackgroundColor_ = false;
  fatfs::SDClass* fs_ = nullptr;  ///< file system of the body (not owned)
  String path_;
  bool plain_ = false;
  int contentHeight_ = 0;

  // Fonts are created once; they only wrap the TinyFonts data
  static tinyfonts::TinyGPUFont<RGB565>& bodyFont() {
    static tinyfonts::TinyGPUFont<RGB565> font(tinyfonts::Lato12);
    return font;
  }
  // Headings use Lato16: Lato32 is too big for the 240 px screen
  static tinyfonts::TinyGPUFont<RGB565>& smallFont() {
    static tinyfonts::TinyGPUFont<RGB565> font(tinyfonts::Lato8);
    return font;
  }
  static tinyfonts::TinyGPUFont<RGB565>& headingFont() {
    static tinyfonts::TinyGPUFont<RGB565> font(tinyfonts::Lato16);
    return font;
  }

  /// One pass over the content: draws it when target is set, always measures it
  void render(tinygpu::ISurface<RGB565>* target) {
    Pass pass(*this, target);
    if (header_.length() > 0) {
      TinyTextStream headerStream(header_.c_str());
      pass.run(headerStream, true);
    }
    if (path_.length() > 0 && fs_ != nullptr) {
      fatfs::File f = fs_->open(path_.c_str(), FILE_READ);
      if (f) {
        pass.run(f, plain_);
        f.close();
      }
    }
    pass.finish();
    contentHeight_ = pass.height();
  }

  /// State of one pass. Words are the only text kept in memory (max 60 bytes).
  struct Pass {
    static constexpr int kMaxWord = 60;
    static constexpr int kMaxTag = 24;
    static constexpr int kSmallToggle = 0x01;

    Pass(TinyHtmlView& view, tinygpu::ISurface<RGB565>* target)
        : view_(view), target_(target), maxW_(view.bounds.w - 2 * kPadding) {
      y_ = kPadding;
    }

    void run(Stream& in, bool plain) {
      plain_ = plain;
      inTag_ = false;
      inEntity_ = false;
      inlineSmall_ = false;
      int c;
      while ((c = in.read()) >= 0) {
        feed(c);
      }
    }

    void finish() {
      flushWord();
      height_ = y_ + (lineH_ > 0 ? lineH_ : 0) + kPadding;
    }

    int height() const { return height_; }

   private:
    TinyHtmlView& view_;
    tinygpu::ISurface<RGB565>* target_;
    int maxW_;
    int x_ = 0;
    int y_ = 0;
    int lineH_ = 0;
    int height_ = 0;
    bool space_ = false;
    bool plain_ = false;
    bool heading_ = false;
    bool inlineSmall_ = false;  // the small font is on (see kSmallToggle)
    int skip_ = 0;  // > 0 inside script, style, head or title

    char word_[kMaxWord + 1] = {0};
    int wlen_ = 0;

    bool inTag_ = false;
    bool comment_ = false;
    int dashes_ = 0;
    char tag_[kMaxTag + 1] = {0};
    int tlen_ = 0;

    bool inEntity_ = false;
    char entity_[12] = {0};
    int elen_ = 0;

    tinyfonts::TinyGPUFont<RGB565>& font() const {
      if (heading_) return headingFont();
      return (view_.small_ || inlineSmall_) ? smallFont() : bodyFont();
    }

    int fontHeight() const { return static_cast<int>(font().getHeight(1)); }

    // Moves a word of a smaller font down so that its baseline is on the baseline of the body text
    int baselineShift() const { return bodyFont().font().ascent() - font().font().ascent(); }

    void feed(int c) {
      // in plain text, byte 0x01 switches the small font on and off for the text after it
      if (plain_ && c == kSmallToggle) {
        flushWord();
        inlineSmall_ = !inlineSmall_;
        return;
      }
      if (inTag_) {
        tagChar(c);
      } else if (inEntity_) {
        entityChar(c);
      } else if (!plain_ && c == '<') {
        flushWord();
        inTag_ = true;
        comment_ = false;
        dashes_ = 0;
        tlen_ = 0;
        tag_[0] = 0;
      } else if (!plain_ && c == '&') {
        inEntity_ = true;
        elen_ = 0;
      } else {
        text(c);
      }
    }

    // ---- text -----------------------------------------------------------------

    void text(int c) {
      if (skip_ > 0 || c == '\r') return;
      if (c == '\n') {
        flushWord();
        if (plain_) {
          newline();
        } else {
          space_ = true;
        }
        return;
      }
      if (c == ' ' || c == '\t') {
        flushWord();
        space_ = true;
        return;
      }
      addChar(c);
    }

    void addChar(int c) {
      if (skip_ > 0) return;
      if (wlen_ >= kMaxWord) flushWord();
      word_[wlen_++] = static_cast<char>(c);
      word_[wlen_] = 0;
    }

    void addCodepoint(uint32_t cp) {
      if (cp < 0x80) {
        addChar(static_cast<int>(cp));
      } else if (cp < 0x800) {
        addChar(0xC0 | (cp >> 6));
        addChar(0x80 | (cp & 0x3F));
      } else if (cp < 0x10000) {
        addChar(0xE0 | (cp >> 12));
        addChar(0x80 | ((cp >> 6) & 0x3F));
        addChar(0x80 | (cp & 0x3F));
      } else {
        addChar(0xF0 | (cp >> 18));
        addChar(0x80 | ((cp >> 12) & 0x3F));
        addChar(0x80 | ((cp >> 6) & 0x3F));
        addChar(0x80 | (cp & 0x3F));
      }
    }

    /// Places the word on the line, wrapping to the next line when it does not fit
    void flushWord() {
      if (wlen_ == 0) return;
      tinyfonts::TinyGPUFont<RGB565>& f = font();
      int spaceW = (space_ && x_ > 0) ? static_cast<int>(f.measureTextWidth(" ")) : 0;
      int w = static_cast<int>(f.measureTextWidth(word_));
      if (x_ > 0 && x_ + spaceW + w > maxW_) {
        newline();
        spaceW = 0;
      }
      x_ += spaceW;
      if (target_ != nullptr) {
        f.drawText(*target_, static_cast<int16_t>(view_.bounds.x + kPadding + x_),
                   static_cast<int16_t>(view_.bounds.y + y_ + baselineShift()), word_, textColor(),
                   background(), false);
      }
      x_ += w;
      if (fontHeight() > lineH_) lineH_ = fontHeight();
      wlen_ = 0;
      word_[0] = 0;
      space_ = false;
    }

    void newline() {
      int h = lineH_ > 0 ? lineH_ : fontHeight();
      y_ += h;
      x_ = 0;
      lineH_ = 0;
      space_ = false;
    }

    /// Gap between two blocks (paragraphs, headings, list)
    void blockGap() {
      flushWord();
      if (x_ > 0 || lineH_ > 0) newline();
      y_ += fontHeight() / 2;
    }

    RGB565 textColor() const { return view_.theme().colors.onSurface; }
    RGB565 background() const {
      return view_.hasBackgroundColor_ ? view_.backgroundColor_ : view_.theme().colors.background;
    }

    // ---- tags -------------------------------------------------------------------

    void tagChar(int c) {
      if (comment_) {
        // a comment ends with "-->"
        if (c == '>' && dashes_ >= 2) {
          inTag_ = false;
        } else {
          dashes_ = (c == '-') ? dashes_ + 1 : 0;
        }
        return;
      }
      if (c == '>') {
        inTag_ = false;
        endTag();
        return;
      }
      if (tlen_ < kMaxTag) {
        tag_[tlen_++] = static_cast<char>(c);
        tag_[tlen_] = 0;
      }
      if (tlen_ == 3 && strcmp(tag_, "!--") == 0) {
        comment_ = true;
        dashes_ = 0;
      }
    }

    void endTag() {
      bool closing = tag_[0] == '/';
      bool selfClosing = tlen_ > 0 && tag_[tlen_ - 1] == '/';
      if (tag_[0] == '!') return;  // doctype

      // name: letters up to a space, '/' or the end
      char name[12] = {0};
      int n = 0;
      for (int i = closing ? 1 : 0; i < tlen_ && n < 11; i++) {
        char c = tag_[i];
        if (c == ' ' || c == '/' || c == '\t' || c == '\n') break;
        name[n++] = (c >= 'A' && c <= 'Z') ? c + 32 : c;
      }
      name[n] = 0;

      if (isOneOf(name, {"script", "style", "head", "title"})) {
        if (closing) {
          if (skip_ > 0) skip_--;
        } else if (!selfClosing) {
          skip_++;
        }
        return;
      }
      if (skip_ > 0) return;

      if (strcmp(name, "br") == 0) {
        flushWord();
        newline();
      } else if (strcmp(name, "h1") == 0 || strcmp(name, "h2") == 0) {
        blockGap();
        heading_ = !closing;
      } else if (strcmp(name, "h3") == 0 || strcmp(name, "h4") == 0 ||
                 strcmp(name, "h5") == 0 || strcmp(name, "h6") == 0) {
        blockGap();
      } else if (strcmp(name, "li") == 0) {
        flushWord();
        newline();
        if (!closing) {
          addChar('-');
          addChar(' ');
        }
      } else if (isOneOf(name, {"p", "div", "ul", "ol", "table", "tr", "blockquote", "hr",
                                "section", "header", "footer", "article"})) {
        blockGap();
      }
    }

    static bool isOneOf(const char* name, std::initializer_list<const char*> names) {
      for (const char* n : names) {
        if (strcmp(name, n) == 0) return true;
      }
      return false;
    }

    // ---- entities -----------------------------------------------------------------

    void entityChar(int c) {
      bool end = c == ';' || elen_ >= 10 || !(alnum(c) || c == '#');
      if (!end) {
        entity_[elen_++] = static_cast<char>(c);
        return;
      }
      entity_[elen_] = 0;
      inEntity_ = false;
      int32_t cp = entityCode(entity_);
      if (cp >= 0) {
        addCodepoint(static_cast<uint32_t>(cp));
      } else {
        // unknown entity: show it as written
        addChar('&');
        for (int i = 0; i < elen_; i++) addChar(entity_[i]);
        if (c == ';') addChar(';');
      }
      if (c != ';') feed(c);
    }

    static bool alnum(int c) {
      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    }

    static int32_t entityCode(const char* name) {
      struct Named {
        const char* name;
        int32_t code;
      };
      static const Named named[] = {
          {"amp", 38},     {"lt", 60},     {"gt", 62},      {"quot", 34},    {"apos", 39},
          {"nbsp", 0xA0},  {"copy", 0xA9}, {"reg", 0xAE},   {"ndash", 0x2013}, {"mdash", 0x2014},
          {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"ldquo", 0x201C}, {"rdquo", 0x201D},
          {"hellip", 0x2026}, {"bull", 0x2022}, {"euro", 0x20AC},
      };
      for (const Named& e : named) {
        if (strcmp(e.name, name) == 0) return e.code;
      }
      if (name[0] == '#') {
        long value = (name[1] == 'x' || name[1] == 'X') ? strtol(name + 2, nullptr, 16)
                                                        : strtol(name + 1, nullptr, 10);
        if (value > 0 && value < 0x110000) return static_cast<int32_t>(value);
      }
      return -1;
    }
  };
};
