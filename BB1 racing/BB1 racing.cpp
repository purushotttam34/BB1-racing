// Hill Climb Racing-style Game with Levels, Fuel, Flipping, and Score (SFML Only)
// Hill Climb Racing-style Game in SFML with Fuel, Score, Levels, and Flip Detection

// SFML Hill-Style Climber
// Single-file C++ game inspired by Hill Climb Racing (not a clone).
// Requirements implemented per user request:
//  - Start screen: press 1 to play, 0 to exit
//  - Man on a car; head rotates with car and game over if head hits ground
//  - Black car, white background; camera follows car
//  - Hold accelerate/decelerate to induce flips; gravity & terrain following
//  - 5 levels: lengths 100m,200m,300m,400m,500m (increasing steepness)
//  - Terrain black; pr

#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>
#include <SFML/System.hpp>
#include <cmath>
#include <vector>
#include <string>
#include <iostream>
#include <iomanip>

// ---------------------------- Config ---------------------------------
static const unsigned WINDOW_W = 1280;
static const unsigned WINDOW_H = 720;
static const float   PPM = 8.0f;      // pixels per meter (1 m = 8 px)
static const float   GRAVITY = 40.0f;     // px/s^2 downward
static const float   DT_FIXED = 1.0f / 120.f;// fixed-step physics


// Level lengths (meters)
static const int LEVEL_METERS[5] = { 300, 500, 700, 900, 1100 };

// Fuel rules
static const float FUEL_TANK_METERS = 100.0f;    // full tank lasts 50 m
static const float FUEL_CAN_GAP_M = 80.0f;    // cans appear every 40 m

// Coin rules
static const int   COINS_PER_LEVEL = 20;
static const float COIN_GAP_M = 10.0f;    // approx spacing target

// ---------------------------- Helpers ---------------------------------
float clampf(float v, float lo, float hi) { return std::max(lo, std::min(v, hi)); }

// Procedural terrain function; returns ground Y (px) and slope (dy/dx) for a given x (px)
struct GroundSample { float y; float slope; };

GroundSample sampleGround(float x_px, int levelIndex) {
    // Base ground around 3/4 height up the screen from top (white background)
    float base = WINDOW_H * 0.80f; // lower is larger y

    // Increase roughness with level
    float rough = 15.0f + levelIndex * 10.0f;   // amplitude multiplier (px)
    float freq1 = 1.0f / 140.0f + levelIndex * 0.0008f; // spatial frequency
    float freq2 = 1.0f / 280.0f + levelIndex * 0.0005f;

    float y = base
        - rough * std::sin(x_px * freq1)
        - 0.6f * rough * std::sin(x_px * freq2 + 1.7f)
        - 0.3f * rough * std::sin(x_px * (freq1 * 2.3f) + 0.6f);

    // Numerical slope via small delta
    float dx = 1.0f;
    float y2 = base
        - rough * std::sin((x_px + dx) * freq1)
        - 0.6f * rough * std::sin((x_px + dx) * freq2 + 1.7f)
        - 0.3f * rough * std::sin((x_px + dx) * (freq1 * 2.3f) + 0.6f);
    float slope = (y2 - y) / dx; // dy/dx in px/px
    return { y, slope };
}

// Convert meters to pixels and vice versa
inline float m2px(float m) { return m * PPM; }
inline float px2m(float px) { return px / PPM; }

// ---------------------------- Entities --------------------------------
struct FuelCan { float x_px; bool taken = false; };
struct Coin { float x_px; float y_px; bool taken = false; };

struct Vehicle {
    // Physical state (car chassis center of mass)
    float x_px = 50.0f;
    float y_px = 400.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float angle = 0.0f;   // radians (0 along +x)
    float angV = 0.0f;   // rad/s

    // Dimensions
    float bodyW = 90.0f;
    float bodyH = 28.0f;
    float wheelBase = 70.0f; // distance between wheels (px)
    float wheelR = 18.0f;

    // Controls
    bool pressingLeft = false, pressingRight = false;

    void reset(float startX, float groundY) {
        x_px = startX; y_px = groundY - wheelR - bodyH * 0.5f - 2.0f;
        vx = vy = 0.0f; angle = 0.02f; angV = 0.0f;
    }

    // Local->world helper
    sf::Vector2f localToWorld(float lx, float ly) const {
        float c = std::cos(angle), s = std::sin(angle);
        return sf::Vector2f(x_px + c * lx - s * ly, y_px + s * lx + c * ly);
    }

    sf::Vector2f frontWheelPos() const { return localToWorld(+wheelBase * 0.5f, bodyH * 0.5f); }
    sf::Vector2f rearWheelPos()  const { return localToWorld(-wheelBase * 0.5f, bodyH * 0.5f); }
    sf::Vector2f headPos()       const { return localToWorld(0.0f, -bodyH * 0.9f); }
};

