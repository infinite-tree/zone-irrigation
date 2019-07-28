# 6 Zone Irrigation Controller with drain valve

Turn on and off irrigation zones while logging data to influxdb.


## Status
Experimental


## Software Dependencies

  - pySerial
  - python-apscheduler
  - flask
  - influxdb


## Installation

```
sudo cp zone-irrigation.service /lib/systemd/system/
sudo systemctl enable zone-irrigation.service
sudo systemctl start zone-rrigation.service
```

