#include <stdio.h>
#include <unistd.h>
#include <math.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>
#include <SDL2/SDL.h>

int verbose = 0;
SDL_Event _event;

#define STACK_SIZE 16
#define TIMER_INTERVAL ((double)1.0/60.0)
#define NOT_HEX_KEY 255
#define START_FONT_ADDR 0x0

uint8_t font[16][5] = {
  { 0xF0, 0x90, 0x90, 0x90, 0xF0 },
  { 0x20, 0x60, 0x20, 0x20, 0x70 },
  { 0xF0, 0x10, 0xF0, 0x80, 0xF0 },
  { 0xF0, 0x10, 0xF0, 0x10, 0xF0 },
  { 0x90, 0x90, 0xF0, 0x10, 0x10 },
  { 0xF0, 0x80, 0xF0, 0x10, 0xF0 },
  { 0xF0, 0x80, 0xF0, 0x90, 0xF0 },
  { 0xF0, 0x10, 0x20, 0x40, 0x40 },
  { 0xF0, 0x90, 0xF0, 0x90, 0xF0 },
  { 0xF0, 0x90, 0xF0, 0x10, 0xF0 },
  { 0xF0, 0x90, 0xF0, 0x90, 0x90 },
  { 0xE0, 0x90, 0xE0, 0x90, 0xE0 },
  { 0xF0, 0x80, 0x80, 0x80, 0xF0 },
  { 0xE0, 0x90, 0x90, 0x90, 0xE0 },
  { 0xF0, 0x80, 0xF0, 0x80, 0xF0 },
  { 0xF0, 0x80, 0xF0, 0x80, 0x80 }
};

struct chip {
  uint8_t  mem[4096];      // memory
  uint16_t st[STACK_SIZE]; // stack
  uint8_t  v[16];          // registers
  uint16_t i;              // I register
  uint16_t pc;             // program counter
  uint8_t  sp;             // stack pointer
  uint8_t  dt;             // delay timer
  uint8_t  sot;            // sound timer
  uint8_t  disp[64][32];   // display
  double   time_acc;       // time accumulator
  uint8_t  keypad[16];     // keypad state
};
typedef struct chip* Chip;

void show_chip(Chip chip) {
  printf("////// chip dump //////\n");
  printf("registers: ");
  for (int i = 0; i < 16; ++i)
    printf("v%d: %d | ", i, chip->v[i]);
  printf("i: %04X | pc: %04X | sp: %d | dt: %d, sot | %d\n", chip->i, chip->pc, chip->sp, chip->dt, chip->sot);
  printf("/////////////////\n");
}

void init_chip(Chip chip) {
  memset(chip, 0, sizeof(struct chip));
  memcpy(chip->mem + START_FONT_ADDR, font, 16 * 5);
  chip->pc = 0x200;
}

int load_program(char *file_name, Chip chip) {
  FILE *fp;
  fp = fopen(file_name, "rb");
  if (fp == NULL) {
    fprintf(stderr, "Error opening file: %s\n", strerror(errno));
    return 1;
  }

  // we load data starting at 0x200 as addresses before this
  // are not used for instructions
  while (fread(chip->mem + 0x200, 4096 - 0x200, 1, fp) > 0) { }
  return 0;
}

uint8_t key_to_hex(SDL_Keycode code) {
  // similar to octo, use keys 1-V to simulate hex keypad
  switch (code) {
    case SDLK_1: return 1;
    case SDLK_2: return 2;
    case SDLK_3: return 3;
    case SDLK_4: return 0xC;
    case SDLK_q: return 4;
    case SDLK_w: return 5;
    case SDLK_e: return 6;
    case SDLK_r: return 0xD;
    case SDLK_a: return 7;
    case SDLK_s: return 8;
    case SDLK_d: return 9;
    case SDLK_f: return 0xE;
    case SDLK_z: return 0xA;
    case SDLK_x: return 0;
    case SDLK_c: return 0xB;
    case SDLK_v: return 0xF;
    default: return NOT_HEX_KEY;
  }
}

