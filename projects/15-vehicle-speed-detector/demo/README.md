# Nepal demo: Kathmandu, Pokhara, Chitwan, Hetauda

`nepal-speed-watch.html` is a live demo of the speed detector at four sites. Open it in any browser, no install needed. Vehicles drive through the two beams, the big LED display shows each driver's speed, and the SLOW DOWN (बिस्तारै चलाउनुहोस्), THANK YOU (धन्यवाद) and KEEP DISTANCE (दूरी कायम राख्नुहोस्) signs light up. The statistics, the speed histogram, the hourly flow and the CSV log update as the day goes on.

The traffic is simulated. The sites, limits and volumes are demo settings I picked to show four different kinds of road. They aren't official figures or measurements.

## The four sites

| Site | Road (demo) | Limit car/bike | Limit bus/truck | What it shows |
|---|---|---|---|---|
| Kathmandu काठमाडौं | Koteshwor, Araniko Highway | 40 | 30 | Heavy urban traffic, about two thirds motorbikes, morning and evening peaks, lots of close following |
| Pokhara पोखरा | Lakeside, Baidam Road | 30 | 25 | Tourist street with people walking, busiest in the evening |
| Chitwan चितवन | Bharatpur, East-West Highway through town | 50 | 40 | Highway speeds, many long-distance buses and trucks, traffic all night |
| Hetauda हेटौंडा | Main road near a school | 30 | 25 | School-zone peaks around 10:00 and 16:00, where the 30 km/h limit matters most |

## What you can do in the demo

- Switch between the four cities. All four detectors keep running in the background.
- Run at 1×, 5× or 20× speed, or jump to the end of the day with "Run rest of day".
- Change the limit with `SET LIMIT −/+`, the same setting as the firmware command. The violation count, the histogram line and the "fits / ignored" verdict update at once.
- Compare all four sites in the table at the bottom. "Ignored" means the 85th percentile speed is more than 5 km/h over the limit, so a sign on its own won't fix that road.

The vehicle records are worked out the way the firmware does it: beam times in 4 µs steps, speed from the time between the beams, length from how long the beams are blocked, then class, headway and the tailgating flag. The log uses the firmware's exact CSV format, so `tools/traffic_report.py` can read a saved copy.

## Setting up a real detector for one of these sites

On a real board, send these commands over USB (9600 baud) after installation. Use the limits the traffic police or road authority set for that road. The numbers below are only the demo values.

```
PIN 1234
SET PIN 4821          (choose your own PIN first)
PIN 4821
SET GAP 1000          (your measured beam spacing in mm)
SET LIMIT 40          (Kathmandu demo: 40, Pokhara 30, Chitwan 50, Hetauda 30)
SET HEAVY 30          (Kathmandu demo: 30, Pokhara 25, Chitwan 40, Hetauda 25)
TIME 07:30:00
STATUS
```
