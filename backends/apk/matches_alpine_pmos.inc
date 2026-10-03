/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jane Rachinger <jane400@postmarketos.org>
 */

// ifndef for ide syntax highlighting
#ifndef MATCH
#define MATCH(...)
#define PREFIX(...)
#define SUFFIX(...)
#endif

MATCH(postmarketos, PK_GROUP_ENUM_VENDOR, PREFIX("postmarketos"), SUFFIX())
MATCH(lang, PK_GROUP_ENUM_LOCALIZATION, PREFIX(), SUFFIX("-lang"))
MATCH(dev, PK_GROUP_ENUM_PROGRAMMING, PREFIX(),
      SUFFIX("-dev", "-dbg", "-static"))
MATCH(system, PK_GROUP_ENUM_SYSTEM, PREFIX(),
      SUFFIX("-bash-completion", "-zsh-completion", "-openrc", "-systemd",
             "-udev", "-pyc", "-libs"))
MATCH(firewall, PK_GROUP_ENUM_SECURITY, PREFIX(), SUFFIX("-nftrules"))
MATCH(fonts, PK_GROUP_ENUM_FONTS, PREFIX(), SUFFIX("-font"))
