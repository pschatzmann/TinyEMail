/**
 * @file TinyMailStore.h
 * @brief Stores mails in a FAT file system, using TinyFATFS.
 *
 * The storage is given as a TinyFATFS driver: fatfs::ArduinoSpiIO for an SD
 * card on an Arduino board, fatfs::FileIO (a disk image file) on the desktop.
 *
 * Layout (the stored mails are the folders m<uid> of mails/, found by a scan):
 *   mails/m<uid>/mail.txt    From, Subject and one Attachment line per file
 *   mails/m<uid>/body.html   the HTML body of the mail (if it has one)
 *   mails/m<uid>/body.txt    the text body of the mail (if it has one)
 *   mails/m<uid>/<name>      each attachment as a file
 *
 * The parts are copied to their files while they are read, and the bodies are
 * rendered from their files as well: no mail is ever held in RAM as a whole.
 */
#pragma once

#include <string.h>

#include <fatfs.h>
#include <filesystem.h>  // directory_iterator: lists the stored mails

#include "../TinyEMail.h"

class TinyMailStore {
 public:
  static constexpr int kMaxMails = 32;

  /// Mounts the file system on the driver and loads the list of stored mails
  bool begin(fatfs::IO& driver) {
    if (!fs_.begin(driver)) {
      Serial.println("TinyMailStore: file system not available");
      return false;
    }
    if (!fs_.exists(kRoot)) {
      fs_.mkdir(kRoot);
    }
    loadIndex();
    return true;
  }

  /// The file system holding the mails (also used for other files of the GUI)
  fatfs::SDClass& fs() { return fs_; }

  /// Number of stored mails
  int count() const { return count_; }

  /// Folder name of the mail at index i (relative to kRoot)
  const String& folder(int i) const { return folders_[i]; }

  /// Subject of the mail at index i
  const String& subject(int i) const { return subjects_[i]; }

  /// Sender of the mail at index i
  const String& from(int i) const { return froms_[i]; }

  /// Date of the mail at index i, as written in its Date header (may be empty)
  const String& date(int i) const { return dates_[i]; }

  /// Folder of the mail at index i: "inbox" (received) or "sent"
  const String& box(int i) const { return boxes_[i]; }

  /// True when the mail at index i has been opened (see markRead())
  bool read(int i) const { return reads_[i]; }

  /// Sets the read flag of the mail at index i and writes it to the header of the mail
  bool markRead(int i, bool read) {
    reads_[i] = read;
    String lines = "From: " + froms_[i] + "\nSubject: " + subjects_[i] + "\nDate: " + dates_[i] + "\nBox: " +
                   boxes_[i] + "\nRead: " + String(read ? "1" : "0") + "\n";
    for (int k = 0; k < attachmentCount(i); k++) {
      lines += String(kAttachment) + attachmentName(i, k) + "\n";
    }
    return writeText(String(kRoot) + "/" + folders_[i] + "/mail.txt", lines);
  }

  /// File of the body of the mail at index i: the HTML part if there is one,
  /// otherwise the text part. plain is set to true for a text part. The file
  /// is not read here, it is rendered by TinyHtmlView while it is streamed.
  String bodyPath(int i, bool& plain) {
    String html = path(i, "body.html");
    plain = false;
    if (fs_.exists(html.c_str())) return html;
    plain = true;
    String text = path(i, "body.txt");
    return fs_.exists(text.c_str()) ? text : String();
  }

