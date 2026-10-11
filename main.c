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
#define COLOR_SKIN      RGB15(31, 22, 15) // Peach color for human heads

// Game States
enum { STATE_START, STATE_PLAY, STATE_PAUSE, STATE_GAMEOVER };

// Minimal Font
const uint16_t font[38] = {
    075557, 022222, 071747, 071717, 055711, 074717, 074757, 071111, 075757, 075717,
    025755, 065656, 074447, 065556, 074747, 074744, 074557, 055755, 072227, 031153,
    055655, 044447, 057555, 065555, 075557, 075744, 075573, 075765, 074717, 072222,
    055557, 055552, 055575, 055255, 055222, 071247, 000000, 000200
};

#define MAX_ZOMBIES 25
#define MAX_BULLETS 20

typedef struct {
    int x, y, prev_x, prev_y, w, h;
    int dx, dy;
    int health, iframes;
    int weapon; 
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

int game_state = STATE_START;
int wave = 1;
bool force_redraw = true;

uint16_t prev_keys = 0x03FF;
uint16_t curr_keys = 0x03FF;

static inline void vsync() {
    while (REG_VCOUNT >= 160);
    while (REG_VCOUNT < 160);
}

// Optimized Drawing Routine with HUD Protection
void draw_rect(int x, int y, int w, int h, uint16_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    
    // Prevents the background gray from ever erasing the black HUD bar
    if (color == COLOR_DARKGRAY && y < 10) {
        int diff = 10 - y;
        y = 10;
        h -= diff;
    }

    if (x + w > SCREEN_WIDTH) w = SCREEN_WIDTH - x;
    if (y + h > SCREEN_HEIGHT) h = SCREEN_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    for (int j = 0; j < h; j++) {
        int offset = (y + j) * SCREEN_WIDTH + x;
        for (int i = 0; i < w; i++) {
            VRAM[offset + i] = color;
        }
    }
}

void draw_text(const char* str, int x, int y, uint16_t color) {
    while (*str) {
        int idx = 36;
        if (*str >= '0' && *str <= '9') idx = *str - '0';
        else if (*str >= 'A' && *str <= 'Z') idx = *str - 'A' + 10;
        
        uint16_t bits = font[idx];
        for (int row = 0; row < 5; row++) {
            for (int col = 0; col < 3; col++) {
                if ((bits >> (12 - row * 3 + (2 - col))) & 1) {
                    draw_rect(x + col*2, y + row*2, 2, 2, color);
                }
            }
        }
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

int main(void) {
    REG_DISPCNT = MODE_3 | BG2_ENABLE;

    Player player;
    Zombie zombies[MAX_ZOMBIES];
    Bullet bullets[MAX_BULLETS];

    int zombies_to_spawn, zombies_spawned, zombies_alive;
    int shoot_cooldown = 0, zombie_move_timer = 0;

    void reset_game() {
        player = (Player){120, 80, 120, 80, 6, 6, 0, -1, 5, 0, 0};
        for (int i = 0; i < MAX_ZOMBIES; i++) { zombies[i].active = false; zombies[i].prev_x = -1; }
        for (int i = 0; i < MAX_BULLETS; i++) { bullets[i].active = false; bullets[i].prev_x = -1; }
        wave = 1; zombies_to_spawn = 4; zombies_spawned = 0; zombies_alive = 0;
        force_redraw = true;
    }

    reset_game();

    while (1) {
        curr_keys = ~REG_KEYINPUT & 0x03FF;
        uint16_t keys_pressed = curr_keys & ~prev_keys;
        prev_keys = curr_keys;

        // --- GAME LOGIC ---
        if (game_state == STATE_START) {
            if (keys_pressed & KEY_START) { game_state = STATE_PLAY; reset_game(); }
        }
        else if (game_state == STATE_GAMEOVER) {
            if (keys_pressed & KEY_START) { game_state = STATE_START; force_redraw = true; }
        }
        else if (game_state == STATE_PAUSE) {
            if (keys_pressed & KEY_START) { game_state = STATE_PLAY; force_redraw = true; }
            if (keys_pressed & KEY_SELECT) { game_state = STATE_START; force_redraw = true; }
        }
        else { // STATE_PLAY
            if (keys_pressed & KEY_START) { game_state = STATE_PAUSE; force_redraw = true; continue; }
            if (keys_pressed & KEY_R) player.weapon = !player.weapon;

            if (zombies_alive == 0 && zombies_spawned >= zombies_to_spawn) {
                wave++;
                zombies_to_spawn = 4 + (wave * 2);
                if (zombies_to_spawn > MAX_ZOMBIES) zombies_to_spawn = MAX_ZOMBIES;
                zombies_spawned = 0;
            }

            if (zombies_spawned < zombies_to_spawn && (simple_rand() % 40 == 0)) {
                for (int i = 0; i < MAX_ZOMBIES; i++) {
                    if (!zombies[i].active) {
                        zombies[i].active = true;
                        zombies[i].w = 6; zombies[i].h = 6;
                        
                        if (wave >= 3 && (simple_rand() % 5) == 0) {
                            zombies[i].type = 1; 
                            zombies[i].health = 4 + (wave/3);
                            zombies[i].damage = 2;
                            zombies[i].speed_delay = 3; 
                        } else {
                            zombies[i].type = 0; 
                            zombies[i].health = (wave > 3) ? 2 : 1;
                            zombies[i].damage = 1;
                            zombies[i].speed_delay = 2;
                        }

                        int edge = simple_rand() % 4;
                        if (edge == 0) { zombies[i].x = simple_rand() % (SCREEN_WIDTH - 6); zombies[i].y = 10; }
                        else if (edge == 1) { zombies[i].x = simple_rand() % (SCREEN_WIDTH - 6); zombies[i].y = SCREEN_HEIGHT - 6; }
                        else if (edge == 2) { zombies[i].x = 0; zombies[i].y = 10 + simple_rand() % (SCREEN_HEIGHT - 16); }
                        else { zombies[i].x = SCREEN_WIDTH - 6; zombies[i].y = 10 + simple_rand() % (SCREEN_HEIGHT - 16); }

                        zombies[i].prev_x = zombies[i].x; zombies[i].prev_y = zombies[i].y;
                        zombies_spawned++; zombies_alive++;
                        break;
                    }
                }
            }

            player.prev_x = player.x; player.prev_y = player.y;
            if (curr_keys & KEY_LEFT)  { player.x--; player.dx = -1; player.dy = 0; }
            if (curr_keys & KEY_RIGHT) { player.x++; player.dx = 1;  player.dy = 0; }
            if (curr_keys & KEY_UP)    { player.y--; player.dx = 0;  player.dy = -1; }
            if (curr_keys & KEY_DOWN)  { player.y++; player.dx = 0;  player.dy = 1; }

            if (player.x < 0) player.x = 0;
            if (player.x > SCREEN_WIDTH - 6) player.x = SCREEN_WIDTH - 6;
            if (player.y < 10) player.y = 10; 
            if (player.y > SCREEN_HEIGHT - 6) player.y = SCREEN_HEIGHT - 6;

            if (shoot_cooldown > 0) shoot_cooldown--;
            if ((curr_keys & KEY_A) && shoot_cooldown == 0) {
                if (player.weapon == 0) { 
                    // Pistol
                    for (int i = 0; i < MAX_BULLETS; i++) {
                        if (!bullets[i].active) {
                            bullets[i].active = true;
                            bullets[i].x = player.x + 2; bullets[i].y = player.y + 2;
                            bullets[i].dx = player.dx * 4; bullets[i].dy = player.dy * 4;
                            shoot_cooldown = 12;
                            break;
                        }
                    }
                } else { 
                    // Shotgun (4-way blast)
                    int spawn_count = 0;
                    for (int i = 0; i < MAX_BULLETS && spawn_count < 4; i++) {
                        if (!bullets[i].active) {
                            bullets[i].active = true;
                            bullets[i].x = player.x + 2; bullets[i].y = player.y + 2;
                            
                            if (spawn_count == 0) { bullets[i].dx = 0; bullets[i].dy = -4; } 
                            else if (spawn_count == 1) { bullets[i].dx = 0; bullets[i].dy = 4; } 
                            else if (spawn_count == 2) { bullets[i].dx = -4; bullets[i].dy = 0; } 
                            else { bullets[i].dx = 4; bullets[i].dy = 0; } 
                            
                            spawn_count++;
                        }
                    }
                    shoot_cooldown = 35; 
                }
            }

            if (player.iframes > 0) player.iframes--;

            for (int i = 0; i < MAX_BULLETS; i++) {
                if (!bullets[i].active) continue;
                
                bullets[i].prev_x = bullets[i].x; 
                bullets[i].prev_y = bullets[i].y;
                
                bullets[i].x += bullets[i].dx; 
                bullets[i].y += bullets[i].dy;

                if (bullets[i].x < 0 || bullets[i].x > SCREEN_WIDTH - 2 || bullets[i].y < 10 || bullets[i].y > SCREEN_HEIGHT - 2) {
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
                            zombies[z].prev_x = zombies[z].x; 
                            zombies[z].prev_y = zombies[z].y;
                            zombies_alive--;
                        }
                        break;
                    }
                }
            }

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
                    if (player.health <= 0) { game_state = STATE_GAMEOVER; force_redraw = true; }
                }
            }
        }

        // --- V-SYNC WINDOW ---
        vsync();

        // --- RENDERING LOGIC ---
        if (game_state == STATE_START) {
            if (force_redraw) {
                draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BLACK);
                draw_text("ZOMBIE SURVIVAL", 60, 50, COLOR_RED);
                draw_text("PRESS START", 76, 80, COLOR_WHITE);
                force_redraw = false;
            }
        }
        else if (game_state == STATE_GAMEOVER) {
            if (force_redraw) {
                draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BLACK);
                draw_text("GAME OVER", 84, 40, COLOR_RED);
                
                char wave_text[] = "ROUND 00";
                wave_text[6] = (wave / 10) + '0';
                wave_text[7] = (wave % 10) + '0';
                if (wave_text[6] == '0') { wave_text[6] = wave_text[7]; wave_text[7] = '\0'; }
                draw_text(wave_text, 92, 60, COLOR_YELLOW);

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
                draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_DARKGRAY);
                draw_rect(0, 0, SCREEN_WIDTH, 10, COLOR_BLACK); 
                force_redraw = false;
            } else {
                // Erase entire 6x6 footprint securely
                draw_rect(player.prev_x, player.prev_y, player.w, player.h, COLOR_DARKGRAY);
                
                for (int i = 0; i < MAX_BULLETS; i++) {
                    if (bullets[i].prev_x != -1) {
                        draw_rect(bullets[i].prev_x, bullets[i].prev_y, 2, 2, COLOR_DARKGRAY);
                        if (!bullets[i].active) bullets[i].prev_x = -1;
                    }
                }
                for (int i = 0; i < MAX_ZOMBIES; i++) {
                    if (zombies[i].prev_x != -1) {
                        draw_rect(zombies[i].prev_x, zombies[i].prev_y, zombies[i].w, zombies[i].h, COLOR_DARKGRAY);
                        if (!zombies[i].active) zombies[i].prev_x = -1;
                    }
                }
            }

            // --- HUD REFRESH ---
            
            // 1. Segmented Health Bar
            draw_rect(4, 3, 30, 4, COLOR_RED); // Background (lost health)
            if (player.health > 0) draw_rect(4, 3, player.health * 6, 4, COLOR_GREEN); // Active health
            for (int i = 1; i < 5; i++) {
                draw_rect(4 + (i * 6) - 1, 3, 1, 4, COLOR_BLACK); // Vertical dividers
            }

            // 2. Shaped Weapon Indicator
            int gun_x = SCREEN_WIDTH / 2 - 8;
            draw_rect(gun_x, 2, 16, 6, COLOR_BLACK); // Clear previous gun area
            if (player.weapon == 0) { // Pistol Shape
                draw_rect(gun_x + 6, 3, 4, 2, COLOR_WHITE); // Barrel
                draw_rect(gun_x + 6, 5, 2, 2, COLOR_WHITE); // Grip
            } else { // Shotgun Shape
                draw_rect(gun_x + 3, 3, 8, 2, COLOR_ORANGE); // Long Barrel
                draw_rect(gun_x + 3, 5, 2, 2, COLOR_ORANGE); // Stock
                draw_rect(gun_x + 7, 5, 3, 1, COLOR_ORANGE); // Pump Handle
            }
            
            // 3. Numeric Round Counter
            draw_rect(170, 2, 70, 6, COLOR_BLACK); 
            draw_text("RND:", 180, 2, COLOR_WHITE);
            char rnd_str[3] = {(wave / 10) + '0', (wave % 10) + '0', 0};
            if (rnd_str[0] == '0') { rnd_str[0] = rnd_str[1]; rnd_str[1] = '\0'; }
            draw_text(rnd_str, 212, 2, COLOR_YELLOW);


            // --- ENTITY DRAWING ---

            for (int i = 0; i < MAX_BULLETS; i++) {
                if (bullets[i].active) draw_rect(bullets[i].x, bullets[i].y, 2, 2, COLOR_WHITE);
            }
            
            // Zombies (Top-Down Body + Head + Reaching Arms)
            for (int i = 0; i < MAX_ZOMBIES; i++) {
                if (zombies[i].active) {
                    uint16_t z_col = (zombies[i].type == 1) ? COLOR_ORANGE : COLOR_RED;
                    
                    draw_rect(zombies[i].x + 1, zombies[i].y + 1, 4, 4, z_col); // Body
                    draw_rect(zombies[i].x + 2, zombies[i].y + 2, 2, 2, COLOR_GREEN); // Zombie Head
                    
                    // Arms dynamically point toward the player
                    if (player.x < zombies[i].x) { // Reaching Left
                        draw_rect(zombies[i].x, zombies[i].y + 1, 2, 1, z_col);
                        draw_rect(zombies[i].x, zombies[i].y + 4, 2, 1, z_col);
                    } else if (player.x > zombies[i].x) { // Reaching Right
                        draw_rect(zombies[i].x + 4, zombies[i].y + 1, 2, 1, z_col);
                        draw_rect(zombies[i].x + 4, zombies[i].y + 4, 2, 1, z_col);
                    } else if (player.y < zombies[i].y) { // Reaching Up
                        draw_rect(zombies[i].x + 1, zombies[i].y, 1, 2, z_col);
                        draw_rect(zombies[i].x + 4, zombies[i].y, 1, 2, z_col);
                    } else { // Reaching Down
                        draw_rect(zombies[i].x + 1, zombies[i].y + 4, 1, 2, z_col);
                        draw_rect(zombies[i].x + 4, zombies[i].y + 4, 1, 2, z_col);
                    }
                }
            }

            // Player (Top-Down Body + Head + Gun Aiming)
            uint16_t p_col = (player.iframes % 4 > 1) ? COLOR_WHITE : COLOR_BLUE;
            draw_rect(player.x + 1, player.y + 1, 4, 4, p_col); // Body
            draw_rect(player.x + 2, player.y + 2, 2, 2, COLOR_SKIN); // Head
            
            // Gun actively points in the direction of movement
            if (player.dx < 0) { draw_rect(player.x, player.y + 2, 2, 2, COLOR_WHITE); } // Aim Left
            else if (player.dx > 0) { draw_rect(player.x + 4, player.y + 2, 2, 2, COLOR_WHITE); } // Aim Right
            else if (player.dy < 0) { draw_rect(player.x + 2, player.y, 2, 2, COLOR_WHITE); } // Aim Up
            else { draw_rect(player.x + 2, player.y + 4, 2, 2, COLOR_WHITE); } // Aim Down
        }
    }
    return 0;
}
