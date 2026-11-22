/**
 * @file TinyLogger.h
 * @brief A logger with levels for TinyEMail. There is one logger object,
 * reached through the static reference TinyLogger::instance().
 *
 * @code
 * TinyLogger::instance().begin(Serial, TinyLogger::Level::Debug);  // Serial and Debug are the defaults
 * TinyLogger::instance().info("ready");
 * @endcode
 *
 * Until begin() is called, nothing is logged. The library never writes
 * passwords or the content of mails to the log.
 */
#pragma once

#include <stdint.h>

#include <Arduino.h>  // Print, Serial, millis

/**
 * @brief Writes messages with a level to a Print (Serial by default).
 */
class TinyLogger {
 public:
  /// Log levels: a message is written if its level is at most the level of the logger
  enum class Level : uint8_t {
    None = 0,   ///< no messages
    Error = 1,  ///< something failed
    Warn = 2,   ///< something unexpected, the operation can go on
    Info = 3,   ///< the main steps: login, send, receive
    Debug = 4,  ///< the protocol lines and details
  };

  /// The one logger of the library. A function static, so it is shared by all files.
  static TinyLogger& instance() {
    static TinyLogger logger;
    return logger;
  }

  /**
   * @brief Starts logging.
   * @param out where the messages are written, Serial by default
   * @param level the highest level that is written, Info by default
   */
  void begin(Print& out = Serial, Level level = Level::Info) {
    out_ = &out;
    level_ = level;
  }

  /// Stops logging
  void end() {
    out_ = nullptr;
    level_ = Level::None;
  }

  /// Changes the highest level that is written
  void setLevel(Level level) { level_ = level; }

  /// @return the highest level that is written
  Level level() const { return level_; }

  /// true if a message of this level would be written
  bool enabled(Level level) const {
    return out_ != nullptr && level != Level::None &&
           static_cast<uint8_t>(level) <= static_cast<uint8_t>(level_);
  }

  /// Writes a message with the given level, e.g. "[1234] I: login accepted"
  void log(Level level, const char* message) {
    if (!enabled(level)) {
      return;
    }
    out_->print('[');
    out_->print(static_cast<unsigned long>(millis()));
    out_->print("] ");
    out_->print(prefix(level));
    out_->println(message);
  }

  void log(Level level, const String& message) { log(level, message.c_str()); }

  void error(const char* message) { log(Level::Error, message); }
  void warn(const char* message) { log(Level::Warn, message); }
  void info(const char* message) { log(Level::Info, message); }
  void debug(const char* message) { log(Level::Debug, message); }

  void error(const String& message) { log(Level::Error, message); }
  void warn(const String& message) { log(Level::Warn, message); }
  void info(const String& message) { log(Level::Info, message); }
  void debug(const String& message) { log(Level::Debug, message); }

 private:
  Print* out_ = nullptr;
  Level level_ = Level::None;

  static const char* prefix(Level level) {
    switch (level) {
      case Level::Error:
        return "E ";
      case Level::Warn:
        return "W ";
      case Level::Info:
        return "I ";
      case Level::Debug:
        return "D ";
      default:
        return "";
    }
  }
};

/// The static logger object of the library: TinyLogger::instance().begin(), TinyLogger::instance().info(), ...
static TinyLogger& Logger __attribute__((unused)) = TinyLogger::instance();
