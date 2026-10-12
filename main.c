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
#define HUD_HEIGHT      14 

// Keys (Active Low)
#define KEY_A           (1 << 0)
#define KEY_B           (1 << 1)
#define KEY_SELECT      (1 << 2)
#define KEY_START       (1 << 3)
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
#define COLOR_DARKGREEN RGB15(0, 15, 0)
#define COLOR_BLUE      RGB15(0, 15, 31)
#define COLOR_LIGHTBLUE RGB15(15, 25, 31)
#define COLOR_YELLOW    RGB15(31, 31, 0)
#define COLOR_ORANGE    RGB15(31, 15, 0)
#define COLOR_PURPLE    RGB15(18, 0, 18)
#define COLOR_DARKGRAY  RGB15(4, 4, 4)
#define COLOR_LIGHTGRAY RGB15(15, 15, 15)
#define COLOR_SKIN      RGB15(31, 22, 15)

// Game States
enum { STATE_MAP_SELECT, STATE_START, STATE_PLAY, STATE_PAUSE, STATE_GAMEOVER };

const uint16_t font[38] = {
    075557, 022222, 071747, 071717, 055711, 074717, 074757, 071111, 075757, 075717,
    025755, 065656, 074447, 065556, 074747, 074744, 074557, 055755, 072227, 031153,
    055655, 044447, 057555, 065555, 075557, 075744, 075573, 075765, 074717, 072222,
    055557, 055552, 055575, 055255, 055222, 071247, 000000, 000200
};

#define MAX_ZOMBIES 40 
#define MAX_BULLETS 20
#define MAX_DROPS 10
#define MAX_WALLS 8

typedef struct { int x, y, w, h; } Rect;
typedef struct { const char* name; int wall_count; Rect walls[MAX_WALLS]; } MapDef;

MapDef maps[4] = {
    { "FLAT", 0, {} },
    { "CORNERS", 8, {
        {30, 45, 25, 8}, {47, 20, 8, 25},   
        {185, 45, 25, 8}, {185, 20, 8, 25}, 
        {30, 115, 25, 8}, {47, 123, 8, 25}, 
        {185, 115, 25, 8}, {185, 123, 8, 25}
    }},
    { "THIRDS", 4, {
        {80, 30, 10, 46}, {80, 90, 10, 54},   
        {150, 30, 10, 46}, {150, 90, 10, 54}
    }},
    { "PILLARS", 5, {
        {45, 35, 16, 16}, {179, 35, 16, 16},
        {45, 110, 16, 16}, {179, 110, 16, 16},
        {112, 72, 16, 16}
    }}
};

typedef struct { int x, y, prev_x, prev_y, w, h, dx, dy, health, iframes, weapon, grenades; } Player;
typedef struct { int x, y, prev_x, prev_y, w, h, health, type, damage, speed_delay; bool active; } Zombie;
typedef struct { int x, y, prev_x, prev_y, dx, dy; bool active; } Bullet;
typedef struct { int x, y, timer, state; } Grenade; 
typedef struct { int x, y, type, timer; bool active; } Drop;
typedef struct { int type, value, max_value; } ActivePowerUp; // type -1 is empty

Player player;
Zombie zombies[MAX_ZOMBIES];
Bullet bullets[MAX_BULLETS];
Grenade grenade;
Drop drops[MAX_DROPS];
ActivePowerUp active_pups[3];

int kill_count_since_drop = 0;
int zombies_to_spawn, zombies_spawned, zombies_alive;
int game_state = STATE_MAP_SELECT;
int selected_map = 0;
int wave = 1;
bool force_redraw = true;

uint16_t prev_keys = 0x03FF;
uint16_t curr_keys = 0x03FF;

static inline void vsync() {
    while (REG_VCOUNT >= 160);
    while (REG_VCOUNT < 160);
}

void draw_rect(int x, int y, int w, int h, uint16_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (color == COLOR_DARKGRAY && y < HUD_HEIGHT) { int diff = HUD_HEIGHT - y; y = HUD_HEIGHT; h -= diff; }
    if (x + w > SCREEN_WIDTH) w = SCREEN_WIDTH - x;
    if (y + h > SCREEN_HEIGHT) h = SCREEN_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    for (int j = 0; j < h; j++) {
        int offset = (y + j) * SCREEN_WIDTH + x;
        for (int i = 0; i < w; i++) VRAM[offset + i] = color;
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
                if ((bits >> (12 - row * 3 + (2 - col))) & 1) draw_rect(x + col*2, y + row*2, 2, 2, color);
            }
        }
        x += 8; str++;
    }
}