// ---------------------------- Game State ------------------------------
enum class Screen { Menu, Playing, GameOver, LevelComplete, Exit };

struct Button { sf::RectangleShape rect; sf::Text text; bool hovered = false; };

struct Level {
    int index = 0; // 0..4
    float length_m = 100.0f;
    float length_px = 800.0f;
    float finishX_px = 800.0f;

    std::vector<FuelCan> cans;
    std::vector<Coin> coins;
};

struct Game {
    Screen screen = Screen::Menu;
    sf::Font font; bool hasFont = false;

    int unlockedLevels = 1; // must beat previous to unlock next
    int currentLevel = 0;

    Vehicle car;
    Level level;

    // Progress
    float fuel_m = FUEL_TANK_METERS; // remaining meters worth of fuel
    float lastX_forFuel_px = 0.0f;   // to deduct fuel by horizontal travel

    int coinsCollected = 0;
    float levelDistance_m = 0.0f; // current level distance traveled

    // Totals across all finished levels
    float totalDistance_m = 0.0f;
    int   totalCoins = 0;

    bool headHitGround = false;

    // Menu buttons
    Button playButton;
    Button exitButton;

    void setupFont() {
        if (font.loadFromFile("C:/Windows/Fonts/arial.ttf")) hasFont = true;
        else hasFont = false; // HUD will be minimal but playable

        // Initialize buttons if font is loaded
        if (hasFont) {
            // Play button
            playButton.rect.setSize(sf::Vector2f(200.0f, 50.0f));
            playButton.rect.setFillColor(sf::Color(0, 120, 255)); // Blue
            playButton.rect.setOutlineColor(sf::Color::Black);
            playButton.rect.setOutlineThickness(2.0f);
            playButton.text.setFont(font);
            playButton.text.setString("Play");
            playButton.text.setCharacterSize(24);
            playButton.text.setFillColor(sf::Color::White);
            // Center button
            playButton.rect.setPosition((WINDOW_W - 200.0f) / 2, (WINDOW_H - 50.0f * 2 - 20.0f) / 2);
            sf::FloatRect textBounds = playButton.text.getLocalBounds();
            playButton.text.setPosition(
                playButton.rect.getPosition().x + (200.0f - textBounds.width) / 2,
                playButton.rect.getPosition().y + (50.0f - textBounds.height) / 2 - textBounds.top
            );

            // Exit button
            exitButton.rect.setSize(sf::Vector2f(200.0f, 50.0f));
            exitButton.rect.setFillColor(sf::Color(0, 120, 255)); // Blue
            exitButton.rect.setOutlineColor(sf::Color::Black);
            exitButton.rect.setOutlineThickness(2.0f);
            exitButton.text.setFont(font);
            exitButton.text.setString("Exit");
            exitButton.text.setCharacterSize(24);
            exitButton.text.setFillColor(sf::Color::White);
            // Center button below play button
            exitButton.rect.setPosition((WINDOW_W - 200.0f) / 2, playButton.rect.getPosition().y + 50.0f + 20.0f);
            textBounds = exitButton.text.getLocalBounds();
            exitButton.text.setPosition(
                exitButton.rect.getPosition().x + (200.0f - textBounds.width) / 2,
                exitButton.rect.getPosition().y + (50.0f - textBounds.height) / 2 - textBounds.top
            );
        }
    }

    void buildLevel(int idx) {
        currentLevel = idx;
        level.index = idx;
        level.length_m = static_cast<float>(LEVEL_METERS[idx]);
        level.length_px = m2px(level.length_m);
        level.finishX_px = level.length_px;

        // Build fuel cans every 40m
        level.cans.clear();
        float gap_px = m2px(FUEL_CAN_GAP_M);
        for (float x = m2px(20.0f); x < level.finishX_px; x += gap_px) {
            level.cans.push_back({ x,false });
        }

        // Build coins: 20 coins ~10m apart, hovering a bit above ground
        level.coins.clear();
        float coinGap_px = level.length_px / (COINS_PER_LEVEL + 1);
        for (int i = 1; i <= COINS_PER_LEVEL; i++) {
            float x = i * coinGap_px;
            auto g = sampleGround(x, currentLevel);
            float y = g.y - 50.0f; // hover above ground
            level.coins.push_back({ x, y, false });
        }

        // Reset progress
        fuel_m = FUEL_TANK_METERS;
        lastX_forFuel_px = 0.0f;
        levelDistance_m = 0.0f;
        headHitGround = false;

        // Place vehicle at start
        auto g0 = sampleGround(0.0f, currentLevel);
        car.reset(10.0f, g0.y);
    }

