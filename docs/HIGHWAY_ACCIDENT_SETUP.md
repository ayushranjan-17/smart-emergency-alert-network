# Running the Highway Accident Warning Add-on on Your Laptop

This add-on (`simulation/sean-highway-accident.cc`) simulates a vehicle crash on a
highway. The crashed car broadcasts a warning, the vehicles behind it relay the
warning and brake, and the results are written to a CSV file.

## 1. Install ns-3.49

- **Linux (Ubuntu/Debian):** follow the official guide at https://www.nsnam.org/
- **Windows:** install WSL2 (Ubuntu) and do the Linux steps inside it.
- **macOS:** use Homebrew for the build tools, then follow the same Linux steps.

Typical Ubuntu setup:

```bash
sudo apt update
sudo apt install -y g++ python3 cmake ninja-build git
cd ~
wget https://www.nsnam.org/releases/ns-allinone-3.49.tar.bz2
tar xjf ns-allinone-3.49.tar.bz2
cd ns-allinone-3.49/ns-3.49
./ns3 configure --enable-examples --enable-tests
./ns3 build
```

## 2. Get the project and the add-on

```bash
git clone https://github.com/ayushranjan-17/smart-emergency-alert-network.git
cd smart-emergency-alert-network
git checkout feature/highway-accident-warning   # if you use the branch
cp simulation/sean-highway-accident.cc ~/ns-allinone-3.49/ns-3.49/scratch/
```

## 3. Run the simulation

From the `ns-3.49` folder:

```bash
# Warnings with EF/QoS priority (recommended)
./ns3 run "sean-highway-accident --vehicles=30 --priority=1"

# Best-effort baseline (no priority)
./ns3 run "sean-highway-accident --vehicles=30 --priority=0"

# Under civilian background load (priority comparison)
./ns3 run "sean-highway-accident --vehicles=30 --priority=1 --bgRateKbps=800"
./ns3 run "sean-highway-accident --vehicles=30 --priority=0 --bgRateKbps=800"

# Try other values
./ns3 run "sean-highway-accident --vehicles=60 --spacing=25 --speed=30 --maxTtl=15"
```

Useful options:

| Option | Meaning | Default |
|---|---|---|
| `--vehicles` | Vehicles behind the crash | 30 |
| `--spacing` | Gap between cars (m) | 40 |
| `--speed` | Cruising speed (m/s) | 25 (~90 km/h) |
| `--decel` | Braking deceleration (m/s²) | 4 |
| `--standoff` | Distance to stop before the crash (m) | 10 |
| `--accidentTime` | Time of crash (s) | 10 |
| `--maxTtl` | Max relay hops | 10 |
| `--priority` | 1 = EF/QoS for warnings, 0 = best effort | 1 |
| `--bgRateKbps` | Civilian traffic per vehicle (kbps) | 0 |
| `--csv` | Output file | highway-accident-vehicles.csv |

## 4. Read the results

The console prints a summary (vehicles warned, hops used, delay, collisions).
Per-vehicle details are in `highway-accident-vehicles.csv`. Columns:
`vehicle, warned, warn_time_s, hops, distance_at_warning_m, min_speed_mps, gap_at_closest_m, collision`.

A run is good if collisions = 0 and nearly all vehicles behind the crash are warned.

## Notes and limitations

- This is a **simulation model**. It uses Wi-Fi ad hoc networking, not real
  DSRC/C-V2X hardware, and the car model is a simple constant-velocity vehicle
  with a braking rule. Use it to compare designs, not to certify a real system.
- If `./ns3 run` says the program is not found, check that the file is in
  `ns-3.49/scratch/` and run `./ns3 build` again.
- The code was written without being compiled in this environment. If the
  build reports an error, send the first error message to get it fixed.
