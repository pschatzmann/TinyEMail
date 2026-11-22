/**
 * @file TinyEMailGUI.h
 * @brief E-mail client with a GUI for TinyMaterialDesign (240x320 screens).
 *
 * Screens:
 *   - list of the stored mails (subject and sender); the menu (top right) has
 *     Fetch, Setup and Exit
 *   - one mail: the app bar has a back arrow and the subject; below it the
 *     sender, a row per attachment, a divider and the body (HTML rendered with TinyFonts)
 *   - one attachment: a text file is shown as text; any other file shows its
 *     name and size (it stays in the mail folder on the storage)
 *   - login: shown when Fetch cannot log in or cannot reach the network
 *
 * Every mail is stored on the SD card (see TinyMailStore), each attachment as a
 * file of its own. Needs TinyMaterialDesign (with TinyGPU) and TinyFonts, so
 * include this header only in sketches that use them:
 *
 *   App<RGB565> app(board);
 *   TinyEMailGUI gui(app, {"me@gmail.com", "app-password", 10, "mails.img"});
 *
 *   void setup() { app.begin(); gui.begin(); }
 *   void loop()  { gui.tick(); app.update(); }
 */
#pragma once

#include <stdlib.h>

#include <string>
#include <vector>

#include <TinyMaterialDesign.h>
#include <TinyMaterialDesign/Widgets/WifiLoginPage.h>
#include <TinyMaterialDesign/Wifi/WifiControl.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "../TinyEMail.h"
#include "../TinyEMail/TinyMailStore.h"
#include "FileIO.h"
#include "TinyHtmlView.h"
#include "TinyImageView.h"

#if defined(ESP32)
#include <driver/ArduinoSpiIO.h>
#include <TinyMaterialDesign/Wifi/WifiEsp32.h>
#endif

/// Back arrow for the app bar (an IconPainter: draws a "<" in the given box)
inline void drawBackArrow(tinygpu::ISurface<RGB565>& target, const Bounds& r, RGB565 color,
                          uint8_t thickness) {
  const int32_t cx = r.x + r.w * 0.6f;
  const int32_t top = r.y + r.h * 0.25f;
  const int32_t mid = r.y + r.h / 2;
  const int32_t bottom = r.y + r.h * 0.75f;
  const int32_t tip = r.x + r.w * 0.35f;
  drawLineThick<RGB565>(target, cx, top, tip, mid, color, thickness);
  drawLineThick<RGB565>(target, tip, mid, cx, bottom, color, thickness);
}

/// Envelope icon: a mail without attachments (an IconPainter)
inline void drawMail(tinygpu::ISurface<RGB565>& target, const Bounds& r, RGB565 color, uint8_t thickness) {
  const int32_t x0 = r.x + 2;
  const int32_t x1 = r.x + r.w - 3;
  const int32_t y0 = r.y + r.h / 5;
  const int32_t y1 = r.y + r.h - r.h / 5 - 1;
  const int32_t xm = (x0 + x1) / 2;
  const int32_t ym = y0 + (y1 - y0) * 6 / 10;
  drawLineThick<RGB565>(target, x0, y0, x1, y0, color, thickness);
  drawLineThick<RGB565>(target, x1, y0, x1, y1, color, thickness);
  drawLineThick<RGB565>(target, x1, y1, x0, y1, color, thickness);
  drawLineThick<RGB565>(target, x0, y1, x0, y0, color, thickness);
  drawLineThick<RGB565>(target, x0, y0, xm, ym, color, thickness);  // the flap
  drawLineThick<RGB565>(target, xm, ym, x1, y0, color, thickness);
}

/// Paper plane: a sent mail (an IconPainter)
inline void drawSent(tinygpu::ISurface<RGB565>& target, const Bounds& r, RGB565 color, uint8_t thickness) {
  const int32_t ax = r.x + 2, ay = r.y + r.h * 45 / 100;               // left point
  const int32_t bx = r.x + r.w - 2, by = r.y + r.h * 10 / 100;         // top right tip
  const int32_t cx = r.x + r.w * 45 / 100, cy = r.y + r.h - 3;         // bottom point
  const int32_t dx = cx, dy = r.y + r.h * 60 / 100;                    // the fold
  drawLineThick<RGB565>(target, ax, ay, bx, by, color, thickness);
  drawLineThick<RGB565>(target, bx, by, cx, cy, color, thickness);
  drawLineThick<RGB565>(target, cx, cy, ax, ay, color, thickness);
  drawLineThick<RGB565>(target, bx, by, dx, dy, color, thickness);
}