    void resetGame() {
        unlockedLevels = 1;
        currentLevel = 0;
        totalDistance_m = 0.0f;
        totalCoins = 0;
        coinsCollected = 0;
        fuel_m = FUEL_TANK_METERS;
        lastX_forFuel_px = 0.0f;
        levelDistance_m = 0.0f;
        headHitGround = false;
        car.pressingLeft = false;
        car.pressingRight = false;
        buildLevel(0);
    }
};

// ---------------------------- Physics ---------------------------------
void stepVehicle(Game& G, float dt) {
    Vehicle& V = G.car;

    // Simple gravity
    V.vy += GRAVITY * dt;

    // Input forces
    const float accel = 120.0f;      // px/s^2 along facing direction (flat)
    const float maxVx = 260.0f;      // px/s
    const float torque = 1.8f;       // rad/s^2 (air)

    if (V.pressingRight) {
        V.vx += accel * dt;
        V.angV -= torque * dt; // tendency to front flip
    }
    if (V.pressingLeft) {
        V.vx -= accel * dt;
        V.angV += torque * dt; // tendency to back flip
    }
    V.vx = clampf(V.vx, -maxVx, maxVx);

    // Integrate tentative position
    V.x_px += V.vx * dt;
    V.y_px += V.vy * dt;
    V.angle += V.angV * dt;

    // Wheel-ground collision & alignment
    auto fixWheel = [&](sf::Vector2f wp) {
        auto gs = sampleGround(wp.x, G.currentLevel);
        float groundY = gs.y - V.wheelR;
        float dy = wp.y - groundY;
        if (dy > 0.0f) { // wheel penetrates ground -> push car up
            // Move chassis up by dy projected along body-down direction (approx)
            V.y_px -= dy;
            V.vy = std::min(0.0f, V.vy);
            // Dampen angular velocity and align angle slightly with slope
            float targetAngle = std::atan2(gs.slope, 1.0f);
            float alignRate = 4.5f * dt;
            // wrap to nearest
            float da = targetAngle - V.angle;
            while (da > 3.14159f) da -= 6.28318f;
            while (da < -3.14159f) da += 6.28318f;
            V.angle += clampf(da, -alignRate, alignRate);
            V.angV *= 0.85f;
            // Ground friction affecting vx
            V.vx *= 0.99f;
        }
        };

    fixWheel(V.frontWheelPos());
    fixWheel(V.rearWheelPos());

    // Air drag & angular damping
    V.vx *= 0.999f;
    V.angV *= 0.999f;
}

// Deduct fuel based on horizontal distance traveled; refill on can pickup
void updateFuelAndPickups(Game& G) {
    float dx_px = std::fabs(G.car.x_px - G.lastX_forFuel_px);
    if (dx_px > 0.0f) {
        float consumed_m = px2m(dx_px);
        G.fuel_m = std::max(0.0f, G.fuel_m - consumed_m);
        G.levelDistance_m += consumed_m;
        G.lastX_forFuel_px = G.car.x_px;
    }

    // Fuel cans
    for (auto& c : G.level.cans) {
        if (!c.taken && std::fabs(c.x_px - G.car.x_px) < 20.0f) {
            c.taken = true;
            G.fuel_m = FUEL_TANK_METERS; // refill to full
        }
    }

    // Coins
    for (auto& coin : G.level.coins) {
        if (!coin.taken) {
            float dx = coin.x_px - G.car.x_px;
            float dy = coin.y_px - G.car.y_px;
            if (std::sqrt(dx * dx + dy * dy) < 28.0f) {
                coin.taken = true;
                G.coinsCollected++;
            }
        }
    }
}

// Head-ground collision -> game over
bool checkHeadHit(const Game& G) {
    auto hp = G.car.headPos();
    auto gs = sampleGround(hp.x, G.currentLevel);
    return (hp.y >= gs.y - 3.0f); // small tolerance
}

// ---------------------------- Rendering -------------------------------
void drawTerrain(sf::RenderWindow& win, const Game& G, float xStart, float xEnd) {
    const float step = 8.0f; // px
    sf::VertexArray strip(sf::TriangleStrip);

    for (float x = xStart; x <= xEnd; x += step) {
        auto gs = sampleGround(x, G.currentLevel);
        strip.append(sf::Vertex(sf::Vector2f(x, gs.y), sf::Color::Black));
        strip.append(sf::Vertex(sf::Vector2f(x, WINDOW_H), sf::Color::Black));
    }

    win.draw(strip);
}

