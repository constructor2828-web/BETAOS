#include "os.h"
static unsigned shift, ctrl, alt, caps, caps_down, ext, pause_remaining;
static unsigned layout;
static const char *layout_names[] = {"us", "es", "de", "fr"};
unsigned input_layout(void) { return layout; }
const char *input_layout_name(unsigned n) {
  return n < 4 ? layout_names[n] : "";
}
int input_set_layout(const char *name) {
  for (unsigned i = 0; i < 4; i++)
    if (!strcmp(name, layout_names[i])) {
      layout = i;
      return 1;
    }
  return 0;
}
static int packet_pos;
static u8 packet[4];
static int packet_bytes = 3, wheel_bits = 8;
static const char keys[128] = {
    [2] = '1',  [3] = '2',   [4] = '3',   [5] = '4',   [6] = '5',  [7] = '6',
    [8] = '7',  [9] = '8',   [10] = '9',  [11] = '0',  [12] = '-', [13] = '=',
    [14] = 8,   [15] = 9,    [16] = 'q',  [17] = 'w',  [18] = 'e', [19] = 'r',
    [20] = 't', [21] = 'y',  [22] = 'u',  [23] = 'i',  [24] = 'o', [25] = 'p',
    [26] = '[', [27] = ']',  [28] = '\n', [30] = 'a',  [31] = 's', [32] = 'd',
    [33] = 'f', [34] = 'g',  [35] = 'h',  [36] = 'j',  [37] = 'k', [38] = 'l',
    [39] = ';', [40] = '\'', [41] = '`',  [43] = '\\', [44] = 'z', [45] = 'x',
    [46] = 'c', [47] = 'v',  [48] = 'b',  [49] = 'n',  [50] = 'm', [51] = ',',
    [52] = '.', [53] = '/',  [57] = ' '};
static const char upper[128] = {
    [2] = '!',  [3] = '@',  [4] = '#',  [5] = '$',  [6] = '%',  [7] = '^',
    [8] = '&',  [9] = '*',  [10] = '(', [11] = ')', [12] = '_', [13] = '+',
    [26] = '{', [27] = '}', [39] = ':', [40] = '"', [41] = '~', [43] = '|',
    [51] = '<', [52] = '>', [53] = '?'};
