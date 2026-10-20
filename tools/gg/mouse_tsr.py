"""MOUSEKEY: a tiny DOS TSR that turns posted keys into INT 33h mouse events (G&G reference captures).

Why: Gizmos & Gadgets cannot be played without the mouse, and DOSBox-X cannot be given guest mouse
input without the host pointer. Its SDL1 DIB backend reads the pointer position with GetCursorPos
(DIB_PumpEvents / DIB_CheckMouse) and ignores the coordinates of posted WM_MOUSEMOVE messages. A
posted button message first syncs the guest to the real pointer, clamped to the window edge. This
TSR uses the channel tools/tmctl.py already has, posted key messages, so the host pointer is never
involved. Details and measurements: re/gg/docs/REFERENCE_CAPTURES.md section 2.

The TSR is generated here as raw 8086 machine code (no assembler needed) and written to the
writable C: drive (extracted/gg_c/MOUSEKEY.COM); tmctl's gg profile loads it from C:\\RUN.BAT before
SSG.EXE. It contains no game code or data.

What it does once resident:
  INT 33h hook (installed before the game, so it sees all of the game's driver calls):
    fn 0Ch  remembers the caller's event handler (ES:DX) and mask (CX) and registers mask 0 with the
            real driver: host mouse events can never reach the program, only injected ones.
    fn 03h  returns the injected state (BX buttons, CX x, DX y) instead of the driver's.
    fn 04h  the program moves the pointer: the injected position follows (clamped), then passed on.
    fn 07h/08h  the horizontal/vertical range: kept for clamping (as a real driver does), passed on.
    fn 00h/21h  reset: forget the handler, full range, centre (320,100), no buttons; passed on.
    all others are passed on unchanged (fn 14h/18h are not intercepted).
  INT 21h hook: AX=2509h (set the INT 9 vector) makes the program's handler the TSR's chain target
    instead of the vector, and AX=3509h returns that chain target. So the TSR stays FIRST in the
    INT 9 chain (TSR -> G&G's Kbd_ISR 1454:00e4 -> BIOS) and its keys never reach the game. This is
    needed: the game's INT 9 writes a ring record (type 3 + tick) for every key it passes on, and
    Input_GetCode 198d:0106 pops one record per call and, when the BIOS buffer is empty, replaces the
    mouse code it has just read from its latches with 0. A release latched by a command made of
    such keys was lost that way (observed: clicks on buttons did nothing while the TSR sat behind
    the game's handler).
  INT 9 hook:
    a command key (F5..F9) followed by 4 data keys is a command; the 5 keys and their releases are
    swallowed (acknowledged and EOI'd here, never reaching the game or the BIOS). Data key k of the 32
    SYMBOLS carries 5 bits: x = s0*32 + s1, y = s2*32 + s3 in INT 33h units (x 0..639, y 0..199 in
    mode 13h; G&G halves x). A command not completed within 200 BIOS ticks (1.4 s while G&G runs,
    whose timer makes the BIOS clock 8 times fast; 11 s otherwise) is dropped.
      F5 move          event: moved (always reported, even to the same position)
      F6 left down     moved (if the position changed) + left pressed
      F7 left up       moved (if changed) + left released
      F8 right down    moved (if changed) + right pressed
      F9 right up      moved (if changed) + right released
    The position is clamped to the current range. The real driver is told the position (fn 04h) and
    the program's handler is called far with AX = event bits AND its mask, BX = buttons, CX = x,
    DX = y, SI = DI = 0 (no mickeys), from the keyboard interrupt with interrupts enabled but before
    its EOI (IRQ 1 still in service, the timer still runs). That is the same context as a driver's
    IRQ 12 callback and as G&G's own keyboard ISR, which also enables interrupts at once.
  Data keys while no command is pending, and every other key, pass through untouched (typing names
  works as usual).

Usage (python -P mouse_tsr.py ...):
  build OUT.COM            write the TSR
  keys X Y ACTION          print the key names tmctl posts (ACTION: move, ldown, lup, rdown, rup;
                           X, Y in INT 33h units)
  status STATE             read a DOSBox-X save state (.sav, or a name in extracted/captures/gg/
                           savestates): the TSR's variables and, if G&G runs, its mouse variables
"""
import os
import struct
import sys
import zipfile

