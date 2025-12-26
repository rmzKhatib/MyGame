// MyGame.cpp (SFML 3.x)
// Home screen (menu) + Levels + world-space camera + wall-occluded 360° vision (range + warm glow)
//
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

static sf::Vector2f clampViewCenter(sf::Vector2f desiredCenter, sf::Vector2f viewSize, sf::Vector2f worldSize) {
    float halfW = viewSize.x / 2.f;
    float halfH = viewSize.y / 2.f;

    if (worldSize.x <= viewSize.x) desiredCenter.x = worldSize.x / 2.f;
    else desiredCenter.x = std::clamp(desiredCenter.x, halfW, worldSize.x - halfW);

    if (worldSize.y <= viewSize.y) desiredCenter.y = worldSize.y / 2.f;
    else desiredCenter.y = std::clamp(desiredCenter.y, halfH, worldSize.y - halfH);

    return desiredCenter;
}

// ---------------- Blend modes ----------------
static const sf::BlendMode ERASE_BLEND(
    sf::BlendMode::Factor::Zero,
    sf::BlendMode::Factor::OneMinusSrcAlpha,
    sf::BlendMode::Equation::Add
);

static const sf::BlendMode ADD_GLOW(
    sf::BlendMode::Factor::SrcAlpha,
    sf::BlendMode::Factor::One,
    sf::BlendMode::Equation::Add
);

// ---------------- Wall-occluded visibility (world-space) ----------------
struct Segment { sf::Vector2f a, b; };

static float cross2(const sf::Vector2f& a, const sf::Vector2f& b) {
    return a.x * b.y - a.y * b.x;
}

