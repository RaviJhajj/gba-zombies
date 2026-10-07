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

// Keys (Active Low: 0 = pressed, 1 = released)
#define KEY_A           (1 << 0)
#define KEY_B           (1 << 1)
#define KEY_RIGHT       (1 << 4)
#define KEY_LEFT        (1 << 5)
#define KEY_UP          (1 << 6)
#define KEY_DOWN        (1 << 7)

// 15-bit BGR Color Macro
#define RGB15(r, g, b)  ((uint16_t)(((r) & 0x1F) | (((g) & 0x1F) << 5) | (((b) & 0x1F) << 10)))

#define COLOR_BLACK     RGB15(0, 0, 0)
#define COLOR_WHITE     RGB15(31, 31, 31)
#define COLOR_RED       RGB15(31, 0, 0)
#define COLOR_GREEN     RGB15(0, 31, 0)
#define COLOR_BLUE      RGB15(0, 15, 31)
#define COLOR_YELLOW    RGB15(31, 31, 0)
#define COLOR_DARKGRAY  RGB15(6, 6, 6)

// --- Game Constants & Structs ---
#define MAX_ZOMBIES 20
#define MAX_BULLETS 10

typedef struct {
    int x, y;
    int prev_x, prev_y;
    int w, h;
    int dx, dy; // facing direction
    int health;
    int iframes;
} Player;

typedef struct {
    int x, y;
    int prev_x, prev_y;
    int w, h;
    int health;
    bool active;
} Zombie;

typedef struct {
    int x, y;
    int prev_x, prev_y;
    int dx, dy;
    bool active;
} Bullet;

// --- Helper Functions ---
static inline void vsync(void) {
    while (REG_VCOUNT >= 160);
    while (REG_VCOUNT < 160);
}

static inline bool key_pressed(uint16_t key) {
    return !(REG_KEYINPUT & key);
}

void draw_rect(int x, int y, int w, int h, uint16_t color) {
    for (int j = 0; j < h; j++) {
        int py = y + j;
        if (py < 0 || py >= SCREEN_HEIGHT) continue;
        for (int i = 0; i < w; i++) {
            int px = x + i;
            if (px < 0 || px >= SCREEN_WIDTH) continue;
            VRAM[py * SCREEN_WIDTH + px] = color;
        }
    }
}