int step_chip(Chip chip, double dt) {
  // time_acc stores the amount of time that has passed
  // since we last decremented the timers
  chip->time_acc += dt;

  // we want to decrement timers at a rate given by TIMER_INTERVAL (60hz)
  if (chip->time_acc >= TIMER_INTERVAL) {
    // this subtraction will ensure that any extra time is accounted for
    chip->time_acc -= TIMER_INTERVAL;
    if (chip->dt  > 0) --chip->dt;
    if (chip->sot > 0) --chip->sot;
  }

  // decode instruction into bytes bX and nibbles nX
  uint8_t b1 = chip->mem[chip->pc];
  uint8_t b2 = chip->mem[chip->pc + 1];
  uint8_t n1 = (b1 >> 4) & 0x0F;
  uint8_t n2 = b1 & 0x0F;
  uint8_t n3 = (b2 >> 4) & 0x0F;
  uint8_t n4 = b2 & 0x0F;
  chip->pc += 2;

  if (verbose) printf("%X%X%X%X\n", n1, n2, n3, n4);

  // execute instruction
  switch (n1) {
    case 0:
      if (n4 == 0) {
        // Clear the screen
        printf("clear the screen\n");
        memset(chip->disp, 0, 32*64);
      } else {
        // Return from a subroutine
        if (verbose) printf("return from a subroutine %d\n", chip->sp);
        --chip->sp;
        chip->pc = chip->st[chip->sp];
      }
      break;
    case 1: // 1NNN	Jump to address NNN
      if (verbose) printf("jump to address %X%X%X\n", n2, n3, n4);
      chip->pc = ((uint16_t)n2 << 8) | b2;
      break;
    case 2: // 2NNN	Execute subroutine starting at address NNN
      if (verbose) printf("execute subroutine\n");
      if (chip->sp == STACK_SIZE) {
        printf("too many subroutines\n");
        return 1;
      }
      chip->st[chip->sp] = chip->pc;
      ++chip->sp;
      chip->pc = ((uint16_t)n2 << 8) | b2;
      break;
    case 3: // 4XNN	Skip the following instruction if the value of register VX equals NN
      if (chip->v[n2] == b2) chip->pc += 2;
      break;
    case 4: // 4XNN	Skip the following instruction if the value of register VX is not equal to NN
      if (chip->v[n2] != b2) chip->pc += 2;
      break;
    case 5: // 5XY0	Skip the following instruction if the value of register VX is equal to the value of register VY
      if (chip->v[n2] == chip->v[n3]) chip->pc += 2;
      break;
    case 6: // 6XNN	Store number NN in register VX
      if (verbose) printf("store %X in V%X\n", b2, n2);
      chip->v[n2] = b2;
      break;
    case 7: // 7XNN	Add the value NN to register VX
      if (verbose) printf("add %d to register v%d=%d\n", b2, n2, chip->v[n2]);
      chip->v[n2] += b2;
      break;
    case 8:
      switch (n4) {
        case 0: // 8XY0	Store the value of register VY in register VX
          chip->v[n2] = chip->v[n3];
          break;
        case 1: // 8XY1	Set VX to VX OR VY
          chip->v[n2] = chip->v[n2] | chip->v[n3];
          break;
        case 2: // 8XY1	Set VX to VX AND VY
          chip->v[n2] = chip->v[n2] & chip->v[n3];
          break;
        case 3: // 8XY1	Set VX to VX XOR VY
          chip->v[n2] = chip->v[n2] ^ chip->v[n3];
          break;
        case 4: // 8XY4	Add the value of register VY to register VX
          chip->v[0xF] = chip->v[n2] > 255 - chip->v[n3];
          chip->v[n2] += chip->v[n3];
          break;
        case 5: // 8XY5	Subtract the value of register VY from register VX
          chip->v[0xF] = chip->v[n2] < chip->v[n3];
          chip->v[n2] -= chip->v[n3];
          break;
        case 6: // 8XY6	Store the value of register VY shifted right one bit in register 
          chip->v[0xF] = chip->v[n3] & 1;
          chip->v[n2] = chip->v[n3] >> 1;
          break;
        case 7: // 8XY7 Set register VX to the value of VY minus VX
          chip->v[0xF] = chip->v[n2] > chip->v[n3];
          chip->v[n2] = chip->v[n3] - chip->v[n2];
          break;
        case 0xE: // 8XYE Store the value of register VY shifted left one bit in register VX
          chip->v[0xF] = chip->v[n3] >> 7;
          chip->v[n2] = chip->v[n3] << 1;
          break;
        default:
          printf("can't do %X%X %X%X\n", n1, n2, n3, n4);
          return 1;
          break;
      }
      break;
    case 9: // 9XY0	Skip the following instruction if the value of register VX is not equal to the value of register VY
      if (chip->v[n2] != chip->v[n3]) chip->pc += 2;
      break;
    case 0xA:
      if (verbose) printf("store memory address %X%X%X in register I\n", n2, n3, n4);
      chip->i = ((uint16_t)n2 << 8) | b2;
      break;
    case 0xC:
      chip->v[n2] = rand() & b2;
      break;
    case 0xD:
      {
        if (verbose) printf("draw a sprite at position %d, %d with %d bytes addr %04X regs %X %X\n", chip->v[n2], chip->v[n3], n4, chip->i, n2, n3);
        uint8_t x = chip->v[n2] % 64;
        uint8_t y = chip->v[n3] % 32;
        chip->v[0xF] = 0;

        // each row is 1 byte, so the outer loop has n4 iterations
        for (int row = 0; row < n4; ++row) {
          uint8_t spriteRow = chip->mem[chip->i + row];
          for (int col = 7; col >= 0; --col) {
            // luminance is either 0 or 255
            uint8_t pixelLum = (spriteRow & 1) * 255;
            uint8_t existingLum = chip->disp[x + col][y + row];
            //printf("(%02X %02X %02X) ", existingLum, pixelLum, pixelLum ^ existingLum);
            chip->disp[x + col][y + row] = pixelLum ^ existingLum;
            spriteRow = spriteRow >> 1;
            if (pixelLum && existingLum) chip->v[0xF] = 1;
          }
        }
      }
      break;
    case 0xE:
      switch (n3) {
        case 9:   // EX9E	Skip the following instruction if the key corresponding to the hex value currently stored in register VX is pressed
          if (chip->keypad[chip->v[n2]]) chip->pc += 2;
          break;
        case 0xA: // EXA1	Skip the following instruction if the key corresponding to the hex value currently stored in register VX is not pressed
          if (!chip->keypad[chip->v[n2]]) chip->pc += 2;
          break;
      }
      break;
    case 0xF:
      switch (n3) {
        case 0:
          switch (n4) {
            case 7: // FX07	Store the current value of the delay timer in register VX
              if (verbose) printf("store delay timer in V%d\n", n2);
              chip->v[n2] = chip->dt;
              break;
            case 0xA: // FX0A	Wait for a keypress and store the result in register VX
              {
                uint8_t hex;
                printf("waiting for keypad input... ");
                do {
                  if (!SDL_WaitEvent(&_event)) {
                    printf("error from SDL_WaitEvent\n");
                    return 1;
                  }
                  if (_event.type == SDL_KEYDOWN) {
                    hex = key_to_hex(_event.key.keysym.sym);
                  }
                } while (_event.type != SDL_KEYDOWN || hex == NOT_HEX_KEY);
                printf("received %X\n", hex);
                chip->v[n2] = hex;
              }
              break;
            default:
              printf("can't do %X%X %X%X\n", n1, n2, n3, n4);
              return 1;
              break;
          }
          break;
        case 1:
          switch (n4) {
            case 5: // FX15	Set the delay timer to the value of register VX
              chip->dt = chip->v[n2];
              break;
            case 8: // FX18	Set the sound timer to the value of register VX
              chip->sot = chip->v[n2];
              break;
            case 0xE: // FX1E	Add the value stored in register VX to register I
              if (verbose) printf("increment I by v%d which has val %d\n", n2, chip->v[n2]);
              chip->i += chip->v[n2];
              break;
            default:
              printf("can't do %X%X %X%X\n", n1, n2, n3, n4);
              return 1;
              break;
          }
          break;
        case 2: // FX29	Set I to the memory address of the sprite data corresponding to the hexadecimal digit stored in register VX
          chip->i = START_FONT_ADDR + chip->v[n2] * 5;
          break;
        case 3: // FX33	Store the binary-coded decimal equivalent of the value stored in register VX at addresses I, I+1, and I+2
          {
            int r100s = chip->v[n2] / 100;
            int r10s  = (chip->v[n2] - r100s * 100) / 10;
            int r1s   = chip->v[n2] - r100s * 100 - r10s * 10;
            if (verbose) printf("fx33 val %d, store %d %d %d\n", chip->v[n2], r100s, r10s, r1s);
            chip->mem[chip->i] = r100s;
            chip->mem[chip->i + 1] = r10s;
            chip->mem[chip->i + 2] = r1s;
          }
          break;
        case 5: // FX55	Store the values of registers V0 to VX inclusive in memory starting at address I
          for (uint8_t i = 0; i <= n2; ++i) {
            chip->mem[chip->i] = chip->v[i];
            ++chip->i;
          }
          break;
        case 6: // FX65	Fill registers V0 to VX inclusive with the values stored in memory starting at address I
          for (uint8_t i = 0; i <= n2; ++i) {
            chip->v[i] = chip->mem[chip->i];
            ++chip->i;
          }
          break;
        default:
          printf("can't do %X%X %X%X\n", n1, n2, n3, n4);
          return 1;
          break;
      }
      break;
    default:
      printf("can't do %X%X %X%X\n", n1, n2, n3, n4);
      return 1;
  }
  return 0;
}