SIGNATURE = b'TMCTL-MOUSEKEY\x00'
VERSION = 2
TIMEOUT_TICKS = 200                 # BIOS ticks (0040:006C) a started command may take

# command keys (tmctl key names) -> command number; data symbols: 32 key names, value = index
COMMANDS = {'move': 'f5', 'ldown': 'f6', 'lup': 'f7', 'rdown': 'f8', 'rup': 'f9'}
COMMAND_NUMBER = {'f5': 1, 'f6': 2, 'f7': 3, 'f8': 4, 'f9': 5}
SYMBOLS = ['1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
           'o', 'p', 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', 'z', 'x', 'v']
# set-1 make codes of those keys (tmctl.KEYS uses the same); 'c' (2Eh) is avoided: it is in G&G's
# own key table (Ctrl+C), as are the arrows, Space and Ctrl
SCANCODES = {'f5': 0x3F, 'f6': 0x40, 'f7': 0x41, 'f8': 0x42, 'f9': 0x43,
             '1': 0x02, '2': 0x03, '3': 0x04, '4': 0x05, '5': 0x06, '6': 0x07, '7': 0x08, '8': 0x09,
             '9': 0x0A, '0': 0x0B, 'q': 0x10, 'w': 0x11, 'e': 0x12, 'r': 0x13, 't': 0x14, 'y': 0x15,
             'u': 0x16, 'i': 0x17, 'o': 0x18, 'p': 0x19, 'a': 0x1E, 's': 0x1F, 'd': 0x20, 'f': 0x21,
             'g': 0x22, 'h': 0x23, 'j': 0x24, 'k': 0x25, 'l': 0x26, 'z': 0x2C, 'x': 0x2D, 'v': 0x2F}

AX, CX, DX, BX, SP, BP, SI, DI = range(8)
AL, CL, DL, BL, AH, CH, DH, BH = range(8)
CS_ = 0x2E                          # CS segment-override prefix


class Asm:
    """Just enough of an 8086 assembler: raw bytes plus label fixups (absolute 16-bit, rel8, rel16)."""

    def __init__(self, org=0x100):
        self.org, self.code, self.labels, self.fix = org, bytearray(), {}, []

    def label(self, name):
        assert name not in self.labels, name
        self.labels[name] = self.org + len(self.code)

    def b(self, *bs):
        self.code += bytes(bs)

    def w(self, v):
        self.code += struct.pack('<H', v & 0xFFFF)

    def a16(self, name, add=0):
        self.fix.append(('abs', len(self.code), name, add))
        self.w(0)

    def rel8(self, name):
        self.fix.append(('rel8', len(self.code), name, 0))
        self.b(0)

    def rel16(self, name):
        self.fix.append(('rel16', len(self.code), name, 0))
        self.w(0)

    def data(self, name, raw):
        self.label(name)
        self.code += raw

    def link(self):
        for kind, pos, name, add in self.fix:
            t = self.labels[name] + add
            if kind == 'abs':
                struct.pack_into('<H', self.code, pos, t)
            elif kind == 'rel8':
                d = t - (self.org + pos + 1)
                if not -128 <= d <= 127:
                    raise ValueError(f'short jump to {name} out of range ({d})')
                self.code[pos] = d & 0xFF
            else:
                struct.pack_into('<h', self.code, pos, t - (self.org + pos + 2))
        return bytes(self.code)

    # ---- instruction helpers (only the forms used below) ----
    def jcc(self, cc, name):        # cc: opcode 70..7F
        self.b(cc)
        self.rel8(name)

    def jmp(self, name):
        self.b(0xEB)
        self.rel8(name)

    def jccn(self, cc, name):       # conditional near jump: inverted short jcc over a near jmp
        self.b(cc ^ 1, 0x03)
        self.jmpn(name)

    def jmpn(self, name):
        self.b(0xE9)
        self.rel16(name)

    def call(self, name):
        self.b(0xE8)
        self.rel16(name)

    def mov_r16_m(self, reg, name, seg=None, add=0):     # mov r16, [name]
        if seg:
            self.b(seg)
        if reg == AX:
            self.b(0xA1)
        else:
            self.b(0x8B, (reg << 3) | 6)
        self.a16(name, add)

    def mov_m_r16(self, name, reg, seg=None, add=0):     # mov [name], r16
        if seg:
            self.b(seg)
        if reg == AX:
            self.b(0xA3)
        else:
            self.b(0x89, (reg << 3) | 6)
        self.a16(name, add)

    def mov_m16_imm(self, name, imm, seg=None, add=0):   # mov word [name], imm16
        if seg:
            self.b(seg)
        self.b(0xC7, 0x06)
        self.a16(name, add)
        self.w(imm)

    def mov_m8_imm(self, name, imm, seg=None):            # mov byte [name], imm8
        if seg:
            self.b(seg)
        self.b(0xC6, 0x06)
        self.a16(name)
        self.b(imm)

    def cmp_m8_imm(self, name, imm):                      # cmp byte [name], imm8
        self.b(0x80, 0x3E)
        self.a16(name)
        self.b(imm)

    def jmp_far_m(self, name):                            # jmp far cs:[name]
        self.b(CS_, 0xFF, 0x2E)
        self.a16(name)

    def call_far_m(self, name, seg=None):                 # call far [name]
        if seg:
            self.b(seg)
        self.b(0xFF, 0x1E)
        self.a16(name)


JZ, JNZ, JB, JAE, JL, JGE, JLE = 0x74, 0x75, 0x72, 0x73, 0x7C, 0x7D, 0x7E


def build():
    """Return the bytes of MOUSEKEY.COM."""
    a = Asm()
    a.b(0xE9)
    a.rel16('init')
    # ---------------- resident data (fixed layout right after the signature) ----------------
    a.data('sig', SIGNATURE)
    a.data('ver', struct.pack('<H', VERSION))
    a.data('old9', bytes(4))
    a.data('old33', bytes(4))
    a.data('hnd', bytes(4))
    a.data('hmask', bytes(2))
    a.data('mx', struct.pack('<H', 320))
    a.data('my', struct.pack('<H', 100))
    a.data('mbtn', bytes(2))
    a.data('minx', struct.pack('<H', 0))
    a.data('maxx', struct.pack('<H', 639))
    a.data('miny', struct.pack('<H', 0))
    a.data('maxy', struct.pack('<H', 199))
    a.data('state', bytes(1))
    a.data('cmd', bytes(1))
    a.data('buf', bytes(4))
    a.data('t0', bytes(2))
    a.data('nevents', bytes(2))
    a.data('lastax', bytes(2))
    a.data('old21', bytes(4))
    kind = bytearray(128)
    for name, n in COMMAND_NUMBER.items():
        kind[SCANCODES[name]] = n
    for v, name in enumerate(SYMBOLS):
        kind[SCANCODES[name]] = 0x80 | v
    a.data('kind', bytes(kind))
    a.data('pend', bytes(128))

    # ---------------- INT 33h hook ----------------
    a.label('int33')
    for fn, target in ((0x0C, 'f0c'), (0x03, 'f03'), (0x04, 'f04'), (0x07, 'f07'), (0x08, 'f08'),
                       (0x00, 'f00'), (0x21, 'f00')):
        a.b(0x3D)                     # cmp ax, imm16
        a.w(fn)
        a.jccn(JZ, target)
    a.jmp_far_m('old33')

    a.label('f0c')                    # set event handler: remember it, give the driver mask 0
    a.mov_m_r16('hnd', DX, CS_)
    a.b(CS_, 0x8C, 0x06)              # mov cs:[hnd+2], es
    a.a16('hnd', 2)
    a.mov_m_r16('hmask', CX, CS_)
    a.b(0x51)                         # push cx
    a.b(0x31, 0xC9)                   # xor cx, cx
    a.b(0x9C)                         # pushf
    a.call_far_m('old33', CS_)
    a.b(0x59)                         # pop cx
    a.b(0xCF)                         # iret

    a.label('f03')                    # position and buttons
    a.mov_r16_m(BX, 'mbtn', CS_)
    a.mov_r16_m(CX, 'mx', CS_)
    a.mov_r16_m(DX, 'my', CS_)
    a.b(0xCF)

    a.label('f04')                    # set position
    a.b(0x50)                         # push ax
    a.b(0x89, 0xC8)                   # mov ax, cx
    a.call('clampx')
    a.mov_m_r16('mx', AX, CS_)
    a.b(0x89, 0xD0)                   # mov ax, dx
    a.call('clampy')
    a.mov_m_r16('my', AX, CS_)
    a.b(0x58)                         # pop ax
    a.jmp_far_m('old33')

    for fn, lo, hi, pos, clamp in (('f07', 'minx', 'maxx', 'mx', 'clampx'),
                                   ('f08', 'miny', 'maxy', 'my', 'clampy')):
        a.label(fn)                   # set range (min/max in either order, as drivers accept)
        a.b(0x50, 0x53)               # push ax; push bx
        a.b(0x89, 0xC8)               # mov ax, cx
        a.b(0x89, 0xD3)               # mov bx, dx
        a.b(0x39, 0xD8)               # cmp ax, bx
        a.b(JLE, 0x01)                # jle +1
        a.b(0x93)                     # xchg ax, bx
        a.mov_m_r16(lo, AX, CS_)
        a.mov_m_r16(hi, BX, CS_)
        a.mov_r16_m(AX, pos, CS_)
        a.call(clamp)
        a.mov_m_r16(pos, AX, CS_)
        a.b(0x5B, 0x58)               # pop bx; pop ax
        a.jmp_far_m('old33')

    a.label('f00')                    # reset
    for name, v in (('hnd', 0), ('hmask', 0), ('mbtn', 0), ('minx', 0), ('maxx', 639), ('miny', 0),
                    ('maxy', 199), ('mx', 320), ('my', 100)):
        a.mov_m16_imm(name, v, CS_)
    a.mov_m16_imm('hnd', 0, CS_, add=2)
    a.jmp_far_m('old33')

    for name, lo, hi in (('clampx', 'minx', 'maxx'), ('clampy', 'miny', 'maxy')):
        a.label(name)                 # AX = clamp(AX, [lo], [hi]) (signed)
        a.b(CS_, 0x3B, 0x06)          # cmp ax, cs:[lo]
        a.a16(lo)
        a.b(JGE, 0x04)                # jge +4
        a.mov_r16_m(AX, lo, CS_)      # 4 bytes
        a.b(CS_, 0x3B, 0x06)          # cmp ax, cs:[hi]
        a.a16(hi)
        a.b(JLE, 0x04)
        a.mov_r16_m(AX, hi, CS_)
        a.b(0xC3)

    # ---------------- INT 9 hook ----------------
    a.label('int9')
    a.b(0x50, 0x53, 0x1E)             # push ax; push bx; push ds
    a.b(0x0E, 0x1F)                   # push cs; pop ds
    a.b(0xE4, 0x60)                   # in al, 60h
    a.b(0x3C, 0xE0)                   # cmp al, 0E0h  (E0/E1 prefixes, controller bytes: not ours)
    a.jccn(JAE, 'pass')
    a.b(0x88, 0xC3)                   # mov bl, al
    a.b(0x30, 0xFF)                   # xor bh, bh
    a.b(0x80, 0xE3, 0x7F)             # and bl, 7Fh
    a.b(0xA8, 0x80)                   # test al, 80h
    a.jcc(JNZ, 'brk')
    a.call('tmo')                     # drop a command that took too long
    a.b(0x8A, 0xA7)                   # mov ah, [bx+kind]
    a.a16('kind')
    a.cmp_m8_imm('state', 0)
    a.jcc(JNZ, 'armed')
    a.label('idle')
    a.b(0x84, 0xE4)                   # test ah, ah
    a.jccn(JZ, 'pass')
    a.b(0xF6, 0xC4, 0x80)             # test ah, 80h
    a.jccn(JNZ, 'pass')               # a data key without a command: not ours
    a.b(0x88, 0x26)                   # mov [cmd], ah
    a.a16('cmd')
    a.mov_m8_imm('state', 1)
    a.b(0x06)                         # push es
    a.b(0xB8, 0x40, 0x00)             # mov ax, 40h
    a.b(0x8E, 0xC0)                   # mov es, ax
    a.b(0x26, 0xA1, 0x6C, 0x00)       # mov ax, es:[6Ch]
    a.mov_m_r16('t0', AX)
    a.b(0x07)                         # pop es
    a.jmp('swmake')
    a.label('armed')
    a.b(0xF6, 0xC4, 0x80)             # test ah, 80h
    a.jcc(JZ, 'notsym')
    a.b(0x80, 0xE4, 0x1F)             # and ah, 1Fh
    a.b(0x53)                         # push bx
    a.b(0x8A, 0x1E)                   # mov bl, [state]
    a.a16('state')
    a.b(0x30, 0xFF)                   # xor bh, bh
    a.b(0x88, 0xA7)                   # mov [bx+buf-1], ah
    a.a16('buf', -1)
    a.b(0x5B)                         # pop bx
    a.b(0xFE, 0x06)                   # inc byte [state]
    a.a16('state')
    a.cmp_m8_imm('state', 5)
    a.jcc(JB, 'swmake')
    a.mov_m8_imm('state', 0)
    a.b(0xFB)                         # sti (keyboard IRQ stays in service until our EOI)
    a.call('execute')
    a.jmp('swmake')
    a.label('notsym')                 # another key while a command is pending: drop the command
    a.mov_m8_imm('state', 0)
    a.jmp('idle')
    a.label('swmake')
    a.b(0xC6, 0x87)                   # mov byte [bx+pend], 1
    a.a16('pend')
    a.b(0x01)
    a.jmp('swallow')
    a.label('brk')
    a.b(0x80, 0xBF)                   # cmp byte [bx+pend], 0
    a.a16('pend')
    a.b(0x00)
    a.jccn(JZ, 'pass')
    a.b(0xC6, 0x87)                   # mov byte [bx+pend], 0
    a.a16('pend')
    a.b(0x00)
    a.label('swallow')                # acknowledge (as the BIOS and G&G do) and EOI
    a.b(0xE4, 0x61, 0x0C, 0x80, 0xE6, 0x61, 0x24, 0x7F, 0xE6, 0x61)
    a.b(0xB0, 0x20, 0xE6, 0x20)
    a.b(0x1F, 0x5B, 0x58)             # pop ds; pop bx; pop ax
    a.b(0xCF)
    a.label('pass')
    a.b(0x1F, 0x5B, 0x58)
    a.jmp_far_m('old9')

    a.label('tmo')
    a.cmp_m8_imm('state', 0)
    a.jcc(JZ, 'tmo_r')
    a.b(0x06, 0x50)                   # push es; push ax
    a.b(0xB8, 0x40, 0x00, 0x8E, 0xC0, 0x26, 0xA1, 0x6C, 0x00)
    a.b(0x2B, 0x06)                   # sub ax, [t0]
    a.a16('t0')
    a.b(0x3D)                         # cmp ax, TIMEOUT
    a.w(TIMEOUT_TICKS)
    a.b(JB, 0x05)                     # jb +5
    a.mov_m8_imm('state', 0)          # 5 bytes
    a.b(0x58, 0x07)                   # pop ax; pop es
    a.label('tmo_r')
    a.b(0xC3)

    a.label('execute')                # run the completed command in cmd/buf (DS = CS)
    a.b(0x51, 0x52, 0x56, 0x57, 0x55, 0x06, 0x50, 0x53)
    a.b(0xA0)                         # mov al, [buf]
    a.a16('buf')
    a.b(0x30, 0xE4)                   # xor ah, ah
    a.b(0xB1, 0x05, 0xD3, 0xE0)       # mov cl, 5; shl ax, cl
    a.b(0x0A, 0x06)                   # or al, [buf+1]
    a.a16('buf', 1)
    a.call('clampx')
    a.b(0x89, 0xC1)                   # mov cx, ax
    a.b(0xA0)
    a.a16('buf', 2)
    a.b(0x30, 0xE4)
    a.b(0x51, 0xB1, 0x05, 0xD3, 0xE0, 0x59)   # push cx; mov cl,5; shl ax,cl; pop cx
    a.b(0x0A, 0x06)
    a.a16('buf', 3)
    a.call('clampy')
    a.b(0x89, 0xC2)                   # mov dx, ax
    a.b(0x31, 0xDB)                   # xor bx, bx   (BL = event bits)
    a.b(0x3B, 0x0E)                   # cmp cx, [mx]
    a.a16('mx')
    a.jcc(JNZ, 'moved')
    a.b(0x3B, 0x16)                   # cmp dx, [my]
    a.a16('my')
    a.jcc(JZ, 'notmoved')
    a.label('moved')
    a.b(0x80, 0xCB, 0x01)             # or bl, 1
    a.label('notmoved')
    a.mov_m_r16('mx', CX)
    a.mov_m_r16('my', DX)
    a.b(0xA0)                         # mov al, [cmd]
    a.a16('cmd')
    for n, bit, op in ((1, 0x01, None), (2, 0x02, (0x0E, 0x01)), (3, 0x04, (0x26, 0xFE)),
                       (4, 0x08, (0x0E, 0x02)), (5, 0x10, (0x26, 0xFD))):
        a.b(0x3C, n)                  # cmp al, n
        a.jcc(JNZ, f'c{n + 1}')
        a.b(0x80, 0xCB, bit)          # or bl, bit
        if op:                        # or/and byte [mbtn], mask
            a.b(0x80, op[0])
            a.a16('mbtn')
            a.b(op[1])
        a.jmp('deliver')
        a.label(f'c{n + 1}')
    a.label('deliver')
    a.b(0x53)                         # push bx
    a.b(0xB8, 0x04, 0x00)             # mov ax, 4 (driver: set position CX, DX)
    a.b(0x9C)                         # pushf
    a.call_far_m('old33')
    a.b(0x5B)                         # pop bx
    a.b(0xFF, 0x06)                   # inc word [nevents]
    a.a16('nevents')
    a.mov_r16_m(AX, 'hnd')
    a.b(0x0B, 0x06)                   # or ax, [hnd+2]
    a.a16('hnd', 2)
    a.jcc(JZ, 'done')
    a.b(0x88, 0xD8)                   # mov al, bl
    a.b(0x30, 0xE4)                   # xor ah, ah
    a.b(0x23, 0x06)                   # and ax, [hmask]
    a.a16('hmask')
    a.mov_m_r16('lastax', AX)         # (mov keeps the flags of the AND)
    a.jcc(JZ, 'done')
    a.mov_r16_m(BX, 'mbtn')
    a.mov_r16_m(CX, 'mx')
    a.mov_r16_m(DX, 'my')
    a.b(0x31, 0xF6, 0x31, 0xFF)       # xor si, si; xor di, di
    a.b(0x1E)                         # push ds
    a.call_far_m('hnd')
    a.b(0x1F)                         # pop ds
    a.label('done')
    a.b(0x5B, 0x58, 0x07, 0x5D, 0x5F, 0x5E, 0x5A, 0x59)
    a.b(0xC3)

    # ---------------- INT 21h hook: stay first in the INT 9 chain ----------------
    a.label('int21')
    a.b(0x3D)                         # cmp ax, 2509h (set INT 9 vector)
    a.w(0x2509)
    a.jcc(JZ, 'set9')
    a.b(0x3D)                         # cmp ax, 3509h (get INT 9 vector)
    a.w(0x3509)
    a.jcc(JZ, 'get9')
    a.b(CS_, 0xFF, 0x2E)              # jmp far cs:[old21]
    a.a16('old21')
    a.label('set9')                   # the program's handler becomes our chain target
    a.mov_m_r16('old9', DX, CS_)
    a.b(CS_, 0x8C, 0x1E)              # mov cs:[old9+2], ds
    a.a16('old9', 2)
    a.b(0xCF)                         # iret
    a.label('get9')                   # report our chain target as the current vector
    a.mov_r16_m(BX, 'old9', CS_)
    a.b(CS_, 0x8E, 0x06)              # mov es, cs:[old9+2]
    a.a16('old9', 2)
    a.b(0xCF)

    # ---------------- transient installer ----------------
    a.label('init')
    a.b(0xB8, 0x09, 0x35, 0xCD, 0x21)         # mov ax, 3509h; int 21h
    a.mov_m_r16('old9', BX)
    a.b(0x8C, 0x06)                           # mov [old9+2], es
    a.a16('old9', 2)
    a.b(0xB8, 0x33, 0x35, 0xCD, 0x21)         # mov ax, 3533h; int 21h
    a.mov_m_r16('old33', BX)
    a.b(0x8C, 0x06)
    a.a16('old33', 2)
    a.b(0xBA)                                 # mov dx, int9
    a.a16('int9')
    a.b(0xB8, 0x09, 0x25, 0xCD, 0x21)         # mov ax, 2509h; int 21h
    a.b(0xBA)
    a.a16('int33')
    a.b(0xB8, 0x33, 0x25, 0xCD, 0x21)         # mov ax, 2533h; int 21h
    a.b(0xB8, 0x21, 0x35, 0xCD, 0x21)         # mov ax, 3521h; int 21h
    a.mov_m_r16('old21', BX)
    a.b(0x8C, 0x06)                           # mov [old21+2], es
    a.a16('old21', 2)
    a.b(0xBA)                                 # mov dx, int21 (hooked last: our own 25h/35h calls above
    a.a16('int21')                            # went straight to DOS)
    a.b(0xB8, 0x21, 0x25, 0xCD, 0x21)         # mov ax, 2521h; int 21h
    a.b(0xBA)
    a.a16('msg')
    a.b(0xB4, 0x09, 0xCD, 0x21)               # mov ah, 9; int 21h
    a.b(0x8E, 0x06, 0x2C, 0x00)               # mov es, [2Ch] (environment)
    a.b(0xB4, 0x49, 0xCD, 0x21)               # mov ah, 49h; int 21h (free it)
    a.b(0xBA)                                 # mov dx, resident paragraphs
    a.fix.append(('paras', len(a.code), 'init', 0))
    a.w(0)
    a.b(0xB8, 0x00, 0x31, 0xCD, 0x21)         # mov ax, 3100h; int 21h (keep resident)
    a.data('msg', b'MOUSEKEY (tmctl mouse) installed\r\n$')
    paras = [f for f in a.fix if f[0] == 'paras']
    a.fix = [f for f in a.fix if f[0] != 'paras']
    code = bytearray(a.link())
    for _, pos, name, _ in paras:
        struct.pack_into('<H', code, pos, (a.labels[name] + 15) >> 4)
    return bytes(code)


def keys_for(x, y, action):
    """Key names (tmctl.KEYS) of one command: a command key and 4 data keys."""
    if action not in COMMANDS:
        raise ValueError(f'unknown mouse action {action!r}')
    if not (0 <= x < 1024 and 0 <= y < 1024):
        raise ValueError(f'coordinates out of range: {x},{y}')
    return [COMMANDS[action], SYMBOLS[x >> 5], SYMBOLS[x & 31], SYMBOLS[y >> 5], SYMBOLS[y & 31]]


# ---------------- save-state reader ----------------
HDR = 0x88
GG_SIG = b'Borland C++ - Copyright 1991 Borland Intl.'   # at G&G's DS:0004
STATELIB = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..',
                                         'extracted', 'captures', 'gg', 'savestates'))