/// Envelope with a paperclip at the top right: a mail with attachments (an IconPainter)
inline void drawMailWithAttachment(tinygpu::ISurface<RGB565>& target, const Bounds& r, RGB565 color,
                                   uint8_t thickness) {
  // the envelope in the lower left part
  drawMail(target, Bounds(r.x, r.y + r.h * 2 / 5, r.w * 3 / 4, r.h * 3 / 5), color, thickness);

  // the paperclip in the upper right part: an outer loop and an inner loop, as one wire
  const int32_t x0 = r.x + r.w - 10;  // outer left
  const int32_t x1 = r.x + r.w - 2;   // outer right
  const int32_t top = r.y;
  const int32_t bottom = r.y + r.h * 3 / 4;
  const int32_t ix0 = x0 + 3;         // inner left
  const int32_t ix1 = x1 - 3;         // inner right
  const int32_t iTop = top + 6;       // inner top
  // outer loop: left side, rounded top, right side, down to the bottom
  drawLineThick<RGB565>(target, x0, bottom - 3, x0, top + 4, color, thickness);
  drawLineThick<RGB565>(target, x0, top + 4, x0 + 4, top, color, thickness);
  drawLineThick<RGB565>(target, x0 + 4, top, x1 - 4, top, color, thickness);
  drawLineThick<RGB565>(target, x1 - 4, top, x1, top + 4, color, thickness);
  drawLineThick<RGB565>(target, x1, top + 4, x1, bottom - 3, color, thickness);
  drawLineThick<RGB565>(target, x1, bottom - 3, x1 - 3, bottom, color, thickness);
  // inner loop: from the bottom of the right side up, over, and down the left side
  drawLineThick<RGB565>(target, ix1, bottom, ix1, iTop + 3, color, thickness);
  drawLineThick<RGB565>(target, ix1, iTop + 3, ix0 + 1, iTop, color, thickness);
  drawLineThick<RGB565>(target, ix0, iTop, ix0, bottom - 4, color, thickness);
}

class TinyEMailGUI {
 public:
  /// Settings. The strings are not copied: they must stay valid while the GUI runs.
  struct Config {
    const char* mailAddress;   ///< account used by Fetch until the user signs in
    const char* mailPassword;  ///< use an App Password with Gmail
    int sdChipSelect;          ///< ESP32: chip select of the SD card
    const char* diskImage;     ///< desktop: disk image file that holds the mails
  };

  /// The GUI draws on app's screen and keeps the mails on its own storage: the
  /// SD card on the board, a disk image file on the desktop. Construct it before
  /// app.begin(), call begin() after it.
  TinyEMailGUI(App<RGB565>& app, const Config& config)
      : app_(app),
        config_(config),
#if defined(ESP32)
        storage_(config.sdChipSelect),
#else
        storage_(config.diskImage, kDiskSectors),
#endif
        menuButton_(Bounds(240 - kBarH, 4, kBarH - 8, kBarH - 8), drawMenu<RGB565>),
        menu_(Bounds(90, kBarH, 150, 6 * 40)),
        fetchItem_(menu_.itemRect(0), "Fetch"),
        composeItem_(menu_.itemRect(1), "New mail"),
        inboxItem_(menu_.itemRect(2), "> Inbox"),
        sentItem_(menu_.itemRect(3), "  Sent"),
        setupItem_(menu_.itemRect(4), "Setup"),
        exitItem_(menu_.itemRect(5), "Exit"),
        listView_(Bounds(0, kBarH + 8, 240, 320 - kBarH - 8)),  // a gap below the app bar
        mailView_(Bounds(0, kBarH, 240, 320 - kBarH)),
        backButton_(Bounds(4, 4, kBarH - 8, kBarH - 8), drawBackArrow),
        loginError_(Bounds(12, kBarH + 8, 216, 32), "", TypographyRole::kLabel),
        addressField_(Bounds(12, 100, 216, 40), "Address", "me@gmail.com"),
        passwordField_(Bounds(12, 146, 216, 40), "Password", "app password"),
        signInButton_(Bounds(12, 194, 104, 36), "Sign in"),
        cancelButton_(Bounds(124, 194, 104, 36), "Cancel"),
        keyboard_(Bounds(0, 232, 240, 88)),
        wifiPage_(keyboard_, app_.screen()),
        userAddress_(config.mailAddress),
        userPassword_(config.mailPassword),
        darkMode_(false),
        setupDialog_(Bounds(4, 4, 232, 312), "Setup", ""),
        darkLabel_(Bounds(16, 56, 150, 20), "Dark mode", TypographyRole::kBody),
        darkSwitch_(Bounds(0, 0, 52, 32), false),
        setupSaveButton_(Bounds(16, 200, 96, 36), "Save"),
        setupCancelButton_(Bounds(124, 200, 96, 36), "Cancel") {
    bodyWidget_ = &mailBody_;
  }

  TinyEMailGUI(const TinyEMailGUI&) = delete;
  TinyEMailGUI& operator=(const TinyEMailGUI&) = delete;

