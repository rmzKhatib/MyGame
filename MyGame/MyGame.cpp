// MyGame.cpp (SFML 3.x)
#include <SFML/Graphics.hpp>
#include <vector>
#include <cmath>
#include <string>
#include <algorithm>
#include <iostream>
#include <filesystem>
#include <optional>
#include <cstdint>

// ---------------- Helpers ----------------
static sf::Vector2f normalize(sf::Vector2f v) {
    float len = std::sqrt(v.x * v.x + v.y * v.y);
    if (len == 0.f) return { 0.f, 0.f };
    return { v.x / len, v.y / len };
}

static bool circleIntersectsRect(const sf::Vector2f& c, float r, const sf::FloatRect& rect) {
    float left = rect.position.x;
    float top = rect.position.y;
    float right = rect.position.x + rect.size.x;
    float bottom = rect.position.y + rect.size.y;

    float closestX = std::max(left, std::min(c.x, right));
    float closestY = std::max(top, std::min(c.y, bottom));

    float dx = c.x - closestX;
    float dy = c.y - closestY;
    return (dx * dx + dy * dy) < (r * r);
}

static bool circleIntersectsCircle(sf::Vector2f a, float ra, sf::Vector2f b, float rb) {
    sf::Vector2f d = a - b;
    float dist2 = d.x * d.x + d.y * d.y;
    float r = ra + rb;
    return dist2 < (r * r);
}

static sf::RectangleShape makeWall(float x, float y, float w, float h) {
    sf::RectangleShape r({ w, h });
    r.setPosition({ x, y });
    r.setFillColor(sf::Color(80, 80, 80));
    return r;
}

static void setCentered(sf::Text& t, float cx, float cy) {
    sf::FloatRect b = t.getLocalBounds();
    t.setOrigin({ b.position.x + b.size.x / 2.f,
                  b.position.y + b.size.y / 2.f });
    t.setPosition({ cx, cy });
}

static void fitSpriteToDiameter(sf::Sprite& spr, const sf::Texture& tex, float desiredDiameter) {
    sf::Vector2u s = tex.getSize();
    if (s.x == 0 || s.y == 0) return;

    spr.setOrigin({ s.x / 2.f, s.y / 2.f });

    float scaleX = desiredDiameter / static_cast<float>(s.x);
    float scaleY = desiredDiameter / static_cast<float>(s.y);
    spr.setScale({ scaleX, scaleY });
}

// Blend mode that "punches holes" in the darkness using the sprite/shape alpha
static sf::BlendMode ERASE_BLEND(
    sf::BlendMode::Factor::Zero,
    sf::BlendMode::Factor::OneMinusSrcAlpha,
    sf::BlendMode::Equation::Add
);

