#include "grid.hpp"
#include "simulation.hpp"
#include <SFML/Graphics.hpp>
#include <vector>

// How many times slower the display is vs the simulation.
// 1 = real-time, 5 = 5x slower, etc.
constexpr int DISPLAY_SPEED_DIVISOR = 120;

int main() {
    sf::RenderWindow window(
        sf::VideoMode(WINDOW_W, WINDOW_H),
        "Mycelium Simulator — click to grow",
        sf::Style::Close
    );
    window.setFramerateLimit(60);

    std::vector<sf::Uint8> pixels(WINDOW_W * WINDOW_H * 4, 0);
    sf::Texture texture;
    texture.create(WINDOW_W, WINDOW_H);
    sf::Sprite sprite(texture);

    // The display grid is owned entirely by the render thread.
    // No synchronisation needed — only this thread ever writes to it.
    std::vector<int> displayGrid(GRID_ROWS * GRID_COLS, -1);
    int displayHead = 0; // how many events we have already applied

    Simulation sim;
    sim.start();

    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed)
                window.close();
            if (event.type == sf::Event::KeyPressed &&
                event.key.code == sf::Keyboard::Escape)
                window.close();
            if (event.type == sf::Event::MouseButtonPressed &&
                event.mouseButton.button == sf::Mouse::Left) {
                int col = event.mouseButton.x;
                int row = event.mouseButton.y;
                if (row >= 0 && row < GRID_ROWS && col >= 0 && col < GRID_COLS)
                    sim.addSite(row, col);
            }
        }

        // --- Advance display at 1/DISPLAY_SPEED_DIVISOR of the simulation rate ---
        //
        // simHead = total events the simulation has produced so far.
        // pending = events produced since our last frame.
        // toApply = pending / DIVISOR  →  display lags behind and catches up slowly.
        //
        // The simulation runs entirely in its own threads and never waits for us,
        // so this read is always safe (eventLog is pre-allocated, append-only).
        int simHead = sim.eventLogSize.load(std::memory_order_acquire);
        int pending  = simHead - displayHead;
        int toApply  = std::max(1, pending / DISPLAY_SPEED_DIVISOR);
            toApply  = std::min(toApply, pending); // never exceed what exists

        for (int i = displayHead; i < displayHead + toApply; ++i) {
            const GrowthEvent& e = sim.eventLog[i];
            displayGrid[e.cellIdx] = e.siteIdx;
        }
        displayHead += toApply;

        // --- Build pixel buffer from displayGrid (not sim.grid) ---
        for (int i = 0; i < GRID_ROWS * GRID_COLS; ++i) {
            int site = displayGrid[i];
            sf::Color c = (site < 0) ? sf::Color::Black : sim.siteColors[site];
            pixels[i * 4]     = c.r;
            pixels[i * 4 + 1] = c.g;
            pixels[i * 4 + 2] = c.b;
            pixels[i * 4 + 3] = 255;
        }

        texture.update(pixels.data());
        window.clear();
        window.draw(sprite);
        window.display();
    }

    sim.stop();
    return 0;
}
