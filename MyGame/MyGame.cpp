// MyGame.cpp (SFML 3.x) - Wall-occluded flashlight WITH range + warm (yellow) light + visible glow
// Files:
//   assets/fonts/arial.ttf
//   assets/sprites/six1.png, six2.png
//   assets/sprites/seven1.png, seven2.png

#include <SFML/Graphics.hpp>
#include <vector>
#include <cmath>
#include <string>
#include <algorithm>
#include <iostream>
#include <optional>
#include <cstdint>
#include <limits>

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

    spr.setOrigin({ static_cast<float>(s.x) / 2.f, static_cast<float>(s.y) / 2.f });

    float scaleX = desiredDiameter / static_cast<float>(s.x);
    float scaleY = desiredDiameter / static_cast<float>(s.y);
    spr.setScale({ scaleX, scaleY });
}

// Blend mode to "erase" darkness using alpha
static const sf::BlendMode ERASE_BLEND(
    sf::BlendMode::Factor::Zero,
    sf::BlendMode::Factor::OneMinusSrcAlpha,
    sf::BlendMode::Equation::Add
);

// Additive glow on top of the darkness overlay (THIS is what makes the yellow tint visible)
static const sf::BlendMode ADD_GLOW(
    sf::BlendMode::Factor::SrcAlpha,
    sf::BlendMode::Factor::One,
    sf::BlendMode::Equation::Add
);

// ---------------- Wall-occluded flashlight (visibility polygon) ----------------
struct Segment { sf::Vector2f a, b; };

static float cross2(const sf::Vector2f& a, const sf::Vector2f& b) {
    return a.x * b.y - a.y * b.x;
}

static bool raySegmentIntersect(const sf::Vector2f& p, const sf::Vector2f& r,
    const sf::Vector2f& q, const sf::Vector2f& s,
    float& tHit, sf::Vector2f& hitPoint) {
    float rxs = cross2(r, s);
    if (std::fabs(rxs) < 1e-8f) return false;

    sf::Vector2f qmp = { q.x - p.x, q.y - p.y };
    float t = cross2(qmp, s) / rxs;
    float u = cross2(qmp, r) / rxs;

    if (t >= 0.f && u >= 0.f && u <= 1.f) {
        tHit = t;
        hitPoint = { p.x + t * r.x, p.y + t * r.y };
        return true;
    }
    return false;
}

static std::vector<Segment> buildWallSegments(const std::vector<sf::RectangleShape>& walls) {
    std::vector<Segment> segs;
    segs.reserve(walls.size() * 4);

    for (const auto& w : walls) {
        sf::FloatRect b = w.getGlobalBounds();
        float x = b.position.x;
        float y = b.position.y;
        float ww = b.size.x;
        float hh = b.size.y;

        sf::Vector2f p1(x, y);
        sf::Vector2f p2(x + ww, y);
        sf::Vector2f p3(x + ww, y + hh);
        sf::Vector2f p4(x, y + hh);

        segs.push_back({ p1, p2 });
        segs.push_back({ p2, p3 });
        segs.push_back({ p3, p4 });
        segs.push_back({ p4, p1 });
    }
    return segs;
}

static std::vector<sf::Vector2f> computeVisibilityPolygon(
    const sf::Vector2f& origin,
    const std::vector<Segment>& segs,
    float maxDist
) {
    std::vector<float> angles;
    angles.reserve(segs.size() * 2 * 3);

    auto addAnglesForPoint = [&](const sf::Vector2f& pt) {
        float a = std::atan2(pt.y - origin.y, pt.x - origin.x);
        const float eps = 0.0007f;
        angles.push_back(a - eps);
        angles.push_back(a);
        angles.push_back(a + eps);
        };

    for (const auto& s : segs) { addAnglesForPoint(s.a); addAnglesForPoint(s.b); }

    struct Hit { float angle; sf::Vector2f p; };
    std::vector<Hit> hits;
    hits.reserve(angles.size());

    for (float ang : angles) {
        sf::Vector2f dir(std::cos(ang), std::sin(ang));

        float bestT = std::numeric_limits<float>::infinity();
        sf::Vector2f bestP = { origin.x + dir.x * maxDist, origin.y + dir.y * maxDist };

        for (const auto& seg : segs) {
            sf::Vector2f sdir = { seg.b.x - seg.a.x, seg.b.y - seg.a.y };
            float tHit;
            sf::Vector2f hp;
            if (raySegmentIntersect(origin, dir, seg.a, sdir, tHit, hp)) {
                if (tHit < bestT && tHit <= maxDist) {
                    bestT = tHit;
                    bestP = hp;
                }
            }
        }
        hits.push_back({ ang, bestP });
    }

    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        return a.angle < b.angle;
        });

    std::vector<sf::Vector2f> poly;
    poly.reserve(hits.size());
    for (auto& h : hits) poly.push_back(h.p);
    return poly;
}