// ---------------- Main ----------------
int main() {
    const unsigned W = 900;
    const unsigned H = 650;

    const float PLAYER_RADIUS = 22.f;
    const float TARGET_RADIUS = 18.f;
    const float PLAYER_SPEED = 320.f;
    const float TIME_LIMIT = 30.f;

    // Animation (9 PNG frames for the player)
    const float ANIM_FPS = 12.f; // smooth color cycling
    const int   FRAME_COUNT = 9;

    // Window
    sf::RenderWindow window(sf::VideoMode({ W, H }), "67 Hunt");
    window.setFramerateLimit(120);

    enum class State { Playing, Win, Lose };
    State state = State::Playing;

    float timeLeft = TIME_LIMIT;
    sf::Clock clock;

    // =========================
    // Darkness / Flashlight overlay
    // =========================
    sf::RenderTexture darknessRT;
    if (!darknessRT.resize(sf::Vector2u(W, H))) {
        std::cout << "Failed to create darkness render texture.\n";
    }

    // Create once (don’t recreate every frame)
    sf::Sprite darknessSprite(darknessRT.getTexture());

    // how dark the map is (0 = not dark, 255 = fully black)
    const std::uint8_t DARK_ALPHA = 220;

    // flashlight radius in pixels
    float lightRadius = 170.f;

    // A full-screen dark rectangle
    sf::RectangleShape darknessRect(sf::Vector2f((float)W, (float)H));
    darknessRect.setFillColor(sf::Color(0, 0, 0, DARK_ALPHA));

    // The center "hole" circle that erases the darkness fully
    sf::CircleShape lightHole(lightRadius);
    lightHole.setOrigin(sf::Vector2f(lightRadius, lightRadius));
    lightHole.setFillColor(sf::Color(255, 255, 255, 255));

    // soft edge: multiple rings (simple fake gradient)
    const int SOFT_RINGS = 6;
    std::vector<sf::CircleShape> softHoles;
    softHoles.reserve(SOFT_RINGS);

    for (int i = 0; i < SOFT_RINGS; i++) {
        float r = lightRadius + i * 18.f;
        sf::CircleShape c(r);
        c.setOrigin(sf::Vector2f(r, r));

        // outer rings erase less (smaller alpha -> less erase -> looks like fade)
        int alphaInt = std::max(0, 255 - i * 40);
        std::uint8_t a = static_cast<std::uint8_t>(alphaInt);

        c.setFillColor(sf::Color(255, 255, 255, a));
        softHoles.push_back(c);
    }

    // ---- Player fallback circle ----
    sf::CircleShape playerCircle(PLAYER_RADIUS);
    playerCircle.setOrigin({ PLAYER_RADIUS, PLAYER_RADIUS });
    playerCircle.setPosition({ 100.f, 100.f });
    playerCircle.setFillColor(sf::Color::Cyan);

    // ---- Animated player sprite (9 PNGs) ----
    std::vector<sf::Texture> playerFrames;
    playerFrames.reserve(FRAME_COUNT);

    std::optional<sf::Sprite> playerSprite; // SFML 3: no default constructor

    int   currentFrame = 0;
    float animTimer = 0.f;
    float frameTime = 1.f / ANIM_FPS;

    bool framesOK = true;
    for (int i = 1; i <= FRAME_COUNT; ++i) {
        sf::Texture t;
        std::string path = "assets/sprites/six" + std::to_string(i) + ".png";
        if (!t.loadFromFile(path)) {
            std::cout << "Missing player frame: " << path << "\n";
            framesOK = false;
            break;
        }
        playerFrames.push_back(std::move(t));
    }

    if (framesOK) {
        sf::Sprite s(playerFrames[0]);
        fitSpriteToDiameter(s, playerFrames[0], PLAYER_RADIUS * 2.f);
        s.setPosition({ 100.f, 100.f });
        playerSprite = s;
    }
    else {
        std::cout << "Player frames not loaded, using circle.\n";
    }

    // ---- Target (keep circle for now) ----
    sf::CircleShape targetCircle(TARGET_RADIUS);
    targetCircle.setOrigin({ TARGET_RADIUS, TARGET_RADIUS });
    targetCircle.setPosition({ 780.f, 520.f });
    targetCircle.setFillColor(sf::Color::Yellow);

    // ---- Walls ----
    std::vector<sf::RectangleShape> walls;
    walls.push_back(makeWall(0, 0, (float)W, 20));
    walls.push_back(makeWall(0, (float)H - 20, (float)W, 20));
    walls.push_back(makeWall(0, 0, 20, (float)H));
    walls.push_back(makeWall((float)W - 20, 0, 20, (float)H));

    walls.push_back(makeWall(200, 120, 450, 25));
    walls.push_back(makeWall(150, 260, 25, 250));
    walls.push_back(makeWall(350, 420, 380, 25));
    walls.push_back(makeWall(650, 180, 25, 190));

    // ---- Font + UI ----
    sf::Font font;
    if (!font.openFromFile("assets/fonts/arial.ttf")) {
        std::cout << "FAILED to load font assets/fonts/arial.ttf\n";
    }

    sf::Text timerText(font, "Time: 30");
    timerText.setCharacterSize(24);
    timerText.setFillColor(sf::Color::White);
    timerText.setPosition({ 20.f, 20.f });

    sf::Text centerText(font, "");
    centerText.setCharacterSize(52);
    centerText.setFillColor(sf::Color::White);

    sf::Text hintText(font, "Press R to restart");
    hintText.setCharacterSize(22);
    hintText.setFillColor(sf::Color(220, 220, 220));
    hintText.setPosition({ 20.f, 55.f });

    // Position helpers
    auto getPlayerPos = [&]() -> sf::Vector2f {
        return playerSprite ? playerSprite->getPosition()
            : playerCircle.getPosition();
        };

    auto setPlayerPos = [&](sf::Vector2f p) {
        if (playerSprite) playerSprite->setPosition(p);
        playerCircle.setPosition(p);
        };

    // ---------------- Main loop ----------------
    while (window.isOpen()) {
        float dt = clock.restart().asSeconds();

        while (auto ev = window.pollEvent()) {
            if (ev->is<sf::Event::Closed>()) window.close();
        }

        // Restart
        if (state != State::Playing &&
            sf::Keyboard::isKeyPressed(sf::Keyboard::Key::R)) {

            state = State::Playing;
            timeLeft = TIME_LIMIT;
            setPlayerPos({ 100.f, 100.f });
            currentFrame = 0;
            animTimer = 0.f;
        }

        // Animate player
        if (playerSprite && state == State::Playing && framesOK) {
            animTimer += dt;
            while (animTimer >= frameTime) {
                animTimer -= frameTime;
                currentFrame = (currentFrame + 1) % FRAME_COUNT;
                playerSprite->setTexture(playerFrames[currentFrame], true);
                fitSpriteToDiameter(*playerSprite, playerFrames[currentFrame], PLAYER_RADIUS * 2.f);
            }
        }

        // Update game
        if (state == State::Playing) {
            timeLeft -= dt;
            if (timeLeft <= 0.f) {
                timeLeft = 0.f;
                state = State::Lose;
            }

            sf::Vector2f dir(0.f, 0.f);
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::W)) dir.y -= 1.f;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::S)) dir.y += 1.f;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A)) dir.x -= 1.f;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::D)) dir.x += 1.f;
            dir = normalize(dir);

            sf::Vector2f oldPos = getPlayerPos();
            setPlayerPos(oldPos + dir * PLAYER_SPEED * dt);

            for (auto& w : walls) {
                if (circleIntersectsRect(getPlayerPos(), PLAYER_RADIUS, w.getGlobalBounds())) {
                    setPlayerPos(oldPos);
                    break;
                }
            }

            if (circleIntersectsCircle(
                getPlayerPos(), PLAYER_RADIUS,
                targetCircle.getPosition(), TARGET_RADIUS)) {
                state = State::Win;
            }
        }

        // UI
        timerText.setString("Time: " + std::to_string((int)std::ceil(timeLeft)));

        if (state == State::Win) {
            centerText.setString("YOU MADE 67!");
            setCentered(centerText, W / 2.f, H / 2.f);
        }
        else if (state == State::Lose) {
            centerText.setString("TIME'S UP!");
            setCentered(centerText, W / 2.f, H / 2.f);
        }

        // Update flashlight position
        sf::Vector2f p = getPlayerPos();
        lightHole.setPosition(p);
        for (auto& c : softHoles) c.setPosition(p);

        // Render world
        window.clear({ 15, 15, 20 });

        window.draw(targetCircle);
        for (auto& w : walls) window.draw(w);

        if (playerSprite) window.draw(*playerSprite);
        else window.draw(playerCircle);

        // =========================
        // Build + draw darkness overlay
        // =========================
        darknessRT.clear(sf::Color(0, 0, 0, 0));
        darknessRT.draw(darknessRect);

        // Strong center + soft edge
        darknessRT.draw(lightHole, ERASE_BLEND);
        for (auto& c : softHoles) darknessRT.draw(c, ERASE_BLEND);

        darknessRT.display();

        window.draw(darknessSprite);

        // UI on top
        window.draw(timerText);
        if (state != State::Playing) {
            window.draw(centerText);
            window.draw(hintText);
        }

        window.display();
    }

    return 0;
}