void drawVehicle(sf::RenderWindow& win, const Game& G) {
    const Vehicle& V = G.car;

    // Wheels
    sf::CircleShape wheel(V.wheelR);
    wheel.setOrigin(V.wheelR, V.wheelR);
    wheel.setFillColor(sf::Color(30, 30, 30));

    auto fw = V.frontWheelPos();
    auto rw = V.rearWheelPos();
    wheel.setPosition(fw); win.draw(wheel);
    wheel.setPosition(rw); win.draw(wheel);

    // Chassis (black)
    sf::RectangleShape body(sf::Vector2f(V.bodyW, V.bodyH));
    body.setOrigin(V.bodyW * 0.5f, V.bodyH * 0.5f);
    body.setFillColor(sf::Color::Black);
    body.setPosition(V.x_px, V.y_px);
    body.setRotation(V.angle * 180.0f / 3.1415926f);
    win.draw(body);

    // Man: simple seat + head; head follows rotation
    // Seat / torso
    sf::RectangleShape torso(sf::Vector2f(V.bodyH * 0.6f, V.bodyH * 0.8f));
    torso.setOrigin(torso.getSize().x * 0.5f, torso.getSize().y);
    auto seat = V.localToWorld(-V.bodyW * 0.1f, -V.bodyH * 0.1f);
    torso.setPosition(seat);
    torso.setRotation(V.angle * 180.0f / 3.1415926f);
    torso.setFillColor(sf::Color(60, 60, 60));
    win.draw(torso);

    // Head
    float headR = V.bodyH * 0.28f;
    sf::CircleShape head(headR);
    head.setOrigin(headR, headR);
    auto headP = V.headPos();
    head.setPosition(headP);
    head.setFillColor(sf::Color(80, 80, 80));
    win.draw(head);
}

void drawHUD(sf::RenderWindow& win, const Game& G) {
    // Background is white; draw black HUD elements
    // Fuel bar
    float barW = 280.0f, barH = 18.0f;
    sf::RectangleShape outline(sf::Vector2f(barW, barH));
    outline.setPosition(20.0f, 20.0f);
    outline.setFillColor(sf::Color::Transparent);
    outline.setOutlineColor(sf::Color::Black);
    outline.setOutlineThickness(2.0f);
    win.draw(outline);

    float pct = clampf(G.fuel_m / FUEL_TANK_METERS, 0.0f, 1.0f);
    sf::RectangleShape fill(sf::Vector2f(barW * pct, barH));
    fill.setPosition(20.0f, 20.0f);
    fill.setFillColor(sf::Color::Black);
    win.draw(fill);

    if (G.hasFont) {
        // Distance & coins
        sf::Text t;
        t.setFont(G.font);
        t.setCharacterSize(18);
        t.setFillColor(sf::Color::Black);

        char buf[128];
        std::snprintf(buf, sizeof(buf), "Level %d  Dist: %.1fm  Coins: %d", G.currentLevel + 1, G.levelDistance_m, G.coinsCollected);
        t.setString(buf);
        t.setPosition(20.0f, 46.0f);
        win.draw(t);
    }
}

void drawPickups(sf::RenderWindow& win, const Game& G, float xStart, float xEnd) {
    // Fuel cans
    for (const auto& c : G.level.cans) {
        if (c.taken) continue;
        if (c.x_px < xStart - 50 || c.x_px > xEnd + 50) continue;
        auto gs = sampleGround(c.x_px, G.currentLevel);
        sf::RectangleShape can(sf::Vector2f(18.0f, 22.0f));
        can.setOrigin(9.0f, 11.0f);
        can.setPosition(c.x_px, gs.y - 18.0f);
        can.setFillColor(sf::Color::Black);
        win.draw(can);
    }

    // Coins
    for (const auto& coin : G.level.coins) {
        if (coin.taken) continue;
        if (coin.x_px < xStart - 50 || coin.x_px > xEnd + 50) continue;
        sf::CircleShape c(8.0f, 12);
        c.setOrigin(8.0f, 8.0f);
        c.setPosition(coin.x_px, coin.y_px);
        c.setFillColor(sf::Color(0, 0, 0));
        win.draw(c);
    }
}

