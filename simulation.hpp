#pragma once
#include "grid.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <deque>
#include <vector>
#include <SFML/Graphics.hpp>

// growth parameters
constexpr int   MAX_FIBERS         = 3000;   // hard cap on concurrent growing tips
constexpr int   INITIAL_FIBERS     = 6;      // tips spawned per colony click
constexpr float FORK_CHANCE        = 0.05f;  // probability of branching each step
constexpr float DRIFT_MAX          = 0.22f;  // max angle change per step (~12 deg)
constexpr float FORK_ANGLE_MIN     = 0.6f;   // min branch angle at a fork (radians)
constexpr float FORK_ANGLE_MAX     = 1.9f;   // max branch angle at a fork (radians)
constexpr int   BLOB_RADIUS        = 1;      // radius of the dot painted at each tip
constexpr float BLOB_CHANCE        = 0.15f;  // chance of filling each cell in the blob
constexpr int   STEPS_PER_TURN     = 20;     // steps a fiber runs before yielding the lock
constexpr int   LOCAL_STUCK_RADIUS = 5;      // search radius when a tip gets stuck

constexpr long SLOW_THRESHOLD  = 300;  // cells per 500ms below which we count a slow interval
constexpr int  SLOW_INTERVALS  = 5;    // consecutive slow intervals before we call it done

/**
 * @brief A single growing tip in the mycelium network
 *
 * Lives in the worker queue and advances one step at a time. Forking pushes
 * a new Fiber onto the queue for the child branch.
 */
struct Fiber {
    int   siteIdx;   // colony this tip belongs to
    int   row, col;  // current position on the grid
    float angle;     // current heading in radians
};

/**
 * @brief Written to the event log each time a cell gets colonized
 *
 * The log is pre-allocated and append-only. Workers claim a slot with
 * fetch_add so there's no collision between producers, and the render
 * thread can read at its own pace without any lock.
 */
struct GrowthEvent {
    int cellIdx;  // flat index: row * GRID_COLS + col
    int siteIdx;  // colony that colonized this cell
};

/**
 * @brief Runs the mycelium growth simulation on a thread pool
 *
 * @code
 *   Simulation sim;
 *   sim.start();
 *   sim.addSite(row, col);  // on mouse click
 *   // render loop reads eventLog[0..eventLogSize] and siteColors
 *   sim.stop();
 * @endcode
 *
 * start(), stop() and addSite() are safe to call from any thread.
 * eventLog and siteColors are safe to read from the render thread at any
 * time since the log is pre-allocated (no reallocation) and siteColors
 * only grows, one entry per addSite() call.
 */
struct Simulation {
    Grid                   grid;
    std::atomic<bool>      running{false};
    std::atomic<long>      cellsColonized{0};
    std::vector<sf::Color> siteColors;  // one color per colony, indexed by siteIdx

    std::vector<GrowthEvent> eventLog;
    std::atomic<int>         eventLogSize{0};

    /**
     * @brief Starts the worker pool and monitor thread
     *
     * Spawns one worker per logical CPU core (min 2) plus a monitor thread.
     * Must be called once before addSite().
     */
    void start();

    /**
     * @brief Signals all threads to stop and waits for them to finish
     *
     * Safe to call even if the simulation already stopped on its own
     * (the monitor sets running=false when growth slows down enough).
     */
    void stop();

    /**
     * @brief Plants a new colony and kicks off the initial fibers
     *
     * Assigns a color, marks the seed cell, then pushes INITIAL_FIBERS
     * evenly-spaced fibers onto the worker queue.
     *
     * @param row   row of the seed cell, in [0, GRID_ROWS)
     * @param col   column of the seed cell, in [0, GRID_COLS)
     */
    void addSite(int row, int col);

private:
    std::vector<std::thread> workers;
    std::thread              monitor;
    std::deque<Fiber>        queue;
    std::mutex               queueMutex;
    std::condition_variable  queueCv;
    std::atomic<int>         fiberCount{0};

    sf::Color generateColor(int siteIdx);
    void      pushFiber(Fiber f);
    void      workerLoop();
    void      monitorLoop();
    bool      stepFiber(Fiber& f);
};