TSR_FIELDS = [('ver', 'H'), ('old9', 'I'), ('old33', 'I'), ('hnd', 'I'), ('hmask', 'H'), ('mx', 'h'),
              ('my', 'h'), ('mbtn', 'H'), ('minx', 'h'), ('maxx', 'h'), ('miny', 'h'), ('maxy', 'h'),
              ('state', 'B'), ('cmd', 'B'), ('buf', '4s'), ('t0', 'H'), ('nevents', 'H'), ('lastax', 'H'),
              ('old21', 'I')]
GG_MOUSE = [(0x7A88, 'cursor_x'), (0x7A86, 'cursor_y'), (0x01E2, 'draw_busy'), (0x0A10, 'held'),
            (0x0A1A, 'press_x'), (0x0A1C, 'press_y'), (0x0A1E, 'first_press'), (0x0A20, 'release'),
            (0x0A2A, 'release_x'), (0x0A2C, 'release_y')]


def read_state(path):
    if not os.path.exists(path):
        path = os.path.join(STATELIB, path + '.sav')
    with zipfile.ZipFile(path) as z:
        return z.read('Memory')


def tsr_status(mem):
    i = mem.find(SIGNATURE)
    if i < 0:
        return None
    out, o = {'segment': (i - HDR - 3) >> 4 if (i - HDR - 3) % 16 == 0 else None}, i + len(SIGNATURE)
    psp = i - HDR - 0x103
    out['psp'] = psp >> 4 if psp % 16 == 0 else None
    for name, fmt in TSR_FIELDS:
        out[name] = struct.unpack_from('<' + fmt, mem, o)[0]
        o += struct.calcsize('<' + fmt)
    return out


