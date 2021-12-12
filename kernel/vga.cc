#include "kernel.hh"
#include "string.h"
#include "multiboot.hh"
#include "cmdline.hh"
#include "kstream.hh"
#include "condvar.hh"

// From http://czyborra.com/unifont
static const char* unifont[] = {
  "000000000000000000004A506A505A50499E0000000000000000000000000000",
  "0000000000000000000039924252325E0A527192000000000000000000000000",
  "000000000000000000003BA44124311809247124000000000000000000000000",
  "000000000000000000007BA44124791841247924000000000000000000000000",
  "0000000000000000000079BE42487A4842487988000000000000000000000000",
  "000000000000000000007A4C42527B5242D67A4E000000000000000000000000",
  "0000000000000000000031A44A287A304A2849A4000000000000000000000000",
  "0000000000000000000073D04A1073D04A1073DE000000000000000000000000",
  "0000000000000000000078E0450078C0442079C0000000000000000000000000",
  "0000000000000000000045F044407C4044404440000000000000000000000000",
  "0000000000000000000041F0410041F041007D00000000000000000000000000",
  "0000000000000000000045F04440444028401040000000000000000000000000",
  "000000000000000000007DF041007DF041004100000000000000000000000000",
  "000000000000000000003DE0411041E041203D10000000000000000000000000",
  "000000000000000000003CE041103910051078E0000000000000000000000000",
  "000000000000000000003DF040403840044079F0000000000000000000000000",
  "0000000000000000000072384A204A384A2073B8000000000000000000000000",
  "0000000000000000000071884A184A084A08719C000000000000000000000000",
  "0000000000000000000071984A044A084A10719C000000000000000000000000",
  "0000000000000000000071984A044A184A047198000000000000000000000000",
  "0000000000000000000071844A0C4A144A1C7184000000000000000000000000",
  "0000000000000000000049926A546A585BD44A52000000000000000000000000",
  "000000000000000000003452429A311609127112000000000000000000000000",
  "000000000000000000007BB84124793841247938000000000000000000000000",
  "00000000000000000000332444B447AC44A434A4000000000000000000000000",
  "000000000000000000007D1041B07D5041107D10000000000000000000000000",
  "000000000000000000003A5C4252325C0A52719C000000000000000000000000",
  "0000000000000000000079CE4210799040507B8E000000000000000000000000",
  "0000000000000000000079C04200798040404380000000000000000000000000",
  "0000000000000000000039C04200598048403B80000000000000000000000000",
  "0000000000000000000071C04A00718050404B80000000000000000000000000",
  "0000000000000000000049C04A00498048403380000000000000000000000000",
  "00000000000000000000000000000000",
  "00000000080808080808080008080000",
  "00002222222200000000000000000000",
  "000000001212127E24247E4848480000",
  "00000000083E4948380E09493E080000",
  "00000000314A4A340808162929460000",
  "000000001C2222221C39454246390000",
  "00000808080800000000000000000000",
  "00000004080810101010101008080400",
  "00000020101008080808080810102000",
  "00000000000008492A1C2A4908000000",
  "0000000000000808087F080808000000",
  "00000000000000000000000018080810",
  "0000000000000000007E000000000000",
  "00000000000000000000000018180000",
  "00000000020204080810102040400000",
  "00000000182442424242424224180000",
  "000000000818280808080808083E0000",
  "000000003C4242020C102040407E0000",
  "000000003C4242021C020242423C0000",
  "00000000040C142444447E0404040000",
  "000000007E4040407C020202423C0000",
  "000000001C2040407C424242423C0000",
  "000000007E0202040404080808080000",
  "000000003C4242423C424242423C0000",
  "000000003C4242423E02020204380000",
  "00000000000018180000001818000000",
  "00000000000018180000001808081000",
  "00000000000204081020100804020000",
  "000000000000007E0000007E00000000",
  "00000000004020100804081020400000",
  "000000003C4242020408080008080000",
  "000000001C224A565252524E201E0000",
  "0000000018242442427E424242420000",
  "000000007C4242427C424242427C0000",
  "000000003C42424040404042423C0000",
  "00000000784442424242424244780000",
  "000000007E4040407C404040407E0000",
  "000000007E4040407C40404040400000",
  "000000003C424240404E4242463A0000",
  "00000000424242427E42424242420000",
  "000000003E08080808080808083E0000",
  "000000001F0404040404044444380000",
  "00000000424448506060504844420000",
  "000000004040404040404040407E0000",
  "00000000424266665A5A424242420000",
  "0000000042626252524A4A4646420000",
  "000000003C42424242424242423C0000",
  "000000007C4242427C40404040400000",
  "000000003C4242424242425A663C0300",
  "000000007C4242427C48444442420000",
  "000000003C424240300C0242423C0000",
  "000000007F0808080808080808080000",
  "000000004242424242424242423C0000",
  "00000000414141222222141408080000",
  "00000000424242425A5A666642420000",
  "00000000424224241818242442420000",
  "00000000414122221408080808080000",
  "000000007E02020408102040407E0000",
  "0000000E080808080808080808080E00",
  "00000000404020101008080402020000",
  "00000070101010101010101010107000",
  "00001824420000000000000000000000",
  "00000000000000000000000000007F00",
  "00201008000000000000000000000000",
  "0000000000003C42023E4242463A0000",
  "0000004040405C6242424242625C0000",
  "0000000000003C4240404040423C0000",
  "0000000202023A4642424242463A0000",
  "0000000000003C42427E4040423C0000",
  "0000000C1010107C1010101010100000",
  "0000000000023A44444438203C42423C",
  "0000004040405C624242424242420000",
  "000000080800180808080808083E0000",
  "0000000404000C040404040404044830",
  "00000000404044485060504844420000",
  "000000001808080808080808083E0000",
  "00000000000076494949494949490000",
  "0000000000005C624242424242420000",
  "0000000000003C4242424242423C0000",
  "0000000000005C6242424242625C4040",
  "0000000000003A4642424242463A0202",
  "0000000000005C624240404040400000",
  "0000000000003C4240300C02423C0000",
  "0000000010107C1010101010100C0000",
  "000000000000424242424242463A0000",
  "00000000000042424224242418180000",
  "00000000000041494949494949360000",
  "00000000000042422418182442420000",
  "0000000000004242424242261A02023C",
  "0000000000007E0204081020407E0000",
  "0000000C101008081010080810100C00",
  "00000808080808080808080808080808",
  "00000030080810100808101008083000",
  "00000031494600000000000000000000",
  "0000000000000000000073D04A104BD04A1073DE000000000000000000000000",
};

