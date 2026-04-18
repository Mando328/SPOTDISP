#include <Arduino.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <WiFi.h>
#include <SpotifyEsp32.h>
#include <SPI.h>
#include <WiFiManager.h>

#define TFT_CS 1
#define TFT_RST 2
#define TFT_DC 3
#define TFT_SCLK 4
#define TFT_MOSI 5
#define Next 6
#define Previous 7
#define PlayPause 8
#define OnOff 9

#define POLL_INTERVAL 3000
#define SCREEN_W 160
#define SCREEN_H 128
#define ART_SIZE 90
#define SPOTIFY_GREEN 0x0DA5  // fixed — was #1DB954 which doesn't compile

volatile bool nextPressed = false;
volatile bool prevPressed = false;
volatile bool playPausePressed = false;
volatile bool powerState = true;  // true = on, false = off

const char* CLIENT_ID = "YOUR CLIENT ID FROM THE SPOTIFY DASHBOARD";
const char* CLIENT_SECRET = "YOUR CLIENT SECRET FROM THE SPOTIFY DASHBOARD";
Spotify sp(CLIENT_ID, CLIENT_SECRET);
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

char currentTrack[64] = "";
char currentArtist[64] = "";
bool currentlyPlaying = false;
long progressMs = 0;
long durationMs = 1;
unsigned long lastPollTime = 0;
unsigned long lastProgressTick = 0;

void wifistartup() {
    WiFiManager wm;
    bool connected = wm.autoConnect("SpotDisp");
    if (!connected) {
        Serial.println("Failed to connect to WiFi, restarting...");
        tft.fillScreen(ST77XX_RED);
        tft.setCursor(0, 0);
        tft.write("WiFi Failed, Restarting...");
        delay(3000);
        ESP.restart();
    }
    Serial.println("Connected to WiFi!");
    Serial.println(WiFi.localIP());
    tft.write(WiFi.localIP().toString().c_str());
    delay(3000);
    tft.setCursor(0, 0);
    tft.fillScreen(ST77XX_BLACK);
}

void spotifyStartup() {
    sp.begin();
    while (!sp.is_auth()) {
        sp.handle_client();
        delay(2000);
    }
    Serial.println("Spotify connected!");
    tft.fillScreen(ST77XX_BLACK);
    tft.setCursor(0, 0);
    tft.write("Spotify Connected!");
    delay(2000);
    tft.fillScreen(ST77XX_BLACK);
}

void screenStartup() {
    tft.initR(INITR_BLACKTAB);
    tft.setRotation(1);
    Serial.println("Display initialized!");
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(1);
    tft.setCursor(0, 0);
}

void screen_off() {
    tft.fillScreen(ST77XX_BLACK);
}

void screen_on() {
    draw_UI();
    poll_spotify();
}

void IRAM_ATTR onNext() {
    static unsigned long last = 0;
    unsigned long now = millis();
    if (now - last > 200) { nextPressed = true; last = now; }
}

void IRAM_ATTR onPrev() {
    static unsigned long last = 0;
    unsigned long now = millis();
    if (now - last > 200) { prevPressed = true; last = now; }
}

void IRAM_ATTR onPlayPause() {
    static unsigned long last = 0;
    unsigned long now = millis();
    if (now - last > 200) { playPausePressed = true; last = now; }
}

// reads the physical on/off switch — HIGH = on, LOW = off
// lever switches hold their state so we just read the pin directly, no interrupt needed
bool read_power_switch() {
    return digitalRead(OnOff) == HIGH;
}

void truncate(const char* input, char* output, int maxChars) {
    if (strlen(input) <= maxChars) {
        strcpy(output, input);
    } else {
        strncpy(output, input, maxChars - 3);
        output[maxChars - 3] = '.';
        output[maxChars - 2] = '.';
        output[maxChars - 1] = '.';
        output[maxChars] = '\0';
    }
}

void draw_icons(bool playing) {
    int x = ART_SIZE + 6;
    int y = SCREEN_H - 22;
    tft.fillRect(x, y, 50, 16, ST77XX_BLACK);
    if (playing) {
        tft.fillRect(x + 14, y + 1, 4, 13, ST77XX_WHITE);
        tft.fillRect(x + 22, y + 1, 4, 13, ST77XX_WHITE);
    } else {
        for (int i = 0; i < 8; i++) {
            tft.drawFastVLine(x + 14 + i, y + i, 15 - (i * 2), ST77XX_WHITE);
        }
    }
    tft.fillRect(x, y + 2, 3, 11, ST77XX_WHITE);
    for (int i = 0; i < 6; i++) {
        tft.drawFastVLine(x + 3 + i, y + 2 + i, 11 - (i * 2), ST77XX_WHITE);
    }
    tft.fillRect(x + 40, y + 2, 3, 11, ST77XX_WHITE);
    for (int i = 0; i < 6; i++) {
        tft.drawFastVLine(x + 34 + i, y + 2 + (5 - i), (i * 2) + 1, ST77XX_WHITE);
    }
}

void draw_progress_bar() {
    int barX = ART_SIZE + 6;
    int barY = SCREEN_H - 32;
    int barW = SCREEN_W - ART_SIZE - 12;
    int barH = 4;
    tft.fillRect(barX, barY, barW, barH, 0x2104);
    int filled = 0;
    if (durationMs > 0) {
        filled = (int)((float)progressMs / durationMs * barW);
        filled = constrain(filled, 0, barW);
    }
    tft.fillRect(barX, barY, filled, barH, SPOTIFY_GREEN);
}

