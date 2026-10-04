# Traffic signal sites

One folder per real junction. Each folder is complete on its own: site layout, firmware built for that junction, HEX files, Proteus project, timing plan, bill of materials, and the installation and commissioning checklist.

The controller itself is developed and tested in [projects/07-traffic-light-controller](../projects/07-traffic-light-controller). A site folder takes that firmware and builds in the junction's own profile, arm names and timings.

| Site | Junction | Location | Plan | Cycle | Status |
|---|---|---|---|---|---|
| [1](site-01-bharatpur-eatwell) | Mahendra Highway at Eatwell Bakery Cafe | Bharatpur, Chitwan (27.6965 N, 84.4210 E) | Split, highway 35 s, side road 20 s | 138 s | Design ready, countdown display added, tested; timings awaiting approval |
| 2 to 8 | | | | | Not started |

## Adding the next site

1. Put the site sketch and map screenshot in `tools/Sites-raw-photos/`.
2. Copy `site-01-bharatpur-eatwell` to `site-0N-<city>-<landmark>`.
3. In the controller `.ino`, change the `SITES` entry (name, green times, yellow, all-red, pedestrian times) and the four `ARM_NAME` lines. The countdown unit firmware needs no change.
4. Update `site.json`, `tools/make_images.py` (arm names, corners, shops) and the README.
5. Run `tools/build.sh`, `tools/test.sh` and `tools/make_images.py`.