  /// Opens the store and adds the widgets to the screen. Returns false if the SD card is missing.
  bool begin() {
    bool ok = store_.begin(storage_);
    loadSetup();

#if defined(ESP32)
    wifi_.begin();  // connects with the stored network
#else
    wifi_.setConnected("Desktop");  // the desktop uses the network of the computer
#endif
    wifiPage_.setWifi(&wifi_);
    wifiPage_.bounds = Bounds(0, kBarH, 240, 320 - kBarH);
    wifiPage_.onDone = [this]() { showList(); };
    app_.screen().addWidget(wifiPage_);
    wifiPage_.build();
    wifiPage_.visible = false;

    // app bar: menu button on the right, its popover has the Fetch, Setup and Exit actions
    appBar_.trailing = &menuButton_;
    menuButton_.onClick = [this]() { showMenu(); };
    fetchItem_.onClick = [this]() {
      hideMenu();
      fetchFromMenu();
    };
    setupItem_.onClick = [this]() {
      hideMenu();
      showSetup();
    };
    exitItem_.onClick = []() { exitApp(); };
    inboxItem_.onClick = [this]() {
      hideMenu();
      setFilter(Filter::kInbox);
    };
    sentItem_.onClick = [this]() {
      hideMenu();
      setFilter(Filter::kSent);
    };
    composeItem_.onClick = [this]() {
      hideMenu();
      showCompose();
    };
    menu_.addItem(fetchItem_);
    menu_.addItem(composeItem_);
    menu_.addItem(inboxItem_);
    menu_.addItem(sentItem_);
    menu_.addItem(setupItem_);
    menu_.addItem(exitItem_);
    menu_.onOutsideTap = [this]() { hideMenu(); };

    setupList();
    setupMail();
    setupLogin();
    setupSetup();

    app_.screen().addFixedWidget(appBar_);
    app_.screen().addWidget(listView_);
    app_.screen().addWidget(mailView_);
    app_.screen().addFixedWidget(loginError_);
    app_.screen().addFixedWidget(addressField_);
    app_.screen().addFixedWidget(passwordField_);
    app_.screen().addFixedWidget(signInButton_);
    app_.screen().addFixedWidget(cancelButton_);
    app_.screen().addFixedWidget(hostField_);
    app_.screen().addFixedWidget(portField_);
    app_.screen().addFixedWidget(smtpHostField_);
    app_.screen().addFixedWidget(smtpPortField_);
    app_.screen().addFixedWidget(toField_);
    app_.screen().addFixedWidget(subjectField_);
    app_.screen().addFixedWidget(bodyField_);
    app_.screen().addFixedWidget(keyboard_);  // last: drawn on top of the fields
    applyTheme();
    showList();
    if (wifi_.state() != tinymd::WifiControl::State::kConnected) showWifi();  // no network at start
    return ok;
  }

  /// The store of the mails (e.g. to add test data before the first frame)
  TinyMailStore& store() { return store_; }

  /// Shows the list again: call it after mails were added to store()
  void refresh() { showList(); }

 private:
  // ---- layout (portrait 240x320) -------------------------------------------------
  static constexpr int32_t kBarH = 56;
  static constexpr int32_t kRowH = 56;        // a mail row: subject and sender
  static constexpr int32_t kCardW = 216;      // width of a mail card in the list (screen: 240)
  static constexpr int32_t kChipH = 20;       // an attachment badge in the mail view
  static constexpr int kPoolSize = 8;         // MailRows that are relabeled for the visible rows
  static constexpr int kMaxAttachments = 4;   // attachment rows shown per mail
  static constexpr int32_t kTextW = 228;      // mail text: leaves room for the scroll bar

  enum class FetchStatus { kOk, kNoWifi, kLoginFailed };
  enum class View { kList, kMail, kAttachment };

  App<RGB565>& app_;
  Config config_;
#if defined(ESP32)
  fatfs::ArduinoSpiIO storage_;
#else
  fatfs::FileIO storage_;
  static constexpr size_t kDiskSectors = 65536;  // 32 MB disk image
#endif
  TinyMailStore store_;

  AppBar<RGB565> appBar_{Bounds(0, 0, 240, kBarH), "Mails"};
  IconButton<RGB565> menuButton_;
  Menu<RGB565> menu_;
  ListItem<RGB565> fetchItem_;
  ListItem<RGB565> composeItem_;  // opens the compose screen
  ListItem<RGB565> inboxItem_;  // filter: received mails
  ListItem<RGB565> sentItem_;   // filter: sent mails
  ListItem<RGB565> setupItem_;
  ListItem<RGB565> exitItem_;

  Container<RGB565> listView_;
  ListItem<RGB565> rowPool_[kPoolSize];  // TinyMaterialDesign list rows: "sender: subject"
  enum class Filter { kInbox, kSent };  // set from the menu
  Filter filter_ = Filter::kInbox;
  int visible_[TinyMailStore::kMaxMails];  // store index of each row of the list
  int visibleCount_ = 0;
  bool hasAttachments_[TinyMailStore::kMaxMails] = {};  // per mail, set by refreshList()

  Container<RGB565> mailView_;
  IconButton<RGB565> backButton_;  // in the app bar, shown on the mail and attachment views
  TinyHtmlView mailHeader_;
  Chip<RGB565> attachmentChips_[kMaxAttachments];  // the attachments as tappable badges
  std::string attachmentNames_[kMaxAttachments];
  int attachmentCount_ = 0;        // chips of the attachments that are shown now
  TinyHtmlView mailBody_;          // the body of the mail, or the content of a text attachment
  TinyImageView imageView_;        // a picture attachment
  Widget<RGB565>* bodyWidget_ = nullptr;  // mailBody_ or imageView_: the widget below the header

  View view_ = View::kList;
  int currentMail_ = -1;
  int currentAttachment_ = -1;

  TextBlock<RGB565> loginError_;
  TextField<RGB565> addressField_;
  TextField<RGB565> passwordField_;
  Button<RGB565> signInButton_;
  Button<RGB565> cancelButton_;  // leaves the login screen without changes

