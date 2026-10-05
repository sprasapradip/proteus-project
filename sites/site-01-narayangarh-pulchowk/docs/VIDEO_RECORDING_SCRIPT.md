# Video recording script: GitHub → Terminal → Proteus live simulation

A shot list for a real screen recording on my Windows PC. It follows one story: someone finds the project on Google, gets it from GitHub, opens it in Proteus and watches the Site 1 junction run.

Everything on screen is real: real Chrome, real GitHub, real PowerShell, real Proteus and the real project files. No mock-ups, no fake star, no fake terminal output.

## The project

| | |
|---|---|
| Repository | https://github.com/sprasapradip/proteus-project |
| Site folder | https://github.com/sprasapradip/proteus-project/tree/main/sites/site-01-narayangarh-pulchowk |
| Name | Site 1: Narayangarh Pulchowk (Narayani bridge chowk), Chitwan |
| Proteus project | `sites/site-01-narayangarh-pulchowk/proteus/site01-narayangarh-pulchowk.pdsprj` |
| Demo HEX (5x faster) | `sites/site-01-narayangarh-pulchowk/firmware/build/traffic_controller_site01_sim_x5.hex` |
| Real-time HEX (for the road, not the demo) | `firmware/build/traffic_controller_site01.hex` |
| Countdown unit HEX | `firmware/build/countdown_display.hex` |

### The four arms (use these names in captions and voice-over)

| Arm | Road | Pole | Green |
|---|---|---|---|
| North | Road to the Pokhara bus park | P1, NE corner | 30 s |
| East | Mahendra Highway, Birendra Campus / Tandi side | P2, SE corner | 35 s |
| South | Road to Rampur | P3, SW corner | 20 s |
| West | Mahendra Highway, Narayani bridge side | P4, NW corner | 35 s |

Yellow 4 s, all-red 3 s, phase order North → East → South → West, cycle 148 s. In the `_sim_x5` demo build the whole cycle takes about 30 real seconds. Say clearly that this is a Proteus demo speed, not the road timing.

## Before recording

- Proteus 8 installed, project opens without errors.
- Chrome logged in to the GitHub account you want to star from. If that account has already starred the repo, leave it starred.
- Close notifications, personal tabs and anything private. Desktop clean, cursor visible.
- Record at 1920×1080, 30 fps (OBS or the Xbox Game Bar).
- In Proteus, if you want the countdown on screen, add the countdown unit first (README, "Adding it in Proteus"). The schematic in the repo does not include it yet. If it isn't in your schematic, don't show or mention it as working.

## Shot list

| # | Scene | What to do | Keep on screen |
|---|---|---|---|
| 0 | Title card (edit) | "REAL PROTEUS PROJECT · Site 01, Narayangarh Pulchowk, Chitwan · GitHub → Clone → Proteus → Live Simulation" | 3 s |
| 1 | Desktop | Start on the Windows desktop, open Chrome | 3 s |
| 2 | Google | Search `Pradip Subedi Proteus Projects GitHub`, wait for results, click `sprasapradip/proteus-project`. If Google doesn't list it yet, search `sprasapradip proteus-project github` instead. Don't paste the URL. | 8 s |
| 3 | Repository | Pause on the repo page: name, Public badge, folders, README, Star and Code buttons. Scroll a little through the README to the part about ready HEX files. | 8 s |
| 4 | Star | Click **Star** only if it isn't starred yet. Never unstar. | 3 s |
| 5 | Site 1 | Open `sites`, then `site-01-narayangarh-pulchowk`. Scroll the README: location, the arms table (Pokhara bus park, Birendra Campus / Tandi, Rampur, Narayani bridge), timing plan, countdown, Proteus steps. | 12 s |
| 6 | Files | Show `proteus/site01-narayangarh-pulchowk.pdsprj`, then `firmware/build/` with the three HEX files. Open `images/site01-layout.png` for a moment. | 8 s |
| 7 | Terminal | Open Windows Terminal. `cd $HOME\Desktop`, then `git clone https://github.com/sprasapradip/proteus-project.git`. If you already have it: `cd proteus-project` and `git pull`. | 10 s |
| 8 | Site folder | `cd proteus-project\sites\site-01-narayangarh-pulchowk`, `dir`, `cd proteus`, `dir` (shows the `.pdsprj`) | 6 s |
| 9 | File Explorer | Open the same folder in Explorer: `firmware`, `proteus`, `images`, `docs`, `test`, `README.md`. Open `proteus`. | 5 s |
| 10 | Proteus | Open `site01-narayangarh-pulchowk.pdsprj`. Wait for it to load. Don't press Run yet. | 6 s |
| 11 | Schematic | Move the mouse slowly over the Arduino and the North, East, South and West signal LEDs, then the inputs and wiring. Moderate zoom. | 10 s |
| 12 | HEX setting | Double-click the Arduino. Show **Program File** = `..\firmware\build\traffic_controller_site01_sim_x5.hex` and **Clock** = 16 MHz. Set them only if they're wrong. OK. | 6 s |
| 13 | Run | Press Play. Let start-up all-red run (all four red). | 4 s |
| 14 | North phase | North (Pokhara bus park) green → yellow → all red → East green. Caption: "North green 30 s, yellow 4 s, all-red 3 s (shown 5x faster)". | 9 s |
| 15 | Full cycle | Keep it running: East (Birendra Campus / Tandi) → South (Rampur) → West (Narayani bridge). Don't touch the LEDs. | 22 s |
| 16 | Countdown (only if wired) | Zoom on the displays: `G 30` on North, `r 37` on East, `Y  4` during yellow. | 6 s |
| 17 | Virtual terminal (optional) | Show the boot lines: `[S01 NGH-PULCHOWK t=0s] Traffic controller v2.2.0 boot`, `4 phases, plan=SPLIT`, the four arm lines, `GREEN N for 30s`. 9600 baud. | 6 s |
| 18 | Final shot | Zoom out so all four signals are visible. Let one full change happen (North green → yellow → all red → East green). | 8 s |
| 19 | Split screen (edit) | Chrome on the repo page beside Proteus running. | 4 s |
| 20 | End card (edit) | "Site 01 · Narayangarh Pulchowk traffic signal controller · Designed and simulated in Proteus · github.com/sprasapradip/proteus-project" | 4 s |

Total: about 2.5 minutes. For a short version, cut scenes 9, 17 and 19.

## Captions to use

- "Site 01 · Narayangarh Pulchowk, Chitwan"
- "North · Pokhara bus park · East · Birendra Campus / Tandi · South · Rampur · West · Narayani bridge"
- "Split phasing · N → E → S → W"
- "Demo build runs 5x faster · road timing: 30 / 35 / 20 / 35 s"
- "Real HEX · real Proteus · tested in the simulator"

## Don't

- Don't fake the star, the clone, the terminal output or the simulation.
- Don't unstar if it's already starred.
- Don't use the real-time HEX for the demo, and don't call the 5x timing the road timing.
- Don't show the countdown as working unless it's wired in your schematic.
- Don't change project files just for the recording.
- Don't use the old names (Eatwell Bakery Cafe, shop names, `site-01-bharatpur-eatwell`). The folder is now `site-01-narayangarh-pulchowk`.

## Suggested titles

- Building and simulating a real traffic signal controller in Proteus | GitHub to live simulation
- GitHub to Proteus: Narayangarh Pulchowk 4-way traffic signal simulation