static void wait_write(void) {
  unsigned n = 100000;
  while ((in8(0x64) & 2) && --n) {
  }
}
static int read_controller(void) {
  unsigned n = 100000;
  while (!(in8(0x64) & 1) && --n) {
  }
  return n ? in8(0x60) : -1;
}
static void ctl(u8 c) {
  wait_write();
  out8(0x64, c);
}
static void data(u8 c) {
  wait_write();
  out8(0x60, c);
}
static void mouse_command(u8 c) {
  ctl(0xd4);
  data(c);
  read_controller();
}
void input_init(void) {
  input_reset_decoder();
  ctl(0xad);
  ctl(0xa7);
  for (unsigned i = 0; i < 64 && (in8(0x64) & 1); i++)
    in8(0x60);
  ctl(0x20);
  int config = read_controller();
  if (config < 0)
    config = 0x40;
  ctl(0x60);
  data(((config | 0x40) & ~0x33) | (input_irq_enabled ? 3 : 0));
  ctl(0xae);
  ctl(0xa8);
  data(0xf4);
  read_controller();
  mouse_command(0xf6);
  /* IntelliMouse sample-rate negotiation; old three-byte mice remain usable. */
  mouse_command(0xf3);
  mouse_command(200);
  mouse_command(0xf3);
  mouse_command(100);
  mouse_command(0xf3);
  mouse_command(80);
  mouse_command(0xf2);
  int mouse_id = read_controller();
  packet_bytes = mouse_id == 3 || mouse_id == 4 ? 4 : 3;
  wheel_bits = mouse_id == 4 ? 4 : 8;
  mouse_command(0xf4);
  serial("PS2 MOUSE ID: ");
  serial_num(mouse_id < 0 ? 255 : (unsigned)mouse_id);
  serial(" / bytes: ");
  serial_num(packet_bytes);
  serial("\n");
  serial("PS2 initialized\n");
}
void input_reset_decoder(void) {
  shift = ctrl = alt = caps = caps_down = ext = pause_remaining = 0;
}
void input_decode(u8 sc) {
  if (pause_remaining) {
    pause_remaining--;
    return;
  }
  if (sc == 0xe1) {
    pause_remaining = 5;
    ext = 0;
    return;
  }
  if (sc == 0xe0) {
    ext = 1;
    return;
  }
  unsigned extended = ext;
  ext = 0;
  unsigned key = sc & 127, released = sc & 128, bit;
  if (!extended && (key == 42 || key == 54)) {
    bit = key == 42 ? 1 : 2;
    if (released)
      shift &= ~bit;
    else
      shift |= bit;
    return;
  }
  if (key == 29) {
    bit = extended ? 2 : 1;
    if (released)
      ctrl &= ~bit;
    else
      ctrl |= bit;
    return;
  }
  if (key == 56) {
    bit = extended ? 2 : 1;
    if (released)
      alt &= ~bit;
    else
      alt |= bit;
    return;
  }
  if (!extended && key == 58) {
    if (released)
      caps_down = 0;
    else {
      if (!caps_down)
        caps = !caps;
      caps_down = 1;
    }
    return;
  }
  if (released)
    return;
  if (extended) {
    if (key == 73)
      gui_key(ctrl ? 132 : 16);
    if (key == 81)
      gui_key(ctrl ? 133 : 17);
    if (key == 72)
      gui_key(18);
    if (key == 80)
      gui_key(19);
    if (key == 75)
      gui_key(alt ? 135 : 20);
    if (key == 77)
      gui_key(alt ? 136 : 21);
    if (key == 71)
      gui_key(ctrl ? 11 : 22);
    if (key == 79)
      gui_key(ctrl ? 12 : 23);
    if (key == 83)
      gui_key(7);
    if (key == 91 || key == 92)
      gui_key(30);
    return;
  }
  if (alt && key == 15) {
    gui_key(31);
    return;
  }
  if (ctrl && alt && key == 57) {
    gui_key(130);
    return;
  }
  if (ctrl && !(alt & 2)) {
    if (layout == 2) {
      if (key == 21)
        key = 44;
      else if (key == 44)
        key = 21;
    }
    if (layout == 3) {
      if (key == 17)
        key = 44;
      else if (key == 44)
        key = 17;
    }
    if (key == 30)
      gui_key(137);
    if (key == 44)
      gui_key(128);
    if (key == 21)
      gui_key(129);
    if (key == 49)
      gui_key(15);
    if (key == 17)
      gui_key(14);
    if (key == 46)
      gui_key(24);
    if (key == 47)
      gui_key(131);
    if (key == 33)
      gui_key(134);
    if (key == 38)
      gui_key(25);
    if (key == 31)
      gui_key(26);
    if (key == 48)
      gui_key(28);
    if (key == 19)
      gui_key(29);
    return;
  }
  if (key == 1) {
    gui_key(27);
    return;
  }
  if (key >= 59 && key <= 64) {
    gui_key(key - 58);
    return;
  }
  unsigned char c = keys[key], shifted = upper[key];
  /* ISO physical positions, Latin-1 text. AltGr uses the right Alt bit. */
  if (layout == 1) {
    switch (key) {
    case 3:
      shifted = '"';
      break;
    case 4:
      shifted = 0xb7;
      break;
    case 7:
      shifted = '&';
      break;
    case 8:
      shifted = '/';
      break;
    case 9:
      shifted = '(';
      break;
    case 10:
      shifted = ')';
      break;
    case 11:
      shifted = '=';
      break;
    case 12:
      c = '\'';
      shifted = '?';
      break;
    case 13:
      c = 0xa1;
      shifted = 0xbf;
      break;
    case 26:
      c = '`';
      shifted = '^';
      break;
    case 27:
      c = '+';
      shifted = '*';
      break;
    case 39:
      c = 0xf1;
      shifted = 0xd1;
      break;
    case 40:
      c = 0xb4;
      shifted = 0xa8;
      break;
    case 41:
      c = 0xba;
      shifted = 0xaa;
      break;
    case 43:
      c = 0xe7;
      shifted = 0xc7;
      break;
    case 51:
      shifted = ';';
      break;
    case 52:
      shifted = ':';
      break;
    case 53:
      c = '-';
      shifted = '_';
      break;
    case 86:
      c = '<';
      shifted = '>';
      break;
    }
    if (alt & 2) {
      const unsigned char altgr[128] = {[2] = '|',   [3] = '@',  [4] = '#',
                                        [5] = '~',   [6] = 0x80, [7] = 0xac,
                                        [12] = '\\', [26] = '[', [27] = ']',
                                        [40] = '{',  [43] = '}'};
      if (altgr[key] && altgr[key] != 0x80) {
        gui_key(altgr[key]);
        return;
      }
      return;
    }
  } else if (layout == 2) {
    if (key == 21)
      c = 'z';
    if (key == 44)
      c = 'y';
    switch (key) {
    case 3:
      shifted = '"';
      break;
    case 7:
      shifted = '&';
      break;
    case 8:
      shifted = '/';
      break;
    case 9:
      shifted = '(';
      break;
    case 10:
      shifted = ')';
      break;
    case 11:
      shifted = '=';
      break;
    case 12:
      c = 0xdf;
      shifted = '?';
      break;
    case 13:
      c = 0xb4;
      shifted = '`';
      break;
    case 26:
      c = 0xfc;
      shifted = 0xdc;
      break;
    case 27:
      c = '+';
      shifted = '*';
      break;
    case 39:
      c = 0xf6;
      shifted = 0xd6;
      break;
    case 40:
      c = 0xe4;
      shifted = 0xc4;
      break;
    case 41:
      c = '^';
      shifted = 0xb0;
      break;
    case 43:
      c = '#';
      shifted = '\'';
      break;
    case 51:
      shifted = ';';
      break;
    case 52:
      shifted = ':';
      break;
    case 53:
      c = '-';
      shifted = '_';
      break;
    case 86:
      c = '<';
      shifted = '>';
      break;
    }
    if (alt & 2) {
      const char altgr[128] = {[16] = '@', [8] = '{',   [9] = '[',  [10] = ']',
                               [11] = '}', [12] = '\\', [27] = '~', [86] = '|'};
      if (altgr[key])
        gui_key(altgr[key]);
      return;
    }
  } else if (layout == 3) {
    if (key == 16)
      c = 'a';
    if (key == 30)
      c = 'q';
    if (key == 17)
      c = 'z';
    if (key == 44)
      c = 'w';
    if (key == 39)
      c = 'm';
    if (key >= 2 && key <= 13) {
      const unsigned char row[] = {'&',  0xe9, '"',  '\'', '(', '-',
                                   0xe8, '_',  0xe7, 0xe0, ')', '='};
      c = row[key - 2];
      shifted = key <= 11 ? '1' + (key - 2) % 10 : key == 12 ? 0xb0 : '+';
      if (key == 11)
        shifted = '0';
    }
    switch (key) {
    case 26:
      c = '^';
      shifted = 0xa8;
      break;
    case 27:
      c = '$';
      shifted = 0xa3;
      break;
    case 40:
      c = 0xf9;
      shifted = '%';
      break;
    case 41:
      c = 0xb2;
      shifted = 0;
      break;
    case 43:
      c = '*';
      shifted = 0xb5;
      break;
    case 50:
      c = ',';
      shifted = '?';
      break;
    case 51:
      c = ';';
      shifted = '.';
      break;
    case 52:
      c = ':';
      shifted = '/';
      break;
    case 53:
      c = '!';
      shifted = 0xa7;
      break;
    case 86:
      c = '<';
      shifted = '>';
      break;
    }
    if (alt & 2) {
      const char altgr[128] = {[3] = '~',  [4] = '#',  [5] = '{',  [6] = '[',
                               [7] = '|',  [8] = '`',  [9] = '\\', [10] = '^',
                               [11] = '@', [12] = ']', [13] = '}'};
      if (altgr[key])
        gui_key(altgr[key]);
      return;
    }
  }
  if (c >= 'a' && c <= 'z') {
    if ((shift != 0) ^ (caps != 0))
      c -= 32;
  } else if (c >= 0xe0 && c <= 0xfe && c != 0xf7 && shifted == c - 32) {
    if ((shift != 0) ^ (caps != 0))
      c -= 32;
  } else if (shift && shifted)
    c = shifted;
  else if (caps &&
           (c == 0xf1 || c == 0xe4 || c == 0xf6 || c == 0xfc || c == 0xe7))
    c -= 32;
  if (c)
    gui_key(c);
}
void input_poll(void) {
  for (unsigned count = 0; count < 32; count++) {
    u8 status = in8(0x64);
    if (!(status & 1))
      break;
    u8 sc = in8(0x60);
    if (status & 0x20) {
      if (!packet_pos && !(sc & 8))
        continue;
      packet[packet_pos++] = sc;
      if (packet_pos == packet_bytes) {
        packet_pos = 0;
        if (!(packet[0] & 0xc0))
          gui_mouse((int)packet[1] - ((packet[0] & 16) ? 256 : 0),
                    (int)packet[2] - ((packet[0] & 32) ? 256 : 0),
                    packet[0] & 7);
        if (packet_bytes == 4) {
          int wheel = (signed char)packet[3];
          if (wheel_bits == 4) {
            wheel = (signed char)(packet[3] << 4);
            wheel >>= 4;
          }
          if (wheel)
            gui_wheel(wheel);
        }
      }
      continue;
    }
    input_decode(sc);
  }
  /* Serial console: Ctrl-A..D switch apps, useful without a PS/2 keyboard. */
  if (in8(0x3fd) & 1) {
    char c = in8(0x3f8);
    gui_key(c);
  }
}
