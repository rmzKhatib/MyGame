// MyGame.cpp (SFML 3.x compatible)
#include <SFML/Graphics.hpp>
#include <vector>
#include <cmath>
#include <string>
#include <algorithm>
#include <iostream>
#include <filesystem>
#include <optional>

// ---------------- Helpers ----------------
static sf::Vector2f normalize(sf::Vector2f v) {
    float len = std::sqrt(v.x * v.x + v.y * v.y);
    if (len == 0.f) return sf::Vector2f(0.f, 0.f);
    return sf::Vector2f(v.x / len, v.y / len);
}

// SFML 3: FloatRect uses .position and .size
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
    sf::RectangleShape r(sf::Vector2f(w, h));
    r.setPosition(sf::Vector2f(x, y));
    r.setFillColor(sf::Color(80, 80, 80));
    return r;
}

static void setCentered(sf::Text& t, float cx, float cy) {
    sf::FloatRect b = t.getLocalBounds();
    t.setOrigin(sf::Vector2f(b.position.x + b.size.x / 2.f, b.position.y + b.size.y / 2.f));
    t.setPosition(sf::Vector2f(cx, cy));
}

static void fitSpriteToDiameter(sf::Sprite& spr, const sf::Texture& tex, float desiredDiameter) {
    sf::Vector2u s = tex.getSize();
    if (s.x == 0 || s.y == 0) return;

    spr.setOrigin(sf::Vector2f(s.x / 2.f, s.y / 2.f));

    float scaleX = desiredDiameter / static_cast<float>(s.x);
    float scaleY = desiredDiameter / static_cast<float>(s.y);
    spr.setScale(sf::Vector2f(scaleX, scaleY));
}

