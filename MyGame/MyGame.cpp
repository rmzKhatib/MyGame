#include <SFML/Graphics.hpp>
#include <vector>
#include <cmath>
#include <string>

static sf::Vector2f normalize(sf::Vector2f v) {
    float len = std::sqrt(v.x * v.x + v.y * v.y);
    if (len == 0.f) return { 0.f, 0.f };
    return { v.x / len, v.y / len };
}

int main() {
    const unsigned W = 900;
    const unsigned H = 650;

    const float PLAYER_RADIUS = 22.f;
    const float PLAYER_SPEED = 320.f;     // px/sec
    const float TIME_LIMIT = 30.f;        // seconds

    sf::RenderWindow window(sf::VideoMode(W, H), "67 Hunt");
    window.setFramerateLimit(120);

    // ---- Game state ----
    enum class State { Playing, Win, Lose };
    State state = State::Playing;

    float timeLeft = TIME_LIMIT;
    sf::Clock clock;

    // ---- Player ("6") ----
    sf::CircleShape player(PLAYER_RADIUS);
    player.setOrigin(PLAYER_RADIUS, PLAYER_RADIUS);  // position = center
    player.setPosition(100.f, 100.f);
    player.setFillColor(sf::Color::Cyan);

    // ---- Target ("7") ----
    sf::CircleShape target(18.f);
    target.setOrigin(18.f, 18.f);
    target.setPosition(780.f, 520.f);
    target.setFillColor(sf::Color::Yellow);

    // ---- Walls ----
    auto makeWall = [](float x, float y, float w, float h) {
        sf::RectangleShape r({ w, h });
        r.setPosition(x, y);
        r.setFillColor(sf::Color(80, 80, 80));
        return r;
        };

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

    // ---- UI Text (simple) ----
    // NOTE: SFML text needs a font file. If you don't want fonts yet, we can skip text.
    // For now, we'll display win/lose by changing the window title.
    // If you want on-screen text, tell me and I'll show you the easiest font setup.

    auto circleIntersectsRect = [](const sf::Vector2f& c, float r, const sf::FloatRect& rect) {
        float closestX = std::max(rect.left, std::min(c.x, rect.left + rect.width));
        float closestY = std::max(rect.top, std::min(c.y, rect.top + rect.height));
        float dx = c.x - closestX;
        float dy = c.y - closestY;
        return (dx * dx + dy * dy) < (r * r);
        };

    auto circleIntersectsCircle = [](sf::Vector2f a, float ra, sf::Vector2f b, float rb) {
        sf::Vector2f d = a - b;
        float dist2 = d.x * d.x + d.y * d.y;
        float r = ra + rb;
        return dist2 < (r * r);
        };

    while (window.isOpen()) {
        float dt = clock.restart().asSeconds();

        // Events
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed) window.close();
        }

        if (state == State::Playing) {
            // Timer
            timeLeft -= dt;
            if (timeLeft <= 0.f) {
                state = State::Lose;
                window.setTitle("67 Hunt - TIME'S UP");
            }

            // Input -> direction
            sf::Vector2f dir(0.f, 0.f);
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::W)) dir.y -= 1.f;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::S)) dir.y += 1.f;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::A)) dir.x -= 1.f;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::D)) dir.x += 1.f;
            dir = normalize(dir);

            // Move (attempt)
            sf::Vector2f oldPos = player.getPosition();
            sf::Vector2f newPos = oldPos + dir * PLAYER_SPEED * dt;
            player.setPosition(newPos);

            // Collision with walls: if intersect, revert
            for (const auto& w : walls) {
                if (circleIntersectsRect(player.getPosition(), PLAYER_RADIUS, w.getGlobalBounds())) {
                    player.setPosition(oldPos);
                    break;
                }
            }

            // Win condition: touch the "7"
            if (circleIntersectsCircle(player.getPosition(), PLAYER_RADIUS, target.getPosition(), target.getRadius())) {
                state = State::Win;
                window.setTitle("67 Hunt - YOU MADE 67!");
            }
        }
        else {
            // Restart controls (simple)
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::R)) {
                state = State::Playing;
                timeLeft = TIME_LIMIT;
                player.setPosition(100.f, 100.f);
                window.setTitle("67 Hunt");
            }
        }

        // Render
        window.clear(sf::Color(15, 15, 20));

        // draw target + walls + player
        window.draw(target);
        for (auto& w : walls) window.draw(w);
        window.draw(player);

        window.display();
    }

    return 0;
}