static const char* erase[] = {
  "00000000000000000000000000000000",
  "0000000000000000000000000000000000000000000000000000000000000000",
};

static const u32 ansi_colors[16] = {
  // normal
  0x002E3436,
  0x00CC0000,
  0x004E9A06,
  0x00C4A000,
  0x003465A4,
  0x0075507B,
  0x0006989A,
  0x00D3D7CF,

  // bright
  0x00555753,
  0x00EF2929,
  0x008AE234,
  0x00FCE94F,
  0x00729FCF,
  0x00AD7FA8,
  0x0034E2E2,
  0x00EEEEEC,
};

static console_stream verbose(false);

const u16 BORDER = 4;

u32* front_buffer = nullptr;
u32* back_buffer = nullptr;
u32* graphics_buffer = nullptr;
u16 screen_width;
u16 screen_height;

u16 cursor_x = BORDER;
u16 cursor_y = BORDER;

int line[512];
int line_end = 0;

u32 vga_foreground_color = ansi_colors[15];
u32 vga_background_color = ansi_colors[0];

const size_t ESCAPE_SEQ_MAX_LEN = 16;
char ansi_escape_sequence[ESCAPE_SEQ_MAX_LEN] = { 0 };

int text_scale = 2;

bool vga_graphics_output = false;

void swap_buffers() {
  u64* back = vga_graphics_output ? (u64*)graphics_buffer : (u64*)back_buffer;
  if (back) {
    volatile u64* front = (volatile u64*)front_buffer;
    for (size_t i = 0; i < screen_width * screen_height / 2; i++)
      front[i] = back[i];
  }
}

