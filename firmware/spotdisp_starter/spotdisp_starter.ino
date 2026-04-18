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

#define POLL_INTERVAL 3000
#define SCREEN_W 160
#define SCREEN_H 128
#define ART_SIZE 90
#define SPOTIFY_GREEN #1DB954 

volatile bool nextPressed = false;
volatile bool prevPressed = false;
volatile bool playPausePressed = false;

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
    while(!sp.is_auth()) {
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

    // clear icon area
    tft.fillRect(x, y, 50, 16, ST77XX_BLACK);

    if (playing) {
        // pause icon — two rectangles
        tft.fillRect(x + 14, y + 1, 4, 13, ST77XX_WHITE);
        tft.fillRect(x + 22, y + 1, 4, 13, ST77XX_WHITE);
    } else {
        // play icon — triangle
        for (int i = 0; i < 8; i++) {
            tft.drawFastVLine(x + 14 + i, y + i, 15 - (i * 2), ST77XX_WHITE);
        }
    }

    // prev icon — left of play
    tft.fillRect(x, y + 2, 3, 11, ST77XX_WHITE);
    for (int i = 0; i < 6; i++) {
        tft.drawFastVLine(x + 3 + i, y + 2 + i, 11 - (i * 2), ST77XX_WHITE);
    }

    // next icon — right of play
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

    // background
    tft.fillRect(barX, barY, barW, barH, 0x2104); // dark grey in RGB565

    // filled portion
    int filled = 0;
    if (durationMs > 0) {
        filled = (int)((float)progressMs / durationMs * barW);
        filled = constrain(filled, 0, barW);
    }
    tft.fillRect(barX, barY, filled, barH, SPOTIFY_GREEN);
}

void draw_UI() {
    tft.fillScreen(ST77XX_BLACK);

    // album art placeholder — dark square with music note
    tft.fillRect(0, 0, ART_SIZE, SCREEN_H, 0x1082); // very dark blue-grey
    tft.fillRect(4, 4, ART_SIZE - 8, SCREEN_H - 8, 0x2104);

    // simple music note in the art box
    tft.fillRect(ART_SIZE/2 - 6, SCREEN_H/2 - 12, 12, 3, SPOTIFY_GREEN);
    tft.fillRect(ART_SIZE/2 + 4, SCREEN_H/2 - 12, 3, 14, SPOTIFY_GREEN);
    tft.fillCircle(ART_SIZE/2 - 4, SCREEN_H/2 + 4, 5, SPOTIFY_GREEN);
    tft.fillCircle(ART_SIZE/2 + 7, SCREEN_H/2 + 2, 5, SPOTIFY_GREEN);

    // divider line between art and text area
    tft.drawFastVLine(ART_SIZE + 2, 0, SCREEN_H, 0x4208);

    // track name
    char truncTrack[20];
    truncate(currentTrack, truncTrack, 18);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(ART_SIZE + 6, 10);
    tft.print(truncTrack);

    // artist name
    char truncArtist[20];
    truncate(currentArtist, truncArtist, 18);
    tft.setTextColor(0x8410); // mid grey
    tft.setCursor(ART_SIZE + 6, 24);
    tft.print(truncArtist);

    draw_progress_bar();
    draw_icons(currentlyPlaying);
}

// updates just the parts of the UI that changed to minimize flicker
void draw_UI_partial(bool trackChanged) {
    if (trackChanged) {
        // clear and redraw text area only
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
        // clear track/artist, show loading
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

        sp.current_track_name(newTrack);   // fills newTrack with the track name
        sp.current_artist_names(newArtist); // fills newArtist with artist name(s)

        bool trackChanged = (strcmp(newTrack, currentTrack) != 0);

        strncpy(currentTrack, newTrack, sizeof(currentTrack) - 1);
        strncpy(currentArtist, newArtist, sizeof(currentArtist) - 1);
        currentlyPlaying = sp.is_playing();

        // progress and duration still need to come from the JSON
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
    if (nextPressed) {
        nextPressed = false;
        optimistic_UI("next");
        sp.skip();
        lastPollTime = millis(); // reset poll timer
        delay(800);              // give Spotify a moment to change track
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

    // poll every POLL_INTERVAL ms
    if (millis() - lastPollTime >= POLL_INTERVAL) {
        poll_spotify();
    }

    // tick progress bar locally between polls so it feels live
    if (currentlyPlaying && millis() - lastProgressTick >= 1000) {
        progressMs += 1000;
        lastProgressTick = millis();
        draw_progress_bar();
    }
}