// ---------------------------- Screens ---------------------------------
void drawMenu(sf::RenderWindow& win, Game& G) {
    win.clear(sf::Color::White);

    if (G.hasFont) {
        // Draw title
        sf::Text title("Hill-Style Climber", G.font, 42);
        title.setFillColor(sf::Color::Black);
        title.setPosition((WINDOW_W - title.getLocalBounds().width) / 2, 80);
        win.draw(title);

        // Update button hover state
        sf::Vector2i mousePos = sf::Mouse::getPosition(win);
        G.playButton.hovered = G.playButton.rect.getGlobalBounds().contains(static_cast<float>(mousePos.x), static_cast<float>(mousePos.y));
        G.exitButton.hovered = G.exitButton.rect.getGlobalBounds().contains(static_cast<float>(mousePos.x), static_cast<float>(mousePos.y));

        // Adjust button colors for hover effect
        G.playButton.rect.setFillColor(G.playButton.hovered ? sf::Color(0, 80, 200) : sf::Color(0, 120, 255));
        G.exitButton.rect.setFillColor(G.exitButton.hovered ? sf::Color(0, 80, 200) : sf::Color(0, 120, 255));

        // Draw buttons
        win.draw(G.playButton.rect);
        win.draw(G.playButton.text);
        win.draw(G.exitButton.rect);
        win.draw(G.exitButton.text);
    }
    else {
        // Fallback if font fails to load
        sf::Text fallback("Click top half to Play\nClick bottom half to Exit", G.font, 28);
        fallback.setFillColor(sf::Color::Black);
        fallback.setPosition((WINDOW_W - fallback.getLocalBounds().width) / 2, (WINDOW_H - fallback.getLocalBounds().height) / 2);
        win.draw(fallback);
    }

    win.display();
}

// Modified drawGameOver function
void drawGameOver(sf::RenderWindow& win, Game& G) {
    win.clear(sf::Color::White);

    if (G.hasFont) {
        sf::Text t("Game Over", G.font, 48);
        t.setFillColor(sf::Color::Black);
        t.setPosition((WINDOW_W - t.getLocalBounds().width) / 2, 80);
        win.draw(t);

        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "Distance travelled: %.1fm\nCoins obtained: %d",
            G.totalDistance_m, G.totalCoins);
        sf::Text s(buf, G.font, 28);
        s.setFillColor(sf::Color::Black);
        s.setPosition((WINDOW_W - s.getLocalBounds().width) / 2, 160);
        win.draw(s);

        sf::Vector2i mousePos = sf::Mouse::getPosition(win);
        sf::Color arrowNormal = sf::Color::Black;
        sf::Color arrowHover = sf::Color::Red;
        sf::Color buttonNormal = sf::Color(0, 120, 255);
        sf::Color buttonHover = sf::Color(0, 80, 200);
        sf::Color buttonTextColor = sf::Color::White;

        // Draw left arrow
        sf::Text leftArrow(sf::String(static_cast<sf::Uint32>(0x2190)), G.font, 60);
        leftArrow.setFillColor(leftArrow.getGlobalBounds().contains(static_cast<sf::Vector2f>(mousePos)) ? arrowHover : arrowNormal);
        leftArrow.setPosition(300, 350);
        win.draw(leftArrow);

        // Draw right arrow
        sf::Text rightArrow(sf::String(static_cast<sf::Uint32>(0x2192)), G.font, 60);
        rightArrow.setFillColor(rightArrow.getGlobalBounds().contains(static_cast<sf::Vector2f>(mousePos)) ? arrowHover : arrowNormal);
        rightArrow.setPosition(900, 350);
        win.draw(rightArrow);

        // Draw retry button (middle)
        sf::RectangleShape retryButton(sf::Vector2f(200.0f, 50.0f));
        retryButton.setPosition(540.0f, 350.0f);
        retryButton.setFillColor(retryButton.getGlobalBounds().contains(static_cast<sf::Vector2f>(mousePos)) ? buttonHover : buttonNormal);
        retryButton.setOutlineColor(sf::Color::Black);
        retryButton.setOutlineThickness(2.0f);
        win.draw(retryButton);

        sf::Text retryText("Retry", G.font, 24);
        retryText.setFillColor(buttonTextColor);
        sf::FloatRect textBounds = retryText.getLocalBounds();
        retryText.setPosition(
            540.0f + (200.0f - textBounds.width) / 2,
            350.0f + (50.0f - textBounds.height) / 2 - textBounds.top
        );
        win.draw(retryText);

        // Draw exit button (lower)
        sf::RectangleShape exitButton(sf::Vector2f(200.0f, 50.0f));
        exitButton.setPosition(540.0f, 500.0f);
        exitButton.setFillColor(exitButton.getGlobalBounds().contains(static_cast<sf::Vector2f>(mousePos)) ? buttonHover : buttonNormal);
        exitButton.setOutlineColor(sf::Color::Black);
        exitButton.setOutlineThickness(2.0f);
        win.draw(exitButton);

        sf::Text exitText("Exit", G.font, 24);
        exitText.setFillColor(buttonTextColor);
        textBounds = exitText.getLocalBounds();
        exitText.setPosition(
            540.0f + (200.0f - textBounds.width) / 2,
            500.0f + (50.0f - textBounds.height) / 2 - textBounds.top
        );
        win.draw(exitText);

        // Hint (updated to reflect Exit as close, but key remains Backspace for main menu; adjust as needed)
        sf::Text hint("Left/Right: Change Level   R: Restart   Backspace: Main Menu", G.font, 22);
        hint.setFillColor(sf::Color::Black);
        hint.setPosition((WINDOW_W - hint.getLocalBounds().width) / 2, 620);
        win.draw(hint);
    }
    win.display();
}

