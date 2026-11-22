/**
 * @file TestData.h
 * @brief Test mails for the desktop: they show up in the list when the store is
 * empty at start. Not used on the board.
 */
#pragma once

#include <TinyEMailGUI.h>

#include "TestImages.h"

inline void seedTestMails(TinyMailStore& store) {
  if (store.count() > 0) return;  // keep the mails that are already stored

  // HTML with a heading, a list, entities and a comment
  store.add("1001", "Team <team@example.com>", "Welcome to TinyEMail",
            "Welcome to TinyEMail. This is the plain text part.",
            R"HTML(<html><head><style>p { color: red; }</style><title>Welcome</title></head>
<body>
<h1>Welcome to TinyEMail</h1>
<p>This mail is <b>rendered</b> with <i>TinyFonts</i>, streamed from the disk.</p>
<h2>Features</h2>
<ul>
<li>HTML and text bodies</li>
<li>Attachments saved as files</li>
<li>Dark mode in the setup</li>
</ul>
<p>Entities: &lt;tag&gt; &amp; &copy; 2026 &mdash; done &#8364;</p>
<!-- this comment is not shown -->
<p>Long words wrap automatically: supercalifragilisticexpialidocious-and-more-text-to-wrap-the-line.</p>
</body></html>)HTML");

  // plain text with a CSV attachment
  store.add("1002", "Alice <alice@example.com>", "Quarterly report",
            "Hi,\n\nThe quarterly report is attached as CSV.\n\nBest,\nAlice\n",
            "", "report.csv", "month,sales\njan,120\nfeb,135\nmar,160\n");

  // long plain text: the lines wrap in the view
  store.add("1003", "Bob <bob@example.com>", "Lunch on Friday?",
            "Hi,\n\nare you free for lunch on Friday? I know a small place near the station "
            "that serves very good soup and the bread is fresh every morning.\n\n"
            "If Friday does not work, Monday is fine too. Just tell me which day suits you best "
            "and I will book a table.\n\nCheers,\nBob\n");

  // a second HTML mail with a table-like list
  store.add("1004", "Support <support@example.com>", "Your order has shipped",
            "Your order has shipped and arrives in two days.",
            R"HTML(<html><body>
<h2>Your order has shipped</h2>
<p>Order <b>#4711</b> left our warehouse today.</p>
<ol>
<li>1 x Breadboard</li>
<li>2 x Jumper wires</li>
<li>1 x ESP32-S3 board</li>
</ol>
<p>Thank you for shopping with us!</p>
</body></html>)HTML");

  // pictures in the four supported formats: JPEG (streamed), PNG, GIF, BMP
  store.add("1005", "Photos <photos@example.com>", "Photos from the trip",
            "Here are the pictures from our trip.\n", "", "", "",
            "Sun, 22 Nov 2026 09:30:00 +0000");
  store.attachFile("1005", "photo.jpg", photo_jpg, photo_jpg_size);
  store.attachFile("1005", "logo.png", logo_png, logo_png_size);
  store.attachFile("1005", "banner.gif", banner_gif, banner_gif_size);
  store.attachFile("1005", "icon.bmp", icon_bmp, icon_bmp_size);

  // a sent mail: it is listed under Sent
  store.add("1006", "Me <me@example.com>", "Re: Quarterly report",
            "Thanks Alice, the numbers look good.\n", "", "", "",
            "Mon, 23 Nov 2026 08:15:00 +0000", "sent");
}
