#include <stdint.h>
#include <stdbool.h>

// --- GBA Hardware Registers & Constants ---
#define REG_DISPCNT     *(volatile uint16_t*)0x04000000
#define REG_VCOUNT      *(volatile uint16_t*)0x04000006
#define REG_KEYINPUT    *(volatile uint16_t*)0x04000130
#define VRAM            ((volatile uint16_t*)0x06000000)

#define MODE_3          0x0003
#define BG2_ENABLE      0x0400

#define SCREEN_WIDTH    240
#define SCREEN_HEIGHT   160
#define HUD_HEIGHT      16

// Keys (Active Low)
#define KEY_A           (1 << 0)
#define KEY_START       (1 << 3)
#define KEY_SELECT      (1 << 2)
#define KEY_RIGHT       (1 << 4)
#define KEY_LEFT        (1 << 5)
#define KEY_UP          (1 << 6)
#define KEY_DOWN        (1 << 7)
#define KEY_R           (1 << 8)

// Colors
#define RGB15(r, g, b)  ((uint16_t)(((r) & 0x1F) | (((g) & 0x1F) << 5) | (((b) & 0x1F) << 10)))
#define COLOR_BLACK     RGB15(0, 0, 0)
#define COLOR_WHITE     RGB15(31, 31, 31)
#define COLOR_RED       RGB15(31, 0, 0)
#define COLOR_GREEN     RGB15(0, 31, 0)
#define COLOR_BLUE      RGB15(0, 15, 31)
#define COLOR_YELLOW    RGB15(31, 31, 0)
#define COLOR_ORANGE    RGB15(31, 15, 0)
#define COLOR_DARKGRAY  RGB15(4, 4, 4)

// Game States
enum { STATE_START, STATE_PLAY, STATE_PAUSE, STATE_GAMEOVER };

// --- Minimal 3x5 Pixel Font (Encoded in Octal) ---
const uint16_t font[38] = {
    075557, 022222, 071747, 071717, 055711, 074717, 074757, 071111, 075757, 075717, // 0-9
    025755, 065656, 074447, 065556, 074747, 074744, 074557, 055755, 072227, 031153, // A-J
    055655, 044447, 057555, 065555, 075557, 075744, 075573, 075765, 074717, 072222, // K-T
    055557, 055552, 055575, 055255, 055222, 071247, 000000, 000200                  // U-Z, Space, Period
};

// --- Entities ---
#define MAX_ZOMBIES 25
#define MAX_BULLETS 20

typedef struct {
    int x, y, prev_x, prev_y, w, h;
    int dx, dy;
    int health, iframes;
    int weapon; // 0 = Pistol, 1 = Shotgun
} Player;

typedef struct {
    int x, y, prev_x, prev_y, w, h;
    int health, type, damage, speed_delay;
    bool active;
} Zombie;

typedef struct {
    int x, y, prev_x, prev_y, dx, dy;
    bool active;
} Bullet;

// --- Globals ---
int game_state = STATE_START;
int score = 0;
int highscore = 0;
int wave = 1;
bool force_redraw = true;

uint16_t prev_keys = 0x03FF;
uint16_t curr_keys = 0x03FF;

// --- Helper Functions ---
static inline void vsync() {
    while (REG_VCOUNT >= 160);
    while (REG_VCOUNT < 160);
}

void draw_rect(int x, int y, int w, int h, uint16_t color) {
    for (int j = 0; j < h; j++) {
        if (y + j < 0 || y + j >= SCREEN_HEIGHT) continue;
        for (int i = 0; i < w; i++) {
            if (x + i < 0 || x + i >= SCREEN_WIDTH) continue;
            VRAM[(y + j) * SCREEN_WIDTH + (x + i)] = color;
        }
    }
}

void draw_char(char c, int x, int y, uint16_t color) {
    int idx = 36; // Space
    if (c >= '0' && c <= '9') idx = c - '0';
    else if (c >= 'A' && c <= 'Z') idx = c - 'A' + 10;
    else if (c == ':') idx = 37;

    uint16_t bits = font[idx];
    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 3; col++) {
            if ((bits >> (12 - row * 3 + (2 - col))) & 1) {
                draw_rect(x + col*2, y + row*2, 2, 2, color);
            }
        }
    }
}

void draw_text(const char* str, int x, int y, uint16_t color) {
    while (*str) {
        draw_char(*str, x, y, color);
        x += 8;
        str++;
    }
}

