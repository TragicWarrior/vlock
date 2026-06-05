/* info_box.h -- shared "press a key to wake" overlay for vlock screen savers
 *
 * This program is copyright (C) 2007 Frank Benkstein, and is free
 * software which is freely distributable under the terms of the
 * GNU General Public License version 2, included as the file COPYING in this
 * distribution.  It is NOT public domain software, and any
 * redistribution not permitted by the GNU General Public License is
 * expressly forbidden without prior written permission from
 * the author.
 *
 */

#pragma once

/* Draw the info box over the current ncurses frame, if enabled.  Enabled and
 * configured through the environment: VLOCK_INFO_BOX is the move interval in
 * seconds (0 or unset disables it) and VLOCK_WAKE_KEY names the wake key shown
 * in the message.  The box jumps to a new fully-on-screen random position
 * every interval.  Call once per animation frame, after the saver has drawn
 * its own content and before the frame is refreshed.  ncurses must already be
 * initialized. */
void info_box_draw(void);
