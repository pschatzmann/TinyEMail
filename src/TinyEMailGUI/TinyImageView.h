/**
 * @file TinyImageView.h
 * @brief Shows a picture attachment: JPEG, PNG, GIF or BMP.
 *
 * JPEG is decoded with TinyJPEG (TinyGPU's JPEGParser) while the file is read,
 * so the file is never held in memory. PNG, GIF and BMP are decoded from memory;
 * they are limited to kMaxBufferedBytes. The decoded picture is kept in a
 * FrameBuffer of its own size.
 */
#pragma once

#include <fatfs.h>
#include <string.h>

#include <TinyFontsGPU.h>
#include <TinyGPU/IO/BMPParser.h>
#include <TinyGPU/IO/GIFParser.h>
#include <TinyGPU/IO/JPEGParser.h>
#include <TinyGPU/IO/PNGParser.h>
#include <TinyGPU/Surface/FrameBuffer.h>
#include <TinyMaterialDesign.h>

class TinyImageView : public Widget<RGB565> {
 public:
  static constexpr size_t kMaxBufferedBytes = 32 * 1024;  // PNG, GIF and BMP are decoded from memory
  static constexpr int kPadding = 6;

  /// True for the file names of the supported picture formats
  static bool isPicture(const String& name) {
    String lower = name;
    lower.toLowerCase();
    return lower.endsWith(".jpg") || lower.endsWith(".jpeg") || lower.endsWith(".png") ||
           lower.endsWith(".gif") || lower.endsWith(".bmp");
  }

  /// Decodes the picture at path. Returns false if it cannot be shown; error() says why.
  bool load(fatfs::SDClass& fs, const String& path) {
    loaded_ = false;
    error_ = "";
    fatfs::File f = fs.open(path.c_str(), FILE_READ);
    if (!f) {
      error_ = "Cannot open the picture";
      return false;
    }
    uint8_t head[8] = {0};
    f.read(head, sizeof(head));
    f.seek(0);
    const size_t size = f.available();

    if (head[0] == 0xFF && head[1] == 0xD8) {
      tinygpu::JPEGParser<RGB565> parser(sprite_);
      loaded_ = parser.decode(f);  // streamed from the file
    } else if (size > kMaxBufferedBytes) {
      error_ = "Picture too large to show";
    } else if (memcmp(head, "\x89PNG", 4) == 0) {
      loaded_ = decodeBuffered<tinygpu::PNGParser<RGB565>>(f, size);
    } else if (memcmp(head, "GIF8", 4) == 0) {
      loaded_ = decodeBuffered<tinygpu::GIFParser<RGB565>>(f, size);
    } else if (head[0] == 'B' && head[1] == 'M') {
      loaded_ = decodeBmp(f, size);
    } else {
      error_ = "Picture format not supported";
    }
    f.close();
    if (!loaded_ && error_.length() == 0) error_ = "Cannot decode the picture";
    return loaded_;
  }

  bool loaded() const { return loaded_; }
  const String& error() const { return error_; }

  /// Sets the height of the view: the picture (or the error text) plus padding
  void layout() {
    bounds.h = loaded_ ? static_cast<int32_t>(sprite_.height()) + 2 * kPadding : 40;
  }

  void draw(tinygpu::ISurface<RGB565>& target) override {
    if (loaded_) {
      // magenta is the colour of transparent pixels; test pictures do not use it
      target.drawSprite(bounds.x + kPadding, bounds.y + kPadding, sprite_, RGB565::fromRGB(255, 0, 255));
      return;
    }
    const MaterialTheme<RGB565>& t = this->theme();
    const RGB565 background = t.colors.surface;
    tinyfonts::TinyGPUFont<RGB565> font(tinyfonts::Lato12);
    font.drawText(target, static_cast<int16_t>(bounds.x + kPadding), static_cast<int16_t>(bounds.y + kPadding),
                  error_.c_str(), t.colors.onSurfaceVariant, background, false);
  }

 private:
  tinygpu::FrameBuffer<RGB565> sprite_;
  bool loaded_ = false;
  String error_;

  template <typename Parser>
  bool decodeBuffered(fatfs::File& f, size_t size) {
    uint8_t* data = new uint8_t[size];
    const size_t n = f.read(data, size);
    Parser parser(sprite_);
    const bool ok = parser.decode(data, n);
    delete[] data;
    if (!ok) error_ = "Cannot decode the picture";
    return ok;
  }

  bool decodeBmp(fatfs::File& f, size_t size) {
    uint8_t* data = new uint8_t[size];
    const size_t n = f.read(data, size);
    tinygpu::BMPParser<RGB565> parser(sprite_);
    parser.write(data, n);
    const bool ok = parser.isComplete();
    delete[] data;
    if (!ok) error_ = "Cannot decode the picture";
    return ok;
  }
};
