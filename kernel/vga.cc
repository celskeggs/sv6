#include "kernel.hh"
#include "string.h"
#include "multiboot.hh"
#include "cmdline.hh"

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

const u16 BORDER = 4;

u32* front_buffer = nullptr;
u16 screen_width;
u16 screen_height;

u16 cursor_x = BORDER;
u16 cursor_y = BORDER;

void initvga() {
  if (!cmdline_params.use_vga) {
    cprintf("vga: disabled by command line\n");
  } else if (multiboot.flags & MULTIBOOT_FLAG_FRAMEBUFFER) {
    cprintf("vga: detected framebuffer at %16p [w=%d, h=%d]\n",
            multiboot.framebuffer, multiboot.framebuffer_width, multiboot.framebuffer_height);

    front_buffer = multiboot.framebuffer;
    screen_width = multiboot.framebuffer_width;
    screen_height = multiboot.framebuffer_height;

    for (const char *p=DEBUG?"xv6 DEBUG VGA\n":"xv6 VGA\n"; *p; p++)
      vgaputc(*p);
  } else {
    cprintf("vga: could not detect framebuffer\n");
  }
}

void vgaputc(int c) {
  if (!front_buffer)
    return;

  if (c == '\n') {
    cursor_x = BORDER;
    cursor_y += 16;
    return;
  }
  if (c == '\r') {
    cursor_x = BORDER;
    return;
  }

  const char* bitmap = unifont[c & 0x7f];
  int width = bitmap[32] == '\0' ? 8 : 16;
  int height = 16;

  if (cursor_x + width + BORDER > screen_width) {
    cursor_x = 0;
    cursor_y += 16;
  }
  while (cursor_y + height + BORDER > screen_height) {
    memmove(front_buffer,
            front_buffer + 16 * screen_width,
            screen_width * (screen_height - 16) * 4);
    memset(front_buffer + (screen_height - 16) * screen_width, 0,
           16 * screen_width * 4);
    cursor_y -= 16;
  }

  for(int i = 0; bitmap[i]; i++) {
    u8 nibble = 0;
    if (bitmap[i] >= '0' && bitmap[i] <= '9')
      nibble = bitmap[i] - '0';
    else
      nibble = 0xA + (bitmap[i] - 'A');

    for(int j = 0; j < 4; j++) {
      int h = (i*4+j) % width;
      int k = (i*4+j) / width;
      front_buffer[(cursor_x+h) + (cursor_y+k) * screen_width] = nibble & (1<<(3-j)) ? 0xffffff : 0;
    }
  }

  cursor_x += width;
}
