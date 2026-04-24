#include "simulation.hpp"
#include <cmath>
#include <random>
#include <chrono>

static thread_local std::mt19937 rng(std::random_device{}());

void Simulation::start() {
    running = true;
    // pre-allocate to max possible events so appends never reallocate
    eventLog.resize(GRID_ROWS * GRID_COLS);
    unsigned int n = std::max(2u, std::thread::hardware_concurrency());
    for (unsigned int i = 0; i < n; ++i)
        workers.emplace_back(&Simulation::workerLoop, this);
    monitor = std::thread(&Simulation::monitorLoop, this);
}

void Simulation::stop() {
    running = false;
    queueCv.notify_all();
    for (auto& w : workers)
        if (w.joinable()) w.join();
    if (monitor.joinable())
        monitor.join();
}

sf::Color Simulation::generateColor(int siteIdx) {
    // golden-angle spacing keeps successive colonies visually distinct
    float hue = std::fmod(siteIdx * 137.508f, 360.f);
    float c = 0.85f;
    float x = c * (1.f - std::fabs(std::fmod(hue / 60.f, 2.f) - 1.f));
    float m = 0.15f;
    float r, g, b;

    switch (int(hue / 60.f) % 6) {
        case 0:  r = c; g = x; b = 0; break;
        case 1:  r = x; g = c; b = 0; break;
        case 2:  r = 0; g = c; b = x; break;
        case 3:  r = 0; g = x; b = c; break;
        case 4:  r = x; g = 0; b = c; break;
        default: r = c; g = 0; b = x; break;
    }

    return { uint8_t((r + m) * 255), uint8_t((g + m) * 255), uint8_t((b + m) * 255) };
}

void Simulation::addSite(int row, int col) {
    int siteIdx = int(siteColors.size());
    siteColors.push_back(generateColor(siteIdx));
    grid.at(row, col).store(siteIdx, std::memory_order_relaxed);
    ++cellsColonized;

    for (int i = 0; i < INITIAL_FIBERS; ++i)
        pushFiber({ siteIdx, row, col, i * 2.f * float(M_PI) / INITIAL_FIBERS });
}

void Simulation::pushFiber(Fiber f) {
    if (fiberCount.load(std::memory_order_relaxed) >= MAX_FIBERS) return;
    ++fiberCount;
    {
        std::lock_guard lock(queueMutex);
        queue.push_back(f);
    }
    queueCv.notify_one();
}

// grabs a fiber, runs STEPS_PER_TURN steps, then re-queues it
// batching steps per lock acquisition keeps mutex traffic low
void Simulation::workerLoop() {
    while (running) {
        Fiber f;
        {
            std::unique_lock lock(queueMutex);
            queueCv.wait(lock, [this] { return !queue.empty() || !running; });
            if (!running) break;
            f = queue.front();
            queue.pop_front();
        }

        bool alive = true;
        for (int i = 0; i < STEPS_PER_TURN && alive && running; ++i)
            alive = stepFiber(f);

        if (alive) {
            std::lock_guard lock(queueMutex);
            queue.push_back(f);
            queueCv.notify_one();
        } else {
            --fiberCount;
        }
    }
}

// runs every 500ms — stops the simulation when the spread rate stays too low
// for too long, and respawns fibers from frontier cells if fiberCount hits zero
void Simulation::monitorLoop() {
    std::uniform_real_distribution<float> angleDist(0.f, 2.f * float(M_PI));
    std::uniform_int_distribution<int>    startDist(0, GRID_ROWS * GRID_COLS - 1);

    long prevColonized = 0;
    int  slowCount     = 0;

    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (!running) break;

        long currColonized = cellsColonized.load(std::memory_order_relaxed);
        long delta         = currColonized - prevColonized;
        prevColonized      = currColonized;

        if (siteColors.empty()) { slowCount = 0; continue; }

        if (delta < SLOW_THRESHOLD) {
            if (++slowCount >= SLOW_INTERVALS) {
                running = false;
                queueCv.notify_all();
                break;
            }
        } else {
            slowCount = 0;
        }

        if (fiberCount.load(std::memory_order_relaxed) >= INITIAL_FIBERS) continue;

        // scan from a random offset so we don't always start from the top-left;
        // spawn new fibers on frontier cells (colonized with at least one empty neighbor)
        // so they enter the gap immediately without teleporting
        int lastSite = int(siteColors.size()) - 1;
        int spawned  = 0;
        int start    = startDist(rng);

        for (int j = 0; j < GRID_ROWS * GRID_COLS && spawned < INITIAL_FIBERS; ++j) {
            int i   = (start + j) % (GRID_ROWS * GRID_COLS);
            int row = i / GRID_COLS, col = i % GRID_COLS;

            if (grid.cells[i].load(std::memory_order_relaxed) < 0) continue;

            bool onFrontier = false;
            for (auto [dr, dc] : { std::pair{-1,0}, {1,0}, {0,-1}, {0,1} }) {
                int nr = row + dr, nc = col + dc;
                if (nr < 0 || nr >= GRID_ROWS || nc < 0 || nc >= GRID_COLS) continue;
                if (grid.at(nr, nc).load(std::memory_order_relaxed) == -1) {
                    onFrontier = true; break;
                }
            }
            if (!onFrontier) continue;

            pushFiber({ lastSite, row, col, angleDist(rng) });
            ++spawned;
        }
    }
}

