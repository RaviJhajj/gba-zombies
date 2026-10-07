#include <gba_video.h>
#include <gba_input.h>
#include <gba_math.h>

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 160
#define PLAYER_SIZE 16
#define BULLET_SIZE 8
#define ZOMBIE_SIZE 16
#define MAX_ZOMBIES 10

typedef struct {
    int x, y;
    int health;
} Zombie;

Zombie zombies[MAX_ZOMBIES];
int zombieCount = 0;
int wave = 1;
int playerX = SCREEN_WIDTH / 2 - PLAYER_SIZE / 2;
int playerY = SCREEN_HEIGHT / 2 - PLAYER_SIZE / 2;
int playerHealth = 5;
int bulletX, bulletY;
int bulletVisible = 0;

void initGame() {
    // Initialize VRAM
    REG_DISPCNT = DCNT_MODE3 | DCNT_OBJ_1D | DCNT_BG0_ON | DCNT_BG1_ON | DCNT_BG2_ON | DCNT_BG3_ON;
    REG_VRAM0CNT = VRAM_A_32BPP | VRAM_A_BG0;
    REG_VRAM1CNT = VRAM_A_32BPP | VRAM_A_BG1;
    REG_VRAM2CNT = VRAM_A_32BPP | VRAM_A_BG2;
    REG_VRAM3CNT = VRAM_A_32BPP | VRAM_A_BG3;

    // Clear screen
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            SetPixel(x, y, RGB15(0, 0, 0));
        }
    }

    // Initialize player and zombies
    playerHealth = 5;
    zombieCount = 0;

    // Spawn initial zombies
    for (int i = 0; i < wave * 2; i++) {
        zombies[zombieCount].x = rand() % SCREEN_WIDTH;
        zombies[zombieCount].y = rand() % SCREEN_HEIGHT;
        zombies[zombieCount].health = 1;
        zombieCount++;
    }
}

void updateGame() {
    // Handle input
    u16 keys = REG_KEYINPUT;
    if (keys & KEY_UP) playerY -= 2;
    if (keys & KEY_DOWN) playerY += 2;
    if (keys & KEY_LEFT) playerX -= 2;
    if (keys & KEY_RIGHT) playerX += 2;

    // Move zombies
    for (int i = 0; i < zombieCount; i++) {
        int dx = playerX - zombies[i].x;
        int dy = playerY - zombies[i].y;
        int distance = sqrt(dx * dx + dy * dy);
        if (distance > 0) {
            zombies[i].x += dx / distance;
            zombies[i].y += dy / distance;
        }
    }

    // Handle bullets
    if (keys & KEY_A && !bulletVisible) {
        bulletX = playerX + PLAYER_SIZE / 2 - BULLET_SIZE / 2;
        bulletY = playerY + PLAYER_SIZE / 2 - BULLET_SIZE / 2;
        bulletVisible = 1;
    }

    if (bulletVisible) {
        bulletY -= 4;
        if (bulletY < 0) {
            bulletVisible = 0;
        }
    }

    // Collision detection
    for (int i = 0; i < zombieCount; i++) {
        int dx = bulletX - zombies[i].x;
        int dy = bulletY - zombies[i].y;
        int distance = sqrt(dx * dx + dy * dy);
        if (distance < BULLET_SIZE / 2 + ZOMBIE_SIZE / 2) {
            zombies[i].health--;
            if (zombies[i].health <= 0) {
                zombieCount--;
            }
        }
    }

    // Wave system
    if (zombieCount == 0) {
        wave++;
        for (int i = 0; i < wave * 2; i++) {
            zombies[zombieCount].x = rand() % SCREEN_WIDTH;
            zombies[zombieCount].y = rand() % SCREEN_HEIGHT;
            zombies[zombieCount].health = 1;
            zombieCount++;
        }
    }

    // Player health
    if (playerHealth <= 0) {
        initGame();
    }
}

void renderGame() {
    // Clear screen
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            SetPixel(x, y, RGB15(0, 0, 0));
        }
    }

    // Draw player
    for (int y = playerY; y < playerY + PLAYER_SIZE; y++) {
        for (int x = playerX; x < playerX + PLAYER_SIZE; x++) {
            SetPixel(x, y, RGB15(31, 0, 0));
        }
    }

    // Draw zombies
    for (int i = 0; i < zombieCount; i++) {
        for (int y = zombies[i].y; y < zombies[i].y + ZOMBIE_SIZE; y++) {
            for (int x = zombies[i].x; x < zombies[i].x + ZOMBIE_SIZE; x++) {
                SetPixel(x, y, RGB15(0, 31, 0));
            }
        }
    }

    // Draw bullets
    if (bulletVisible) {
        for (int y = bulletY; y < bulletY + BULLET_SIZE; y++) {
            for (int x = bulletX; x < bulletX + BULLET_SIZE; x++) {
                SetPixel(x, y, RGB15(0, 0, 31));
            }
        }
    }

    // Draw wave number
    char waveText[20];
    sprintf(waveText, "Wave %d", wave);
    for (int i = 0; i < strlen(waveText); i++) {
        SetPixel(10 + i * 8, SCREEN_HEIGHT - 10, RGB15(31, 31, 31));
    }
}

void main() {
    initGame();

    while (1) {
        updateGame();
        renderGame();
        VBlankIntrWait();
    }
}