// Soft flashlight fan with radial alpha falloff
static sf::VertexArray buildSoftFan(
    const sf::Vector2f& origin,
    const std::vector<sf::Vector2f>& poly,
    float maxDist,
    const sf::Color& tint
) {
    sf::VertexArray fan(sf::PrimitiveType::TriangleFan);

    fan.append(sf::Vertex(origin, sf::Color(tint.r, tint.g, tint.b, 255)));

    for (const auto& p : poly) {
        sf::Vector2f d = { p.x - origin.x, p.y - origin.y };
        float dist = std::sqrt(d.x * d.x + d.y * d.y);
        float t = std::min(1.f, dist / maxDist);

        float a = 255.f * (1.f - t);
        a = std::clamp(a, 0.f, 255.f);
        a = std::max(a, 25.f);

        fan.append(sf::Vertex(p, sf::Color(tint.r, tint.g, tint.b, static_cast<std::uint8_t>(a))));
    }

    if (!poly.empty()) {
        sf::Vector2f p = poly.front();
        sf::Vector2f d = { p.x - origin.x, p.y - origin.y };
        float dist = std::sqrt(d.x * d.x + d.y * d.y);
        float t = std::min(1.f, dist / maxDist);

        float a = 255.f * (1.f - t);
        a = std::clamp(a, 0.f, 255.f);
        a = std::max(a, 25.f);

        fan.append(sf::Vertex(p, sf::Color(tint.r, tint.g, tint.b, static_cast<std::uint8_t>(a))));
    }

    return fan;
}