  // the mail server (IMAP) for Fetch; kept in the setup file
  std::string imapHost_ = "imap.gmail.com";
  uint16_t imapPort_ = 993;
  std::string smtpHost_ = "smtp.gmail.com";
  uint16_t smtpPort_ = 465;

  // the compose screen: a new mail, sent over SMTP
  bool composeMode_ = false;
  TextField<RGB565> toField_{Bounds(12, 62, 216, 36), "To", "name@example.com"};
  TextField<RGB565> subjectField_{Bounds(12, 106, 216, 34), "Subject", "subject"};
  TextArea<RGB565> bodyField_{Bounds(12, 144, 216, 124), "Message", "text"};
  TextField<RGB565> hostField_{Bounds(16, 100, 150, 40), "IMAP server", "imap.gmail.com"};
  TextField<RGB565> portField_{Bounds(174, 100, 50, 40), "Port", "993"};
  TextField<RGB565> smtpHostField_{Bounds(16, 148, 150, 40), "SMTP server", "smtp.gmail.com"};
  TextField<RGB565> smtpPortField_{Bounds(174, 148, 50, 40), "Port", "465"};
  Keyboard<RGB565> keyboard_;

  // WiFi: Esp32Wifi on the board, SimulatedWifi on the desktop; the page selects the network
#if defined(ESP32)
  tinymd::Esp32Wifi wifi_;
#else
  tinymd::SimulatedWifi wifi_;
#endif
  tinymd::WifiLoginPage<RGB565> wifiPage_;
  bool wasConnected_ = false;  // the connection state at the last tick()

  // account used by Fetch: the configured one, or the one typed on the login screen
  std::string userAddress_;
  std::string userPassword_;

  // setup: the theme (saved on the SD card)
  bool darkMode_;
  Dialog<RGB565> setupDialog_;
  TextBlock<RGB565> darkLabel_;   // "Dark mode", placed lower than the dialog message
  Switch<RGB565> darkSwitch_;     // dark mode on or off
  Button<RGB565> setupSaveButton_;    // stores the servers and closes the dialog
  Button<RGB565> setupCancelButton_;  // closes the dialog without storing the servers

  // ---- screens -----------------------------------------------------------------
  // The login screen: message, address, password, Sign in and Cancel
  void setLoginVisible(bool visible) {
    loginError_.visible = visible;
    addressField_.visible = visible;
    passwordField_.visible = visible;
    signInButton_.visible = visible;
    cancelButton_.visible = visible;
    hostField_.visible = false;
    portField_.visible = false;
    smtpHostField_.visible = false;
    smtpPortField_.visible = false;
    toField_.visible = false;
    subjectField_.visible = false;
    bodyField_.visible = false;
    if (!visible) keyboard_.hide();
  }

  void showList() {
    view_ = View::kList;
    if (wifiPage_.visible) {
      wifiPage_.visible = false;
      wifiPage_.onHide();
    }
    composeMode_ = false;
    signInButton_.setLabel("Sign in");
    app_.screen().setBackgroundColor(screenColor());
    setLoginVisible(false);
    appBar_.leading = nullptr;
    appBar_.setTitle(listTitle());
    refreshList();
    listView_.visible = true;
    mailView_.visible = false;
    app_.screen().invalidate();
  }

  // The rows of the list: all mails, or only the mails with attachments
  void refreshList() {
    visibleCount_ = 0;
    for (int i = 0; i < store_.count(); i++) {
      hasAttachments_[i] = store_.attachmentCount(i) > 0;
      const bool inSent = store_.box(i) == "sent";
      if ((filter_ == Filter::kSent) == inSent) {
        visible_[visibleCount_++] = i;
      }
    }
    app_.screen().invalidate();
  }

  // "Sun, 22 Nov 2026 09:30:00 +0000" becomes "Sun, 22 Nov 2026 09:30:00" (no time zone)
  static String withoutOffset(const String& date) {
    String out = date;
    out.trim();
    const int n = out.length();
    if (n >= 6 && (out.charAt(n - 5) == '+' || out.charAt(n - 5) == '-')) {
      out = out.substring(0, n - 5);
      out.trim();
    }
    return out;
  }

  // The title of the list: the name of the selected folder in brackets
  const char* listTitle() const {
    return filter_ == Filter::kSent ? "Mails (Sent)" : "Mails (Inbox)";
  }

  // The menu marks the current filter with "> "
  void setFilter(Filter filter) {
    filter_ = filter;
    inboxItem_.setTitle(filter == Filter::kInbox ? "> Inbox" : "  Inbox");
    sentItem_.setTitle(filter == Filter::kSent ? "> Sent" : "  Sent");
    appBar_.setTitle(listTitle());
    refreshList();
  }

