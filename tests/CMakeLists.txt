add_executable(test_tinyemail test_tinyemail.cpp)
target_link_libraries(test_tinyemail PRIVATE TinyEMail arduino_emulator)
target_compile_options(test_tinyemail PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME tinyemail COMMAND test_tinyemail)

add_executable(test_tinyimap test_tinyimap.cpp)
target_link_libraries(test_tinyimap PRIVATE TinyEMail arduino_emulator)
target_compile_options(test_tinyimap PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME tinyimap COMMAND test_tinyimap)

add_executable(test_tinymime test_tinymime.cpp)
target_link_libraries(test_tinymime PRIVATE TinyEMail arduino_emulator)
target_compile_options(test_tinymime PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME tinymime COMMAND test_tinymime)

add_executable(test_tinyvector test_tinyvector.cpp)
target_link_libraries(test_tinyvector PRIVATE TinyEMail arduino_emulator)
target_compile_options(test_tinyvector PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME tinyvector COMMAND test_tinyvector)

add_executable(test_robustness test_robustness.cpp)
target_link_libraries(test_robustness PRIVATE TinyEMail arduino_emulator)
target_compile_options(test_robustness PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME robustness COMMAND test_robustness)

# Integration test against a real mail server (see README, "Integration test").
option(TINYEMAIL_INTEGRATION "Build the integration test against a mail server" OFF)
if(TINYEMAIL_INTEGRATION)
  add_executable(test_greenmail integration/test_greenmail.cpp)
  target_link_libraries(test_greenmail PRIVATE TinyEMail arduino_emulator)
  target_compile_options(test_greenmail PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME greenmail COMMAND test_greenmail)
  if(TINYEMAIL_TLS)
    add_executable(test_greenmail_tls integration/test_greenmail.cpp)
    target_compile_definitions(test_greenmail_tls PRIVATE TINYEMAIL_GREENMAIL_TLS=1)
    target_link_libraries(test_greenmail_tls PRIVATE TinyEMail arduino_emulator)
    target_compile_options(test_greenmail_tls PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME greenmail_tls COMMAND test_greenmail_tls)
  endif()
endif()