static uint32_t rng_state = 123456789;
uint32_t simple_rand() {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

bool check_collision(int x1, int y1, int w1, int h1, int x2, int y2, int w2, int h2) {
    return (x1 < x2 + w2 && x1 + w1 > x2 && y1 < y2 + h2 && y1 + h1 > y2);
}

// --- Main Program ---
int main(void) {
    REG_DISPCNT = MODE_3 | BG2_ENABLE;

    Player player;
    Zombie zombies[MAX_ZOMBIES];
    Bullet bullets[MAX_BULLETS];

    int zombies_to_spawn, zombies_spawned, zombies_alive;
    int shoot_cooldown = 0, zombie_move_timer = 0;

    void reset_game() {
        player = (Player){120, 80, 120, 80, 6, 6, 0, -1, 5, 0, 0};
        for (int i = 0; i < MAX_ZOMBIES; i++) zombies[i].active = false;
        for (int i = 0; i < MAX_BULLETS; i++) bullets[i].active = false;
        wave = 1; score = 0;
        zombies_to_spawn = 4; zombies_spawned = 0; zombies_alive = 0;
        force_redraw = true;
    }

    reset_game();

    while (1) {
        // --- 1. INPUT ---
        curr_keys = ~REG_KEYINPUT & 0x03FF;
        uint16_t keys_pressed = curr_keys & ~prev_keys;
        prev_keys = curr_keys;

        // --- 2. GAME LOGIC ---
        if (game_state == STATE_START) {
            if (keys_pressed & KEY_START) {
                game_state = STATE_PLAY;
                force_redraw = true;
                reset_game();
            }
        }
        else if (game_state == STATE_GAMEOVER) {
            if (keys_pressed & KEY_START) {
                game_state = STATE_START;
                force_redraw = true;
            }
        }
        else if (game_state == STATE_PAUSE) {
            if (keys_pressed & KEY_START) { game_state = STATE_PLAY; force_redraw = true; }
            if (keys_pressed & KEY_SELECT) { game_state = STATE_START; force_redraw = true; }
        }
        else { // STATE_PLAY
            if (keys_pressed & KEY_START) { game_state = STATE_PAUSE; force_redraw = true; continue; }

            // Weapon Switch (R Button)
            if (keys_pressed & KEY_R) player.weapon = !player.weapon;

            // Wave Manager
            if (zombies_alive == 0 && zombies_spawned >= zombies_to_spawn) {
                wave++;
                zombies_to_spawn = 4 + (wave * 2);
                if (zombies_to_spawn > MAX_ZOMBIES) zombies_to_spawn = MAX_ZOMBIES;
                zombies_spawned = 0;
            }

            // Spawner
            if (zombies_spawned < zombies_to_spawn && (simple_rand() % 40 == 0)) {
                for (int i = 0; i < MAX_ZOMBIES; i++) {
                    if (!zombies[i].active) {
                        zombies[i].active = true;
                        zombies[i].w = 6; zombies[i].h = 6;
                        
                        // Zombie Types
                        if (wave >= 3 && (simple_rand() % 5) == 0) {
                            zombies[i].type = 1; // Tank
                            zombies[i].health = 4 + (wave/3);
                            zombies[i].damage = 2;
                            zombies[i].speed_delay = 4;
                        } else {
                            zombies[i].type = 0; // Normal
                            zombies[i].health = (wave > 3) ? 2 : 1;
                            zombies[i].damage = 1;
                            zombies[i].speed_delay = 2;
                        }

                        // Spawn firmly within the arena bounds (Y >= 17)
                        int edge = simple_rand() % 4;
                        if (edge == 0) { zombies[i].x = simple_rand() % SCREEN_WIDTH; zombies[i].y = HUD_HEIGHT + 2; }
                        else if (edge == 1) { zombies[i].x = simple_rand() % SCREEN_WIDTH; zombies[i].y = SCREEN_HEIGHT - 10; }
                        else if (edge == 2) { zombies[i].x = 4; zombies[i].y = HUD_HEIGHT + (simple_rand() % (SCREEN_HEIGHT - HUD_HEIGHT)); }
                        else { zombies[i].x = SCREEN_WIDTH - 10; zombies[i].y = HUD_HEIGHT + (simple_rand() % (SCREEN_HEIGHT - HUD_HEIGHT)); }

                        zombies[i].prev_x = zombies[i].x; zombies[i].prev_y = zombies[i].y;
                        zombies_spawned++; zombies_alive++;
                        break;
                    }
                }
            }

            // Player Movement
            player.prev_x = player.x; player.prev_y = player.y;
            if (curr_keys & KEY_LEFT)  { player.x--; player.dx = -1; player.dy = 0; }
            if (curr_keys & KEY_RIGHT) { player.x++; player.dx = 1;  player.dy = 0; }
            if (curr_keys & KEY_UP)    { player.y--; player.dx = 0;  player.dy = -1; }
            if (curr_keys & KEY_DOWN)  { player.y++; player.dx = 0;  player.dy = 1; }

            // Hard boundary restricting player from going under the HUD
            if (player.x < 2) player.x = 2;
            if (player.x > SCREEN_WIDTH - 8) player.x = SCREEN_WIDTH - 8;
            if (player.y < HUD_HEIGHT + 2) player.y = HUD_HEIGHT + 2;
            if (player.y > SCREEN_HEIGHT - 8) player.y = SCREEN_HEIGHT - 8;

            // Firing Logic
            if (shoot_cooldown > 0) shoot_cooldown--;
            if ((curr_keys & KEY_A) && shoot_cooldown == 0) {
                if (player.weapon == 0) { // Pistol
                    for (int i = 0; i < MAX_BULLETS; i++) {
                        if (!bullets[i].active) {
                            bullets[i].active = true;
                            bullets[i].x = player.x + 2; bullets[i].y = player.y + 2;
                            bullets[i].dx = player.dx * 4; bullets[i].dy = player.dy * 4;
                            shoot_cooldown = 12;
                            break;
                        }
                    }
                } else { // Shotgun
                    int spawn_count = 0;
                    for (int i = 0; i < MAX_BULLETS && spawn_count < 4; i++) {
                        if (!bullets[i].active) {
                            bullets[i].active = true;
                            bullets[i].x = player.x + 2; bullets[i].y = player.y + 2;
                            
                            if (spawn_count == 0) { bullets[i].dx = player.dx*3 + player.dy; bullets[i].dy = player.dy*3 + player.dx; }
                            else if (spawn_count == 1) { bullets[i].dx = player.dx*3 - player.dy; bullets[i].dy = player.dy*3 - player.dx; }
                            else if (spawn_count == 2) { bullets[i].dx = player.dx*4; bullets[i].dy = player.dy*4; }
                            else { bullets[i].dx = player.dx*2; bullets[i].dy = player.dy*2; }
                            spawn_count++;
                        }
                    }
                    shoot_cooldown = 32;
                }
            }

            if (player.iframes > 0) player.iframes--;

            // Update Bullets
            for (int i = 0; i < MAX_BULLETS; i++) {
                if (!bullets[i].active) continue;
                bullets[i].prev_x = bullets[i].x; bullets[i].prev_y = bullets[i].y;
                bullets[i].x += bullets[i].dx; bullets[i].y += bullets[i].dy;

                // Despawn bullet if it hits the HUD boundary
                if (bullets[i].x < 0 || bullets[i].x > SCREEN_WIDTH || bullets[i].y < HUD_HEIGHT || bullets[i].y > SCREEN_HEIGHT) {
                    bullets[i].active = false;
                    continue;
                }

                for (int z = 0; z < MAX_ZOMBIES; z++) {
                    if (!zombies[z].active) continue;
                    if (check_collision(bullets[i].x, bullets[i].y, 2, 2, zombies[z].x, zombies[z].y, zombies[z].w, zombies[z].h)) {
                        bullets[i].active = false;
                        zombies[z].health--;
                        
                        if (zombies[z].health <= 0) {
                            zombies[z].active = false;
                            zombies_alive--;
                            score += (zombies[z].type == 1) ? 20 : 10;
                            if (score > highscore) highscore = score;
                        }
                        break;
                    }
                }
            }

            // Update Zombies
            zombie_move_timer++;
            for (int i = 0; i < MAX_ZOMBIES; i++) {
                if (!zombies[i].active) continue;
                zombies[i].prev_x = zombies[i].x; zombies[i].prev_y = zombies[i].y;

                if (zombie_move_timer % zombies[i].speed_delay == 0) {
                    int r = simple_rand() % 4;
                    if (r != 0 && zombies[i].x < player.x) zombies[i].x++;
                    if (r != 1 && zombies[i].x > player.x) zombies[i].x--;
                    if (r != 2 && zombies[i].y < player.y) zombies[i].y++;
                    if (r != 3 && zombies[i].y > player.y) zombies[i].y--;
                }

                if (player.iframes == 0 && check_collision(player.x, player.y, player.w, player.h, zombies[i].x, zombies[i].y, zombies[i].w, zombies[i].h)) {
                    player.health -= zombies[i].damage;
                    player.iframes = 45; 

                    if (player.health <= 0) {
                        game_state = STATE_GAMEOVER;
                        force_redraw = true;
                    }
                }
            }
        }

        // --- 3. VSYNC WAIT (Prevents Screen Tearing and Flickering) ---
        vsync();

        // --- 4. RENDER GRAPHICS ---
        if (game_state == STATE_START) {
            if (force_redraw) {
                draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BLACK);
                draw_text("ZOMBIE SURVIVAL", 60, 40, COLOR_RED);
                draw_text("PRESS START", 76, 80, COLOR_WHITE);
                force_redraw = false;
            }
        }
        else if (game_state == STATE_GAMEOVER) {
            if (force_redraw) {
                draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BLACK);
                draw_text("GAME OVER", 84, 50, COLOR_RED);
                draw_text("PRESS START", 76, 90, COLOR_WHITE);
                force_redraw = false;
            }
        }
        else if (game_state == STATE_PAUSE) {
            if (force_redraw) {
                draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BLACK);
                draw_text("PAUSED", 96, 40, COLOR_YELLOW);
                draw_text("START TO RESUME", 60, 80, COLOR_WHITE);
                draw_text("SELECT TO RESTART", 52, 100, COLOR_WHITE);
                force_redraw = false;
            }
        }
        else { // STATE_PLAY
            if (force_redraw) {
                // Redraw entire arena to clean up
                draw_rect(0, HUD_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - HUD_HEIGHT, COLOR_DARKGRAY);
                force_redraw = false;
            } else {
                // Clear only old positions to save performance
                draw_rect(player.prev_x, player.prev_y, player.w, player.h, COLOR_DARKGRAY);
                for (int i = 0; i < MAX_BULLETS; i++) {
                    if (!bullets[i].active && bullets[i].prev_x != 0) draw_rect(bullets[i].prev_x, bullets[i].prev_y, 2, 2, COLOR_DARKGRAY);
                    else if (bullets[i].active) draw_rect(bullets[i].prev_x, bullets[i].prev_y, 2, 2, COLOR_DARKGRAY);
                }
                for (int i = 0; i < MAX_ZOMBIES; i++) {
                    if (!zombies[i].active && zombies[i].prev_x != 0) draw_rect(zombies[i].prev_x, zombies[i].prev_y, zombies[i].w, zombies[i].h, COLOR_DARKGRAY);
                    else if (zombies[i].active) draw_rect(zombies[i].prev_x, zombies[i].prev_y, zombies[i].w, zombies[i].h, COLOR_DARKGRAY);
                }
            }

            // Draw HUD Base
            draw_rect(0, 0, SCREEN_WIDTH, HUD_HEIGHT, COLOR_BLACK);
            draw_rect(0, HUD_HEIGHT - 1, SCREEN_WIDTH, 1, COLOR_WHITE); // Divider line
            
            // Draw Score
            char sc_str[4] = {(score/100)%10+'0', (score/10)%10+'0', score%10+'0', 0};
            draw_text("SCR:", 2, 2, COLOR_WHITE);
            draw_text(sc_str, 34, 2, COLOR_YELLOW);

            // Draw High Score
            char hi_str[4] = {(highscore/100)%10+'0', (highscore/10)%10+'0', highscore%10+'0', 0};
            draw_text("HI:", 70, 2, COLOR_WHITE);
            draw_text(hi_str, 94, 2, COLOR_ORANGE);

            // Draw Weapon
            draw_text(player.weapon == 0 ? "PISTOL" : "SHOTGUN", 130, 2, COLOR_WHITE);

            // Draw Health Bar
            if (player.health > 0) draw_rect(SCREEN_WIDTH - (player.health * 6) - 4, 4, player.health * 6, 6, COLOR_GREEN);

            // Draw Active Entities
            for (int i = 0; i < MAX_BULLETS; i++) {
                if (bullets[i].active) draw_rect(bullets[i].x, bullets[i].y, 2, 2, COLOR_WHITE);
            }
            for (int i = 0; i < MAX_ZOMBIES; i++) {
                if (zombies[i].active) {
                    uint16_t z_color = (zombies[i].type == 1) ? COLOR_ORANGE : COLOR_RED; 
                    draw_rect(zombies[i].x, zombies[i].y, zombies[i].w, zombies[i].h, z_color);
                }
            }

            uint16_t player_color = (player.iframes % 6 > 2) ? COLOR_WHITE : COLOR_BLUE;
            draw_rect(player.x, player.y, player.w, player.h, player_color);
        }
    }
    return 0;
}
