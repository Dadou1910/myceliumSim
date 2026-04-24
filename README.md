# myceliumSim

Multithreaded mycelium growth simulator. Click anywhere on the window to plant a colony and watch it spread. Each colony gets its own color and competes for space against the others.

<img width="953" height="701" alt="Screenshot from 2026-04-24 10-07-40" src="https://github.com/user-attachments/assets/07e914c4-49cd-429f-b147-3e05bdaee80c" />
<img width="953" height="701" alt="Screenshot from 2026-04-24 10-07-53" src="https://github.com/user-attachments/assets/bc7e4b19-af8c-4823-9800-fb1302d6875b" />
<img width="953" height="701" alt="Screenshot from 2026-04-24 10-08-04" src="https://github.com/user-attachments/assets/5117f2fc-9c21-4bc9-97c6-a34009e3f4ef" />


## How it works

Each colony spawns a bunch of growing tips (fibers) that wander across the grid. They drift slightly every step to get organic-looking curves, occasionally fork into two branches, and paint a small blob around each position. When a tip gets completely surrounded it dies, and the monitor thread respawns new ones from the frontier so enclosed gaps still get filled.

The grid is a flat array of atomics — threads race to claim cells with compare-exchange, no coarse lock on the grid itself. The event log is pre-allocated and append-only so the render thread can read it without any synchronization.

## Dependencies

- SFML 2.x
- C++17
- pthreads

On Ubuntu/Debian:
```
sudo apt install libsfml-dev
```

## Build & run

```
make
./myceliumSim
```

`Escape` or closing the window stops everything cleanly.

## Tweaking

All the growth parameters are constants at the top of `simulation.hpp` — fork chance, drift, blob size, max fibers, etc. Easy to play with.
