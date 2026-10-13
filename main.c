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
#define COLOR_DARKBLUE  RGB15(0, 0, 15)
#define COLOR_LIGHTBLUE RGB15(15, 25, 31)
#define COLOR_CYAN      RGB15(0, 31, 31)
#define COLOR_YELLOW    RGB15(31, 31, 0)
#define COLOR_ORANGE    RGB15(31, 15, 0)
#define COLOR_PURPLE    RGB15(18, 0, 18)
#define COLOR_DARKGRAY  RGB15(4, 4, 4)
#define COLOR_LIGHTGRAY RGB15(15, 15, 15)
#define COLOR_SKIN      RGB15(31, 22, 15)

// Game States
enum { STATE_HOME, STATE_MAP_SELECT, STATE_LOADOUT_SELECT, STATE_START, STATE_PLAY, STATE_PAUSE, STATE_GAMEOVER };

const uint16_t font[38] = {
    075557, 022222, 071747, 071717, 055711, 074717, 074757, 071111, 075757, 075717,
    025755, 065656, 074447, 065556, 074747, 074744, 074557, 055755, 072227, 031153,
    055655, 044447, 057555, 065555, 075557, 075744, 075573, 075765, 074717, 072222,
    055557, 055552, 055575, 055255, 055222, 071247, 000000, 000200
};

#define MAX_ZOMBIES 40 
#define MAX_BULLETS 25
#define MAX_DROPS 10
#define MAX_WALLS 12

typedef struct { int x, y, w, h; } Rect;
typedef struct { const char* name; int wall_count; Rect walls[MAX_WALLS]; } MapDef;

MapDef maps[9] = {
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
    }},
    { "BOX", 12, {
        {40, 35, 30, 8}, {105, 35, 30, 8}, {170, 35, 30, 8},
        {40, 122, 30, 8}, {105, 122, 30, 8}, {170, 122, 30, 8},
        {40, 35, 8, 25}, {40, 75, 8, 25}, {40, 115, 8, 15},
        {192, 35, 8, 25}, {192, 75, 8, 25}, {192, 115, 8, 15}
    }},
    { "HOUSE", 6, {
        {70, 40, 90, 8}, {174, 40, 6, 8},  
        {70, 112, 6, 8}, {90, 112, 90, 8}, 
        {70, 48, 8, 64}, {172, 48, 8, 64}  
    }},
    { "SNAKE", 6, {
        {40, 30, 160, 10},         
        {40, 80, 50, 10},          
        {150, 80, 50, 10},         
        {40, 130, 160, 10},        
        {40, 40, 10, 40},          
        {190, 90, 10, 40}          
    }},
    { "GREAT WALL", 1, {
        {30, 80, 180, 10}
    }},
    { "BLOCK", 1, {
        {90, 55, 60, 60}
    }}
};

const char* loadouts[5] = { "OPERATOR", "ASSAULT", "ASSASSIN", "NINJA", "SNIPER" };

typedef struct { int x, y, prev_x, prev_y, w, h, dx, dy, health, iframes, weapon, grenades, ar_heat; } Player;
typedef struct { int x, y, prev_x, prev_y, w, h, health, type, damage, speed_delay; bool active, slowed; } Zombie;
typedef struct { int x, y, prev_x, prev_y, dx, dy, w, h, life, dmg; bool active, pierce; } Bullet;
typedef struct { int x, y, timer, state; } Grenade; // 0=off, 1=ticking, 2=exploding, 3=armed trap
typedef struct { int x, y, type, timer; bool active; } Drop;
typedef struct { int type, value, max_value; } ActivePowerUp; 

Player player;
Zombie zombies[MAX_ZOMBIES];
Bullet bullets[MAX_BULLETS];
Grenade grenade;
Drop drops[MAX_DROPS];
ActivePowerUp active_pups[3];

int kill_count_since_drop = 0;
int zombies_to_spawn, zombies_spawned, zombies_alive;
int game_state = STATE_HOME;
int selected_map = 0;
int selected_loadout = 0;
int wave = 1;
bool force_redraw = true;
bool nuke_active = false;

uint16_t prev_keys = 0x03FF;
uint16_t curr_keys = 0x03FF;

static inline void vsync() {
    while (REG_VCOUNT >= 160);
    while (REG_VCOUNT < 160);
}

void draw_rect(int x, int y, int w, int h, uint16_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_WIDTH) w = SCREEN_WIDTH - x;
    if (y + h > SCREEN_HEIGHT) h = SCREEN_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    for (int j = 0; j < h; j++) {
        int offset = (y + j) * SCREEN_WIDTH + x;
        for (int i = 0; i < w; i++) VRAM[offset + i] = color;
    }
}

void draw_arena_rect(int x, int y, int w, int h, uint16_t color) {
    if (y < HUD_HEIGHT) { int diff = HUD_HEIGHT - y; y = HUD_HEIGHT; h -= diff; }
    if (h <= 0) return;
    draw_rect(x, y, w, h, color);
}

