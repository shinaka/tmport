#pragma once
// The UI area's tools for ggport-cli (gg/app/cli_dispatch.h): UI screens composed as the original
// draws them, from the user's files, written as 320 x 200 PNGs (VRAM through the shadow palette:
// a settled frame, every fade done).
//
//   --gg-screen NAME OUT.png [--data DIR] [options]
//     NAME:
//       title                      the title (attract step 1); --phase 1..3 (default 3), --frame N
//                                  (the Attract_PlayAnim frames run in that phase, default 1; each
//                                  phase restarts the two animations, earlier phases run 37 / 110
//                                  frames)
//       after-title, after-story, before-prompt
//                                  the animated attract steps 2, 9, 10 after --frame N frames (they
//                                  stop where the original's watched channel ends)
//       story1 .. story6           the story pages (steps 3..8)
//       prompt                     the sign-in prompt (step 11)
//       signin                     the sign-in screen after SignIn_Run's entry (an empty roster gets
//                                  New Player's empty entry); the roster (read only) from --players
//                                  FILE, else the game folder's PLAYERS.DAT, else (or with
//                                  --new-file) a newly created one; --new presses New Player first,
//                                  --type TEXT types its characters, then --codes C,C,.. passes input
//                                  codes (hex: 700 a click on the list at the --cursor position, 148
//                                  Up, 150 Down, 8 Backspace, 702 Cancel, 12 Ctrl+R answered No)
//       name                       the same (the name entry: give --type, and --new when the roster
//                                  has players)
//       chimps                     the name entry, Start Game, then the With/Without Chimps question
//       hint:ID                    the New Player Hint box of hint ID (0x1043..0x1055)
//       yesno:ID                   Dlg_YesNoRes(ID) (0x1064..0x106C) under the normal menu bar
//                                  (--no-bar: without)
//       message:ID                 Dlg_MessageRes(ID) (0x1068, 0x1069, 0x106D, 0x106E, 0x106F)
//       menubar                    the menu bar (Menu_DrawBar(-1))
//       menu:help|file|options[:ROW]  the bar with that title highlighted and its drop-down, row ROW
//                                  highlighted (default 0)
//       help:ID[:PAGE]             a help topic's pager page (0x105D..0x1062) under the normal bar
//                                  (--no-bar: without); earlier pages are drawn first, as the pager
//       textview:ID[:LINE]         the puzzles' text viewer showing text ID, scrolled down LINE lines
//     Screens without a background of their own are drawn over colour 0 (--fill N: colour N). The
//     shadow palette starts as the game's after Game_Init (the base palette DS:024e and the HUD
//     bundle 0x1058's range); --pal ID or ID/MEMBER stores more palette resources before drawing.
//     Other options: --cursor X,Y (the arrow at pointer X,Y, drawn by a last Dirty_Flush; the
//     forced arrow under a dialog or a menu), --frame-dir DIR (every frame of an animated attract
//     step as frame_NNNN.png), --scale N.
//     Prints "rect NAME x y w h" for the parts the UI drew (boxes, the bar, the list) and "note ..."
//     lines (scroll positions, the frame a watched channel ended, modal boxes the sign-in skipped).
//   --gg-ui-info [--data DIR]      the menus, the layouts of the prebuilt boxes and hints, and the help
//                                  topics' pages, as text.
//
// Exit codes: 0 done, 1 data error, 2 usage. Outputs are refused inside the game folder.

#include <string>
#include <vector>

namespace tmport::gg {

// `args` without argv[0]; false when none of them is a UI command.
bool uiCli(const std::vector<std::string>& args, int& exitCode);

}  // namespace tmport::gg
