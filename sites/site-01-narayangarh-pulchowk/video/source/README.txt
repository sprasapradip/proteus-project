Site 1 demo video - how it is made

Everything in the video comes from a real capture:
  data/sim.jsonl   source/sim_capture.c run on the real firmware: the _sim_x5 controller
                   and the countdown display unit, wired A5 -> RX, in simavr. Lamps,
                   countdown digits, A5 frames and the serial log, every 50 ms.
  data/clone.txt   git clone --progress https://github.com/sprasapradip/proteus-project.git
  data/ls.txt      ls of sites/site-01-narayangarh-pulchowk in that fresh clone
  data/build.txt   tools/build.sh in that fresh clone
  test/test-report.txt (the site's latest tools/test.sh report)
  images/site01-layout.png (the repo's own layout drawing)

The simulation scene is simavr, not Proteus. Proteus runs the same HEX; record that part
yourself with docs/VIDEO_RECORDING_SCRIPT.md.

Rebuild
  gcc -O1 sim_capture.c -o sim_capture -lsimavr -lelf
  ./sim_capture <site01_sim_x5.elf> <countdown_display.elf> 38000 > data/sim.jsonl
  python3 prep.py                          # -> data/data.js
  npm install playwright                   # or set PLAYWRIGHT to an installed copy
  node render.js 2 45 73                   # preview stills -> preview/
  node render.js                           # -> ../site01-narayangarh-pulchowk-demo.mp4 (needs ffmpeg)

Where to edit
  engine.html   timeline (T), chapter titles, captions, look and feel
  prep.py       which captured lines go into the terminal scenes

Fonts: Montserrat and Jost (SIL Open Font License), same as the Sprasa promo videos.
