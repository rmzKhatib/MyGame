#include <SFML/Graphics.hpp>
#include <vector>
#include <cmath>
#include <string>
#include <algorithm>
#include <iostream>
#include <filesystem>

static sf::Vector2f normalize(sf::Vector2f v) {
    float len = std::sqrt(v.x * v.x + v.y * v.y);
    if (len == 0.f) return { 0.f, 0.f };
    return { v.x / len, v.y / len };
}

// SFML 3: FloatRect uses rect.position + rect.size
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
    r.setPosition({ x, y });                 // SFML 3: Vector2
    r.setFillColor(sf::Color(80, 80, 80));
    return r;
}

static void setCentered(sf::Text& t, float cx, float cy) {
    sf::FloatRect b = t.getLocalBounds();
    t.setOrigin({ b.position.x + b.size.x / 2.f, b.position.y + b.size.y / 2.f }); // SFML 3
    t.setPosition({ cx, cy });
}

int main() {
    const unsigned W = 900;
    const unsigned H = 650;

    const float PLAYER_RADIUS = 22.f;
    const float PLAYER_SPEED = 320.f;   // px/sec
    const float TIME_LIMIT = 30.f;    // seconds

    // SFML 3: VideoMode takes a Vector2u
    sf::RenderWindow window(sf::VideoMode(sf::Vector2u{ W, H }), "67 Hunt");
    window.setFramerateLimit(120);

    enum class State { Playing, Win, Lose };
    State state = State::Playing;

    float timeLeft = TIME_LIMIT;
    sf::Clock clock;

    // ---- Player ("6") ----
    sf::CircleShape player(PLAYER_RADIUS);
    player.setOrigin({ PLAYER_RADIUS, PLAYER_RADIUS });     // SFML 3
    player.setPosition({ 100.f, 100.f });
    player.setFillColor(sf::Color::Cyan);

    // ---- Target ("7") ----
    sf::CircleShape target(18.f);
    target.setOrigin({ 18.f, 18.f });
    target.setPosition({ 780.f, 520.f });
    target.setFillColor(sf::Color::Yellow);

    // ---- Walls ----
    std::vector<sf::RectangleShape> walls;

    // border walls (thickness 20)
    walls.push_back(makeWall(0, 0, (float)W, 20));
    walls.push_back(makeWall(0, (float)H - 20, (float)W, 20));
    walls.push_back(makeWall(0, 0, 20, (float)H));
    walls.push_back(makeWall((float)W - 20, 0, 20, (float)H));

    // a few obstacles
    walls.push_back(makeWall(200, 120, 450, 25));
    walls.push_back(makeWall(150, 260, 25, 250));
    walls.push_back(makeWall(350, 420, 380, 25));
    walls.push_back(makeWall(650, 180, 25, 190));

    // =========================
    // UI: Load font + create text
    // =========================
    const std::string fontPath = "assets/fonts/PressStart2P-Regular.ttf";

    namespace fs = std::filesystem;
    std::cout << "Current working directory: " << fs::current_path() << "\n";
    std::cout << "Trying to load font from: " << (fs::current_path() / fontPath) << "\n";

    sf::Font font;
    bool fontLoaded = font.openFromFile(fontPath); // SFML 3: openFromFile

    if (!fontLoaded) {
        std::cout << "FAILED to load font.\n";
        window.setTitle("67 Hunt (font NOT loaded - check working dir + path)");
    }
    else {
        std::cout << "Font loaded OK.\n";
    }

    // SFML 3: Text needs a font at construction
    sf::Text timerText(font, "", 24);
    sf::Text centerText(font, "", 52);
    sf::Text hintText(font, "Press R to restart", 22);

    timerText.setFillColor(sf::Color::White);
    timerText.setPosition({ 20.f, 20.f });

    centerText.setFillColor(sf::Color::White);

    hintText.setFillColor(sf::Color(220, 220, 220));
    hintText.setPosition({ 20.f, 55.f });

    while (window.isOpen()) {
        float dt = clock.restart().asSeconds();

        // SFML 3: pollEvent returns optional
        while (auto event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>())
                window.close();
        }

        // Restart if not playing
        if (state != State::Playing) {
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::R)) {
                state = State::Playing;
                timeLeft = TIME_LIMIT;
                player.setPosition({ 100.f, 100.f });
                window.setTitle("67 Hunt");
            }
        }

        if (state == State::Playing) {
            // Timer
            timeLeft -= dt;
            if (timeLeft <= 0.f) {
                timeLeft = 0.f;
                state = State::Lose;
                window.setTitle("67 Hunt - TIME'S UP");
            }

            // Input -> direction
            sf::Vector2f dir(0.f, 0.f);
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::W)) dir.y -= 1.f;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::S)) dir.y += 1.f;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A)) dir.x -= 1.f;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::D)) dir.x += 1.f;
            dir = normalize(dir);

            // Move attempt
            sf::Vector2f oldPos = player.getPosition();
            player.setPosition(oldPos + dir * PLAYER_SPEED * dt);

            // Wall collision -> revert
            for (const auto& w : walls) {
                if (circleIntersectsRect(player.getPosition(), PLAYER_RADIUS, w.getGlobalBounds())) {
                    player.setPosition(oldPos);
                    break;
                }
            }

            // Win condition: touch target
            if (circleIntersectsCircle(player.getPosition(), PLAYER_RADIUS,
                target.getPosition(), target.getRadius())) {
                state = State::Win;
                window.setTitle("67 Hunt - YOU MADE 67!");
            }
        }

        // UI strings
        if (fontLoaded) {
            int secondsLeft = (int)std::ceil(timeLeft);
            timerText.setString("Time: " + std::to_string(secondsLeft));

            if (state == State::Win) {
                centerText.setString("YOU MADE 67!");
                setCentered(centerText, W / 2.f, H / 2.f);
            }
            else if (state == State::Lose) {
                centerText.setString("TIME'S UP!");
                setCentered(centerText, W / 2.f, H / 2.f);
            }
        }

        // Render
        window.clear(sf::Color(15, 15, 20));

        window.draw(target);
        for (auto& w : walls) window.draw(w);
        window.draw(player);

        if (fontLoaded) {
            window.draw(timerText);
            if (state != State::Playing) {
                window.draw(centerText);
                window.draw(hintText);
            }
        }

        window.display();
    }

    return 0;
}