void draw_text(const char* str, int x, int y, uint16_t color, int scale) {
    while (*str) {
        int idx = 36;
        if (*str >= '0' && *str <= '9') idx = *str - '0';
        else if (*str >= 'A' && *str <= 'Z') idx = *str - 'A' + 10;
        else if (*str == ':') idx = 37;
        
        uint16_t bits = font[idx];
        for (int row = 0; row < 5; row++) {
            for (int col = 0; col < 3; col++) {
                if ((bits >> (12 - row * 3 + (2 - col))) & 1) {
                    draw_rect(x + col*2*scale, y + row*2*scale, 2*scale, 2*scale, color);
                }
            }
        }
        x += 8 * scale; str++;
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

void spawn_bullet(int x, int y, int dx, int dy, int life, int dmg, int w, int h, bool pierce) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) {
            bullets[i].active = true;
            bullets[i].x = x; bullets[i].y = y;
            bullets[i].dx = dx; bullets[i].dy = dy;
            bullets[i].life = life; bullets[i].dmg = dmg;
            bullets[i].w = w; bullets[i].h = h;
            bullets[i].pierce = pierce;
            bullets[i].prev_x = -1;
            break;
        }
    }
}

void kill_zombie(int z_idx) {
    zombies[z_idx].active = false;
    draw_arena_rect(zombies[z_idx].prev_x, zombies[z_idx].prev_y, zombies[z_idx].w, zombies[z_idx].h, COLOR_DARKGRAY);
    draw_arena_rect(zombies[z_idx].x, zombies[z_idx].y, zombies[z_idx].w, zombies[z_idx].h, COLOR_DARKGRAY);
    zombies_alive--;

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
                drops[i].timer = 300; 
                int roll = simple_rand() % 13;
                drops[i].type = roll / 2; 
                break;
            }
        }
    }

    if (zombies[z_idx].type == 2 && !nuke_active) { 
        int spawned = 0;
        for (int new_z = 0; new_z < MAX_ZOMBIES && spawned < 4; new_z++) {
            if (!zombies[new_z].active && new_z != z_idx) {
                zombies[new_z].active = true;
                zombies[new_z].type = 0; 
                zombies[new_z].health = (wave > 3) ? 2 : 1;
                zombies[new_z].damage = 1;
                zombies[new_z].speed_delay = 2;
                zombies[new_z].slowed = false;
                zombies[new_z].w = 6; zombies[new_z].h = 6;
                
                int ox = 0, oy = 0;
                if (spawned == 0) { ox = 0; oy = 0; }
                else if (spawned == 1) { ox = 2; oy = 0; }
                else if (spawned == 2) { ox = 0; oy = 2; }
                else if (spawned == 3) { ox = 2; oy = 2; }

                zombies[new_z].x = zombies[z_idx].x + ox;
                zombies[new_z].y = zombies[z_idx].y + oy;
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
        // Updated starting Y position to 142 to avoid spawning inside bottom walls on the Snake map
        player = (Player){120, 142, 120, 142, 6, 6, 0, -1, 5, 0, 0, 2, 0}; 
        grenade.state = 0;
        kill_count_since_drop = 0;
        for (int i = 0; i < MAX_ZOMBIES; i++) { zombies[i].active = false; zombies[i].prev_x = -1; zombies[i].slowed = false; }
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
        if (game_state == STATE_HOME) {
            if (keys_pressed & KEY_START) { game_state = STATE_MAP_SELECT; force_redraw = true; }
        }
        else if (game_state == STATE_MAP_SELECT) {
            if (keys_pressed & KEY_UP) { selected_map = (selected_map - 1 + 9) % 9; force_redraw = true; }
            if (keys_pressed & KEY_DOWN) { selected_map = (selected_map + 1) % 9; force_redraw = true; }
            if (keys_pressed & KEY_A) { game_state = STATE_LOADOUT_SELECT; force_redraw = true; }
            if (keys_pressed & KEY_B) { game_state = STATE_HOME; force_redraw = true; }
        }
        else if (game_state == STATE_LOADOUT_SELECT) {
            if (keys_pressed & KEY_UP) { selected_loadout = (selected_loadout - 1 + 5) % 5; force_redraw = true; }
            if (keys_pressed & KEY_DOWN) { selected_loadout = (selected_loadout + 1) % 5; force_redraw = true; }
            if (keys_pressed & KEY_A) { game_state = STATE_START; force_redraw = true; }
            if (keys_pressed & KEY_B) { game_state = STATE_MAP_SELECT; force_redraw = true; }
        }
        else if (game_state == STATE_START) {
            if (keys_pressed & KEY_START) { game_state = STATE_PLAY; reset_game(); }
            if (keys_pressed & KEY_B) { game_state = STATE_LOADOUT_SELECT; force_redraw = true; }
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

            if (!(curr_keys & KEY_A) && player.ar_heat > 0) player.ar_heat -= 2;
            if (player.ar_heat < 0) player.ar_heat = 0;

            for(int i = 0; i < 3; i++) {
                if (active_pups[i].type == 1 || active_pups[i].type == 3) {
                    active_pups[i].value--;
                    if (active_pups[i].value <= 0) active_pups[i].type = -1;
                }
            }

            bool has_insta = false, has_freeze = false, has_biggun = false;
            int bg_slot = -1, sh_slot = -1;
            
            for(int i=0; i<3; i++) {
                if (active_pups[i].type == 0) sh_slot = i;
                if (active_pups[i].type == 1) has_insta = true;
                if (active_pups[i].type == 2) { has_biggun = true; bg_slot = i; }
                if (active_pups[i].type == 3) has_freeze = true;
            }

            for(int i=0; i<MAX_DROPS; i++) {
                if (!drops[i].active) continue;
                drops[i].timer--;
                if (drops[i].timer <= 0) {
                    drops[i].active = false;
                    draw_arena_rect(drops[i].x, drops[i].y, 6, 6, COLOR_DARKGRAY);
                    continue;
                }
                
                if (check_collision(player.x, player.y, player.w, player.h, drops[i].x, drops[i].y, 6, 6)) {
                    drops[i].active = false;
                    draw_arena_rect(drops[i].x, drops[i].y, 6, 6, COLOR_DARKGRAY);
                    
                    int t = drops[i].type;
                    if (t == 4) { if (player.grenades < 2) player.grenades++; }
                    else if (t == 5) { if (player.health < 5) player.health++; }
                    else if (t == 6) { 
                        draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_WHITE);
                        nuke_active = true;
                        for (int z = 0; z < MAX_ZOMBIES; z++) {
                            if (zombies[z].active) { zombies[z].health = 0; kill_zombie(z); }
                        }
                        nuke_active = false; force_redraw = true; 
                    }
                    else {
                        for(int s=0; s<3; s++) {
                            if (active_pups[s].type == -1) {
                                active_pups[s].type = t;
                                if (t == 0) { active_pups[s].value = 1; active_pups[s].max_value = 1; }
                                else if (t == 1 || t == 3) { active_pups[s].value = 300; active_pups[s].max_value = 300; }
                                else if (t == 2) { active_pups[s].value = 160; active_pups[s].max_value = 160; }
                                break;
                            }
                        }
                    }
                }
            }

            // GRENADE USE LOGIC
            if ((keys_pressed & KEY_B) && player.grenades > 0 && grenade.state == 0) {
                player.grenades--;
                if (selected_loadout == 2) { // Assassin
                    grenade.state = 2; grenade.timer = 10;
                    grenade.x = player.x + 2; grenade.y = player.y + 2;
                    for (int z = 0; z < MAX_ZOMBIES; z++) {
                        if (zombies[z].active && !zombies[z].slowed) {
                            zombies[z].speed_delay *= 2; zombies[z].slowed = true;
                        }
                    }
                } else if (selected_loadout == 3) { // Ninja Smoke
                    grenade.state = 2; grenade.timer = 15;
                    grenade.x = player.x + 2; grenade.y = player.y + 2;
                    player.iframes = 180;
                } else if (selected_loadout == 4) { // Sniper Claymore
                    grenade.state = 3; grenade.x = player.x + 1; grenade.y = player.y + 1;
                } else { // Operator / Assault
                    grenade.state = 1; grenade.timer = 180; 
                    grenade.x = player.x + 2; grenade.y = player.y + 2;
                }
            }

            // Claymore Trigger Logic
            if (grenade.state == 3) {
                for(int z=0; z<MAX_ZOMBIES; z++) {
                    if (zombies[z].active && check_collision(grenade.x, grenade.y, 4, 4, zombies[z].x, zombies[z].y, zombies[z].w, zombies[z].h)) {
                        grenade.state = 2; grenade.timer = 10; break;
                    }
                }
            }

            // Explosion execution
            if (grenade.state == 1) {
                grenade.timer--;
                if (grenade.timer <= 0) {
                    grenade.state = 2; grenade.timer = 10; 
                    int r = (selected_loadout == 1) ? 80 : 60; 
                    for(int z=0; z<MAX_ZOMBIES; z++) {
                        if (zombies[z].active && check_collision(grenade.x - (r/2), grenade.y - (r/2), r, r, zombies[z].x, zombies[z].y, zombies[z].w, zombies[z].h)) {
                            zombies[z].health -= (has_insta ? 100 : 2);
                            if (zombies[z].health <= 0) kill_zombie(z);
                        }
                    }
                    if (player.iframes == 0 && check_collision(grenade.x - (r/2), grenade.y - (r/2), r, r, player.x, player.y, player.w, player.h)) {
                        if (sh_slot != -1) { active_pups[sh_slot].type = -1; player.iframes = 45; } 
                        else { player.health -= 1; player.iframes = 45; if (player.health <= 0) { game_state = STATE_GAMEOVER; force_redraw = true; } }
                    }
                }
            } else if (grenade.state == 2) {
                grenade.timer--;
                if (grenade.timer <= 0) { grenade.state = 0; force_redraw = true; }
                else if (grenade.timer == 8 && selected_loadout == 4) {
                    // Sniper explosion damage frame
                    for(int z=0; z<MAX_ZOMBIES; z++) {
                        if (zombies[z].active && check_collision(grenade.x - 15, grenade.y - 15, 30, 30, zombies[z].x, zombies[z].y, zombies[z].w, zombies[z].h)) {
                            zombies[z].health -= (has_insta ? 100 : 15);
                            if (zombies[z].health <= 0) kill_zombie(z);
                        }
                    }
                    if (player.iframes == 0 && check_collision(grenade.x - 15, grenade.y - 15, 30, 30, player.x, player.y, player.w, player.h)) {
                        if (sh_slot != -1) { active_pups[sh_slot].type = -1; player.iframes = 45; } 
                        else { player.health -= 1; player.iframes = 45; if (player.health <= 0) { game_state = STATE_GAMEOVER; force_redraw = true; } }
                    }
                }
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
                        zombies[i].slowed = false;
                        
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

                        if (has_freeze) zombies[i].speed_delay *= 2; 

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
                int bx = player.x + 2; int by = player.y + 2;
                int pdx = (player.dx == 0 && player.dy == 0) ? 0 : player.dx;
                int pdy = (player.dx == 0 && player.dy == 0) ? -1 : player.dy;

                if (has_biggun) {
                    spawn_bullet(bx, by, 0, -4, 100, 1, 2, 2, false); spawn_bullet(bx, by, 0, 4, 100, 1, 2, 2, false);
                    spawn_bullet(bx, by, -4, 0, 100, 1, 2, 2, false); spawn_bullet(bx, by, 4, 0, 100, 1, 2, 2, false);
                    shoot_cooldown = 6; 
                    active_pups[bg_slot].value -= 4;
                    if (active_pups[bg_slot].value <= 0) active_pups[bg_slot].type = -1;
                } else {
                    if (selected_loadout == 0) { 
                        if (player.weapon == 0) { spawn_bullet(bx, by, pdx*4, pdy*4, 100, 1, 2, 2, false); shoot_cooldown = 12; } 
                        else { 
                            spawn_bullet(bx, by, pdx*4, pdy*4, 100, 1, 2, 2, false); spawn_bullet(bx, by, pdx*3 + pdy, pdy*3 + pdx, 100, 1, 2, 2, false);
                            spawn_bullet(bx, by, pdx*3 - pdy, pdy*3 - pdx, 100, 1, 2, 2, false); spawn_bullet(bx, by, pdx*2, pdy*2, 100, 1, 2, 2, false);
                            shoot_cooldown = 35; 
                        }
                    } else if (selected_loadout == 1) { 
                        if (player.weapon == 0) { 
                            player.ar_heat += 6;
                            if (player.ar_heat >= 120) { shoot_cooldown = 42; player.ar_heat = 0; }
                            else { spawn_bullet(bx, by, pdx*4, pdy*4, 100, 1, 2, 2, false); shoot_cooldown = 6; }
                        } else { 
                            spawn_bullet(bx, by, 0, -4, 3, 1, 2, 2, false); spawn_bullet(bx, by, 0, 4, 3, 1, 2, 2, false);
                            spawn_bullet(bx, by, -4, 0, 3, 1, 2, 2, false); spawn_bullet(bx, by, 4, 0, 3, 1, 2, 2, false);
                            shoot_cooldown = 6;
                        }
                    } else if (selected_loadout == 2) { 
                        if (player.weapon == 0) { 
                            spawn_bullet(bx, by, pdx*4, pdy*4, 100, 1, 2, 2, false); spawn_bullet(bx, by, -pdx*4, -pdy*4, 100, 1, 2, 2, false);
                            shoot_cooldown = 15;
                        } else { 
                            int bw = (pdx != 0) ? 6 : 2; int bh = (pdy != 0) ? 6 : 2;
                            spawn_bullet(bx, by, pdx*4, pdy*4, 100, 3, bw, bh, false); shoot_cooldown = 35;
                        }
                    } else if (selected_loadout == 3) { // Ninja
                        if (player.weapon == 0) {
                            spawn_bullet(bx, by, 0, -4, 100, 1, 2, 2, false); spawn_bullet(bx, by, 0, 4, 100, 1, 2, 2, false);
                            spawn_bullet(bx, by, -4, 0, 100, 1, 2, 2, false); spawn_bullet(bx, by, 4, 0, 100, 1, 2, 2, false);
                            shoot_cooldown = 20;
                        } else {
                            spawn_bullet(bx, by, pdx*4, pdy*4, 4, 1, 2, 2, false); spawn_bullet(bx, by, -pdx*4, -pdy*4, 4, 1, 2, 2, false);
                            shoot_cooldown = 6;
                        }
                    } else if (selected_loadout == 4) { // Sniper
                        if (player.weapon == 0) {
                            spawn_bullet(bx, by, pdx*4, pdy*4, 100, 4, 2, 2, true); shoot_cooldown = 50;
                        } else {
                            spawn_bullet(bx, by, pdx*4, pdy*4, 100, 1, 2, 2, false); shoot_cooldown = 15;
                        }
                    }
                }
            }

            if (player.iframes > 0) player.iframes--;

            for (int i = 0; i < MAX_BULLETS; i++) {
                if (!bullets[i].active) continue;
                bullets[i].prev_x = bullets[i].x; bullets[i].prev_y = bullets[i].y;
                bullets[i].x += bullets[i].dx; bullets[i].y += bullets[i].dy;
                bullets[i].life--;

                if (bullets[i].life <= 0 || bullets[i].x < 0 || bullets[i].x > SCREEN_WIDTH - bullets[i].w || bullets[i].y < HUD_HEIGHT || bullets[i].y > SCREEN_HEIGHT - bullets[i].h || (!bullets[i].pierce && map_collision(bullets[i].x, bullets[i].y, bullets[i].w, bullets[i].h))) {
                    bullets[i].active = false;
                    continue;
                }

                for (int z = 0; z < MAX_ZOMBIES; z++) {
                    if (!zombies[z].active) continue;
                    if (check_collision(bullets[i].x, bullets[i].y, bullets[i].w, bullets[i].h, zombies[z].x, zombies[z].y, zombies[z].w, zombies[z].h)) {
                        bullets[i].active = false;
                        zombies[z].health -= (has_insta ? 100 : bullets[i].dmg);
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
                        int target_x = (grenade.state == 1 && selected_loadout == 0) ? grenade.x : player.x; 
                        int target_y = (grenade.state == 1 && selected_loadout == 0) ? grenade.y : player.y;

                        int zmx = 0, zmy = 0;
                        if (zombies[i].x < target_x) zmx = 1;
                        else if (zombies[i].x > target_x) zmx = -1;
                        if (zombies[i].y < target_y) zmy = 1;
                        else if (zombies[i].y > target_y) zmy = -1;

                        for (int j = 0; j < MAX_ZOMBIES; j++) {
                            if (i != j && zombies[j].active) {
                                int dx = zombies[i].x - zombies[j].x;
                                int dy = zombies[i].y - zombies[j].y;
                                if (dx*dx + dy*dy < 16) { 
                                    if (dx != 0) zmx = (dx > 0) ? 1 : -1;
                                    if (dy != 0) zmy = (dy > 0) ? 1 : -1;
                                    break;
                                }
                            }
                        }

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

                        if (zombies[i].x < 0) zombies[i].x = 0;
                        if (zombies[i].x > SCREEN_WIDTH - zombies[i].w) zombies[i].x = SCREEN_WIDTH - zombies[i].w;
                        if (zombies[i].y < HUD_HEIGHT) zombies[i].y = HUD_HEIGHT;
                        if (zombies[i].y > SCREEN_HEIGHT - zombies[i].h) zombies[i].y = SCREEN_HEIGHT - zombies[i].h;
                    }

                    if (!(grenade.state == 1 && selected_loadout == 0) && player.iframes == 0 && check_collision(player.x, player.y, player.w, player.h, zombies[i].x, zombies[i].y, zombies[i].w, zombies[i].h)) {
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
        if (game_state == STATE_HOME) {
            if (force_redraw) {
                draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BLACK);
                draw_text("ZOMBIES", 64, 20, COLOR_RED, 2);
                
                draw_rect(108, 68, 24, 24, COLOR_BLUE); 
                draw_rect(112, 72, 16, 16, COLOR_SKIN); 
                draw_rect(124, 76, 16, 8, COLOR_WHITE); 

                draw_text("PRESS START", 76, 120, COLOR_WHITE, 1);
                force_redraw = false;
            }
        }
        else if (game_state == STATE_MAP_SELECT) {
            if (force_redraw) {
                draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BLACK);
                draw_text("SELECT MAP", 80, 20, COLOR_RED, 1);
                for (int i = 0; i < 9; i++) {
                    draw_text(maps[i].name, 90, 35 + (i * 12), (i == selected_map) ? COLOR_YELLOW : COLOR_WHITE, 1);
                    if (i == selected_map) draw_text(">", 75, 35 + (i * 12), COLOR_YELLOW, 1);
                }
                force_redraw = false;
            }
        }
        else if (game_state == STATE_LOADOUT_SELECT) {
            if (force_redraw) {
                draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BLACK);
                draw_text("SELECT LOADOUT", 64, 20, COLOR_RED, 1);
                for (int i = 0; i < 5; i++) {
                    draw_text(loadouts[i], 90, 50 + (i * 15), (i == selected_loadout) ? COLOR_YELLOW : COLOR_WHITE, 1);
                    if (i == selected_loadout) draw_text(">", 75, 50 + (i * 15), COLOR_YELLOW, 1);
                }
                force_redraw = false;
            }
        }
        else if (game_state == STATE_START) {
            if (force_redraw) {
                draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BLACK);
                draw_text("ZOMBIE SURVIVAL", 60, 50, COLOR_RED, 1);
                draw_text("PRESS START", 76, 80, COLOR_WHITE, 1);
                force_redraw = false;
            }
        }
        else if (game_state == STATE_GAMEOVER) {
            if (force_redraw) {
                draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BLACK);
                draw_text("GAME OVER", 84, 40, COLOR_RED, 1);
                
                char rnd_str[4];
                if (wave < 10) { rnd_str[0] = wave + '0'; rnd_str[1] = '\0'; }
                else if (wave < 100) { rnd_str[0] = (wave / 10) + '0'; rnd_str[1] = (wave % 10) + '0'; rnd_str[2] = '\0'; }
                else { rnd_str[0] = (wave / 100) + '0'; rnd_str[1] = ((wave / 10) % 10) + '0'; rnd_str[2] = (wave % 10) + '0'; rnd_str[3] = '\0'; }
                
                draw_text("ROUND", 92, 60, COLOR_YELLOW, 1);
                draw_text(rnd_str, 138, 60, COLOR_YELLOW, 1);
                
                draw_text("PRESS START", 76, 90, COLOR_WHITE, 1);
                force_redraw = false;
            }
        }
        else if (game_state == STATE_PAUSE) {
            if (force_redraw) {
                draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BLACK);
                draw_text("PAUSED", 96, 40, COLOR_YELLOW, 1);
                draw_text("START TO RESUME", 60, 80, COLOR_WHITE, 1);
                draw_text("SELECT TO QUIT", 60, 100, COLOR_WHITE, 1);
                force_redraw = false;
            }
        }
        else { // STATE_PLAY
            if (force_redraw) {
                draw_rect(0, HUD_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - HUD_HEIGHT, COLOR_DARKGRAY);
                draw_rect(0, 0, SCREEN_WIDTH, HUD_HEIGHT, COLOR_BLACK); // Restored full HUD clear to force redraw
                for (int i = 0; i < maps[selected_map].wall_count; i++) {
                    Rect wall = maps[selected_map].walls[i];
                    draw_arena_rect(wall.x, wall.y, wall.w, wall.h, COLOR_LIGHTGRAY);
                }
                force_redraw = false;
            } else {
                draw_arena_rect(player.prev_x, player.prev_y, player.w, player.h, COLOR_DARKGRAY);
                for (int i = 0; i < MAX_BULLETS; i++) {
                    if (bullets[i].prev_x != -1) {
                        draw_arena_rect(bullets[i].prev_x, bullets[i].prev_y, bullets[i].w, bullets[i].h, COLOR_DARKGRAY);
                        
                        // NEW: Redraw wall overlap if a piercing bullet left a blank trail through it
                        if (bullets[i].pierce) {
                            for (int j = 0; j < maps[selected_map].wall_count; j++) {
                                Rect wall = maps[selected_map].walls[j];
                                if (check_collision(bullets[i].prev_x, bullets[i].prev_y, bullets[i].w, bullets[i].h, wall.x, wall.y, wall.w, wall.h)) {
                                    int ix = (bullets[i].prev_x > wall.x) ? bullets[i].prev_x : wall.x;
                                    int iy = (bullets[i].prev_y > wall.y) ? bullets[i].prev_y : wall.y;
                                    int iw = ((bullets[i].prev_x + bullets[i].w < wall.x + wall.w) ? bullets[i].prev_x + bullets[i].w : wall.x + wall.w) - ix;
                                    int ih = ((bullets[i].prev_y + bullets[i].h < wall.y + wall.h) ? bullets[i].prev_y + bullets[i].h : wall.y + wall.h) - iy;
                                    if (iw > 0 && ih > 0) {
                                        draw_arena_rect(ix, iy, iw, ih, COLOR_LIGHTGRAY);
                                    }
                                }
                            }
                        }

                        if (!bullets[i].active) bullets[i].prev_x = -1;
                    }
                }
                for (int i = 0; i < MAX_ZOMBIES; i++) {
                    if (zombies[i].prev_x != -1) {
                        draw_arena_rect(zombies[i].prev_x, zombies[i].prev_y, zombies[i].w, zombies[i].h, COLOR_DARKGRAY);
                        if (!zombies[i].active) zombies[i].prev_x = -1;
                    }
                }
            }

            // Localized HUD clears restored to prevent clipping/blind spots
            draw_rect(4, 3, 30, 4, COLOR_RED);
            if (player.health > 0) draw_rect(4, 3, player.health * 6, 4, COLOR_GREEN);
            for (int i = 1; i < 5; i++) draw_rect(4 + (i * 6) - 1, 3, 1, 4, COLOR_BLACK);

            draw_rect(50, 2, 35, 10, COLOR_BLACK); 
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
            draw_rect(gun_x, 2, 16, 8, COLOR_BLACK); 
            bool has_bg = false;
            for(int i=0; i<3; i++) if (active_pups[i].type == 2) has_bg = true;
            
            if (has_bg) { 
                draw_rect(gun_x + 3, 3, 8, 2, COLOR_RED); draw_rect(gun_x + 3, 5, 2, 2, COLOR_RED); draw_rect(gun_x + 7, 5, 3, 1, COLOR_RED); 
            } else if (selected_loadout == 0) { 
                if (player.weapon == 0) { draw_rect(gun_x + 6, 4, 4, 2, COLOR_WHITE); draw_rect(gun_x + 6, 6, 2, 2, COLOR_WHITE); } 
                else { draw_rect(gun_x + 3, 4, 8, 2, COLOR_ORANGE); draw_rect(gun_x + 3, 6, 2, 2, COLOR_ORANGE); draw_rect(gun_x + 7, 6, 3, 1, COLOR_ORANGE); } 
            } else if (selected_loadout == 1) { 
                if (player.weapon == 0) { draw_rect(gun_x + 3, 4, 10, 2, COLOR_LIGHTGRAY); draw_rect(gun_x + 3, 6, 3, 2, COLOR_LIGHTGRAY); draw_rect(gun_x + 7, 6, 2, 3, COLOR_LIGHTGRAY); } 
                else { draw_rect(gun_x + 3, 5, 6, 1, COLOR_WHITE); draw_rect(gun_x + 2, 4, 2, 3, COLOR_SKIN); } 
            } else if (selected_loadout == 2) { 
                if (player.weapon == 0) { draw_rect(gun_x + 6, 2, 4, 2, COLOR_WHITE); draw_rect(gun_x + 6, 4, 2, 2, COLOR_WHITE); draw_rect(gun_x + 6, 6, 4, 2, COLOR_WHITE); draw_rect(gun_x + 6, 8, 2, 2, COLOR_WHITE); } 
                else { draw_rect(gun_x + 3, 3, 8, 4, COLOR_YELLOW); } 
            } else if (selected_loadout == 3) {
                if (player.weapon == 0) { draw_rect(gun_x + 5, 4, 6, 2, COLOR_WHITE); draw_rect(gun_x + 7, 2, 2, 6, COLOR_WHITE); } 
                else { draw_rect(gun_x + 2, 4, 12, 1, COLOR_LIGHTGRAY); draw_rect(gun_x + 6, 3, 2, 3, COLOR_ORANGE); } 
            } else if (selected_loadout == 4) {
                if (player.weapon == 0) { draw_rect(gun_x, 4, 16, 2, COLOR_DARKGRAY); draw_rect(gun_x, 5, 4, 2, COLOR_ORANGE); } 
                else { draw_rect(gun_x + 6, 4, 4, 2, COLOR_WHITE); draw_rect(gun_x + 6, 6, 2, 2, COLOR_WHITE); } 
            }
            
            uint16_t g_col = COLOR_DARKGREEN;
            if (selected_loadout == 1) g_col = COLOR_RED;
            else if (selected_loadout == 2) g_col = COLOR_CYAN;
            else if (selected_loadout == 3) g_col = COLOR_LIGHTGRAY;
            else if (selected_loadout == 4) g_col = COLOR_YELLOW;

            draw_rect(130, 1, 16, 12, COLOR_BLACK); 
            if (player.grenades >= 1) { draw_rect(135, 8, 4, 4, g_col); draw_rect(136, 7, 2, 1, COLOR_WHITE); }
            if (player.grenades >= 2) { draw_rect(135, 2, 4, 4, g_col); draw_rect(136, 1, 2, 1, COLOR_WHITE); }
            
            draw_rect(170, 2, 70, 6, COLOR_BLACK); 
            draw_text("RND:", 170, 2, COLOR_WHITE, 1);
            char rnd_str[4];
            if (wave < 10) { rnd_str[0] = wave + '0'; rnd_str[1] = '\0'; }
            else if (wave < 100) { rnd_str[0] = (wave / 10) + '0'; rnd_str[1] = (wave % 10) + '0'; rnd_str[2] = '\0'; }
            else { rnd_str[0] = (wave / 100) + '0'; rnd_str[1] = ((wave / 10) % 10) + '0'; rnd_str[2] = (wave % 10) + '0'; rnd_str[3] = '\0'; }
            draw_text(rnd_str, 202, 2, COLOR_YELLOW, 1);

            for (int i=0; i<MAX_DROPS; i++) {
                if (!drops[i].active) continue;
                int dx = drops[i].x; int dy = drops[i].y;
                uint16_t col = COLOR_GREEN; 
                if (drops[i].type == 0) col = COLOR_YELLOW;
                else if (drops[i].type == 1) col = COLOR_WHITE;
                else if (drops[i].type == 2) col = COLOR_RED;
                else if (drops[i].type == 3) col = COLOR_LIGHTBLUE;
                else if (drops[i].type == 4) col = COLOR_DARKBLUE; 
                else if (drops[i].type == 6) col = COLOR_DARKGREEN; 
                
                if (drops[i].type == 0) { 
                    draw_arena_rect(dx, dy, 6, 6, col); draw_arena_rect(dx+1, dy+1, 4, 4, COLOR_DARKGRAY); 
                } else if (drops[i].type == 1) { 
                    draw_arena_rect(dx+1, dy, 2, 4, col); draw_arena_rect(dx, dy+2, 4, 1, col);
                } else if (drops[i].type == 2) { 
                    draw_arena_rect(dx, dy+1, 4, 2, col); draw_arena_rect(dx+1, dy+3, 1, 1, col);
                } else if (drops[i].type == 3) { 
                    draw_arena_rect(dx, dy+1, 4, 2, col);
                } else if (drops[i].type == 4 || drops[i].type == 5) { 
                    draw_arena_rect(dx+1, dy, 2, 4, col); draw_arena_rect(dx, dy+1, 4, 2, col);
                } else if (drops[i].type == 6) { 
                    draw_arena_rect(dx+1, dy+2, 4, 4, col); draw_arena_rect(dx+2, dy, 2, 2, col); 
                }
            }

            if (grenade.state == 1) {
                draw_arena_rect(grenade.x, grenade.y, 2, 2, g_col);
            } else if (grenade.state == 3) {
                draw_arena_rect(grenade.x, grenade.y, 4, 4, COLOR_YELLOW);
                draw_arena_rect(grenade.x+1, grenade.y+1, 2, 2, COLOR_RED);
            } else if (grenade.state == 2) {
                if (selected_loadout == 2) {
                    draw_arena_rect(0, HUD_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_CYAN); 
                } else if (selected_loadout == 3) {
                    draw_arena_rect(grenade.x - 10, grenade.y - 10, 20, 20, COLOR_LIGHTGRAY); 
                } else if (selected_loadout == 4) {
                    draw_arena_rect(grenade.x - 15, grenade.y - 15, 30, 30, (grenade.timer % 4 > 1) ? COLOR_ORANGE : COLOR_RED);
                } else {
                    int r = (selected_loadout == 1) ? 80 : 60;
                    draw_arena_rect(grenade.x - (r/2), grenade.y - (r/2), r, r, (grenade.timer % 4 > 1) ? COLOR_ORANGE : COLOR_RED);
                }
            }

            for (int i = 0; i < MAX_BULLETS; i++) {
                if (bullets[i].active) draw_arena_rect(bullets[i].x, bullets[i].y, bullets[i].w, bullets[i].h, COLOR_WHITE);
            }
            
            for (int i = 0; i < MAX_ZOMBIES; i++) {
                if (zombies[i].active) {
                    uint16_t z_col = (zombies[i].type == 2) ? COLOR_PURPLE : ((zombies[i].type == 1) ? COLOR_ORANGE : COLOR_RED);
                    draw_arena_rect(zombies[i].x + 1, zombies[i].y + 1, zombies[i].w - 2, zombies[i].h - 2, z_col); 
                    draw_arena_rect(zombies[i].x + 2, zombies[i].y + 2, zombies[i].w - 4, zombies[i].h - 4, COLOR_GREEN); 
                    
                    if (player.x < zombies[i].x) { draw_arena_rect(zombies[i].x, zombies[i].y + 1, 2, 1, z_col); draw_arena_rect(zombies[i].x, zombies[i].y + zombies[i].h - 2, 2, 1, z_col); }
                    else if (player.x > zombies[i].x) { draw_arena_rect(zombies[i].x + zombies[i].w - 2, zombies[i].y + 1, 2, 1, z_col); draw_arena_rect(zombies[i].x + zombies[i].w - 2, zombies[i].y + zombies[i].h - 2, 2, 1, z_col); }
                    else if (player.y < zombies[i].y) { draw_arena_rect(zombies[i].x + 1, zombies[i].y, 1, 2, z_col); draw_arena_rect(zombies[i].x + zombies[i].w - 2, zombies[i].y, 1, 2, z_col); }
                    else { draw_arena_rect(zombies[i].x + 1, zombies[i].y + zombies[i].h - 2, 1, 2, z_col); draw_arena_rect(zombies[i].x + zombies[i].w - 2, zombies[i].y + zombies[i].h - 2, 1, 2, z_col); }
                }
            }

            uint16_t p_col = (player.iframes % 4 > 1) ? COLOR_WHITE : COLOR_BLUE;
            draw_arena_rect(player.x + 1, player.y + 1, 4, 4, p_col); 
            draw_arena_rect(player.x + 2, player.y + 2, 2, 2, COLOR_SKIN); 
            if (player.dx < 0) draw_arena_rect(player.x, player.y + 2, 2, 2, COLOR_WHITE);
            else if (player.dx > 0) draw_arena_rect(player.x + 4, player.y + 2, 2, 2, COLOR_WHITE);
            else if (player.dy < 0) draw_arena_rect(player.x + 2, player.y, 2, 2, COLOR_WHITE);
            else draw_arena_rect(player.x + 2, player.y + 4, 2, 2, COLOR_WHITE);
        }
    }
    return 0;
}