static bool raySegmentIntersect(
    const sf::Vector2f& p, const sf::Vector2f& r,
    const sf::Vector2f& q, const sf::Vector2f& s,
    float& tHit, sf::Vector2f& hitPoint
) {
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

static sf::VertexArray buildSoftFan_Screen(
    const sf::Vector2f& originScreen,
    const std::vector<sf::Vector2f>& polyScreen,
    float maxDist,
    const sf::Color& tint
) {
    sf::VertexArray fan(sf::PrimitiveType::TriangleFan);
    fan.append(sf::Vertex(originScreen, sf::Color(tint.r, tint.g, tint.b, 255)));

    for (const auto& p : polyScreen) {
        sf::Vector2f d = { p.x - originScreen.x, p.y - originScreen.y };
        float dist = std::sqrt(d.x * d.x + d.y * d.y);
        float t = std::min(1.f, dist / maxDist);

        float a = 255.f * (1.f - t);
        a = std::clamp(a, 0.f, 255.f);
        a = std::max(a, 25.f);

        fan.append(sf::Vertex(p, sf::Color(tint.r, tint.g, tint.b, static_cast<std::uint8_t>(a))));
    }

    if (!polyScreen.empty()) {
        sf::Vector2f p = polyScreen.front();
        sf::Vector2f d = { p.x - originScreen.x, p.y - originScreen.y };
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

    // WORLD SIZE (levels will set these)
    float WORLD_W = 2400.f;
    float WORLD_H = 1800.f;

    const float PLAYER_RADIUS = 22.f;
    const float TARGET_RADIUS = 18.f;
    const float PLAYER_SPEED = 320.f;

    // Animation for the "6" and "7"
    const float ANIM_FPS = 6.f;
    const int   FRAME_COUNT = 2;

    // Vision tuning
    const float LIGHT_RANGE = 215.f;
    const std::uint8_t DARK_ALPHA = 250;
    const sf::Color WARM_TINT(255, 190, 140, 255);
    float glowStrength = 120.f;

    sf::RenderWindow window(sf::VideoMode({ W, H }), "67 Hunt");
    window.setFramerateLimit(120);

    // States
    enum class GameMode { Menu, Playing, Win, Lose };
    GameMode mode = GameMode::Menu;

    int currentLevel = 1;

    // Timer per level
    const float LEVEL_TIME_LIMIT = 30.f;
    float timeLeft = LEVEL_TIME_LIMIT;
    sf::Clock clock;

    // Camera
    sf::View camera(sf::FloatRect({ 0.f, 0.f }, { (float)W, (float)H }));

    // Darkness overlay texture (screen)
    sf::RenderTexture darknessRT;
    if (!darknessRT.resize({ W, H })) {
        std::cout << "Failed to create darkness render texture.\n";
    }
    sf::RectangleShape darknessRect({ (float)W, (float)H });
    darknessRect.setFillColor(sf::Color(0, 0, 0, DARK_ALPHA));

    // ---------------- Sprites (player + target) ----------------
    // Player fallback
    sf::CircleShape playerCircle(PLAYER_RADIUS);
    playerCircle.setOrigin({ PLAYER_RADIUS, PLAYER_RADIUS });
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
        playerSprite = s;
    }
    else {
        std::cout << "Using fallback circle for player.\n";
    }

    // Target fallback
    sf::CircleShape targetCircle(TARGET_RADIUS);
    targetCircle.setOrigin({ TARGET_RADIUS, TARGET_RADIUS });
    targetCircle.setFillColor(sf::Color::Yellow);

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
        targetSprite = s;
    }
    else {
        std::cout << "Using fallback circle for target.\n";
    }

    // ---------------- Level data (walls + spawns) ----------------
    std::vector<sf::RectangleShape> walls;
    std::vector<Segment> wallSegs;

    auto setPlayerPos = [&](sf::Vector2f p) {
        if (playerSprite) playerSprite->setPosition(p);
        playerCircle.setPosition(p);
        };
    auto getPlayerPos = [&]() -> sf::Vector2f {
        return playerSprite ? playerSprite->getPosition() : playerCircle.getPosition();
        };

    auto setTargetPos = [&](sf::Vector2f p) {
        if (targetSprite) targetSprite->setPosition(p);
        targetCircle.setPosition(p);
        };
    auto getTargetPos = [&]() -> sf::Vector2f {
        return targetSprite ? targetSprite->getPosition() : targetCircle.getPosition();
        };

    auto resetAnimations = [&]() {
        playerFrame = 0;
        targetFrame = 0;
        playerAnimTimer = 0.f;
        targetAnimTimer = 0.f;

        if (playerSprite && !playerFrames.empty()) {
            sf::Vector2f keep = getPlayerPos();
            playerSprite->setTexture(playerFrames[playerFrame], true);
            fitSpriteToDiameter(*playerSprite, playerFrames[playerFrame], PLAYER_RADIUS * 2.f);
            playerSprite->setPosition(keep);
        }
        if (targetSprite && !targetFrames.empty()) {
            sf::Vector2f keep = getTargetPos();
            targetSprite->setTexture(targetFrames[targetFrame], true);
            fitSpriteToDiameter(*targetSprite, targetFrames[targetFrame], TARGET_RADIUS * 2.f);
            targetSprite->setPosition(keep);
        }
        };

    auto loadLevel = [&](int level) {
        currentLevel = level;

        // reset timer + mode
        timeLeft = LEVEL_TIME_LIMIT;
        mode = GameMode::Playing;

        walls.clear();

        if (level == 1) {
            WORLD_W = 2400.f; WORLD_H = 1800.f;

            // borders
            walls.push_back(makeWall(0, 0, WORLD_W, 20));
            walls.push_back(makeWall(0, WORLD_H - 20, WORLD_W, 20));
            walls.push_back(makeWall(0, 0, 20, WORLD_H));
            walls.push_back(makeWall(WORLD_W - 20, 0, 20, WORLD_H));

            // obstacles
            walls.push_back(makeWall(350, 250, 600, 30));
            walls.push_back(makeWall(300, 450, 30, 500));
            walls.push_back(makeWall(700, 820, 650, 30));
            walls.push_back(makeWall(1250, 380, 30, 380));
            walls.push_back(makeWall(1550, 600, 520, 30));
            walls.push_back(makeWall(1750, 850, 30, 500));
            walls.push_back(makeWall(1050, 1250, 900, 30));
            walls.push_back(makeWall(600, 1100, 30, 450));

            setPlayerPos({ 200.f, 200.f });
            setTargetPos({ 1950.f, 1400.f });
        }
        else { // level 2
            WORLD_W = 2800.f; WORLD_H = 2000.f;

            // borders
            walls.push_back(makeWall(0, 0, WORLD_W, 20));
            walls.push_back(makeWall(0, WORLD_H - 20, WORLD_W, 20));
            walls.push_back(makeWall(0, 0, 20, WORLD_H));
            walls.push_back(makeWall(WORLD_W - 20, 0, 20, WORLD_H));

            // more maze-ish
            walls.push_back(makeWall(250, 250, 900, 30));
            walls.push_back(makeWall(250, 250, 30, 700));
            walls.push_back(makeWall(250, 920, 1200, 30));

            walls.push_back(makeWall(600, 520, 30, 850));
            walls.push_back(makeWall(600, 520, 800, 30));
            walls.push_back(makeWall(1370, 520, 30, 650));
            walls.push_back(makeWall(900, 1170, 500, 30));

            walls.push_back(makeWall(1700, 300, 30, 900));
            walls.push_back(makeWall(1700, 300, 800, 30));
            walls.push_back(makeWall(2500, 300, 30, 1300));
            walls.push_back(makeWall(1700, 1570, 830, 30));

            setPlayerPos({ 140.f, 140.f });
            setTargetPos({ 2550.f, 1750.f });
        }

        wallSegs = buildWallSegments(walls);
        resetAnimations();

        window.setTitle("67 Hunt - Level " + std::to_string(currentLevel));
        };

    // ---------------- UI (screen space) ----------------
    sf::Font font;
    if (!font.openFromFile("assets/fonts/arial.ttf")) {
        std::cout << "Failed to load font: assets/fonts/arial.ttf\n";
    }

    sf::Text titleText(font, "67 Hunt");
    titleText.setCharacterSize(78);
    titleText.setFillColor(sf::Color::White);

    sf::Text menuText(font, "Press 1 for Level 1\nPress 2 for Level 2\nESC to Quit");
    menuText.setCharacterSize(28);
    menuText.setFillColor(sf::Color(220, 220, 220));

    sf::Text timerText(font, "Time: 30");
    timerText.setCharacterSize(24);
    timerText.setFillColor(sf::Color::White);
    timerText.setPosition({ 20.f, 20.f });

    sf::Text centerText(font, "");
    centerText.setCharacterSize(52);
    centerText.setFillColor(sf::Color::White);

    sf::Text hintText(font, "R = restart level    M = menu");
    hintText.setCharacterSize(22);
    hintText.setFillColor(sf::Color(220, 220, 220));
    hintText.setPosition({ 20.f, 55.f });

    // Center menu text once
    setCentered(titleText, W / 2.f, H / 2.f - 140.f);
    setCentered(menuText, W / 2.f, H / 2.f + 10.f);

    // Start in menu view
    window.setView(window.getDefaultView());

    // ---------------- Main loop ----------------
    while (window.isOpen()) {
        float dt = clock.restart().asSeconds();

        while (auto ev = window.pollEvent()) {
            if (ev->is<sf::Event::Closed>()) window.close();
        }

        // Global hotkeys
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Escape)) {
            window.close();
        }

        // MENU
        if (mode == GameMode::Menu) {
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Num1)) loadLevel(1);
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Num2)) loadLevel(2);

            // Render menu
            window.setView(window.getDefaultView());
            window.clear(sf::Color(10, 10, 14));
            window.draw(titleText);
            window.draw(menuText);
            window.display();
            continue;
        }

        // Back to menu from win/lose
        if ((mode == GameMode::Win || mode == GameMode::Lose) &&
            sf::Keyboard::isKeyPressed(sf::Keyboard::Key::M)) {
            mode = GameMode::Menu;
            window.setTitle("67 Hunt");
            continue;
        }

        // Restart current level
        if ((mode == GameMode::Win || mode == GameMode::Lose) &&
            sf::Keyboard::isKeyPressed(sf::Keyboard::Key::R)) {
            loadLevel(currentLevel);
        }

        // Animate sprites (only while playing)
        if (mode == GameMode::Playing) {
            if (playerSprite) {
                playerAnimTimer += dt;
                while (playerAnimTimer >= frameTime) {
                    playerAnimTimer -= frameTime;
                    playerFrame = (playerFrame + 1) % FRAME_COUNT;

                    sf::Vector2f keepPos = getPlayerPos();
                    playerSprite->setTexture(playerFrames[playerFrame], true);
                    fitSpriteToDiameter(*playerSprite, playerFrames[playerFrame], PLAYER_RADIUS * 2.f);
                    playerSprite->setPosition(keepPos);
                }
            }

            if (targetSprite) {
                targetAnimTimer += dt;
                while (targetAnimTimer >= frameTime) {
                    targetAnimTimer -= frameTime;
                    targetFrame = (targetFrame + 1) % FRAME_COUNT;

                    sf::Vector2f keepPos = getTargetPos();
                    targetSprite->setTexture(targetFrames[targetFrame], true);
                    fitSpriteToDiameter(*targetSprite, targetFrames[targetFrame], TARGET_RADIUS * 2.f);
                    targetSprite->setPosition(keepPos);
                }
            }
        }

        // Update gameplay
        if (mode == GameMode::Playing) {
            timeLeft -= dt;
            if (timeLeft <= 0.f) {
                timeLeft = 0.f;
                mode = GameMode::Lose;
                window.setTitle("67 Hunt - TIME'S UP (M = menu)");
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
                mode = GameMode::Win;
                window.setTitle("67 Hunt - YOU MADE 67! (M = menu)");
            }
        }

        // Camera follow
        {
            sf::Vector2f desired = getPlayerPos();
            sf::Vector2f clamped = clampViewCenter(desired, camera.getSize(), { WORLD_W, WORLD_H });
            camera.setCenter(clamped);
        }

        // UI update
        timerText.setString("Time: " + std::to_string((int)std::ceil(timeLeft)));

        if (mode == GameMode::Win) {
            centerText.setString("YOU MADE 67!");
            setCentered(centerText, W / 2.f, H / 2.f);
        }
        else if (mode == GameMode::Lose) {
            centerText.setString("TIME'S UP!");
            setCentered(centerText, W / 2.f, H / 2.f);
        }

        // ---------------- Render WORLD ----------------
        window.clear({ 15, 15, 20 });
        window.setView(camera);

        if (targetSprite) window.draw(*targetSprite);
        else window.draw(targetCircle);

        for (auto& w : walls) window.draw(w);

        if (playerSprite) window.draw(*playerSprite);
        else window.draw(playerCircle);

        // ---------------- Overlay + UI (screen space) ----------------
        window.setView(window.getDefaultView());

        // Darkness overlay
        darknessRT.clear(sf::Color(0, 0, 0, 0));
        darknessRT.draw(darknessRect);

        sf::Vector2f originWorld = getPlayerPos();
        std::vector<sf::Vector2f> polyWorld = computeVisibilityPolygon(originWorld, wallSegs, LIGHT_RANGE);

        sf::Vector2i originPix = window.mapCoordsToPixel(originWorld, camera);
        sf::Vector2f originScreen((float)originPix.x, (float)originPix.y);

        std::vector<sf::Vector2f> polyScreen;
        polyScreen.reserve(polyWorld.size());
        for (const auto& pW : polyWorld) {
            sf::Vector2i pix = window.mapCoordsToPixel(pW, camera);
            polyScreen.push_back({ (float)pix.x, (float)pix.y });
        }

        sf::VertexArray glowFan;

        if (polyScreen.size() >= 3) {
            sf::VertexArray eraseFan = buildSoftFan_Screen(
                originScreen,
                polyScreen,
                LIGHT_RANGE,
                sf::Color(255, 255, 255, 255)
            );
            darknessRT.draw(eraseFan, ERASE_BLEND);

            sf::Color glowColor = WARM_TINT;
            glowColor.a = static_cast<std::uint8_t>(std::clamp(glowStrength, 0.f, 255.f));
            glowFan = buildSoftFan_Screen(originScreen, polyScreen, LIGHT_RANGE, glowColor);
        }

        darknessRT.display();
        window.draw(sf::Sprite(darknessRT.getTexture()));
        if (polyScreen.size() >= 3) window.draw(glowFan, ADD_GLOW);

        // UI
        window.draw(timerText);
        if (mode == GameMode::Win || mode == GameMode::Lose) {
            window.draw(centerText);
            window.draw(hintText);
        }

        window.display();
    }

    return 0;
}