// Simple deterministic random generator
static uint32_t rng_state = 123456789;
uint32_t simple_rand(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

// Check rectangular collision
bool check_collision(int x1, int y1, int w1, int h1, int x2, int y2, int w2, int h2) {
    return (x1 < x2 + w2 && x1 + w1 > x2 && y1 < y2 + h2 && y1 + h1 > y2);
}

// --- Main Program ---
int main(void) {
    // Set Video Mode 3 with Background 2 active
    REG_DISPCNT = MODE_3 | BG2_ENABLE;

    // Fill screen with dark arena floor
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        VRAM[i] = COLOR_DARKGRAY;
    }

    // Initialize Entities
    Player player = {
        .x = 120, .y = 80,
        .prev_x = 120, .prev_y = 80,
        .w = 6, .h = 6,
        .dx = 0, .dy = -1,
        .health = 5,
        .iframes = 0
    };

    Zombie zombies[MAX_ZOMBIES];
    for (int i = 0; i < MAX_ZOMBIES; i++) zombies[i].active = false;

    Bullet bullets[MAX_BULLETS];
    for (int i = 0; i < MAX_BULLETS; i++) bullets[i].active = false;

    int wave = 1;
    int zombies_to_spawn = 4;
    int zombies_spawned = 0;
    int zombies_alive = 0;
    int shoot_cooldown = 0;
    int zombie_move_timer = 0;

    while (1) {
        vsync();

        // Game Over screen
        if (player.health <= 0) {
            draw_rect(100, 75, 40, 10, COLOR_RED);
            continue;
        }

        // --- Wave Manager ---
        if (zombies_alive == 0 && zombies_spawned >= zombies_to_spawn) {
            wave++;
            zombies_to_spawn = 4 + (wave * 2);
            if (zombies_to_spawn > MAX_ZOMBIES) zombies_to_spawn = MAX_ZOMBIES;
            zombies_spawned = 0;
        }

        // Spawn a zombie if needed
        if (zombies_spawned < zombies_to_spawn && (simple_rand() % 40 == 0)) {
            for (int i = 0; i < MAX_ZOMBIES; i++) {
                if (!zombies[i].active) {
                    zombies[i].active = true;
                    zombies[i].health = (wave > 3) ? 2 : 1;
                    zombies[i].w = 6;
                    zombies[i].h = 6;

                    // Spawn on an edge
                    int edge = simple_rand() % 4;
                    if (edge == 0) { zombies[i].x = simple_rand() % SCREEN_WIDTH; zombies[i].y = 12; }
                    else if (edge == 1) { zombies[i].x = simple_rand() % SCREEN_WIDTH; zombies[i].y = SCREEN_HEIGHT - 10; }
                    else if (edge == 2) { zombies[i].x = 4; zombies[i].y = simple_rand() % SCREEN_HEIGHT; }
                    else { zombies[i].x = SCREEN_WIDTH - 10; zombies[i].y = simple_rand() % SCREEN_HEIGHT; }

                    zombies[i].prev_x = zombies[i].x;
                    zombies[i].prev_y = zombies[i].y;
                    zombies_spawned++;
                    zombies_alive++;
                    break;
                }
            }
        }

        // --- Handle Input & Player Movement ---
        player.prev_x = player.x;
        player.prev_y = player.y;

        int move_x = 0;
        int move_y = 0;

        if (key_pressed(KEY_LEFT))  { move_x = -1; player.dx = -1; player.dy = 0; }
        if (key_pressed(KEY_RIGHT)) { move_x = 1;  player.dx = 1;  player.dy = 0; }
        if (key_pressed(KEY_UP))    { move_y = -1; player.dx = 0;  player.dy = -1; }
        if (key_pressed(KEY_DOWN))  { move_y = 1;  player.dx = 0;  player.dy = 1; }

        player.x += move_x;
        player.y += move_y;

        // Arena boundary collision
        if (player.x < 4) player.x = 4;
        if (player.x > SCREEN_WIDTH - 10) player.x = SCREEN_WIDTH - 10;
        if (player.y < 12) player.y = 12;
        if (player.y > SCREEN_HEIGHT - 10) player.y = SCREEN_HEIGHT - 10;

        // Firing Mechanics (Key A)
        if (shoot_cooldown > 0) shoot_cooldown--;
        if (key_pressed(KEY_A) && shoot_cooldown == 0) {
            for (int i = 0; i < MAX_BULLETS; i++) {
                if (!bullets[i].active) {
                    bullets[i].active = true;
                    bullets[i].x = player.x + 2;
                    bullets[i].y = player.y + 2;
                    bullets[i].prev_x = bullets[i].x;
                    bullets[i].prev_y = bullets[i].y;
                    bullets[i].dx = player.dx * 3;
                    bullets[i].dy = player.dy * 3;
                    shoot_cooldown = 12; // rate of fire
                    break;
                }
            }
        }

        if (player.iframes > 0) player.iframes--;

        // --- Update Bullets ---
        for (int i = 0; i < MAX_BULLETS; i++) {
            if (!bullets[i].active) continue;

            bullets[i].prev_x = bullets[i].x;
            bullets[i].prev_y = bullets[i].y;
            bullets[i].x += bullets[i].dx;
            bullets[i].y += bullets[i].dy;

            // Remove if offscreen
            if (bullets[i].x < 2 || bullets[i].x > SCREEN_WIDTH - 4 ||
                bullets[i].y < 10 || bullets[i].y > SCREEN_HEIGHT - 4) {
                bullets[i].active = false;
                draw_rect(bullets[i].prev_x, bullets[i].prev_y, 2, 2, COLOR_DARKGRAY);
                continue;
            }

            // Bullet vs Zombie Collision
            for (int z = 0; z < MAX_ZOMBIES; z++) {
                if (!zombies[z].active) continue;
                if (check_collision(bullets[i].x, bullets[i].y, 2, 2, zombies[z].x, zombies[z].y, zombies[z].w, zombies[z].h)) {
                    bullets[i].active = false;
                    draw_rect(bullets[i].prev_x, bullets[i].prev_y, 2, 2, COLOR_DARKGRAY);
                    zombies[z].health--;
                    if (zombies[z].health <= 0) {
                        zombies[z].active = false;
                        draw_rect(zombies[z].x, zombies[z].y, zombies[z].w, zombies[z].h, COLOR_DARKGRAY);
                        zombies_alive--;
                    }
                    break;
                }
            }
        }

        // --- Update Zombies ---
        zombie_move_timer++;
        bool move_zombie_step = (zombie_move_timer % 3 == 0); // slower than player

        for (int i = 0; i < MAX_ZOMBIES; i++) {
            if (!zombies[i].active) continue;

            zombies[i].prev_x = zombies[i].x;
            zombies[i].prev_y = zombies[i].y;

            if (move_zombie_step) {
                if (zombies[i].x < player.x) zombies[i].x++;
                else if (zombies[i].x > player.x) zombies[i].x--;

                if (zombies[i].y < player.y) zombies[i].y++;
                else if (zombies[i].y > player.y) zombies[i].y--;
            }

            // Zombie hits player
            if (player.iframes == 0 && check_collision(player.x, player.y, player.w, player.h, zombies[i].x, zombies[i].y, zombies[i].w, zombies[i].h)) {
                player.health--;
                player.iframes = 30; // 0.5s invulnerability
            }
        }

        // --- Render Pass ---

        // 1. Clear old positions
        draw_rect(player.prev_x, player.prev_y, player.w, player.h, COLOR_DARKGRAY);
        for (int i = 0; i < MAX_BULLETS; i++) {
            if (bullets[i].active) draw_rect(bullets[i].prev_x, bullets[i].prev_y, 2, 2, COLOR_DARKGRAY);
        }
        for (int i = 0; i < MAX_ZOMBIES; i++) {
            if (zombies[i].active) draw_rect(zombies[i].prev_x, zombies[i].prev_y, zombies[i].w, zombies[i].h, COLOR_DARKGRAY);
        }

        // 2. Draw HUD (Health bar & Wave pips)
        draw_rect(0, 0, SCREEN_WIDTH, 10, COLOR_BLACK);
        // Player Health Bar (Green)
        draw_rect(4, 3, player.health * 6, 4, COLOR_GREEN);
        // Current Wave Indicators (Yellow dots)
        for (int w = 0; w < wave && w < 10; w++) {
            draw_rect(SCREEN_WIDTH - 12 - (w * 6), 3, 4, 4, COLOR_YELLOW);
        }

        // 3. Draw Bullets (White)
        for (int i = 0; i < MAX_BULLETS; i++) {
            if (bullets[i].active) draw_rect(bullets[i].x, bullets[i].y, 2, 2, COLOR_WHITE);
        }

        // 4. Draw Zombies (Red)
        for (int i = 0; i < MAX_ZOMBIES; i++) {
            if (zombies[i].active) draw_rect(zombies[i].x, zombies[i].y, zombies[i].w, zombies[i].h, COLOR_RED);
        }

        // 5. Draw Player (Blue; flashes white when hit)
        uint16_t player_color = (player.iframes % 4 > 1) ? COLOR_WHITE : COLOR_BLUE;
        draw_rect(player.x, player.y, player.w, player.h, player_color);
    }

    return 0;
}