def gg_mouse(mem):
    i = mem.find(GG_SIG)
    if i < 0:
        return None
    ds = i - 4
    out = {'dgroup': (ds - HDR) >> 4}
    for off, name in GG_MOUSE:
        out[name] = struct.unpack_from('<h', mem, ds + off)[0]
    return out


def main(argv):
    if not argv:
        print(__doc__)
        return
    cmd = argv[0]
    if cmd == 'build':
        data = build()
        with open(argv[1], 'wb') as f:
            f.write(data)
        print(f'{argv[1]}: {len(data)} bytes')
    elif cmd == 'keys':
        print(' '.join(keys_for(int(argv[1]), int(argv[2]), argv[3])))
    elif cmd == 'status':
        for p in argv[1:]:
            mem = read_state(p)
            t = tsr_status(mem)
            g = gg_mouse(mem)
            print(p)
            if t:
                t['buf'] = t['buf'].hex()
                t['psp'] = '%04x' % t['psp'] if t['psp'] is not None else '?'
                for k in ('old9', 'old33', 'hnd', 'old21'):
                    t[k] = '%04x:%04x' % (t[k] >> 16, t[k] & 0xFFFF)
                t.pop('segment', None)
                print('  MOUSEKEY', ' '.join(f'{k}={v}' for k, v in t.items()))
            else:
                print('  MOUSEKEY not resident')
            if g:
                print('  G&G     ', ' '.join(f'{k}={v:04x}' if k == 'dgroup' else f'{k}={v}'
                                             for k, v in g.items()))
    else:
        print(__doc__)


if __name__ == '__main__':
    main(sys.argv[1:])