void initvga() {
  if (!cmdline_params.use_vga) {
    verbose.println("vga: disabled by command line\n");
  } else if (multiboot.flags & MULTIBOOT_FLAG_FRAMEBUFFER) {
    verbose.println("vga: detected framebuffer at %16p [w=%d, h=%d]\n",
            multiboot.framebuffer, multiboot.framebuffer_width, multiboot.framebuffer_height);

    if(multiboot.framebuffer_width > 4096 || multiboot.framebuffer_height > 4096) {
      console.println("vga: unsupported framebuffer size\n");
      return;
    }

    screen_width = multiboot.framebuffer_pitch / 4;
    screen_height = multiboot.framebuffer_height;

    // Only clear screen on first call to initvga.
    if (front_buffer != multiboot.framebuffer) {
      front_buffer = multiboot.framebuffer;
      back_buffer = (u32*)kalloc("back_buffer", screen_width * screen_height * 4);
      graphics_buffer = (u32*)kalloc("graphics_buffer", screen_width * screen_height * 4);
      memcpy(back_buffer, front_buffer, screen_width * screen_height * 4);
      memset(graphics_buffer, 0, screen_width * screen_height * 4);
    }
  } else {
    verbose.println("vga: could not detect framebuffer\n");
  }
}

void vga_boot_animation() {
  if (front_buffer && back_buffer) {
    for (int j = 0; j < 29; j++) {
      u64 t = nsectime();
      for (int i = 0; i < screen_width * screen_height; i++) {
        for (int d = 0; d < 3; d++)
          ((u8*)&back_buffer[i])[d] = (4 * (int)((u8*)&back_buffer[i])[d] + (int)((u8*)&vga_background_color)[d]) / 5;
      }
      swap_buffers();
      u64 dt = nsectime() - t;
      if (dt < 16666667) {
        microdelay((16666667 - dt) / 1000);
      }
    }
    for (int i = 0; i < screen_width * screen_height; i++)
      back_buffer[i] = vga_background_color;
    swap_buffers();
  }
}

bool get_framebuffer(paddr* out_address, u64* out_size) {
  if (front_buffer) {
    *out_address = v2p(front_buffer);
    *out_size = 4 * screen_width * screen_height;
    return true;
  }
  return false;
}

void vgaputc(int c) {
  u32* buffer = back_buffer ? back_buffer : front_buffer;
  if (!buffer)
    return;

  if(ansi_escape_sequence[1]) {
    for (int i = 0; i < ESCAPE_SEQ_MAX_LEN; i++) {
      if (!ansi_escape_sequence[i]) {
        ansi_escape_sequence[i] = c;
        break;
      }
    }

    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
      int i = 0;
      int args[4] = { -1, -1, -1, -1 };
      char* s = ansi_escape_sequence+2;
      while (s < ansi_escape_sequence+ESCAPE_SEQ_MAX_LEN) {
        if (*s < '0' || *s > '9')
          break;

        args[i++] = strtoul(s, &s, 10);
        if (*s == ';') {
          s++;
        } else {
          break;
        }
      }

      switch(c) {
      case 'm':
        if (args[0] == -1)
          args[0] = 0;
        for (int i = 0; i < 4 && args[i] != -1; i++) {
          if (args[i] == 0) {
            vga_foreground_color = ansi_colors[15];
            vga_background_color = ansi_colors[0];
          } else if (args[i] >= 30 && args[i] < 38) {
            vga_foreground_color = ansi_colors[args[i] - 30 + 8];
          } else if (args[i] >= 40 && args[i] < 48) {
            vga_background_color = ansi_colors[args[i] - 30];
          }
        }
      }
      memset(ansi_escape_sequence, 0, ESCAPE_SEQ_MAX_LEN);
    }

    return;
  } else if (ansi_escape_sequence[0] && c == '[') {
    ansi_escape_sequence[1] = c;
    return;
  } else if (c == '\033') {
    ansi_escape_sequence[0] = c;
    return;
  }

  int scale = 3;

  if (c == '\n') {
    cursor_x = BORDER;
    cursor_y += 16 * scale;
    line_end = 0;
    return;
  }
  if (c == '\r') {
    cursor_x = BORDER;
    line_end = 0;
    return;
  }

  const char* bitmap = unifont[c & 0x7f];
  int width = bitmap[32] == '\0' ? 8 : 16;

  if (c == '\b' || c == 0x100) { // BACKSPACE
    if (line_end == 0)
      return;

    line_end -= 1;
    if ((unifont[line[line_end]])[32] == '\0') {
      cursor_x -= 8 * scale;
      bitmap = erase[0];
      width = 8;
    } else {
      cursor_x -= 16 * scale;
      bitmap = erase[1];
      width = 16;
    }
  } else {
    assert(line_end < 512);
    line[line_end++] = c;
  }

  int height = 16;
  bool full_redraw = false;

  if (cursor_x + width * scale + BORDER > screen_width) {
    cursor_x = BORDER;
    cursor_y += 16 * scale;
    line_end = 0;
  }
  while (cursor_y + height * scale + BORDER > screen_height) {
    memmove(buffer,
            buffer + 16 * scale * screen_width,
            screen_width * (screen_height - 16 * scale) * 4);
    for(int i = 0; i < 16 * scale * screen_width; i++)
      buffer[(screen_height - 16 * scale) * screen_width + i] = vga_background_color;
    cursor_y -= 16 * scale;
    full_redraw = true;
    line_end = 0;
  }

  for (int i = 0; bitmap[i]; i++) {
    u8 nibble = 0;
    if (bitmap[i] >= '0' && bitmap[i] <= '9')
      nibble = bitmap[i] - '0';
    else
      nibble = 0xA + (bitmap[i] - 'A');

    for(int j = 0; j < 4; j++) {
      int h = (i*4+j) % width;
      int k = (i*4+j) / width;
      for (int kk = 0; kk < scale; kk++)
        for (int hh = 0; hh < scale; hh++)
          buffer[(cursor_x+h*scale+hh) + (cursor_y+k*scale+kk) * screen_width] = nibble & (1<<(3-j))
            ? vga_foreground_color : vga_background_color;
    }
  }

  if (back_buffer && !vga_graphics_output) {
    if (full_redraw) {
      swap_buffers();
    } else {
      for (int y = cursor_y; y < cursor_y + height * scale; y++){
        auto front = (volatile u64*)&front_buffer[cursor_x + y * screen_width];
        auto back = (u64*)&back_buffer[cursor_x + y * screen_width];
        for (size_t i = 0; i < width * scale / 2; i++)
          front[i] = back[i];
      }
    }
  }

  if (c != '\b' && c != 0x100) { // BACKSPACE
    cursor_x += width * scale;
  }
}

