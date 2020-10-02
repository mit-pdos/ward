#include "types.h"
#include "amd64.h"
#include "kernel.hh"

// PC keyboard interface constants

#define KBSTATP         0x64    // kbd controller status port(I)
#define KBS_DIB         0x01    // kbd data in buffer
#define KBDATAP         0x60    // kbd data port(I)

#define SHIFT           (1<<0)
#define CTL             (1<<1)
#define ALT             (1<<2)

#define CAPSLOCK        (1<<3)
#define NUMLOCK         (1<<4)
#define SCROLLLOCK      (1<<5)

#define E0ESC           (1<<6)

// Special keycodes
#define KEY_HOME        0xE0
#define KEY_END         0xE1
#define KEY_UP          0xE2
#define KEY_DN          0xE3
#define KEY_LF          0xE4
#define KEY_RT          0xE5
#define KEY_PGUP        0xE6
#define KEY_PGDN        0xE7
#define KEY_INS         0xE8
#define KEY_DEL         0xE9

u8 kbd_toggle_code(u8 c) {
  switch(c) {
  case 0x3A: return CAPSLOCK;
  case 0x45: return NUMLOCK;
  case 0x46: return SCROLLLOCK;
  }
  return 0;
}

u8 kbd_shift_code(u8 c) {
  switch(c) {
  case 0x1D: return CTL;
  case 0x2A: return SHIFT;
  case 0x36: return SHIFT;
  case 0x38: return ALT;
  case 0x9D: return CTL;
  case 0xB8: return ALT;
  }
  return 0;
}


u8 kbd_normal_map(u8 c) {
  if (c < 0x60) {
    u8 map[0x60] = {
      '\0', 0x1B, '1',  '2',  '3',  '4',  '5',  '6',  // 0x00
      '7',  '8',  '9',  '0',  '-',  '=',  '\b', '\t',
      'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',  // 0x10
      'o',  'p',  '[',  ']',  '\n', '\0', 'a',  's',
      'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',  // 0x20
      '\'', '`',  '\0', '\\', 'z',  'x',  'c',  'v',
      'b',  'n',  'm',  ',',  '.',  '/',  '\0', '*',  // 0x30
      '\0', ' ',  '\0', '\0', '\0', '\0', '\0', '\0',
      '\0', '\0', '\0', '\0', '\0', '\0', '\0', '7',  // 0x40
      '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
      '2',  '3',  '0',  '.',  '\0', '\0', '\0', '\0', // 0x50
    };
    return map[c];
  }
  switch(c) {
  case 0x9C: return '\n';      // KP_Enter
  case 0xB5: return '/';       // KP_Div
  case 0xC8: return KEY_UP;
  case 0xD0: return KEY_DN;
  case 0xC9: return KEY_PGUP;
  case 0xD1: return KEY_PGDN;
  case 0xCB: return KEY_LF;
  case 0xCD: return KEY_RT;
  case 0x97: return KEY_HOME;
  case 0xCF: return KEY_END;
  case 0xD2: return KEY_INS;
  case 0xD3: return KEY_DEL;
  }
  return 0;
}

u8 kbd_shift_map(u8 c) {
  if (c < 0x60) {
    u8 map[0x60] = {
      '\0', 033,  '!',  '@',  '#',  '$',  '%',  '^',  // 0x00
      '&',  '*',  '(',  ')',  '_',  '+',  '\b', '\t',
      'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',  // 0x10
      'O',  'P',  '{',  '}',  '\n', '\0', 'A',  'S',
      'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',  // 0x20
      '"',  '~',  '\0', '|',  'Z',  'X',  'C',  'V',
      'B',  'N',  'M',  '<',  '>',  '?',  '\0', '*',  // 0x30
      '\0', ' ',  '\0', '\0', '\0', '\0', '\0', '\0',
      '\0', '\0', '\0', '\0', '\0', '\0', '\0', '7',  // 0x40
      '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
      '2',  '3',  '0',  '.',  '\0', '\0', '\0', '\0', // 0x50
    };
    return map[c];
  }
  switch(c) {
  case 0x9C: return '\n';      // KP_Enter
  case 0xB5: return '/';       // KP_Div
  case 0xC8: return KEY_UP;
  case 0xD0: return KEY_DN;
  case 0xC9: return KEY_PGUP;
  case 0xD1: return KEY_PGDN;
  case 0xCB: return KEY_LF;
  case 0xCD: return KEY_RT;
  case 0x97: return KEY_HOME;
  case 0xCF: return KEY_END;
  case 0xD2: return KEY_INS;
  case 0xD3: return KEY_DEL;
  }
  return 0;
};

// C('A') == Control-A
#define C(x) (x - '@')

u8 kbd_ctl_map(u8 c)
{
  if (c < 0x38) {
    u8 map[0x38] = {
      '\0',    '\0',    '\0',    '\0',    '\0',    '\0',    '\0',    '\0', // 0x00
      '\0',    '\0',    '\0',    '\0',    '\0',    '\0',    '\0',    '\0',
      C('Q'),  C('W'),  C('E'),  C('R'),  C('T'),  C('Y'),  C('U'),  C('I'), // 0x10
      C('O'),  C('P'),  '\0',    '\0',    '\r',    '\0',    C('A'),  C('S'),
      C('D'),  C('F'),  C('G'),  C('H'),  C('J'),  C('K'),  C('L'),  '\0', // 0x20
      '\0',    '\0',    '\0',    C('\\'), C('Z'),  C('X'),  C('C'),  C('V'),
      C('B'),  C('N'),  C('M'),  '\0',    '\0',    '\0',  '\0',    '\0', // 0x30
    };
    return map[c];
  }
  switch(c) {
  case 0x9C: return '\r';      // KP_Enter
  case 0xB5: return '\0';      // KP_Div
  case 0xC8: return KEY_UP;
  case 0xD0: return KEY_DN;
  case 0xC9: return KEY_PGUP;
  case 0xD1: return KEY_PGDN;
  case 0xCB: return KEY_LF;
  case 0xCD: return KEY_RT;
  case 0x97: return KEY_HOME;
  case 0xCF: return KEY_END;
  case 0xD2: return KEY_INS;
  case 0xD3: return KEY_DEL;
  }
  return 0;
}

int
kbdgetc(void)
{
  static int shift __mpalign__;
  static u8(*charcode[4])(u8) = {
    kbd_normal_map, kbd_shift_map, kbd_ctl_map, kbd_ctl_map
  };
  u32 st, data, c;

  st = inb(KBSTATP);
  if((st & KBS_DIB) == 0)
    return -1;
  data = inb(KBDATAP);

  if(data == 0xE0){
    shift |= E0ESC;
    return 0;
  } else if(data & 0x80){
    // Key released
    data = (shift & E0ESC ? data : data & 0x7F);
    shift &= ~(kbd_shift_code(data) | E0ESC);
    return 0;
  } else if(shift & E0ESC){
    // Last character was an E0 escape; or with 0x80
    data |= 0x80;
    shift &= ~E0ESC;
  }

  shift |= kbd_shift_code(data);
  shift ^= kbd_toggle_code(data);
  c = charcode[shift & (CTL | SHIFT)](data);
  if(shift & CAPSLOCK){
    if('a' <= c && c <= 'z')
      c += 'A' - 'a';
    else if('A' <= c && c <= 'Z')
      c += 'a' - 'A';
  }
  return c;
}

void
kbdintr(void)
{
  consoleintr(kbdgetc);
}

void
mouseintr(void)
{
  // Ignore mouse input
  inb(KBDATAP);
}
