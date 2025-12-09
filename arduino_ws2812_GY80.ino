#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>
#include <FastLED.h>

// --- [可修改] 硬體與基本設定 ---
#define LED_PIN     6
#define NUM_LEDS    64
#define BRIGHTNESS  30      // <--- [可修改] 全域基礎亮度 (建議 20-60，太高會過熱或當機)
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB

// --- [可修改] 漸變速度設定 ---
// 數字越大越快，數字越小越慢 (範圍 1-255)
#define FADE_IN_SPEED   3   // <--- [可修改] 開燈時，火焰燃起的速度
#define FADE_OUT_SPEED  8   // <--- [可修改] 關燈時，火焰熄滅的速度

CRGB leds[NUM_LEDS];

// --- 感測器設定 ---
Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);
float lastX, lastY, lastZ;

// --- 狀態控制變數 ---
bool isLampOn = true;           // 目標狀態 (true=要開, false=要關)
bool isSoulFire = false;        // 靈魂火焰狀態

// 這是新增的變數，用來控制漸亮漸暗的過程 (0=全黑, 255=完全顯示)
uint8_t currentFadeValue = 255; 

unsigned long lastToggleTime = 0;      
unsigned long lastColorSwitchTime = 0; 

// --- [可修改] 感測器靈敏度設定 ---
const int SHAKE_THRESHOLD_Z = 25;      // <--- [可修改] 上下甩動 (開關燈) 的力度門檻
const int SHAKE_THRESHOLD_XY = 25;     // <--- [可修改] 左右甩動 (換色) 的力度門檻
const int COOLDOWN_TIME = 1000;        // 動作冷卻時間 (1秒)

uint8_t fireActivity = 0; 

// --- 色板定義 ---
CRGBPalette16 redCalm = CRGBPalette16(
    CRGB::DarkRed,  CRGB::Maroon,   CRGB::DarkOrange, CRGB::OrangeRed,
    CRGB::Orange,   CRGB::DarkOrange, CRGB::Red,      CRGB::Maroon,
    CRGB::DarkRed,  CRGB::Maroon,   CRGB::DarkOrange, CRGB::OrangeRed,
    CRGB::Orange,   CRGB::DarkOrange,CRGB::Red,      CRGB::Black
);
CRGBPalette16 redActive = CRGBPalette16(
    CRGB::Red,      CRGB::OrangeRed, CRGB::Orange,    CRGB::Gold,
    CRGB::Yellow,   CRGB::LightYellow,CRGB::White,    CRGB::LightYellow,
    CRGB::Yellow,   CRGB::Gold,      CRGB::Orange,    CRGB::OrangeRed,
    CRGB::Red,      CRGB::OrangeRed, CRGB::Orange,    CRGB::Black
);
CRGBPalette16 blueCalm = CRGBPalette16(
    CRGB::DarkBlue, CRGB::Navy,     CRGB::Teal,      CRGB::DarkCyan,
    CRGB::Blue,     CRGB::DarkBlue, CRGB::MediumBlue,CRGB::Navy,
    CRGB::DarkBlue, CRGB::Navy,     CRGB::Teal,      CRGB::DarkCyan,
    CRGB::Blue,     CRGB::DarkBlue, CRGB::MediumBlue,CRGB::Black
);
CRGBPalette16 blueActive = CRGBPalette16(
    CRGB::Blue,     CRGB::DeepSkyBlue, CRGB::Cyan,    CRGB::LightCyan,
    CRGB::SkyBlue,  CRGB::Azure,       CRGB::White,   CRGB::Azure,
    CRGB::SkyBlue,  CRGB::Cyan,        CRGB::DeepSkyBlue, CRGB::Blue,
    CRGB::Blue,     CRGB::MediumBlue,  CRGB::DarkBlue,CRGB::Black
);

CRGBPalette16 targetPalette = redCalm;
CRGBPalette16 currentPalette = redCalm;


void setup() {
    Serial.begin(9600);
    delay(1000); 

    FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
    FastLED.setBrightness(BRIGHTNESS);
    FastLED.clear();

    if(!accel.begin()) {
        Serial.println("Sensor Error!");
        while(1); 
    }
    accel.setRange(ADXL345_RANGE_16_G); 
    
    sensors_event_t event; 
    accel.getEvent(&event);
    lastX = event.acceleration.x;
    lastY = event.acceleration.y;
    lastZ = event.acceleration.z;
}

void loop() {
    // 1. 感測器讀取 (限制頻率以防當機)
    EVERY_N_MILLISECONDS(25) {
        handleSensor();
    }

    // 2. 處理漸亮/漸暗邏輯
    EVERY_N_MILLISECONDS(20) { // 控制漸變的更新頻率
        if (isLampOn) {
            // 如果目標是開燈，就把 Fade 值慢慢加到 255 (使用 qadd8 防止溢位)
            currentFadeValue = qadd8(currentFadeValue, FADE_IN_SPEED); 
        } else {
            // 如果目標是關燈，就把 Fade 值慢慢減到 0 (使用 qsub8 防止溢位)
            currentFadeValue = qsub8(currentFadeValue, FADE_OUT_SPEED);
        }
    }

    // 3. 動畫更新與顯示
    EVERY_N_MILLISECONDS(30) { 
        // 只要 Fade 值不是 0，代表還有光，就要繼續畫圖
        if (currentFadeValue > 0) {
            generateReactiveFire(); 
            FastLED.show();
        } else {
            // 如果 Fade 值是 0，確保燈是全黑的
            FastLED.clear();
            FastLED.show();
        }
    }
}

