#ifndef RALPH_CHARACTERS_H
#define RALPH_CHARACTERS_H
#include <Arduino.h>

// Default 5x8 LCD glyphs used during boot. The animation renderer replaces these dynamically.\nstatic const uint8_t RALPH_CHARS[8][8] = {\n  {0x04,0x0E,0x15,0x15,0x11,0x0A,0x04,0x00},\n  {0x04,0x0E,0x15,0x15,0x1F,0x0A,0x04,0x00},\n  {0x04,0x0E,0x15,0x15,0x11,0x0A,0x04,0x00},\n  {0x04,0x0E,0x15,0x15,0x1F,0x0E,0x04,0x00},\n  {0x00,0x04,0x0E,0x1F,0x0E,0x04,0x00,0x00},\n  {0x00,0x04,0x0E,0x1F,0x0E,0x04,0x00,0x00},\n  {0x00,0x04,0x0A,0x1F,0x0A,0x04,0x00,0x00},\n  {0x00,0x04,0x0E,0x15,0x0E,0x04,0x00,0x00}\n};\n
// 80 real pixel-art animations. The renderer builds 5x8 LCD tiles per frame.
static constexpr uint8_t RALPH_ANIMATION_COUNT = 80;
const char* const RALPH_ANIMATION_NAMES[RALPH_ANIMATION_COUNT] = {
"blink","look_left","look_right","happy","grumpy","surprised","sleepy","yawn",
"wave","nod","shake_head","peek","hide","peek_left","peek_right","walk_left",
"walk_right","walk_home","sit","stand","hop","bounce","dance","wiggle",
"stretch","scratch","think","confused","excited","bored","laugh","cry",
"angry","calm","proud","shy","scared","suspicious","tired","wake_up",
"fall","recover","lean_left","lean_right","tilt_left","tilt_right","dizzy",
"spin","shake","brake","house_bob","house_left","house_right","door_open",
"door_close","window_left","window_right","roof_bounce","roof_wiggle",
"house_sleep","house_wake","house_shake","house_jump","house_land",
"house_tilt_left","house_tilt_right","house_flash","house_peek","ralph_at_door",
"ralph_inside","ralph_outside","ralph_roof","ralph_window","ralph_sleep_house",
"ralph_wakes_house","ralph_greets_house","ralph_checks_house","ralph_repairs",
"ralph_cleans","ralph_relaxes"
};
#endif