// ---------------- Main ----------------
int main() {
    const unsigned W = 900;
    const unsigned H = 650;

    const float PLAYER_RADIUS = 22.f;
    const float TARGET_RADIUS = 18.f;
    const float PLAYER_SPEED = 320.f;
    const float TIME_LIMIT = 30.f;

    // Animation for the "6" and "7"
    const float ANIM_FPS = 6.f;
    const int   FRAME_COUNT = 2;

    // Flashlight tuning
    const float LIGHT_RANGE = 215.f;                 // range
    const std::uint8_t DARK_ALPHA = 250;             // darker overall (0..255)
    const sf::Color WARM_TINT(255, 190, 140, 255);   // warm tint color
    float glowStrength = 120.f;                      // 0..255

    sf::RenderWindow window(sf::VideoMode({ W, H }), "67 Hunt");
    window.setFramerateLimit(120);

    enum class State { Playing, Win, Lose };
    State state = State::Playing;

    float timeLeft = TIME_LIMIT;
    sf::Clock clock;

    // Darkness texture
    sf::RenderTexture darknessRT;
    if (!darknessRT.resize({ W, H })) {
        std::cout << "Failed to create darkness render texture.\n";
    }

    sf::RectangleShape darknessRect({ (float)W, (float)H });
    darknessRect.setFillColor(sf::Color(0, 0, 0, DARK_ALPHA));

    // ---------------- Player ("6") ----------------
    sf::CircleShape playerCircle(PLAYER_RADIUS);
    playerCircle.setOrigin({ PLAYER_RADIUS, PLAYER_RADIUS });
    playerCircle.setPosition({ 100.f, 100.f });
    playerCircle.setFillColor(sf::Color::Cyan);

    std::vector<sf::Texture> playerFrames;
    playerFrames.reserve(FRAME_COUNT);
    std::optional<sf::Sprite> playerSprite;

    int   playerFrame = 0;
    float playerAnimTimer = 0.f;
    float frameTime = 1.f / ANIM_FPS;

    bool playerFramesOK = true;
    for (int i = 1; i <= FRAME_COUNT; ++i) {
        sf::Texture t;
        std::string path = "assets/sprites/six" + std::to_string(i) + ".png";
        if (!t.loadFromFile(path)) {
            std::cout << "Missing player frame: " << path << "\n";
            playerFramesOK = false;
            break;
        }
        playerFrames.push_back(std::move(t));
    }

    if (playerFramesOK) {
        sf::Sprite s(playerFrames[0]);
        fitSpriteToDiameter(s, playerFrames[0], PLAYER_RADIUS * 2.f);
        s.setPosition({ 100.f, 100.f });
        playerSprite = s;
    }
    else {
        std::cout << "Using fallback circle for player.\n";
    }

    // ---------------- Target ("7") ----------------
    // Fallback circle (used only if frames are missing)
    sf::CircleShape targetCircle(TARGET_RADIUS);
    targetCircle.setOrigin({ TARGET_RADIUS, TARGET_RADIUS });
    targetCircle.setPosition({ 780.f, 520.f });
    targetCircle.setFillColor(sf::Color::Yellow);

    // Animated sprite (seven1/seven2)
    std::vector<sf::Texture> targetFrames;
    targetFrames.reserve(FRAME_COUNT);
    std::optional<sf::Sprite> targetSprite;

    int   targetFrame = 0;
    float targetAnimTimer = 0.f;

    bool targetFramesOK = true;
    for (int i = 1; i <= FRAME_COUNT; ++i) {
        sf::Texture t;
        std::string path = "assets/sprites/seven" + std::to_string(i) + ".png";
        if (!t.loadFromFile(path)) {
            std::cout << "Missing target frame: " << path << "\n";
            targetFramesOK = false;
            break;
        }
        targetFrames.push_back(std::move(t));
    }

    if (targetFramesOK) {
        sf::Sprite s(targetFrames[0]);
        fitSpriteToDiameter(s, targetFrames[0], TARGET_RADIUS * 2.f);
        s.setPosition({ 780.f, 520.f });
        targetSprite = s;
    }
    else {
        std::cout << "Using fallback circle for target.\n";
    }

    // ---------------- Walls ----------------
    std::vector<sf::RectangleShape> walls;
    walls.push_back(makeWall(0, 0, (float)W, 20));
    walls.push_back(makeWall(0, (float)H - 20, (float)W, 20));
    walls.push_back(makeWall(0, 0, 20, (float)H));
    walls.push_back(makeWall((float)W - 20, 0, 20, (float)H));

    walls.push_back(makeWall(200, 120, 450, 25));
    walls.push_back(makeWall(150, 260, 25, 250));
    walls.push_back(makeWall(350, 420, 380, 25));
    walls.push_back(makeWall(650, 180, 25, 190));

    std::vector<Segment> wallSegs = buildWallSegments(walls);

    // ---------------- UI ----------------
    sf::Font font;
    if (!font.openFromFile("assets/fonts/arial.ttf")) {
        std::cout << "Failed to load font: assets/fonts/arial.ttf\n";
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

    // ---------------- Position helpers ----------------
    auto getPlayerPos = [&]() -> sf::Vector2f {
        return playerSprite ? playerSprite->getPosition() : playerCircle.getPosition();
        };
    auto setPlayerPos = [&](sf::Vector2f p) {
        if (playerSprite) playerSprite->setPosition(p);
        playerCircle.setPosition(p);
        };

    auto getTargetPos = [&]() -> sf::Vector2f {
        return targetSprite ? targetSprite->getPosition() : targetCircle.getPosition();
        };
    auto setTargetPos = [&](sf::Vector2f p) {
        if (targetSprite) targetSprite->setPosition(p);
        targetCircle.setPosition(p);
        };

    while (window.isOpen()) {
        float dt = clock.restart().asSeconds();

        while (auto ev = window.pollEvent()) {
            if (ev->is<sf::Event::Closed>()) window.close();
        }

        // Restart
        if (state != State::Playing && sf::Keyboard::isKeyPressed(sf::Keyboard::Key::R)) {
            state = State::Playing;
            timeLeft = TIME_LIMIT;

            setPlayerPos({ 100.f, 100.f });
            setTargetPos({ 780.f, 520.f });

            playerFrame = 0;
            targetFrame = 0;
            playerAnimTimer = 0.f;
            targetAnimTimer = 0.f;

            if (playerSprite) {
                playerSprite->setTexture(playerFrames[playerFrame], true);
                fitSpriteToDiameter(*playerSprite, playerFrames[playerFrame], PLAYER_RADIUS * 2.f);
            }
            if (targetSprite) {
                targetSprite->setTexture(targetFrames[targetFrame], true);
                fitSpriteToDiameter(*targetSprite, targetFrames[targetFrame], TARGET_RADIUS * 2.f);
            }

            window.setTitle("67 Hunt");
        }

        // Animate sprites
        if (state == State::Playing) {
            // player animation
            if (playerSprite) {
                playerAnimTimer += dt;
                while (playerAnimTimer >= frameTime) {
                    playerAnimTimer -= frameTime;
                    playerFrame = (playerFrame + 1) % FRAME_COUNT;

                    playerSprite->setTexture(playerFrames[playerFrame], true);
                    fitSpriteToDiameter(*playerSprite, playerFrames[playerFrame], PLAYER_RADIUS * 2.f);

                    // keep exact position
                    playerSprite->setPosition(getPlayerPos());
                }
            }

            // target animation
            if (targetSprite) {
                targetAnimTimer += dt;
                while (targetAnimTimer >= frameTime) {
                    targetAnimTimer -= frameTime;
                    targetFrame = (targetFrame + 1) % FRAME_COUNT;

                    targetSprite->setTexture(targetFrames[targetFrame], true);
                    fitSpriteToDiameter(*targetSprite, targetFrames[targetFrame], TARGET_RADIUS * 2.f);

                    // keep exact position
                    targetSprite->setPosition(getTargetPos());
                }
            }
        }

        // Update game
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

            // Win if player touches target (works for circle OR sprite)
            if (circleIntersectsCircle(getPlayerPos(), PLAYER_RADIUS, getTargetPos(), TARGET_RADIUS)) {
                state = State::Win;
                window.setTitle("67 Hunt - YOU MADE 67!");
            }
        }

        // UI update
        timerText.setString("Time: " + std::to_string((int)std::ceil(timeLeft)));
        if (state == State::Win) {
            centerText.setString("YOU MADE 67!");
            setCentered(centerText, W / 2.f, H / 2.f);
        }
        else if (state == State::Lose) {
            centerText.setString("TIME'S UP!");
            setCentered(centerText, W / 2.f, H / 2.f);
        }

        // ---------------- Render world ----------------
        window.clear({ 15, 15, 20 });

        // draw target
        if (targetSprite) window.draw(*targetSprite);
        else window.draw(targetCircle);

        // walls
        for (auto& w : walls) window.draw(w);

        // draw player
        if (playerSprite) window.draw(*playerSprite);
        else window.draw(playerCircle);

        // ---------------- Darkness overlay (range-limited + wall-occluded) ----------------
        darknessRT.clear(sf::Color(0, 0, 0, 0));
        darknessRT.draw(darknessRect);

        sf::Vector2f origin = getPlayerPos();
        std::vector<sf::Vector2f> poly = computeVisibilityPolygon(origin, wallSegs, LIGHT_RANGE);

        sf::VertexArray glowFan;

        if (poly.size() >= 3) {
            sf::VertexArray eraseFan = buildSoftFan(origin, poly, LIGHT_RANGE, sf::Color(255, 255, 255, 255));
            darknessRT.draw(eraseFan, ERASE_BLEND);

            sf::Color glowColor = WARM_TINT;
            glowColor.a = static_cast<std::uint8_t>(std::clamp(glowStrength, 0.f, 255.f));
            glowFan = buildSoftFan(origin, poly, LIGHT_RANGE, glowColor);
        }

        darknessRT.display();
        window.draw(sf::Sprite(darknessRT.getTexture()));

        if (poly.size() >= 3) {
            window.draw(glowFan, ADD_GLOW);
        }

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