  void showMail(int index) {
    currentMail_ = index;
    view_ = View::kMail;
    showMailColors();
    attachmentCount_ = store_.attachmentCount(index);
    if (attachmentCount_ > kMaxAttachments) attachmentCount_ = kMaxAttachments;
    for (int k = 0; k < attachmentCount_; k++) {
      attachmentNames_[k] = store_.attachmentName(index, k).c_str();
      attachmentChips_[k] = Chip<RGB565>(Bounds(0, 0, 0, kChipH), attachmentNames_[k].c_str());
      attachmentChips_[k].onClick = [this, k]() { showAttachment(k); };
      attachmentChips_[k].setFont(chipFont());
    }
    // the sender is plain text; the body is streamed from its file
    bool plain = false;
    String path = store_.bodyPath(index, plain);
    // the labels "From:" and "Date:" are in the small font (byte 0x01 switches it)
    String header = String("\x01") + "From:" + "\x01" + " " + store_.from(index) + "\n";
    if (store_.date(index).length() > 0) {
      header += String("\x01") + "Date:" + "\x01" + " " + withoutOffset(store_.date(index)) + "\n";
    }
    layoutMail(header, path, plain);
    appBar_.leading = &backButton_;
    appBar_.setTitle(store_.subject(index).c_str());
    listView_.visible = false;
    mailView_.visible = true;
    app_.screen().invalidate();
  }

  // An attachment: a text file is shown as text, any other file shows its name and size
  void showAttachment(int k) {
    currentAttachment_ = k;
    view_ = View::kAttachment;
    showMailColors();
    String name = store_.attachmentName(currentMail_, k);
    String path = store_.attachmentPath(currentMail_, k);
    int size = 0;
    fatfs::File f = store_.fs().open(path.c_str(), FILE_READ);
    if (f) {
      size = f.available();
      f.close();
    }
    bool html = endsWith(name, ".html") || endsWith(name, ".htm");
    bool text = html || endsWith(name, ".txt") || endsWith(name, ".csv") ||
                endsWith(name, ".log") || endsWith(name, ".md") || endsWith(name, ".json") ||
                endsWith(name, ".xml");
    // the labels "Name:" and "Size:" are in the small font (byte 0x01 switches it)
    String header = String("\x01") + "Name:" + "\x01" + " " + name + "\n" + String("\x01") + "Size:" + "\x01" +
                    " " + String(size) + " bytes\n";
    attachmentCount_ = 0;  // no attachment badges in this view
    if (TinyImageView::isPicture(name)) {
      imageView_.load(store_.fs(), path);  // shows the error text if it cannot be decoded
      layoutMail(header, "", true, true);
    } else {
      if (!text) {
        header += "This file is not text. It is stored in the mail folder on the storage.\n";
      }
      layoutMail(header, text ? path : String(""), !html);
    }
    appBar_.leading = &backButton_;
    appBar_.setTitle(name.c_str());
    listView_.visible = false;
    mailView_.visible = true;
    app_.screen().invalidate();
  }

  // The text of the attachment badges: TinyFonts Lato8, the smallest font
  static tinyfonts::TinyGPUFont<RGB565>& chipFont() {
    static tinyfonts::TinyGPUFont<RGB565> font(tinyfonts::Lato8);
    return font;
  }

  // Lays out the mail view top to bottom: header, divider, body, then the
  // attachment badges (wrapped in rows). A badge is as wide as its name plus padding.
  void layoutMail(const String& header, const String& path, bool plain, bool picture = false) {
    mailHeader_.bounds = Bounds(0, kBarH, kTextW, 0);
    mailHeader_.setSource(header, store_.fs(), "", true);
    int32_t y = mailHeader_.bounds.y + mailHeader_.bounds.h;

    if (picture) {
      bodyWidget_ = &imageView_;
      imageView_.bounds = Bounds(0, y, kTextW, 0);
      imageView_.layout();  // sets the height of the picture
    } else {
      bodyWidget_ = &mailBody_;
      mailBody_.bounds = Bounds(0, y, kTextW, 0);
      mailBody_.setSource("", store_.fs(), path, plain);  // sets the height of the body
    }
    y = bodyWidget_->bounds.y + bodyWidget_->bounds.h + 12;

    const int32_t maxX = kTextW - 12;
    int32_t x = 12;
    int32_t rowY = y;
    for (int k = 0; k < attachmentCount_; k++) {
      const int32_t w = static_cast<int32_t>(chipFont().measureTextWidth(attachmentNames_[k].c_str())) + 24;
      if (x > 12 && x + w > maxX) {
        x = 12;
        rowY += kChipH + 6;
      }
      attachmentChips_[k].bounds = Bounds(x, rowY, w, kChipH);
      x += w + 6;
    }
  }

  static bool endsWith(const String& name, const char* suffix) {
    String lower = name;
    lower.toLowerCase();
    return lower.endsWith(suffix);
  }

  void showLogin(const char* message) {
    view_ = View::kList;
    signInButton_.setLabel("Sign in");
    signInButton_.bounds = Bounds(12, 194, 104, 36);
    cancelButton_.bounds = Bounds(124, 194, 104, 36);
    app_.screen().setBackgroundColor(screenColor());
    listView_.visible = false;
    mailView_.visible = false;
    appBar_.leading = nullptr;
    appBar_.setTitle("Login");
    loginError_.setText(message);
    if (addressField_.text().empty()) addressField_.setText(userAddress_.c_str());
    passwordField_.setText("");
    setLoginVisible(true);
    app_.screen().invalidate();
  }