void drawLevelCompleteMenu(sf::RenderWindow& win, Game& G) {
    win.clear(sf::Color::White);

    if (G.hasFont) {
        sf::Text t("Level Complete!", G.font, 48);
        t.setFillColor(sf::Color::Black);
        t.setPosition((WINDOW_W - t.getLocalBounds().width) / 2, 80);
        win.draw(t);

        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "Level %d\nDistance: %.1fm\nCoins: %d",
            G.currentLevel + 1, G.levelDistance_m, G.coinsCollected);
        sf::Text s(buf, G.font, 28);
        s.setFillColor(sf::Color::Black);
        s.setPosition((WINDOW_W - s.getLocalBounds().width) / 2, 160);
        win.draw(s);

        sf::Vector2i mousePos = sf::Mouse::getPosition(win);
        sf::Color arrowNormal = sf::Color::Black;
        sf::Color arrowHover = sf::Color::Red;
        sf::Color buttonNormal = sf::Color(0, 120, 255);
        sf::Color buttonHover = sf::Color(0, 80, 200);
        sf::Color buttonTextColor = sf::Color::White;

        // Draw left arrow
        sf::Text leftArrow(sf::String(static_cast<sf::Uint32>(0x2190)), G.font, 60);
        leftArrow.setFillColor(leftArrow.getGlobalBounds().contains(static_cast<sf::Vector2f>(mousePos)) ? arrowHover : arrowNormal);
        leftArrow.setPosition(300, 350);
        win.draw(leftArrow);

        // Draw right arrow
        sf::Text rightArrow(sf::String(static_cast<sf::Uint32>(0x2192)), G.font, 60);
        rightArrow.setFillColor(rightArrow.getGlobalBounds().contains(static_cast<sf::Vector2f>(mousePos)) ? arrowHover : arrowNormal);
        rightArrow.setPosition(900, 350);
        win.draw(rightArrow);

        // Draw retry button (middle)
        sf::RectangleShape retryButton(sf::Vector2f(200.0f, 50.0f));
        retryButton.setPosition(540.0f, 350.0f);
        retryButton.setFillColor(retryButton.getGlobalBounds().contains(static_cast<sf::Vector2f>(mousePos)) ? buttonHover : buttonNormal);
        retryButton.setOutlineColor(sf::Color::Black);
        retryButton.setOutlineThickness(2.0f);
        win.draw(retryButton);

        sf::Text retryText("Retry", G.font, 24);
        retryText.setFillColor(buttonTextColor);
        sf::FloatRect textBounds = retryText.getLocalBounds();
        retryText.setPosition(
            540.0f + (200.0f - textBounds.width) / 2,
            350.0f + (50.0f - textBounds.height) / 2 - textBounds.top
        );
        win.draw(retryText);

        // Draw exit button (lower)
        sf::RectangleShape exitButton(sf::Vector2f(200.0f, 50.0f));
        exitButton.setPosition(540.0f, 500.0f);
        exitButton.setFillColor(exitButton.getGlobalBounds().contains(static_cast<sf::Vector2f>(mousePos)) ? buttonHover : buttonNormal);
        exitButton.setOutlineColor(sf::Color::Black);
        exitButton.setOutlineThickness(2.0f);
        win.draw(exitButton);

        sf::Text exitText("Exit", G.font, 24);
        exitText.setFillColor(buttonTextColor);
        textBounds = exitText.getLocalBounds();
        exitText.setPosition(
            540.0f + (200.0f - textBounds.width) / 2,
            500.0f + (50.0f - textBounds.height) / 2 - textBounds.top
        );
        win.draw(exitText);

        // Hint
        sf::Text hint("Left/Right: Change Level   R: Restart   Backspace: Main Menu", G.font, 22);
        hint.setFillColor(sf::Color::Black);
        hint.setPosition((WINDOW_W - hint.getLocalBounds().width) / 2, 620);
        win.draw(hint);
    }
    win.display();
}

