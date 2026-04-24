#pragma once
#include <vector>
#include <atomic>

// one grid cell = one screen pixel
constexpr int WINDOW_W  = 960;
constexpr int WINDOW_H  = 720;
constexpr int GRID_COLS = WINDOW_W;
constexpr int GRID_ROWS = WINDOW_H;

/**
 * @brief Flat 2D grid backing the simulation
 *
 * Each cell holds the index of the colony that owns it, or -1 if unclaimed
 * Cells are atomic so worker threads can race on them with compare_exchange
 * without needing a coarse lock.
 */
struct Grid {
    std::vector<std::atomic<int>> cells;

    /**
     * @brief Allocates the grid and marks every cell unclaimed (-1)
     */
    Grid() : cells(GRID_COLS * GRID_ROWS) {
        for (auto& c : cells)
            c.store(-1, std::memory_order_relaxed);
    }

    /**
     * @brief Returns a reference to the cell at (row, col)
     *
     * @param row   row index in [0, GRID_ROWS)
     * @param col   column index in [0, GRID_COLS)
     * @return      reference to the atomic cell value
     */
    std::atomic<int>& at(int row, int col) {
        return cells[row * GRID_COLS + col];
    }
};
