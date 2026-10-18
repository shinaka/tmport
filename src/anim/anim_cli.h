#pragma once
// Headless DAM animation tools of tmport (area anim). No window, no audio.
//
//   --render-dam <scriptId> <bndlId> <frameBase> <tick> <out.png>
//       Runs DAM script <scriptId> for <tick> ticks (tick 1 = the first frame drawn) exactly like
//       Anim_PlayModal / Title_WaitStep do (erase, step, draw layer 0, update rects, dirty
//       flush) over a background and writes the presented 512x384 game area. Frames come from
//       BNDL <bndlId> (0 = none) loaded at <frameBase>: a bundle whose member 0 is an OTHR is an
//       animation bundle (Anim_LoadBundle: members 1.. are frames), otherwise every member is a
//       frame (treasure-hall picture bundles 330+t). Ticking stops when the script ends, as
//       the modal player does.
//       Options: --bg <pmapId> [--bg-at <x> <y>] (opaque, default (0,20)); --bg-room <n> (room
//       n's background PMAP at (0,20)); --origin <x> <y> (channel origin, default (0,20));
//       --slot <n> <pmapId> (repeatable); --treasure <t> (slot 80 = BNDL 330+t member 0);
//       --load <bndlId> <frameBase> (more bundles, repeatable); --also <scriptId> <frameBase>
//       <x> <y> (another channel, started before the main one, repeatable); --pre <n> (ticks
//       of the --also channels before the main script starts);
//       --frames-only; --play-clip (Gfx_SetPlayClip(0)); --stop-at <tick> (Enter before that
//       tick); --sheet <first> <last> [--cols <n>] (contact sheet of ticks first..last);
//       --crop <x> <y> <w> <h>; --buffer (frame buffer rows instead of the presented window);
//       --fps <n> (only for the printed times; default 10 = the 100 ms room/title tick);
//       --fb-dump <png>; --data <path>; --16; --mods <dir> (repeatable, all three commands;
//       cli/tool_mods.h).
//   --dam-info <scriptId>   disassembly and a tick-by-tick simulation of one script
//   --dam-check             parses every DAM script in TMT.DLL (OTHR 20000-20399) and prints
//                           per-script statistics; exit code 1 if any fails

#include <optional>
#include <string>

namespace tmport::anim {

// Runs an anim tool if argv contains one of its commands and returns the process exit code;
// otherwise nullopt (the regular parser handles argv).
std::optional<int> runAnimCommand(int argc, char** argv);
std::string animUsage();

}  // namespace tmport::anim
