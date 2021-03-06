#include <stdio.h>
#include <stdlib.h>

#include <SDL.h>

#include "mem.h"
#include "cpu.h"
#include "disassembler.h"

#define TITLE "Space Invaders"
#define MEM_SIZE 0x10000
#define HEIGHT 256
#define WIDTH 224
#define TIC (1000.0 / 60.0)  // Milliseconds per tic
#define CYCLES_PER_MS 2000  // 8080 runs at 2 Mhz
#define CYCLES_PER_TIC (CYCLES_PER_MS * TIC)

// Globals
mem_t *ram;

SDL_Surface *surf;
int resizef;
SDL_Window *win;
SDL_Surface *winsurf;

void die() {
    printf("Error: Unimplemented instruction: ");
    disassemble(&ram->mem[cpu.pc-1]);
    puts("");
    cpu_dump(cpu);
    exit(1);
}


void load_rom(mem_t *mem, const char *file_name) {
    // Open file
    FILE *f = fopen(file_name, "rb");
    if (!f) {
        printf("Could not open ROM: %s\n", file_name);
        exit(1);
    }

    // Get size
    fseek(f, 0, SEEK_END);
    size_t fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    // Read into buffer
    unsigned char *buffer = malloc(fsize);
    fread(buffer, fsize, 1, f);

    // Load into memory
    mem_load(mem, 0, buffer, fsize);

    // Clean up
    free(buffer);
    fclose(f);
}


void draw_video_ram() {
    uint32_t *pix = surf->pixels;

    int i = 0x2400;  // Start of Video RAM
    for (int col = 0; col < WIDTH; col ++) {
        for (int row = HEIGHT; row > 0; row -= 8) {
            for (int j = 0; j < 8; j++) {
                int idx = (row - j) * WIDTH + col;

                if (ram->mem[i] & 1 << j) {
                    pix[idx] = 0xFFFFFF;
                } else {
                    pix[idx] = 0x000000;
                }
            }

            i++;
        }
    }

    if (resizef) {
        winsurf = SDL_GetWindowSurface(win);
    }

    SDL_BlitScaled(surf, NULL, winsurf, NULL);

    // Update window
    if (SDL_UpdateWindowSurface(win)) {
        puts(SDL_GetError());
    }
}

int HandleResize(void *userdata, SDL_Event *ev) {
    if (ev->type == SDL_WINDOWEVENT) {
        if (ev->window.event == SDL_WINDOWEVENT_RESIZED) {
            resizef = 1;
        }
    }

    return 0;  // Ignored
}



void init() {
    // Init 8080
    ram = mem_new(MEM_SIZE);
    cpu.mem = ram;

    // Init SDL
    if (SDL_Init(SDL_INIT_VIDEO)) {
        printf("%s\n", SDL_GetError());
        exit(1);
    }

    // Create a window
    win = SDL_CreateWindow(
            TITLE,
            SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
            2*WIDTH, 2*HEIGHT,
            SDL_WINDOW_RESIZABLE
            );
    if (!win) {
        puts("Failed to create window");
        exit(1);
    }

    // Get surface
    winsurf = SDL_GetWindowSurface(win);
    if (!winsurf) {
        puts("Failed to get surface");
        exit(1);
    }

    // Handle resize events
    SDL_AddEventWatch(HandleResize, NULL);

    // Create backbuffer surface
    surf = SDL_CreateRGBSurface(0, WIDTH, HEIGHT, 32, 0, 0, 0, 0);
}

void handle_input() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_KEYDOWN:
                switch (ev.key.keysym.sym) {
                    case 'c':  // Insert coin
                        cpu.ports[1] |= 1;
                        break;
                    case 's':  // P1 Start
                        cpu.ports[1] |= 1 << 2;
                        break;
                    case 'w': // P1 Shoot
                        cpu.ports[1] |= 1 << 4;
                        break;
                    case 'a': // P1 Move Left
                        cpu.ports[1] |= 1 << 5;
                        break;
                    case 'd': // P1 Move Right
                        cpu.ports[1] |= 1 << 6;
                        break;
                    case SDLK_LEFT: // P2 Move Left
                        cpu.ports[2] |= 1 << 5;
                        break;
                    case SDLK_RIGHT: // P2 Move Right
                        cpu.ports[2] |= 1 << 6;
                        break;
                    case SDLK_RETURN: // P2 Start
                        cpu.ports[1] |= 1 << 1;
                        break;
                    case SDLK_UP: // P2 Shoot
                        cpu.ports[2] |= 1 << 4;
                        break;
                }
                break;

            case SDL_KEYUP:
                switch (ev.key.keysym.sym) {
                    case 'c': // Insert coin
                        cpu.ports[1] &= ~1;
                        break;
                    case 's': // P1 Start
                        cpu.ports[1] &= ~(1 << 2);
                        break;
                    case 'w': // P1 shoot
                        cpu.ports[1] &= ~(1 << 4);
                        break;
                    case 'a': // P1 Move left
                        cpu.ports[1] &= ~(1 << 5);
                        break;
                    case 'd': // P1 Move Right
                        cpu.ports[1] &= ~(1 << 6);
                        break;
                    case SDLK_LEFT: // P2 Move Left
                        cpu.ports[2] &= ~(1 << 5);
                        break;
                    case SDLK_RIGHT: // P2 Move Right
                        cpu.ports[2] &= ~(1 << 6);
                        break;
                    case SDLK_RETURN: // P2 Start
                        cpu.ports[1] &= ~(1 << 1);
                        break;
                    case SDLK_UP: // P2 Shoot
                        cpu.ports[2] &= ~(1 << 4);
                        break;

                    case 'q':  // Quit
                        exit(0);
                        break;
                }
                break;

            case SDL_QUIT:
                exit(0);
                break;
        }
    }
}


void generate_interrupt(uint16_t addr) {
    cpu_push(cpu.pc);
    cpu.pc = addr;
    cpu.flags.i = 0;
}


void emulate_shift_register() {
    static uint16_t shift_register;
    static int shift_amount;

    if (cpu.ir == 0xd3) { // OUT
        if (ram->mem[cpu.pc] == 2) { // Set shift amount
            shift_amount = cpu.a;
        } else if (ram->mem[cpu.pc] == 4) { // Set data in shift register
            shift_register = (cpu.a << 8) | (shift_register >> 8);
        }
    } else if (cpu.ir == 0xdb) { // IN
        if (ram->mem[cpu.pc] == 3) { // Shift and read data
            cpu.a = shift_register >> (8 - shift_amount);
        }
    }
}


void cpu_run(long cycles) {
    int i = 0;
    while (i < cycles) {
        cpu_fetch();

        emulate_shift_register();

        int c;
        if ((c = cpu_run_instruction())) {
            i += c;
        } else {
            die();
        }
    }
}


int main() {
    init();  // Init 8080 and SDL
    load_rom(ram, "invaders.rom");

    uint32_t last_tic = SDL_GetTicks();  // milliseconds
    while (1) {
        if ((SDL_GetTicks() - last_tic) >= TIC) {
            last_tic = SDL_GetTicks();

            cpu_run(CYCLES_PER_TIC / 2);

            if (cpu.flags.i) {
                generate_interrupt(0x08);
            }

            cpu_run(CYCLES_PER_TIC / 2);

            handle_input();
            draw_video_ram();

            if (cpu.flags.i) {
                generate_interrupt(0x10);
            }

            if (SDL_GetTicks() - last_tic > TIC) {
                puts("Too slow!");
            }
        }
    }

    return 0;
}