  // The WiFi page of TinyMaterialDesign: the network is chosen and connected there
  void showWifi() {
    view_ = View::kList;
    setLoginVisible(false);
    composeMode_ = false;
    signInButton_.setLabel("Sign in");
    listView_.visible = false;
    mailView_.visible = false;
    appBar_.leading = nullptr;
    appBar_.setTitle("WiFi");
    wifiPage_.visible = true;
    wifiPage_.onShow();
    app_.screen().invalidate();
  }

public:
  // Call it in loop(): it drives the scan and the connection of the WiFi page, and shows
  // the page when a connection that was up is lost
  void tick() {
    const bool connected = wifi_.state() == tinymd::WifiControl::State::kConnected;
    if (wasConnected_ && !connected && !wifiPage_.visible) showWifi();
    wasConnected_ = connected;
    if (wifiPage_.visible && wifiPage_.tick(millis())) app_.screen().invalidate();
  }

private:

  void showMenu() { app_.screen().presentDialog(menu_); }
  void hideMenu() { app_.screen().dismissDialog(); }

  // ---- setup -------------------------------------------------------------------
  static constexpr const char* kSetupFile = "mails/setup.txt";

  // The setup dialog: dark mode, the IMAP and SMTP servers, and the keyboard for the fields
  void showSetup() {
    hostField_.setText(imapHost_.c_str());
    portField_.setText(String(imapPort_).c_str());
    smtpHostField_.setText(smtpHost_.c_str());
    smtpPortField_.setText(String(smtpPort_).c_str());
    setServerFieldsVisible(true);
    app_.screen().presentDialog(setupDialog_);
  }

  void setServerFieldsVisible(bool visible) {
    hostField_.visible = visible;
    portField_.visible = visible;
    smtpHostField_.visible = visible;
    smtpPortField_.visible = visible;
    if (!visible) keyboard_.hide();
  }

  void setDarkMode(bool dark) {
    darkMode_ = dark;
    applyTheme();
    saveSetup();
  }

  // The theme of the screen; the app bar takes the primary colour of the theme
  void applyTheme() {
    const MaterialTheme<RGB565> theme = darkMode_ ? defaultDarkTheme<RGB565>() : defaultTheme<RGB565>();
    app_.screen().setTheme(theme);
    applyFieldColors();
    app_.screen().setBackgroundColor(screenColor());
    if (darkMode_) {
      appBar_.setColorOverride(RGB565::fromRGB(0, 0, 0), theme.colors.onSurface);  // black title bar
    } else {
      appBar_.setColorOverride(theme.colors.primary, theme.colors.onPrimary);
    }
  }

  // A mail is shown on white (in dark mode on the dark surface, so the light text stays readable)
  void showMailColors() {
    const RGB565 color = darkMode_ ? app_.screen().theme().colors.surface : RGB565::fromRGB(255, 255, 255);
    app_.screen().setBackgroundColor(color);
    mailHeader_.setBackgroundColor(color);
    mailBody_.setBackgroundColor(color);
  }

  // The entry fields: black text and grey placeholders in light mode; the theme's
  // colours in dark mode, where black text would not be readable
  void applyFieldColors() {
    const MaterialTheme<RGB565>& t = app_.screen().theme();
    const RGB565 text = darkMode_ ? t.colors.onSurface : RGB565::fromRGB(0, 0, 0);
    const RGB565 hint = darkMode_ ? t.colors.onSurfaceVariant : RGB565::fromRGB(158, 158, 158);
    addressField_.setColors(text, hint);
    passwordField_.setColors(text, hint);
    toField_.setColors(text, hint);
    subjectField_.setColors(text, hint);
    bodyField_.setColors(text, hint);
    hostField_.setColors(text, hint);
    portField_.setColors(text, hint);
    smtpHostField_.setColors(text, hint);
    smtpPortField_.setColors(text, hint);
  }

  // The grey of the screen behind the cards: light grey, or dark grey in dark mode
  RGB565 screenColor() const {
    return darkMode_ ? RGB565::fromRGB(48, 48, 48) : RGB565::fromRGB(224, 224, 224);
  }

  void setupSetup() {
    darkSwitch_.setValue(darkMode_);
    darkSwitch_.onChange = [this](bool dark) { setDarkMode(dark); };
    setupSaveButton_.onClick = [this]() {
      saveServerFields();
      app_.screen().dismissDialog();
    };
    setupCancelButton_.onClick = [this]() {
      setServerFieldsVisible(false);
      app_.screen().dismissDialog();
    };
    setupDialog_.addAction(darkLabel_);
    setupDialog_.addAction(darkSwitch_);
    setupDialog_.addAction(hostField_);
    setupDialog_.addAction(portField_);
    setupDialog_.addAction(smtpHostField_);
    setupDialog_.addAction(smtpPortField_);
    setupDialog_.addAction(setupSaveButton_);
    setupDialog_.addAction(setupCancelButton_);
    setupDialog_.addAction(keyboard_);  // drawn inside the dialog, so it is above the card
    // the switch sits on the "Dark mode" line, at the right of the card
    darkSwitch_.bounds = Bounds(170, 52, 52, 32);  // centred on the label
  }