void render_chip(SDL_Renderer *ren, Chip chip) {
  // not the most efficient, just renders 1 pixel at a time
  for (int y = 0; y < 32; ++y) {
    for (int x = 0; x < 64; ++x) {
      uint8_t lum = chip->disp[x][y];
      SDL_SetRenderDrawColor(ren, lum, lum, lum, 255);
      SDL_RenderDrawPoint(ren, x, y);
    }
  }
  SDL_RenderPresent(ren);
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    printf("pass rom file name as argument\n");
    return 1;
  }
  struct chip chip;
  init_chip(&chip);
  if (0 != load_program(argv[1], &chip)) {
    printf("loading of program failed\n");
    return 1;
  }

  if (0 != SDL_Init(SDL_INIT_VIDEO)) {
    printf("SDL_Init error: %s\n", SDL_GetError());
    return 1;
  }

  SDL_Window *win = SDL_CreateWindow("chip", 10, 10, 1024, 512, SDL_WINDOW_SHOWN);
  if (NULL == win) {
    printf("SDL_Window error: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (NULL == ren){
    SDL_DestroyWindow(win);
    printf("SDL_CreateRenderer Error: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  SDL_RenderSetLogicalSize(ren, 64, 32);
  SDL_RenderClear(ren);

  // For accurate speed emulation we use two nested loops.
  // Outer loop: display refresh rate
  // Inner loop: as needed to match the desired instruction rate.
  // We draw the display in the outer loop and CPU in inner loop.
  int cycles = 0;
  uint32_t currentT = SDL_GetTicks();
  double loop_time_acc = 0;
  double clock_rate_hz = 1000;
  double cycle_time_seconds = 1 / clock_rate_hz;
  int exit_loop = 0;
  while (!exit_loop) {
    // get amount of time that has passed since last outer loop iteration
    uint32_t previousT = currentT;
    currentT = SDL_GetTicks();
    double dt = ((double)(currentT - previousT)) / 1000;

    // give our CPU some time to advance in addition to what it didn't consume last frame
    loop_time_acc += dt;

    // figure out how many cycles are "due" and run them
    int instructions = loop_time_acc * clock_rate_hz;
    //printf("dt %f acc %f instr %d\n", dt, loop_time_acc, instructions);
    //printf("time passed %f\n", ((double)(clock() - origT)) / CLOCKS_PER_SEC);
    int err = 0;
    for (int c = 0; c < instructions; ++c) {
      if (verbose) printf("cycle %d, %04X\n", cycles, chip.pc);
      err = step_chip(&chip, cycle_time_seconds);
      ++cycles;
      //printf("after cycle %d delay is %d\n", i, chip.dt);
      if (err) break;
    }
    if (err) break;

    // subtract consumed time from time accumulator
    loop_time_acc -= (double)instructions * cycle_time_seconds;

    render_chip(ren, &chip);

    while (SDL_PollEvent(&_event)) {
      if (_event.type == SDL_QUIT) {
        exit_loop = 1;
        break;
      }
      if (_event.type == SDL_KEYDOWN || _event.type == SDL_KEYUP) {
        uint8_t hex = key_to_hex(_event.key.keysym.sym);
        if (hex == NOT_HEX_KEY) {
          exit_loop = 1;
          break;
        } else {
          chip.keypad[hex] = _event.type == SDL_KEYDOWN;
        }
      }
    }
  }

  while (!SDL_PollEvent(&_event) || (_event.type != SDL_KEYDOWN
                                 && _event.type != SDL_QUIT)) SDL_Delay(10);

  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