void handleSensor() {
    sensors_event_t event; 
    accel.getEvent(&event);

    float currentX = event.acceleration.x;
    float currentY = event.acceleration.y;
    float currentZ = event.acceleration.z;

    float deltaX = abs(currentX - lastX);
    float deltaY = abs(currentY - lastY);
    float deltaZ = abs(currentZ - lastZ);
    float totalMovement = deltaX + deltaY + deltaZ;
    unsigned long currentTime = millis();

    // --- 動作 1: 上下用力甩 (Z軸) -> 開關燈 ---
    if (deltaZ > SHAKE_THRESHOLD_Z) {
        if (currentTime - lastToggleTime > COOLDOWN_TIME) {
            isLampOn = !isLampOn; // 切換目標狀態
            
            // 如果是「開燈」瞬間，將 Fade 值設為一個小數值，讓它從微亮開始升起 (避免完全看不到開燈閃光)
            // 如果你想要完全從黑開始，可以設為 0
            if(isLampOn) currentFadeValue = 10; 

            lastToggleTime = currentTime;
            
            // [視覺回饋] 閃一下灰白光提示 (亮度設低防當機)
            // 只有在 Fade 值夠高時才閃，不然會被漸亮邏輯吃掉
            if(isLampOn) fill_solid(leds, NUM_LEDS, CRGB(100, 100, 100)); 
            
            Serial.println(isLampOn ? "Action: Power ON (Fading In)" : "Action: Power OFF (Fading Out)");
        }
    }
    // --- 動作 2: 左右用力甩 (X或Y軸) -> 切換靈魂火焰 ---
    else if (isLampOn && (deltaX > SHAKE_THRESHOLD_XY || deltaY > SHAKE_THRESHOLD_XY)) {
        if (currentTime - lastColorSwitchTime > COOLDOWN_TIME) {
            isSoulFire = !isSoulFire; 
            lastColorSwitchTime = currentTime;
            
            // [視覺回饋] 切換顏色閃光
            if(isSoulFire) fill_solid(leds, NUM_LEDS, CRGB(0, 100, 100)); // 暗青色
            else fill_solid(leds, NUM_LEDS, CRGB(100, 0, 0));             // 暗紅色
            
            Serial.println(isSoulFire ? "Action: Soul Fire Mode" : "Action: Normal Fire Mode");
        }
    }

    // --- 動作 3: 計算火焰活動量 ---
    // 注意：即使正在關燈(Fade Out)，我們也要繼續計算火焰，這樣漸滅時火焰才會動，比較自然
    if (currentFadeValue > 0) {
        CRGBPalette16 targetBasePalette;
        CRGBPalette16 targetActivePalette;

        if (isSoulFire) {
            targetBasePalette = blueCalm;
            targetActivePalette = blueActive;
        } else {
            targetBasePalette = redCalm;
            targetActivePalette = redActive;
        }

        uint8_t targetActivity = constrain(map(totalMovement * 10, 20, 150, 0, 255), 0, 255);
        
        if (fireActivity < targetActivity) fireActivity++;
        else if (fireActivity > targetActivity) fireActivity -= 2;

        nblendPaletteTowardPalette(currentPalette, targetBasePalette, 10);
        if(fireActivity > 10) {
             nblendPaletteTowardPalette(currentPalette, targetActivePalette, fireActivity/2);
        }
    }

    lastX = currentX;
    lastY = currentY;
    lastZ = currentZ;
}

void generateReactiveFire() {
    uint8_t noiseScale = map(fireActivity, 0, 255, 20, 50); 
    static uint32_t time = 0;
    time += map(fireActivity, 0, 255, 5, 30); 

    // 1. 計算「物理亮度」 (基礎 + 晃動增強)
    // --- [可修改] 晃動時增加的亮度幅度 (最後那個 60 可以改) ---
    uint8_t physicsBrightness = BRIGHTNESS + map(fireActivity, 0, 255, 0, 60);
    
    // 2. 計算「最終亮度」 (物理亮度 * 漸亮漸暗係數)
    // scale8 函數會把數值依比例縮放 (例如 currentFadeValue 是 128 (一半)，結果就是一半亮度)
    uint8_t finalBrightness = scale8(physicsBrightness, currentFadeValue);

    // 3. 套用亮度
    FastLED.setBrightness(finalBrightness);

    for(int x=0; x<8; x++) {
        for(int y=0; y<8; y++) {
            uint8_t heat = inoise8(x * noiseScale, y * noiseScale, time);
            uint8_t centerBoost = (4 - abs(x - 3.5)) * (4 - abs(y - 3.5)) * 10;
            heat = qadd8(heat, centerBoost); 

            if (fireActivity > 200 && random8() < 10) {
                 if (x > 2 && x < 5 && y > 2 && y < 5) heat = qadd8(heat, 100);
            }
            leds[XY(x,y)] = ColorFromPalette(currentPalette, heat);
        }
    }
}

uint8_t XY(uint8_t x, uint8_t y) {
  if (y % 2 == 1) return (y * 8) + (7 - x);
  else return (y * 8) + x;
}