static uint32_t rng_state = 123456789;
uint32_t simple_rand() {
    rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5;
    return rng_state;
}

bool check_collision(int x1, int y1, int w1, int h1, int x2, int y2, int w2, int h2) {
    return (x1 < x2 + w2 && x1 + w1 > x2 && y1 < y2 + h2 && y1 + h1 > y2);
}

bool map_collision(int x, int y, int w, int h) {
    for (int i = 0; i < maps[selected_map].wall_count; i++) {
        Rect wall = maps[selected_map].walls[i];
        if (check_collision(x, y, w, h, wall.x, wall.y, wall.w, wall.h)) return true;
    }
    return false;
}

void kill_zombie(int z_idx) {
    zombies[z_idx].active = false;
    zombies[z_idx].prev_x = zombies[z_idx].x; 
    zombies[z_idx].prev_y = zombies[z_idx].y;
    zombies_alive--;

    // Drop Logic
    kill_count_since_drop++;
    int prob = 0;
    if (kill_count_since_drop >= 20) prob = 100;
    else if (kill_count_since_drop >= 15) prob = 75;
    else if (kill_count_since_drop >= 10) prob = 50;

    if (prob > 0 && (simple_rand() % 100) < prob) {
        kill_count_since_drop = 0;
        for (int i = 0; i < MAX_DROPS; i++) {
            if (!drops[i].active) {
                drops[i].active = true;
                drops[i].x = zombies[z_idx].x + 1;
                drops[i].y = zombies[z_idx].y + 1;
                drops[i].timer = 300; // 5 Seconds
                drops[i].type = simple_rand() % 6; 
                break;
            }
        }
    }

    if (zombies[z_idx].type == 2) { 
        int spawned = 0;
        for (int new_z = 0; new_z < MAX_ZOMBIES && spawned < 4; new_z++) {
            if (!zombies[new_z].active) {
                zombies[new_z].active = true;
                zombies[new_z].type = 0; 
                zombies[new_z].health = (wave > 3) ? 2 : 1;
                zombies[new_z].damage = 1;
                zombies[new_z].speed_delay = 2;
                zombies[new_z].w = 6; zombies[new_z].h = 6;
                
                int ox = 0, oy = 0;
                if (spawned == 0) oy = -8;
                else if (spawned == 1) oy = 8;
                else if (spawned == 2) ox = -8;
                else if (spawned == 3) ox = 8;

                zombies[new_z].x = zombies[z_idx].x + ox;
                zombies[new_z].y = zombies[z_idx].y + oy;

                if (zombies[new_z].x < 0) zombies[new_z].x = 0;
                if (zombies[new_z].x > SCREEN_WIDTH - 6) zombies[new_z].x = SCREEN_WIDTH - 6;
                if (zombies[new_z].y < HUD_HEIGHT) zombies[new_z].y = HUD_HEIGHT;
                if (zombies[new_z].y > SCREEN_HEIGHT - 6) zombies[new_z].y = SCREEN_HEIGHT - 6;

                zombies[new_z].prev_x = zombies[new_z].x;
                zombies[new_z].prev_y = zombies[new_z].y;
                spawned++;
            }
        }
        zombies_to_spawn += spawned; 
        zombies_spawned += spawned;  
        zombies_alive += spawned;    
    }
}

