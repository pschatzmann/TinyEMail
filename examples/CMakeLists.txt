# Arduino sketches, built for the desktop via Arduino-Emulator (Serial reads
# from stdin, WiFi and WiFiClientSecure use the network of the computer).
if(TINYEMAIL_BUILD_EXAMPLES)
  arduino_sketch(tinyemail_send Send/Send.ino LIBRARIES TinyEMail)
  arduino_sketch(tinyemail_receive Receive/Receive.ino LIBRARIES TinyEMail)
endif()

if(TINYEMAIL_BUILD_GUI)
  # Mail client with TinyMaterialDesign, in an SDL2 window (240x320).
  arduino_sketch(tinyemail_gui MailGUI/MailGUI.ino
                 LIBRARIES TinyEMail TinyMaterialDesign TinyFonts::TinyFonts arduino_fatfs
                           TinyJPEG::TinyJPEG SDL2::SDL2)
  # ARDUINO is defined by the Arduino IDE; TinyMaterialDesign selects its
  # Arduino code paths (millis(), namespaces) on it.
  target_compile_definitions(tinyemail_gui PRIVATE ARDUINO=10819
                             TINYGPU_AUTO_NAMESPACE TINYMD_AUTO_NAMESPACE)
  set_target_properties(tinyemail_gui PROPERTIES CXX_STANDARD 17)
endif()