void draw_UI() {
    tft.fillScreen(ST77XX_BLACK);
    tft.fillRect(0, 0, ART_SIZE, SCREEN_H, 0x1082);
    tft.fillRect(4, 4, ART_SIZE - 8, SCREEN_H - 8, 0x2104);
    tft.fillRect(ART_SIZE/2 - 6, SCREEN_H/2 - 12, 12, 3, SPOTIFY_GREEN);
    tft.fillRect(ART_SIZE/2 + 4, SCREEN_H/2 - 12, 3, 14, SPOTIFY_GREEN);
    tft.fillCircle(ART_SIZE/2 - 4, SCREEN_H/2 + 4, 5, SPOTIFY_GREEN);
    tft.fillCircle(ART_SIZE/2 + 7, SCREEN_H/2 + 2, 5, SPOTIFY_GREEN);
    tft.drawFastVLine(ART_SIZE + 2, 0, SCREEN_H, 0x4208);
    char truncTrack[20];
    truncate(currentTrack, truncTrack, 18);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(ART_SIZE + 6, 10);
    tft.print(truncTrack);
    char truncArtist[20];
    truncate(currentArtist, truncArtist, 18);
    tft.setTextColor(0x8410);
    tft.setCursor(ART_SIZE + 6, 24);
    tft.print(truncArtist);
    draw_progress_bar();
    draw_icons(currentlyPlaying);
}

void draw_UI_partial(bool trackChanged) {
    if (trackChanged) {
        tft.fillRect(ART_SIZE + 3, 0, SCREEN_W - ART_SIZE - 3, SCREEN_H - 28, ST77XX_BLACK);
        char truncTrack[20];
        truncate(currentTrack, truncTrack, 18);
        tft.setTextSize(1);
        tft.setTextColor(ST77XX_WHITE);
        tft.setCursor(ART_SIZE + 6, 10);
        tft.print(truncTrack);
        char truncArtist[20];
        truncate(currentArtist, truncArtist, 18);
        tft.setTextColor(0x8410);
        tft.setCursor(ART_SIZE + 6, 24);
        tft.print(truncArtist);
    }
    draw_progress_bar();
    draw_icons(currentlyPlaying);
}

void optimistic_UI(const char* action) {
    if (strcmp(action, "next") == 0 || strcmp(action, "prev") == 0) {
        tft.fillRect(ART_SIZE + 3, 0, SCREEN_W - ART_SIZE - 3, 40, ST77XX_BLACK);
        tft.setTextSize(1);
        tft.setTextColor(0x8410);
        tft.setCursor(ART_SIZE + 6, 10);
        tft.print("Loading...");
        progressMs = 0;
        draw_progress_bar();
    }
    if (strcmp(action, "playpause") == 0) {
        currentlyPlaying = !currentlyPlaying;
        draw_icons(currentlyPlaying);
    }
}

void poll_spotify() {
    response res = sp.get_current_playback();
    if (res.status_code == 200) {
        char newTrack[64];
        char newArtist[64];
        sp.current_track_name(newTrack);
        sp.current_artist_names(newArtist);
        bool trackChanged = (strcmp(newTrack, currentTrack) != 0);
        strncpy(currentTrack, newTrack, sizeof(currentTrack) - 1);
        strncpy(currentArtist, newArtist, sizeof(currentArtist) - 1);
        currentlyPlaying = sp.is_playing();
        progressMs = res.reply["progress_ms"].as<long>();
        durationMs = res.reply["item"]["duration_ms"].as<long>();
        draw_UI_partial(trackChanged);
        lastProgressTick = millis();
    }
    lastPollTime = millis();
}

void setup() {
    pinMode(Next, INPUT_PULLDOWN);
    pinMode(Previous, INPUT_PULLDOWN);
    pinMode(PlayPause, INPUT_PULLDOWN);
    pinMode(OnOff, INPUT_PULLDOWN);  

    attachInterrupt(digitalPinToInterrupt(Next), onNext, RISING);
    attachInterrupt(digitalPinToInterrupt(Previous), onPrev, RISING);
    attachInterrupt(digitalPinToInterrupt(PlayPause), onPlayPause, CHANGE); 

    Serial.begin(115200);
    screenStartup();
    wifistartup();
    spotifyStartup();
    poll_spotify();
    draw_UI();
}

void loop() {
    // read the physical on/off lever state
    bool switchOn = read_power_switch();

    // handle transition from on → off
    if (!switchOn && powerState) {
        powerState = false;
        screen_off();
        Serial.println("Display off");
    }

    // handle transition from off → on
    if (switchOn && !powerState) {
        powerState = true;
        screen_on();
        Serial.println("Display on");
    }

    // ignore everything below if switched off
    if (!powerState) return;

    if (nextPressed) {
        nextPressed = false;
        optimistic_UI("next");
        sp.skip();
        lastPollTime = millis();
        delay(800);
        poll_spotify();
    }

    if (prevPressed) {
        prevPressed = false;
        optimistic_UI("prev");
        sp.previous();
        lastPollTime = millis();
        delay(800);
        poll_spotify();
    }

    if (playPausePressed) {
        playPausePressed = false;
        optimistic_UI("playpause");
        sp.start_resume_playback();
        lastPollTime = millis();
    }

    if (millis() - lastPollTime >= POLL_INTERVAL) {
        poll_spotify();
    }

    if (currentlyPlaying && millis() - lastProgressTick >= 1000) {
        progressMs += 1000;
        lastProgressTick = millis();
        draw_progress_bar();
    }
}