void vga_put_image(u32* data, int width, int height) {
  u32* buffer = back_buffer ? back_buffer : front_buffer;
  if (!buffer)
    return;

  int original_width = width;

  if(width + BORDER * 2 > screen_width)
    width = screen_width - BORDER * 2;
  if(height + BORDER * 2 > screen_height)
    height = screen_height - BORDER * 2;

  if (cursor_x != BORDER) {
    cursor_x = BORDER;
    cursor_y += 16;
    line_end = 0;
  }
  while (cursor_y + height + BORDER > screen_height) {
    memmove(buffer,
            buffer + 16 * screen_width,
            screen_width * (screen_height - 16) * 4);
    for(int i = 0; i < 16 * screen_width; i++)
      buffer[(screen_height - 16) * screen_width + i] = vga_background_color;
    cursor_y -= 16;
    line_end = 0;
  }

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      buffer[(cursor_x+x) + (cursor_y+y) * screen_width] = data[x + y * original_width];
    }
  }

  if (back_buffer && !vga_graphics_output) {
    u64* back = (u64*)back_buffer;
    volatile u64* front = (volatile u64*)front_buffer;
    for (size_t i = 0; i < screen_width * screen_height / 2; i++)
      front[i] = back[i];
  }

  cursor_y += (height | 15) + 1;
}

u32 swap_channels(u32 c) {
  u32 r = c & 0x0000ff;
  u32 g = (c & 0x00ff00) >> 8;
  u32 b = (c & 0xff0000) >> 16;
  return (r << 16) | (g << 8) | b;
}

//SYSCALL
long
sys_vga_op(int op, userptr<void> data)
{
  ensure_secrets();
  if (op == 0)
    return screen_width;
  if (op == 1)
    return screen_height;
  if (op == 2) {
    if (!data.load_bytes(graphics_buffer, screen_width * screen_height * 4))
      return -1;

    for (int y = 0; y < screen_height; y++)
      for (int x = 0; x < screen_width; x++)
        graphics_buffer[y*screen_width+x] = swap_channels(graphics_buffer[y*screen_width+x]);

    swap_buffers();
    return 0;
  }
  
  return -1;
}