  /// Names of the attachments of the mail at index i, separated by ", "
  String attachments(int i) {
    String names;
    fatfs::File f = fs_.open(path(i, "mail.txt").c_str(), FILE_READ);
    if (!f) return names;
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) break;  // end of the header block
      if (line.startsWith(kAttachment)) {
        if (names.length() > 0) names += ", ";
        names += line.substring(strlen(kAttachment));
      }
    }
    f.close();
    return names;
  }

  /**
   * @brief Stores a mail. A mail that is already stored is not stored again.
   * The parts are read once, forward only: the HTML and text bodies go to
   * their files and every other part to a file of its own.
   */
  bool save(const TinyEMail::Mail& mail) {
    String name = "m" + mail.uid;
    if (indexOf(name) >= 0) return true;
    if (count_ >= kMaxMails) {
      Serial.println("TinyMailStore: store is full");
      return false;
    }
    String dir = String(kRoot) + "/" + name;
    if (!fs_.exists(dir.c_str()) && !fs_.mkdir(dir.c_str())) {
      return false;
    }

    String attachmentLines;
    bool htmlWritten = false;
    bool textWritten = false;
    int partNo = 0;
    for (TinyPart& part : mail.parts()) {
      partNo++;
      if (!part.info.attachment && !htmlWritten && part.info.contentType == "text/html") {
        htmlWritten = copyToFile(part.data, dir + "/body.html");
        continue;
      }
      if (!part.info.attachment && !textWritten && part.info.contentType == "text/plain") {
        textWritten = copyToFile(part.data, dir + "/body.txt");
        continue;
      }
      String fileName = part.info.filename.length() > 0 ? part.info.filename
                                                        : "part" + String(partNo);
      fileName = sanitize(fileName);
      if (copyToFile(part.data, dir + "/" + fileName)) {
        attachmentLines += String(kAttachment) + fileName + "\n";
      }
    }

    String date = mail.header("Date");
    String header = "From: " + mail.from + "\nSubject: " + mail.subject + "\nDate: " + date + "\nBox: inbox\nRead: 0\n" +
                    attachmentLines;
    if (!writeText(dir + "/mail.txt", header)) {
      return false;
    }

    folders_[count_] = name;
    froms_[count_] = mail.from;
    subjects_[count_] = mail.subject;
    dates_[count_] = date;
    boxes_[count_] = "inbox";
    reads_[count_] = false;
    count_++;
    return true;
  }

  /// Number of attachments of the mail at index i
  int attachmentCount(int i) {
    int n = 0;
    fatfs::File f = fs_.open(path(i, "mail.txt").c_str(), FILE_READ);
    if (!f) return 0;
    while (f.available()) {
      String line = f.readStringUntil('\n');
      if (line.startsWith(kAttachment)) n++;
    }
    f.close();
    return n;
  }

  /// Name of the attachment k (0-based) of the mail at index i, or "" if there is none
  String attachmentName(int i, int k) {
    int n = 0;
    fatfs::File f = fs_.open(path(i, "mail.txt").c_str(), FILE_READ);
    if (!f) return "";
    String name;
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (!line.startsWith(kAttachment)) continue;
      if (n++ == k) {
        name = line.substring(strlen(kAttachment));
        break;
      }
    }
    f.close();
    return name;
  }

  /// File of the attachment k of the mail at index i (its name is attachmentName())
  String attachmentPath(int i, int k) { return path(i, attachmentName(i, k).c_str()); }

  /**
   * @brief Stores a mail from its texts, without a server (e.g. test data).
   * text and html are the bodies (empty = not present); an attachment is
   * stored as a file named attachmentName with the content attachmentData.
   */
  bool add(const String& uid, const String& from, const String& subject, const String& text,
           const String& html = "", const String& attachmentName = "",
           const String& attachmentData = "", const String& date = "", const String& box = "inbox") {
    String name = "m" + uid;
    if (indexOf(name) >= 0 || count_ >= kMaxMails) return false;
    String dir = String(kRoot) + "/" + name;
    if (!fs_.exists(dir.c_str()) && !fs_.mkdir(dir.c_str())) return false;
    if (html.length() > 0) writeText(dir + "/body.html", html);
    if (text.length() > 0) writeText(dir + "/body.txt", text);
    String header = "From: " + from + "\nSubject: " + subject + "\nDate: " + date + "\nBox: " + box + "\nRead: " + String(box == "sent" ? "1" : "0") + "\n";
    dates_[count_] = date;
    boxes_[count_] = box;
    reads_[count_] = box == "sent";
    if (attachmentName.length() > 0) {
      String fileName = sanitize(attachmentName);
      writeText(dir + "/" + fileName, attachmentData);
      header += String(kAttachment) + fileName + "\n";
    }
    if (!writeText(dir + "/mail.txt", header)) return false;

    folders_[count_] = name;
    froms_[count_] = from;
    subjects_[count_] = subject;
    count_++;
    return true;
  }

  /**
   * @brief Adds a binary attachment (e.g. a picture) to a stored mail. The
   * attachments already listed for the mail are kept.
   */
  bool attachFile(const String& uid, const String& fileName, const uint8_t* data, size_t size) {
    int i = indexOf("m" + uid);
    if (i < 0) return false;
    String dir = String(kRoot) + "/" + folders_[i];
    String name = sanitize(fileName);
    String lines = "From: " + froms_[i] + "\nSubject: " + subjects_[i] + "\nDate: " + dates_[i] + "\nBox: " +
                   boxes_[i] + "\nRead: " + String(reads_[i] ? "1" : "0") + "\n";
    int n = attachmentCount(i);
    for (int k = 0; k < n; k++) {
      lines += String(kAttachment) + attachmentName(i, k) + "\n";
    }
    lines += String(kAttachment) + name + "\n";

    fs_.remove((dir + "/" + name).c_str());
    fatfs::File f = fs_.open((dir + "/" + name).c_str(), FILE_WRITE);
    if (!f) return false;
    f.write(data, size);
    f.close();
    return writeText(dir + "/mail.txt", lines);
  }

  /// Writes a small text file (used for the settings of the GUI)
  bool writeText(const String& filePath, const String& text) {
    fs_.remove(filePath.c_str());
    fatfs::File f = fs_.open(filePath.c_str(), FILE_WRITE);
    if (!f) return false;
    f.print(text);
    f.close();
    return true;
  }

 private:
  static constexpr const char* kRoot = "mails";
  static constexpr const char* kAttachment = "Attachment: ";

  fatfs::SDClass fs_;
  String folders_[kMaxMails];
  String subjects_[kMaxMails];
  String froms_[kMaxMails];
  String dates_[kMaxMails];
  String boxes_[kMaxMails];  // "inbox" or "sent"
  bool reads_[kMaxMails] = {};  // the read flag: a mail without a Read line is unread
  int count_ = 0;

  String path(int i, const char* file) const { return String(kRoot) + "/" + folders_[i] + "/" + file; }

  int indexOf(const String& name) const {
    for (int i = 0; i < count_; i++) {
      if (folders_[i] == name) return i;
    }
    return -1;
  }

  /// Lists the folders m<uid> of kRoot with one directory scan (no index file)
  void loadIndex() {
    count_ = 0;
    fatfs::directory_iterator it = fs_.directoryIterator(kRoot);
    while (it != fatfs::directory_iterator::end() && count_ < kMaxMails) {
      fatfs::directory_entry entry = *it;
      ++it;
      if (!entry.is_directory) continue;
      String full(entry.path.c_str());
      String name = full.substring(full.lastIndexOf('/') + 1);
      if (!name.startsWith("m")) continue;
      folders_[count_++] = name;
    }
    // read the headers after the scan, one file at a time
    for (int i = 0; i < count_; i++) {
      String mailFile = String(kRoot) + "/" + folders_[i] + "/mail.txt";
      froms_[i] = headerValue(mailFile, "From: ");
      subjects_[i] = headerValue(mailFile, "Subject: ");
      dates_[i] = headerValue(mailFile, "Date: ");
      boxes_[i] = headerValue(mailFile, "Box: ");
      reads_[i] = headerValue(mailFile, "Read: ") == "1";
      if (boxes_[i].length() == 0) boxes_[i] = "inbox";
    }
  }

  /// Value of the first header line that starts with key
  String headerValue(const String& filePath, const char* key) {
    fatfs::File f = fs_.open(filePath.c_str(), FILE_READ);
    if (!f) return "";
    String value;
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) break;
      if (line.startsWith(key)) {
        value = line.substring(strlen(key));
        break;
      }
    }
    f.close();
    return value;
  }

  /// Copies a stream to a new file, one buffer at a time
  bool copyToFile(Stream& in, const String& filePath) {
    fs_.remove(filePath.c_str());
    fatfs::File out = fs_.open(filePath.c_str(), FILE_WRITE);
    if (!out) return false;
    uint8_t buf[256];
    size_t n = 0;
    int c;
    while ((c = in.read()) >= 0) {
      buf[n++] = static_cast<uint8_t>(c);
      if (n == sizeof(buf)) {
        out.write(buf, n);
        n = 0;
      }
    }
    if (n > 0) out.write(buf, n);
    out.close();
    return true;
  }

  /// Letters, digits, '.', '-' and '_' only, at most 40 characters
  static String sanitize(const String& name) {
    String out;
    for (unsigned int i = 0; i < name.length() && out.length() < 40; i++) {
      char c = name.charAt(i);
      bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                c == '.' || c == '-' || c == '_';
      out += ok ? c : '_';
    }
    if (out.length() == 0) out = "attachment";
    return out;
  }
};
