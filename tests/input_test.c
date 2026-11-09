void gui_wheel(int n){(void)n;}
#include "os.h"
#include <assert.h>
#include <stdio.h>
int input_irq_enabled;
static int received[256];
static unsigned count;
void gui_key(int c) {
  assert(count < sizeof(received) / sizeof(received[0]));
  received[count++] = c;
}
void gui_mouse(int x, int y, int buttons) {
  (void)x;
  (void)y;
  (void)buttons;
}
void serial(const char *s) { (void)s; }
void serial_num(u64 n){(void)n;}
static void feed(const u8 *p, unsigned n) {
  for (unsigned i = 0; i < n; i++)
    input_decode(p[i]);
}
static void expect(int c) { assert(count && received[count - 1] == c); }
int main(void) {
  input_reset_decoder();
  input_decode(30);
  expect('a');
  input_decode(42);
  input_decode(54);
  input_decode(170);
  input_decode(30);
  expect('A');
  input_decode(182);
  input_decode(30);
  expect('a');
  // Releasing shift while Ctrl is held must never leave shift stuck.
  input_decode(42);
  input_decode(29);
  input_decode(170);
  input_decode(157);
  input_decode(30);
  expect('a');
  input_decode(58);
  input_decode(58);
  input_decode(186);
  input_decode(30);
  expect('A');
  input_decode(42);
  input_decode(30);
  expect('a');
  input_decode(170);
  input_decode(58);
  input_decode(186);
  const u8 pause[] = {0xe1, 0x1d, 0x45, 0xe1, 0x9d, 0xc5};
  unsigned before = count;
  feed(pause, sizeof(pause));
  assert(count == before);
  input_decode(30);
  expect('a');
  const u8 rightctrl[] = {0xe0, 29, 31, 0xe0, 157};
  feed(rightctrl, sizeof(rightctrl));
  expect(26);
  input_decode(30);
  expect('a');
  const u8 ctrlhome[] = {29, 0xe0, 71, 157};
  feed(ctrlhome, sizeof(ctrlhome));
  expect(11);
  const u8 ctrlend[] = {29, 0xe0, 79, 157};
  feed(ctrlend, sizeof(ctrlend));
  expect(12);
  const u8 delete[] = {0xe0, 83};
  feed(delete, sizeof(delete));
  expect(7);
  input_decode(56);
  input_decode(15);
  expect(31);
  input_decode(184);
  input_decode(29);
  input_decode(17);
  expect(14);
  input_decode(157);
  before = count;
  const u8 printscreen[] = {0xe0, 42, 0xe0, 55, 0xe0, 183, 0xe0, 170};
  feed(printscreen, sizeof(printscreen));
  assert(count == before);
  input_decode(30);
  expect('a');
  assert(!input_set_layout("invalid"));
  assert(input_set_layout("es"));
  assert(input_layout() == 1);
  input_decode(39);
  expect(0xf1);
  input_decode(58);
  input_decode(186);
  input_decode(39);
  expect(0xd1);
  input_decode(42);
  input_decode(39);
  expect(0xf1);
  input_decode(170);
  input_decode(58);
  input_decode(186);
  input_decode(42);
  input_decode(39);
  expect(0xd1);
  input_decode(170);
  const u8 altgr[] = {0xe0, 56, 3, 0xe0, 184};
  feed(altgr, sizeof(altgr));
  expect('@');
  input_decode(53);
  expect('-');
  input_decode(42);
  input_decode(53);
  expect('_');
  input_decode(170);
  assert(input_set_layout("de"));
  input_decode(21);
  expect('z');
  input_decode(44);
  expect('y');
  input_decode(29);
  input_decode(21);
  expect(128);
  input_decode(157);
  assert(input_set_layout("fr"));
  input_decode(16);
  expect('a');
  input_decode(30);
  expect('q');
  input_decode(29);
  input_decode(17);
  expect(128);
  input_decode(16);
  expect(137);
  input_decode(45);
  expect(138);
  input_decode(157);
  const u8 cycle[] = {29, 56, 57, 184, 157};
  feed(cycle, sizeof(cycle));
  expect(130);
  assert(input_set_layout("us"));
  input_decode(29);
  input_decode(47);
  expect(131);
  input_decode(157);
  const u8 preview_up[] = {29, 0xe0, 73, 157},
           preview_down[] = {29, 0xe0, 81, 157};
  feed(preview_up, sizeof(preview_up));
  expect(132);
  feed(preview_down, sizeof(preview_down));
  expect(133);
  input_decode(29);
  input_decode(33);
  expect(134);
  input_decode(157);
  puts("PASS: keyboard modifiers, navigation, US/ES/DE/FR maps, AltGr, logical "
       "shortcuts and cycling");
}