// ---------------------------- Main ------------------------------------
int main() {
    sf::RenderWindow window(sf::VideoMode(WINDOW_W, WINDOW_H), "Hill-Style Climber");
    window.setFramerateLimit(120);

    Game G;
    G.setupFont();

    // Initial level
    G.buildLevel(0);

    sf::Clock clock;
    float accumulator = 0.0f;

    // Camera view
    sf::View view(sf::FloatRect(0, 0, WINDOW_W, WINDOW_H));

    while (window.isOpen()) {
        // ---------------- Events ----------------
        sf::Event ev;
        while (window.pollEvent(ev)) {
            if (ev.type == sf::Event::Closed)
                window.close();

            if (ev.type == sf::Event::MouseButtonPressed) {
                if (ev.mouseButton.button == sf::Mouse::Left) {
                    sf::Vector2f mousePos(ev.mouseButton.x, ev.mouseButton.y);
                    if (G.screen == Screen::Menu) {
                        if (G.hasFont) {
                            if (G.playButton.rect.getGlobalBounds().contains(mousePos)) {
                                G.screen = Screen::Playing;
                                G.resetGame();
                            }
                            else if (G.exitButton.rect.getGlobalBounds().contains(mousePos)) {
                                G.screen = Screen::Exit;
                                window.close();
                            }
                        }
                        else {
                            // Fallback: click top half for play, bottom half for exit
                            if (ev.mouseButton.y < WINDOW_H / 2) {
                                G.screen = Screen::Playing;
                                G.resetGame();
                            }
                            else {
                                G.screen = Screen::Exit;
                                window.close();
                            }
                        }
                    }
                    else if (G.screen == Screen::LevelComplete && G.hasFont) {
                        sf::Text leftArrow(sf::String(static_cast<sf::Uint32>(0x2190)), G.font, 60); // ←
                        leftArrow.setPosition(300, 350);
                        if (leftArrow.getGlobalBounds().contains(mousePos)) {
                            // Previous level
                            int prev = std::max(G.currentLevel - 1, 0);
                            G.buildLevel(prev);
                            G.screen = Screen::Playing;
                        }
                        sf::RectangleShape retryButton(sf::Vector2f(200.0f, 50.0f));
                        retryButton.setPosition(540, 350);
                        if (retryButton.getGlobalBounds().contains(mousePos)) {
                            // Restart current level
                            G.buildLevel(G.currentLevel);
                            G.screen = Screen::Playing;
                        }
                        sf::Text rightArrow(sf::String(static_cast<sf::Uint32>(0x2192)), G.font, 60); // →
                        rightArrow.setPosition(900, 350);
                        if (rightArrow.getGlobalBounds().contains(mousePos)) {
                            // Next level
                            int next = std::min(G.currentLevel + 1, 4);
                            G.buildLevel(next);
                            G.screen = Screen::Playing;
                        }
                        sf::RectangleShape exitButton(sf::Vector2f(200.0f, 50.0f));
                        exitButton.setPosition(540, 500);
                        if (exitButton.getGlobalBounds().contains(mousePos)) {
                            // Return to main menu
                            G.screen = Screen::Menu;
                        }
                    }
                    else if (G.screen == Screen::GameOver && G.hasFont) {
                        sf::Text leftArrow(sf::String(static_cast<sf::Uint32>(0x2190)), G.font, 60); // ←
                        leftArrow.setPosition(300, 350);
                        if (leftArrow.getGlobalBounds().contains(mousePos)) {
                            // Previous level
                            int prev = std::max(G.currentLevel - 1, 0);
                            G.buildLevel(prev);
                            G.screen = Screen::Playing;
                        }
                        sf::RectangleShape retryButton(sf::Vector2f(200.0f, 50.0f));
                        retryButton.setPosition(540, 350);
                        if (retryButton.getGlobalBounds().contains(mousePos)) {
                            // Restart current level
                            G.buildLevel(G.currentLevel);
                            G.screen = Screen::Playing;
                        }
                        sf::Text rightArrow(sf::String(static_cast<sf::Uint32>(0x2192)), G.font, 60); // →
                        rightArrow.setPosition(900, 350);
                        if (rightArrow.getGlobalBounds().contains(mousePos)) {
                            // Next level
                            int next = std::min(G.currentLevel + 1, 4);
                            G.buildLevel(next);
                            G.screen = Screen::Playing;
                        }
                        sf::RectangleShape exitButton(sf::Vector2f(200.0f, 50.0f));
                        exitButton.setPosition(540, 500);
                        if (exitButton.getGlobalBounds().contains(mousePos)) {
                            // Return to main menu
                            G.screen = Screen::Menu;
                        }
                    }
                }
            }

            if (ev.type == sf::Event::KeyPressed) {
                if (G.screen == Screen::Menu) {
                    if (ev.key.code == sf::Keyboard::Num1) {
                        G.screen = Screen::Playing;
                        G.resetGame();
                    }
                    else if (ev.key.code == sf::Keyboard::Num0) {
                        G.screen = Screen::Exit;
                        window.close();
                    }
                }
                else if (G.screen == Screen::Playing) {
                    if (ev.key.code == sf::Keyboard::Right || ev.key.code == sf::Keyboard::D)
                        G.car.pressingRight = true;
                    if (ev.key.code == sf::Keyboard::Left || ev.key.code == sf::Keyboard::A)
                        G.car.pressingLeft = true;
                }
                else if (G.screen == Screen::GameOver) {
                    if (ev.key.code == sf::Keyboard::Num1) {
                        G.resetGame();
                        G.screen = Screen::Playing;
                    }
                    else if (ev.key.code == sf::Keyboard::Num0) {
                        window.close();
                    }
                }
                else if (G.screen == Screen::LevelComplete) {
                    if (ev.key.code == sf::Keyboard::Right) {
                        // Next level
                        int next = std::min(G.currentLevel + 1, 4);
                        G.buildLevel(next);
                        G.screen = Screen::Playing;
                    }
                    else if (ev.key.code == sf::Keyboard::Left) {
                        // Previous level
                        int prev = std::max(G.currentLevel - 1, 0);
                        G.buildLevel(prev);
                        G.screen = Screen::Playing;
                    }
                    else if (ev.key.code == sf::Keyboard::R) {
                        // Restart current level
                        G.buildLevel(G.currentLevel);
                        G.screen = Screen::Playing;
                    }
                    else if (ev.key.code == sf::Keyboard::BackSpace) {
                        // Return to main menu
                        G.screen = Screen::Menu;
                    }
                }
            }

            if (ev.type == sf::Event::KeyReleased && G.screen == Screen::Playing) {
                if (ev.key.code == sf::Keyboard::Right || ev.key.code == sf::Keyboard::D)
                    G.car.pressingRight = false;
                if (ev.key.code == sf::Keyboard::Left || ev.key.code == sf::Keyboard::A)
                    G.car.pressingLeft = false;
            }
        } // <-- closes while (pollEvent)

        // ---------------- Screen-specific logic ----------------
        if (G.screen == Screen::Menu) {
            drawMenu(window, G);
            continue;
        }
        if (G.screen == Screen::Exit) {
            break;
        }

        // ---------------- Fixed-step update ----------------
        float dt = clock.restart().asSeconds();
        accumulator += dt;
        while (accumulator >= DT_FIXED) {
            if (G.screen != Screen::Playing)
                break; // Stop updating if not playing

            stepVehicle(G, DT_FIXED);
            updateFuelAndPickups(G);

            // Head-ground check
            if (checkHeadHit(G)) {
                G.headHitGround = true;
            }

            // Finish line
            if (G.car.x_px >= G.level.finishX_px) {
                G.totalDistance_m += G.levelDistance_m;
                G.totalCoins += G.coinsCollected;

                int next = G.currentLevel + 1;
                if (next < 5) {
                    G.unlockedLevels = std::max(G.unlockedLevels, next + 1);
                    // Instead of immediately starting next level, show level complete menu
                    G.screen = Screen::LevelComplete;
                }
                else {
                    // All levels complete -> show final score
                    G.screen = Screen::GameOver;
                }
            }

            // Fuel empty or crash -> game over
            if (G.fuel_m <= 0.0f || G.headHitGround) {
                G.totalDistance_m += G.levelDistance_m;
                G.totalCoins += G.coinsCollected;
                G.screen = Screen::GameOver;
            }

            accumulator -= DT_FIXED;
        }

        if (G.screen == Screen::GameOver) {
            drawGameOver(window, G);
            continue;
        }
        if (G.screen == Screen::LevelComplete) {
            drawLevelCompleteMenu(window, G);
            continue;
        }

        // ---------------- Rendering (Playing) ----------------
        window.clear(sf::Color::White);

        // Camera follows car (clamped within level bounds + margins)
        float camX = G.car.x_px;
        float halfW = WINDOW_W * 0.5f;
        camX = clampf(camX, halfW, std::max(halfW, G.level.finishX_px - halfW));
        view.setCenter(camX, WINDOW_H * 0.5f);
        window.setView(view);

        // Draw terrain in view range
        float xStart = view.getCenter().x - halfW - 50.0f;
        float xEnd = view.getCenter().x + halfW + 50.0f;
        drawTerrain(window, G, std::max(0.0f, xStart), std::min(G.level.finishX_px + 200.0f, xEnd));

        // Draw pickups
        drawPickups(window, G, xStart, xEnd);

        // Draw car + man
        drawVehicle(window, G);

        // Reset to UI view for HUD
        window.setView(window.getDefaultView());
        drawHUD(window, G);

        window.display();
    } // <-- closes while(window.isOpen())

    return 0;
}