  // The setup file has one setting per line: dark=1 or dark=0, address=..., password=...
  // Without a line for the account, the account of the configuration is used.
  void loadSetup() {
    fatfs::File f = store_.fs().open(kSetupFile, FILE_READ);
    if (!f) return;
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line == "dark=1") {
        darkMode_ = true;
      } else if (line == "dark=0") {
        darkMode_ = false;
      } else if (line.startsWith("address=")) {
        userAddress_ = line.substring(strlen("address=")).c_str();
      } else if (line.startsWith("password=")) {
        userPassword_ = line.substring(strlen("password=")).c_str();
      } else if (line.startsWith("imap=")) {
        imapHost_ = line.substring(strlen("imap=")).c_str();
      } else if (line.startsWith("port=")) {
        imapPort_ = static_cast<uint16_t>(atoi(line.substring(strlen("port=")).c_str()));
      } else if (line.startsWith("smtp=")) {
        smtpHost_ = line.substring(strlen("smtp=")).c_str();
      } else if (line.startsWith("smtpport=")) {
        smtpPort_ = static_cast<uint16_t>(atoi(line.substring(strlen("smtpport=")).c_str()));
      }
    }
    f.close();
  }

  void saveSetup() {
    String text = darkMode_ ? "dark=1\n" : "dark=0\n";
    text += "address=" + String(userAddress_.c_str()) + "\n";
    text += "password=" + String(userPassword_.c_str()) + "\n";
    text += "imap=" + String(imapHost_.c_str()) + "\n";
    text += "port=" + String(imapPort_) + "\n";
    text += "smtp=" + String(smtpHost_.c_str()) + "\n";
    text += "smtpport=" + String(smtpPort_) + "\n";
    store_.writeText(kSetupFile, text);
  }

  static uint16_t portOf(const std::string& text, uint16_t fallback) {
    const int port = atoi(text.c_str());
    return port > 0 ? static_cast<uint16_t>(port) : fallback;
  }

  // Stores the servers typed in the setup dialog and writes the setup file
  void saveServerFields() {
    keyboard_.hide();
    imapHost_ = hostField_.text();
    imapPort_ = portOf(portField_.text(), 993);
    smtpHost_ = smtpHostField_.text();
    smtpPort_ = portOf(smtpPortField_.text(), 465);
    setServerFieldsVisible(false);
    saveSetup();
  }

  // Sign in, or send the mail on the compose screen (the Return key of the password field does the same)
  void submit() {
    if (composeMode_) {
      sendMail();
    } else {
      signIn();
    }
  }

  // The compose screen: To, Subject and the message, with Send and Cancel
  void showCompose() {
    listView_.visible = false;
    mailView_.visible = false;
    view_ = View::kList;
    composeMode_ = true;
    setLoginVisible(false);
    toField_.visible = true;
    subjectField_.visible = true;
    bodyField_.visible = true;
    toField_.setText("");
    subjectField_.setText("");
    bodyField_.setText("");
    signInButton_.setLabel("Send");
    signInButton_.visible = true;
    cancelButton_.visible = true;
    signInButton_.bounds = Bounds(12, 276, 104, 32);
    cancelButton_.bounds = Bounds(124, 276, 104, 32);
    appBar_.setTitle("New mail");
    appBar_.leading = &backButton_;
    app_.screen().invalidate();
  }

  // "a@x.com, b@y.com; c@z.com" becomes three addresses: the separators are ',' and ';'
  static std::vector<String> splitAddresses(const std::string& list) {
    std::vector<String> out;
    String current;
    for (size_t i = 0; i <= list.size(); i++) {
      const char c = i < list.size() ? list[i] : ',';
      if (c == ',' || c == ';') {
        current.trim();
        if (current.length() > 0) out.push_back(current);
        current = "";
      } else {
        current += c;
      }
    }
    return out;
  }

  // Sends the mail over SMTP and keeps a copy in the Sent folder. The title shows what went wrong.
  void sendMail() {
    keyboard_.hide();
    const std::string to = toField_.text();
    const std::string subject = subjectField_.text();
    const std::string body = bodyField_.text();
    const std::vector<String> recipients = splitAddresses(to);
    if (recipients.empty()) {
      appBar_.setTitle("Enter a recipient");
      return;
    }
    if (!connectWifi()) return;  // the WiFi page is shown; send again once connected
    WiFiClientSecure secure;
    secure.setInsecure();  // certificate checking is not implemented yet
    TinyEMail mail(secure);
    mail.setSmtpServer(smtpHost_.c_str(), smtpPort_);
    if (!mail.login(userAddress_.c_str(), userPassword_.c_str())) {
      appBar_.setTitle("Login failed: not sent");
      return;
    }
    // one mail to all the recipients; the body is its first (text) attachment
    TinyTextStream bodyText(body.c_str());
    TinyEMail::Attachment text;
    text.contentType = "text/plain";
    text.stream = &bodyText;
    TinyEMail::Mail outgoing;
    for (const String& address : recipients) {
      outgoing.to.push_back(address);
    }
    outgoing.subject = subject.c_str();
    outgoing.attachments.push_back(text);
    if (!mail.send(outgoing)) {
      appBar_.setTitle("Not sent");
      return;
    }
    const String uid = String(static_cast<unsigned long>(millis()));
    store_.add(uid, String("Me <") + userAddress_.c_str() + ">", subject.c_str(), body.c_str(), "", "", "", "",
               "sent");
    composeMode_ = false;
    signInButton_.setLabel("Sign in");
    setFilter(Filter::kSent);
    showList();
  }

  // Desktop: end the program. The board has nothing to return to, so it restarts.
  static void exitApp() {
#if defined(ESP32)
    ESP.restart();
#else
    exit(0);
#endif
  }

  // ---- fetch -------------------------------------------------------------------
  // The desktop emulator reports WL_CONNECTED at once and uses the network of
  // the computer, so the same code runs on both.
  bool connectWifi() {
    if (wifi_.state() == tinymd::WifiControl::State::kConnected) return true;
    showWifi();
    return false;
  }

  FetchStatus fetchMail(const char* address, const char* password) {
    if (!connectWifi()) return FetchStatus::kNoWifi;
    WiFiClientSecure secure;
    secure.setInsecure();  // certificate checking is not implemented yet
    TinyEMail mail(secure);
    mail.setImapServer(imapHost_.c_str(), imapPort_);
    mail.setSmtpServer(smtpHost_.c_str(), smtpPort_);
    if (!mail.login(address, password)) return FetchStatus::kLoginFailed;
    // unread mails only; each one is stored and then marked as read
    mail.receive([this](const TinyEMail::Mail& m) {
      if (store_.save(m)) {
        m.markAsRead();
      }
      return true;
    });
    return FetchStatus::kOk;
  }

  // Shows the list after a successful fetch, otherwise the login screen with the reason
  void handleFetch(FetchStatus status) {
    switch (status) {
      case FetchStatus::kOk:
        showList();
        break;
      case FetchStatus::kNoWifi:
        showWifi();
        break;
      case FetchStatus::kLoginFailed:
        showLogin("Login failed: check the address and the app password");
        break;
    }
  }

  void fetchFromMenu() { handleFetch(fetchMail(userAddress_.c_str(), userPassword_.c_str())); }

  void signIn() {
    keyboard_.hide();
    userAddress_ = addressField_.text();
    userPassword_ = passwordField_.text();
    const FetchStatus status = fetchMail(userAddress_.c_str(), userPassword_.c_str());
    if (status == FetchStatus::kOk) {
      saveSetup();  // the account that worked is kept for the next start
    }
    handleFetch(status);
  }

  // ---- widget setup --------------------------------------------------------------
  void setupList() {
    listView_.setUniformItemHeight(kRowH);
    listView_.setChildProvider(
        [this]() { return visibleCount_; },
        [this](int index) -> Widget<RGB565>& {
          const int mail = visible_[index];  // the mail of this row (see refreshList())
          ListItem<RGB565>& row = rowPool_[static_cast<size_t>(index) % kPoolSize];
          // each mail is a card, narrower than the screen and centred
          row.bounds = Bounds(listView_.bounds.x + (listView_.bounds.w - kCardW) / 2,
                              listView_.bounds.y + index * kRowH, kCardW, kRowH);
          const MaterialTheme<RGB565>& theme = app_.screen().theme();
          row.setBackgroundColor(theme.colors.surface);  // the cards are white (light mode)
          row.setDividerColor(screenColor());            // the gap between two cards: the grey
          row.setTitle(store_.subject(mail).c_str());
          row.setSubtitle(store_.from(mail).c_str());
          if (store_.box(mail) == "sent") {
            row.setIcon(drawSent);
          } else {
            row.setIcon(hasAttachments_[mail] ? drawMailWithAttachment : drawMail);
          }
          row.setDivider(true);
          row.setDividerInset(false);  // full width of the card
          row.setTypographyRole(TypographyRole::kTitle);  // the subject: larger than the address
          row.onClick = [this, mail]() { showMail(mail); };
          return row;
        });
  }

  // Mail view children: header, body, attachment badges
  void setupMail() {
    mailView_.setChildProvider(
        [this]() { return 2 + attachmentCount_; },
        [this](int index) -> Widget<RGB565>& {
          if (index == 0) return mailHeader_;
          if (index == 1) return *bodyWidget_;
          return attachmentChips_[index - 2];
        });
    // back: from an attachment to its mail, from a mail to the list
    backButton_.onClick = [this]() {
      if (view_ == View::kAttachment) {
        showMail(currentMail_);
      } else {
        showList();
      }
    };
    mailView_.visible = false;
  }

  void setupLogin() {
    setLoginVisible(false);
    passwordField_.setPasswordMode(true);
    addressField_.onSubmit = [this]() { passwordField_.focus(); };
    passwordField_.onSubmit = [this]() { submit(); };
    signInButton_.onClick = [this]() { submit(); };
    cancelButton_.onClick = [this]() {
      keyboard_.hide();
      composeMode_ = false;
      signInButton_.setLabel("Sign in");
      showList();
    };
    keyboard_.manage(addressField_);
    keyboard_.manage(passwordField_);
    keyboard_.manage(hostField_);
    keyboard_.manage(portField_);
    keyboard_.manage(smtpHostField_);
    keyboard_.manage(smtpPortField_);
    keyboard_.manage(toField_);
    keyboard_.manage(subjectField_);
    keyboard_.manage(bodyField_);
  }
};