bool Simulation::stepFiber(Fiber& f) {
    std::uniform_real_distribution<float> chanceDist(0.f, 1.f);
    std::uniform_real_distribution<float> driftDist(-DRIFT_MAX, DRIFT_MAX);
    std::uniform_real_distribution<float> forkAngleDist(FORK_ANGLE_MIN, FORK_ANGLE_MAX);

    f.angle += driftDist(rng);
    float pdx = std::cos(f.angle);
    float pdy = std::sin(f.angle);

    // build a list of empty neighbors weighted by alignment with current heading
    struct Candidate { int nr, nc; float weight; };
    Candidate candidates[8];
    int       count = 0;

    for (int dr = -1; dr <= 1; ++dr) {
        for (int dc = -1; dc <= 1; ++dc) {
            if (dr == 0 && dc == 0) continue;
            int nr = f.row + dr, nc = f.col + dc;
            if (nr < 0 || nr >= GRID_ROWS || nc < 0 || nc >= GRID_COLS) continue;
            if (grid.at(nr, nc).load(std::memory_order_relaxed) != -1) continue;

            float len    = std::sqrt(float(dr * dr + dc * dc));
            float align  = (dc * pdx + dr * pdy) / len;
            float weight = std::pow(std::max(0.f, align + 0.5f), 2.f) + 0.01f;
            candidates[count++] = { nr, nc, weight };
        }
    }

    // if stuck, expand search radius before giving up
    if (count == 0) {
        for (int radius = 2; radius <= LOCAL_STUCK_RADIUS && count == 0; ++radius) {
            for (int dr = -radius; dr <= radius; ++dr) {
                for (int dc = -radius; dc <= radius; ++dc) {
                    if (std::abs(dr) != radius && std::abs(dc) != radius) continue;
                    int nr = f.row + dr, nc = f.col + dc;
                    if (nr < 0 || nr >= GRID_ROWS || nc < 0 || nc >= GRID_COLS) continue;
                    if (grid.at(nr, nc).load(std::memory_order_relaxed) == -1) {
                        f.row = nr; f.col = nc;
                        return true;
                    }
                }
            }
        }
        return false; // truly stuck, monitor will respawn from the frontier
    }

    // weighted random pick
    float total = 0.f;
    for (int i = 0; i < count; ++i) total += candidates[i].weight;
    float pick = chanceDist(rng) * total, acc = 0.f;
    int   chosen = 0;
    for (int i = 0; i < count; ++i) {
        acc += candidates[i].weight;
        if (acc >= pick) { chosen = i; break; }
    }

    // atomically claim the cell; log it if we got it
    int expected = -1;
    if (grid.at(candidates[chosen].nr, candidates[chosen].nc)
            .compare_exchange_strong(expected, f.siteIdx, std::memory_order_relaxed)) {
        ++cellsColonized;
        int slot = eventLogSize.fetch_add(1, std::memory_order_relaxed);
        eventLog[slot] = { candidates[chosen].nr * GRID_COLS + candidates[chosen].nc,
                           f.siteIdx };
    }

    f.row = candidates[chosen].nr;
    f.col = candidates[chosen].nc;

    // paint a small blob around the new tip position
    for (int dr = -BLOB_RADIUS; dr <= BLOB_RADIUS; ++dr) {
        for (int dc = -BLOB_RADIUS; dc <= BLOB_RADIUS; ++dc) {
            if (dr * dr + dc * dc > BLOB_RADIUS * BLOB_RADIUS) continue;
            int nr = f.row + dr, nc = f.col + dc;
            if (nr < 0 || nr >= GRID_ROWS || nc < 0 || nc >= GRID_COLS) continue;
            if (chanceDist(rng) < BLOB_CHANCE) {
                int ex = -1;
                if (grid.at(nr, nc).compare_exchange_strong(
                        ex, f.siteIdx, std::memory_order_relaxed)) {
                    ++cellsColonized;
                    int slot = eventLogSize.fetch_add(1, std::memory_order_relaxed);
                    eventLog[slot] = { nr * GRID_COLS + nc, f.siteIdx };
                }
            }
        }
    }

    // maybe branch
    if (chanceDist(rng) < FORK_CHANCE &&
        fiberCount.load(std::memory_order_relaxed) < MAX_FIBERS) {
        float sign = (chanceDist(rng) < 0.5f) ? 1.f : -1.f;
        pushFiber({ f.siteIdx, f.row, f.col,
                    f.angle + sign * forkAngleDist(rng) });
    }

    return true;
}