int main(void) {
    REG_DISPCNT = MODE_3 | BG2_ENABLE;
    int shoot_cooldown = 0, zombie_move_timer = 0;

    void reset_game() {
        player = (Player){120, 130, 120, 130, 6, 6, 0, -1, 5, 0, 0, 2}; 
        grenade.state = 0;
        kill_count_since_drop = 0;
        for (int i = 0; i < MAX_ZOMBIES; i++) { zombies[i].active = false; zombies[i].prev_x = -1; }
        for (int i = 0; i < MAX_BULLETS; i++) { bullets[i].active = false; bullets[i].prev_x = -1; }
        for (int i = 0; i < MAX_DROPS; i++) { drops[i].active = false; }
        for (int i = 0; i < 3; i++) { active_pups[i].type = -1; }
        wave = 1; zombies_to_spawn = 4; zombies_spawned = 0; zombies_alive = 0;
        force_redraw = true;
    }

    reset_game();

    while (1) {
        curr_keys = ~REG_KEYINPUT & 0x03FF;
        uint16_t keys_pressed = curr_keys & ~prev_keys;
        prev_keys = curr_keys;

        // --- GAME LOGIC ---
        if (game_state == STATE_MAP_SELECT) {
            if (keys_pressed & KEY_UP) { selected_map = (selected_map - 1 + 4) % 4; force_redraw = true; }
            if (keys_pressed & KEY_DOWN) { selected_map = (selected_map + 1) % 4; force_redraw = true; }
            if (keys_pressed & KEY_A) { game_state = STATE_START; force_redraw = true; }
        }
        else if (game_state == STATE_START) {
            if (keys_pressed & KEY_START) { game_state = STATE_PLAY; reset_game(); }
        }
        else if (game_state == STATE_GAMEOVER) {
            if (keys_pressed & KEY_START) { game_state = STATE_MAP_SELECT; force_redraw = true; }
        }
        else if (game_state == STATE_PAUSE) {
            if (keys_pressed & KEY_START) { game_state = STATE_PLAY; force_redraw = true; }
            if (keys_pressed & KEY_SELECT) { game_state = STATE_MAP_SELECT; force_redraw = true; }
        }
        else { // STATE_PLAY
            if (keys_pressed & KEY_START) { game_state = STATE_PAUSE; force_redraw = true; continue; }
            if (keys_pressed & KEY_R) player.weapon = !player.weapon;

            // PowerUp Timer Logic
            for(int i = 0; i < 3; i++) {
                if (active_pups[i].type == 1 || active_pups[i].type == 3) {
                    active_pups[i].value--;
                    if (active_pups[i].value <= 0) active_pups[i].type = -1;
                }
            }

            // Status Checkers
            bool has_insta = false;
            bool has_freeze = false;
            bool has_biggun = false;
            int bg_slot = -1;
            int sh_slot = -1;
            
            for(int i=0; i<3; i++) {
                if (active_pups[i].type == 0) sh_slot = i;
                if (active_pups[i].type == 1) has_insta = true;
                if (active_pups[i].type == 2) { has_biggun = true; bg_slot = i; }
                if (active_pups[i].type == 3) has_freeze = true;
            }

            // Process Active Drops & Pickups
            for(int i=0; i<MAX_DROPS; i++) {
                if (!drops[i].active) continue;
                drops[i].timer--;
                if (drops[i].timer <= 0) {
                    drops[i].active = false;
                    draw_rect(drops[i].x, drops[i].y, 4, 4, COLOR_DARKGRAY);
                    continue;
                }
                
                if (check_collision(player.x, player.y, player.w, player.h, drops[i].x, drops[i].y, 4, 4)) {
                    drops[i].active = false;
                    draw_rect(drops[i].x, drops[i].y, 4, 4, COLOR_DARKGRAY);
                    
                    int t = drops[i].type;
                    if (t == 4) { if (player.grenades < 2) player.grenades++; }
                    else if (t == 5) { if (player.health < 5) player.health++; }
                    else {
                        // Find empty slot
                        for(int s=0; s<3; s++) {
                            if (active_pups[s].type == -1) {
                                active_pups[s].type = t;
                                if (t == 0) { active_pups[s].value = 1; active_pups[s].max_value = 1; }
                                else if (t == 1 || t == 3) { active_pups[s].value = 300; active_pups[s].max_value = 300; }
                                else if (t == 2) { active_pups[s].value = 40; active_pups[s].max_value = 40; }
                                break;
                            }
                        }
                    }
                }
            }

            if ((keys_pressed & KEY_B) && player.grenades > 0 && grenade.state == 0) {
                player.grenades--;
                grenade.state = 1; 
                grenade.timer = 180; 
                grenade.x = player.x + 2;
                grenade.y = player.y + 2;
            }

            if (grenade.state == 1) {
                grenade.timer--;
                if (grenade.timer <= 0) {
                    grenade.state = 2; 
                    grenade.timer = 10; 
                    for(int z=0; z<MAX_ZOMBIES; z++) {
                        if (zombies[z].active && check_collision(grenade.x - 30, grenade.y - 30, 60, 60, zombies[z].x, zombies[z].y, zombies[z].w, zombies[z].h)) {
                            zombies[z].health -= (has_insta ? 100 : 2);
                            if (zombies[z].health <= 0) kill_zombie(z);
                        }
                    }
                    if (player.iframes == 0 && check_collision(grenade.x - 30, grenade.y - 30, 60, 60, player.x, player.y, player.w, player.h)) {
                        if (sh_slot != -1) {
                            active_pups[sh_slot].type = -1; // Consume shield
                            player.iframes = 45;
                        } else {
                            player.health -= 1;
                            player.iframes = 45;
                            if (player.health <= 0) { game_state = STATE_GAMEOVER; force_redraw = true; }
                        }
                    }
                }
            } else if (grenade.state == 2) {
                grenade.timer--;
                if (grenade.timer <= 0) { grenade.state = 0; force_redraw = true; }
            }

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
                        
                        if (wave >= 7 && (simple_rand() % 10) == 0) {
                            zombies[i].type = 2; zombies[i].w = 8; zombies[i].h = 8;
                            zombies[i].health = 8 + wave; zombies[i].damage = 3; zombies[i].speed_delay = 5;         
                        } else if (wave >= 3 && (simple_rand() % 5) == 0) {
                            zombies[i].type = 1; zombies[i].w = 6; zombies[i].h = 6;
                            zombies[i].health = 4 + (wave/3); zombies[i].damage = 2; zombies[i].speed_delay = 3; 
                        } else {
                            zombies[i].type = 0; zombies[i].w = 6; zombies[i].h = 6;
                            zombies[i].health = (wave > 3) ? 2 : 1; zombies[i].damage = 1; zombies[i].speed_delay = 2;
                        }

                        int edge = simple_rand() % 4;
                        if (edge == 0) { zombies[i].x = simple_rand() % (SCREEN_WIDTH - zombies[i].w); zombies[i].y = HUD_HEIGHT; }
                        else if (edge == 1) { zombies[i].x = simple_rand() % (SCREEN_WIDTH - zombies[i].w); zombies[i].y = SCREEN_HEIGHT - zombies[i].h; }
                        else if (edge == 2) { zombies[i].x = 0; zombies[i].y = HUD_HEIGHT + simple_rand() % (SCREEN_HEIGHT - HUD_HEIGHT - zombies[i].h); }
                        else { zombies[i].x = SCREEN_WIDTH - zombies[i].w; zombies[i].y = HUD_HEIGHT + simple_rand() % (SCREEN_HEIGHT - HUD_HEIGHT - zombies[i].h); }

                        zombies[i].prev_x = zombies[i].x; zombies[i].prev_y = zombies[i].y;
                        zombies_spawned++; zombies_alive++;
                        break;
                    }
                }
            }

            player.prev_x = player.x; player.prev_y = player.y;
            int mx = 0, my = 0;
            if (curr_keys & KEY_LEFT)  mx = -1;
            if (curr_keys & KEY_RIGHT) mx = 1;
            if (curr_keys & KEY_UP)    my = -1;
            if (curr_keys & KEY_DOWN)  my = 1;
            
            if (mx != 0) { player.dx = mx; player.dy = 0; }
            else if (my != 0) { player.dx = 0; player.dy = my; }

            player.x += mx;
            if (player.x < 0 || player.x > SCREEN_WIDTH - 6 || map_collision(player.x, player.y, player.w, player.h)) player.x -= mx;
            
            player.y += my;
            if (player.y < HUD_HEIGHT || player.y > SCREEN_HEIGHT - 6 || map_collision(player.x, player.y, player.w, player.h)) player.y -= my;

            if (shoot_cooldown > 0) shoot_cooldown--;
            if ((curr_keys & KEY_A) && shoot_cooldown == 0) {
                if (has_biggun) {
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
                    shoot_cooldown = 6; 
                    active_pups[bg_slot].value--;
                    if (active_pups[bg_slot].value <= 0) active_pups[bg_slot].type = -1;
                }
                else if (player.weapon == 0) { 
                    for (int i = 0; i < MAX_BULLETS; i++) {
                        if (!bullets[i].active) {
                            bullets[i].active = true;
                            bullets[i].x = player.x + 2; bullets[i].y = player.y + 2;
                            bullets[i].dx = (player.dx == 0 && player.dy == 0) ? 0 : player.dx * 4;
                            bullets[i].dy = (player.dx == 0 && player.dy == 0) ? -4 : player.dy * 4;
                            shoot_cooldown = 12; break;
                        }
                    }
                } else { 
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
                bullets[i].prev_x = bullets[i].x; bullets[i].prev_y = bullets[i].y;
                bullets[i].x += bullets[i].dx; bullets[i].y += bullets[i].dy;

                if (bullets[i].x < 0 || bullets[i].x > SCREEN_WIDTH - 2 || bullets[i].y < HUD_HEIGHT || bullets[i].y > SCREEN_HEIGHT - 2 || map_collision(bullets[i].x, bullets[i].y, 2, 2)) {
                    bullets[i].active = false;
                    continue;
                }

                for (int z = 0; z < MAX_ZOMBIES; z++) {
                    if (!zombies[z].active) continue;
                    if (check_collision(bullets[i].x, bullets[i].y, 2, 2, zombies[z].x, zombies[z].y, zombies[z].w, zombies[z].h)) {
                        bullets[i].active = false;
                        zombies[z].health -= (has_insta ? 100 : 1);
                        if (zombies[z].health <= 0) kill_zombie(z);
                        break;
                    }
                }
            }

            zombie_move_timer++;
            for (int i = 0; i < MAX_ZOMBIES; i++) {
                if (!zombies[i].active) continue;
                zombies[i].prev_x = zombies[i].x; zombies[i].prev_y = zombies[i].y;

                if (!has_freeze) {
                    if (zombie_move_timer % zombies[i].speed_delay == 0) {
                        int target_x = (grenade.state == 1) ? grenade.x : player.x;
                        int target_y = (grenade.state == 1) ? grenade.y : player.y;

                        int zmx = 0, zmy = 0;
                        if (zombies[i].x < target_x) zmx = 1;
                        else if (zombies[i].x > target_x) zmx = -1;
                        if (zombies[i].y < target_y) zmy = 1;
                        else if (zombies[i].y > target_y) zmy = -1;

                        int orig_x = zombies[i].x;
                        zombies[i].x += zmx;
                        if (map_collision(zombies[i].x, zombies[i].y, zombies[i].w, zombies[i].h)) {
                            zombies[i].x = orig_x; 
                            if (zmy == 0) zmy = (target_y > zombies[i].y) ? 1 : -1; 
                        }

                        int orig_y = zombies[i].y;
                        zombies[i].y += zmy;
                        if (map_collision(zombies[i].x, zombies[i].y, zombies[i].w, zombies[i].h)) {
                            zombies[i].y = orig_y; 
                            if (zmx == 0) {
                                zmx = (target_x > zombies[i].x) ? 1 : -1;
                                zombies[i].x += zmx;
                                if (map_collision(zombies[i].x, zombies[i].y, zombies[i].w, zombies[i].h)) zombies[i].x = orig_x;
                            }
                        }
                    }

                    if (grenade.state != 1 && player.iframes == 0 && check_collision(player.x, player.y, player.w, player.h, zombies[i].x, zombies[i].y, zombies[i].w, zombies[i].h)) {
                        if (sh_slot != -1) {
                            active_pups[sh_slot].type = -1;
                            player.iframes = 45;
                        } else {
                            player.health -= zombies[i].damage;
                            player.iframes = 45; 
                            if (player.health <= 0) { game_state = STATE_GAMEOVER; force_redraw = true; }
                        }
                    }
                }
            }
        }

        // --- V-SYNC WINDOW ---
        vsync();

        // --- RENDERING LOGIC ---
        if (game_state == STATE_MAP_SELECT) {
            if (force_redraw) {
                draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BLACK);
                draw_text("SELECT MAP", 80, 20, COLOR_RED);
                for (int i = 0; i < 4; i++) {
                    draw_text(maps[i].name, 90, 60 + (i * 15), (i == selected_map) ? COLOR_YELLOW : COLOR_WHITE);
                    if (i == selected_map) draw_text(">", 75, 60 + (i * 15), COLOR_YELLOW);
                }
                force_redraw = false;
            }
        }
        else if (game_state == STATE_START) {
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
                wave_text[6] = (wave / 10) + '0'; wave_text[7] = (wave % 10) + '0';
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
                draw_text("SELECT TO QUIT", 60, 100, COLOR_WHITE);
                force_redraw = false;
            }
        }
        else { // STATE_PLAY
            if (force_redraw) {
                draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_DARKGRAY);
                draw_rect(0, 0, SCREEN_WIDTH, HUD_HEIGHT, COLOR_BLACK); 
                
                for (int i = 0; i < maps[selected_map].wall_count; i++) {
                    Rect wall = maps[selected_map].walls[i];
                    draw_rect(wall.x, wall.y, wall.w, wall.h, COLOR_LIGHTGRAY);
                }
                force_redraw = false;
            } else {
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

            // HUD Refresh
            draw_rect(4, 3, 30, 4, COLOR_RED);
            if (player.health > 0) draw_rect(4, 3, player.health * 6, 4, COLOR_GREEN);
            for (int i = 1; i < 5; i++) draw_rect(4 + (i * 6) - 1, 3, 1, 4, COLOR_BLACK);

            // PowerUp Slots 
            draw_rect(50, 2, 35, 10, COLOR_BLACK); // Clear Area
            for(int i=0; i<3; i++) {
                if (active_pups[i].type != -1) {
                    uint16_t c = COLOR_WHITE;
                    if (active_pups[i].type == 0) c = COLOR_YELLOW;
                    else if (active_pups[i].type == 2) c = COLOR_RED;
                    else if (active_pups[i].type == 3) c = COLOR_LIGHTBLUE;
                    
                    int h = (active_pups[i].value * 10) / active_pups[i].max_value;
                    if (h <= 0 && active_pups[i].value > 0) h = 1;
                    draw_rect(50 + i*10, 12 - h, 4, h, c);
                }
            }

            int gun_x = SCREEN_WIDTH / 2 - 20;
            draw_rect(gun_x, 2, 16, 6, COLOR_BLACK); 
            bool has_bg = false;
            for(int i=0; i<3; i++) if (active_pups[i].type == 2) has_bg = true;
            
            if (has_bg || player.weapon == 1) { 
                draw_rect(gun_x + 3, 3, 8, 2, has_bg ? COLOR_RED : COLOR_ORANGE); 
                draw_rect(gun_x + 3, 5, 2, 2, has_bg ? COLOR_RED : COLOR_ORANGE); 
                draw_rect(gun_x + 7, 5, 3, 1, has_bg ? COLOR_RED : COLOR_ORANGE); 
            } else { 
                draw_rect(gun_x + 6, 3, 4, 2, COLOR_WHITE); 
                draw_rect(gun_x + 6, 5, 2, 2, COLOR_WHITE); 
            } 
            
            draw_rect(130, 1, 16, 12, COLOR_BLACK); 
            if (player.grenades >= 1) {
                draw_rect(135, 8, 4, 4, COLOR_DARKGREEN);
                draw_rect(136, 7, 2, 1, COLOR_WHITE);
            }
            if (player.grenades >= 2) {
                draw_rect(135, 2, 4, 4, COLOR_DARKGREEN);
                draw_rect(136, 1, 2, 1, COLOR_WHITE);
            }
            
            draw_rect(170, 2, 70, 6, COLOR_BLACK); 
            draw_text("RND:", 170, 2, COLOR_WHITE);
            char rnd_str[3] = {(wave / 10) + '0', (wave % 10) + '0', 0};
            if (rnd_str[0] == '0') { rnd_str[0] = rnd_str[1]; rnd_str[1] = '\0'; }
            draw_text(rnd_str, 202, 2, COLOR_YELLOW);

            // Draw Drops
            for (int i=0; i<MAX_DROPS; i++) {
                if (!drops[i].active) continue;
                int dx = drops[i].x; int dy = drops[i].y;
                uint16_t col = COLOR_GREEN; // Default Health
                if (drops[i].type == 0) col = COLOR_YELLOW;
                else if (drops[i].type == 1) col = COLOR_WHITE;
                else if (drops[i].type == 2) col = COLOR_RED;
                else if (drops[i].type == 3) col = COLOR_LIGHTBLUE;
                else if (drops[i].type == 4) col = COLOR_DARKGREEN;
                
                if (drops[i].type == 0 || drops[i].type == 5) { 
                    draw_rect(dx+1, dy, 2, 4, col); draw_rect(dx, dy+1, 4, 2, col);
                } else if (drops[i].type == 1) { 
                    draw_rect(dx+1, dy, 2, 4, col); draw_rect(dx, dy+2, 4, 1, col);
                } else if (drops[i].type == 2) { 
                    draw_rect(dx, dy+1, 4, 2, col); draw_rect(dx+1, dy+3, 1, 1, col);
                } else if (drops[i].type == 3) { 
                    draw_rect(dx, dy+1, 4, 2, col);
                } else if (drops[i].type == 4) { 
                    draw_rect(dx+1, dy, 2, 4, col); draw_rect(dx, dy+1, 4, 2, col);
                }
            }

            // Active Grenade
            if (grenade.state == 1) {
                draw_rect(grenade.x, grenade.y, 2, 2, COLOR_DARKGREEN);
            } else if (grenade.state == 2) {
                draw_rect(grenade.x - 30, grenade.y - 30, 60, 60, (grenade.timer % 4 > 1) ? COLOR_ORANGE : COLOR_RED);
            }

            for (int i = 0; i < MAX_BULLETS; i++) {
                if (bullets[i].active) draw_rect(bullets[i].x, bullets[i].y, 2, 2, COLOR_WHITE);
            }
            
            for (int i = 0; i < MAX_ZOMBIES; i++) {
                if (zombies[i].active) {
                    uint16_t z_col = (zombies[i].type == 2) ? COLOR_PURPLE : ((zombies[i].type == 1) ? COLOR_ORANGE : COLOR_RED);
                    draw_rect(zombies[i].x + 1, zombies[i].y + 1, zombies[i].w - 2, zombies[i].h - 2, z_col); 
                    draw_rect(zombies[i].x + 2, zombies[i].y + 2, zombies[i].w - 4, zombies[i].h - 4, COLOR_GREEN); 
                    
                    if (player.x < zombies[i].x) { draw_rect(zombies[i].x, zombies[i].y + 1, 2, 1, z_col); draw_rect(zombies[i].x, zombies[i].y + zombies[i].h - 2, 2, 1, z_col); }
                    else if (player.x > zombies[i].x) { draw_rect(zombies[i].x + zombies[i].w - 2, zombies[i].y + 1, 2, 1, z_col); draw_rect(zombies[i].x + zombies[i].w - 2, zombies[i].y + zombies[i].h - 2, 2, 1, z_col); }
                    else if (player.y < zombies[i].y) { draw_rect(zombies[i].x + 1, zombies[i].y, 1, 2, z_col); draw_rect(zombies[i].x + zombies[i].w - 2, zombies[i].y, 1, 2, z_col); }
                    else { draw_rect(zombies[i].x + 1, zombies[i].y + zombies[i].h - 2, 1, 2, z_col); draw_rect(zombies[i].x + zombies[i].w - 2, zombies[i].y + zombies[i].h - 2, 1, 2, z_col); }
                }
            }

            uint16_t p_col = (player.iframes % 4 > 1) ? COLOR_WHITE : COLOR_BLUE;
            draw_rect(player.x + 1, player.y + 1, 4, 4, p_col); 
            draw_rect(player.x + 2, player.y + 2, 2, 2, COLOR_SKIN); 
            if (player.dx < 0) draw_rect(player.x, player.y + 2, 2, 2, COLOR_WHITE);
            else if (player.dx > 0) draw_rect(player.x + 4, player.y + 2, 2, 2, COLOR_WHITE);
            else if (player.dy < 0) draw_rect(player.x + 2, player.y, 2, 2, COLOR_WHITE);
            else draw_rect(player.x + 2, player.y + 4, 2, 2, COLOR_WHITE);
        }
    }
    return 0;
}