// ---------------- Main ----------------
int main() {
    const unsigned W = 900;
    const unsigned H = 650;

    const float PLAYER_RADIUS = 22.f;
    const float TARGET_RADIUS = 18.f;

    const float PLAYER_SPEED = 320.f;
    const float TIME_LIMIT = 30.f;

    // Optional sprite paths (put files here later)
    const std::string PLAYER_SPRITE_PATH = "assets/sprites/player.png";
    const std::string TARGET_SPRITE_PATH = "assets/sprites/target.png";

    // SFML 3: VideoMode typically takes a Vector2u
    sf::RenderWindow window(sf::VideoMode(sf::Vector2u(W, H)), "67 Hunt");
    window.setFramerateLimit(120);

    enum class State { Playing, Win, Lose };
    State state = State::Playing;

    float timeLeft = TIME_LIMIT;
    sf::Clock clock;

    // ---- Player fallback circle ----
    sf::CircleShape playerCircle(PLAYER_RADIUS);
    playerCircle.setOrigin(sf::Vector2f(PLAYER_RADIUS, PLAYER_RADIUS));
    playerCircle.setPosition(sf::Vector2f(100.f, 100.f));
    playerCircle.setFillColor(sf::Color::Cyan);

    // ---- Player sprite optional ----
    sf::Texture playerTex;
    std::optional<sf::Sprite> playerSprite; // SFML 3: no default constructor
    if (playerTex.loadFromFile(PLAYER_SPRITE_PATH)) {
        sf::Sprite s(playerTex);
        fitSpriteToDiameter(s, playerTex, PLAYER_RADIUS * 2.f);
        s.setPosition(sf::Vector2f(100.f, 100.f));
        playerSprite = s;
    }
    else {
        std::cout << "Player sprite not found (ok for now): " << PLAYER_SPRITE_PATH << "\n";
    }

    // ---- Target fallback circle ----
    sf::CircleShape targetCircle(TARGET_RADIUS);
    targetCircle.setOrigin(sf::Vector2f(TARGET_RADIUS, TARGET_RADIUS));
    targetCircle.setPosition(sf::Vector2f(780.f, 520.f));
    targetCircle.setFillColor(sf::Color::Yellow);

    // ---- Target sprite optional ----
    sf::Texture targetTex;
    std::optional<sf::Sprite> targetSprite;
    if (targetTex.loadFromFile(TARGET_SPRITE_PATH)) {
        sf::Sprite s(targetTex);
        fitSpriteToDiameter(s, targetTex, TARGET_RADIUS * 2.f);
        s.setPosition(sf::Vector2f(780.f, 520.f));
        targetSprite = s;
    }
    else {
        std::cout << "Target sprite not found (ok for now): " << TARGET_SPRITE_PATH << "\n";
    }

    // ---- Walls ----
    std::vector<sf::RectangleShape> walls;

    // borders (thickness 20)
    walls.push_back(makeWall(0, 0, (float)W, 20));
    walls.push_back(makeWall(0, (float)H - 20, (float)W, 20));
    walls.push_back(makeWall(0, 0, 20, (float)H));
    walls.push_back(makeWall((float)W - 20, 0, 20, (float)H));

    // obstacles
    walls.push_back(makeWall(200, 120, 450, 25));
    walls.push_back(makeWall(150, 260, 25, 250));
    walls.push_back(makeWall(350, 420, 380, 25));
    walls.push_back(makeWall(650, 180, 25, 190));

    // =========================
    // UI: load font (SFML 3)
    // =========================
    const std::string fontPath = "assets/fonts/arial.ttf"; // use your existing one

    namespace fs = std::filesystem;
    std::cout << "Current working directory: " << fs::current_path() << "\n";
    std::cout << "Trying to load font from: " << (fs::current_path() / fontPath) << "\n";

    sf::Font font;
    bool fontLoaded = font.openFromFile(fontPath); // SFML 3 uses openFromFile

    if (!fontLoaded) {
        std::cout << "FAILED to load font.\n";
        window.setTitle("67 Hunt (font NOT loaded - check working dir + path)");
    }
    else {
        std::cout << "Font loaded OK.\n";
    }

    // SFML 3: no default constructors -> create only if font loaded
    std::optional<sf::Text> timerText;
    std::optional<sf::Text> centerText;
    std::optional<sf::Text> hintText;

    if (fontLoaded) {
        timerText = sf::Text(font, "Time: 30");
        timerText->setCharacterSize(24);
        timerText->setFillColor(sf::Color::White);
        timerText->setPosition(sf::Vector2f(20.f, 20.f));

        centerText = sf::Text(font, "");
        centerText->setCharacterSize(52);
        centerText->setFillColor(sf::Color::White);

        hintText = sf::Text(font, "Press R to restart");
        hintText->setCharacterSize(22);
        hintText->setFillColor(sf::Color(220, 220, 220));
        hintText->setPosition(sf::Vector2f(20.f, 55.f));
    }

    // ---------------- Position helpers ----------------
    auto getPlayerPos = [&]() -> sf::Vector2f {
        return playerSprite ? playerSprite->getPosition() : playerCircle.getPosition();
        };

    auto setPlayerPos = [&](sf::Vector2f p) {
        if (playerSprite) playerSprite->setPosition(p);
        playerCircle.setPosition(p); // keep circle in sync either way
        };

    auto getTargetPos = [&]() -> sf::Vector2f {
        return targetSprite ? targetSprite->getPosition() : targetCircle.getPosition();
        };

    // ---------------- Main loop ----------------
    while (window.isOpen()) {
        float dt = clock.restart().asSeconds();

        // SFML 3: pollEvent() returns optional event
        while (auto ev = window.pollEvent()) {
            if (ev->is<sf::Event::Closed>()) {
                window.close();
            }
        }

        // Restart if not playing
        if (state != State::Playing) {
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::R)) {
                state = State::Playing;
                timeLeft = TIME_LIMIT;
                setPlayerPos(sf::Vector2f(100.f, 100.f));
                window.setTitle("67 Hunt");
            }
        }

        // Update
        if (state == State::Playing) {
            timeLeft -= dt;
            if (timeLeft <= 0.f) {
                timeLeft = 0.f;
                state = State::Lose;
                window.setTitle("67 Hunt - TIME'S UP");
            }

            sf::Vector2f dir(0.f, 0.f);
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::W)) dir.y -= 1.f;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::S)) dir.y += 1.f;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A)) dir.x -= 1.f;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::D)) dir.x += 1.f;
            dir = normalize(dir);

            sf::Vector2f oldPos = getPlayerPos();
            setPlayerPos(oldPos + dir * PLAYER_SPEED * dt);

            for (const auto& w : walls) {
                if (circleIntersectsRect(getPlayerPos(), PLAYER_RADIUS, w.getGlobalBounds())) {
                    setPlayerPos(oldPos);
                    break;
                }
            }

            if (circleIntersectsCircle(getPlayerPos(), PLAYER_RADIUS, getTargetPos(), TARGET_RADIUS)) {
                state = State::Win;
                window.setTitle("67 Hunt - YOU MADE 67!");
            }
        }

        // UI update
        if (timerText) {
            int secondsLeft = (int)std::ceil(timeLeft);
            timerText->setString("Time: " + std::to_string(secondsLeft));

            if (state == State::Win) {
                centerText->setString("YOU MADE 67!");
                setCentered(*centerText, W / 2.f, H / 2.f);
            }
            else if (state == State::Lose) {
                centerText->setString("TIME'S UP!");
                setCentered(*centerText, W / 2.f, H / 2.f);
            }
        }

        // Render
        window.clear(sf::Color(15, 15, 20));

        // target
        if (targetSprite) window.draw(*targetSprite);
        else window.draw(targetCircle);

        // walls
        for (auto& w : walls) window.draw(w);

        // player
        if (playerSprite) window.draw(*playerSprite);
        else window.draw(playerCircle);

        // UI
        if (timerText) {
            window.draw(*timerText);
            if (state != State::Playing) {
                window.draw(*centerText);
                window.draw(*hintText);
            }
        }

        window.display();
    }

    return 0;
}
