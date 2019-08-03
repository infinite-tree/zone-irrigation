#! /usr/bin/env python

"""
Script to turn on/off irrigastion valves
"""
import datetime
import glob
import json
import logging
import logging.handlers
import os
import pytz
import serial
import subprocess
import sys
import time

from flask import Flask
from apscheduler.schedulers.background import BackgroundScheduler
from influxdb import InfluxDBClient

DEFAULT_SERIAL_DEVICE = "/dev/ttyUSB0"
LOG_FILE = "~/logs/zone_irrigation.log"
CONFIG_FILE = os.path.expanduser("~/.zone-irrigation.config")
INFLUXDB_CONFIG_FILE = os.path.expanduser("~/.influxdb.config")

MULTI_LOOPS = 2
ON_PAUSE = 20
OFF_PAUSE = 5

LOOP_DELAY = 1

app = Flask(__name__)

config = {
    "valves": {
        1: {
            "minutes": 360.0,
        },
        2: {
            "minutes": 360.0,
        },
        3: {
            "minutes": 360.0,
        },
        4: {
            "minutes": 360.0,
        },
        5: {
            "minutes": 360.0,
        },
        6: {
            "minutes": 0.0,
        },
        7: {
            "minutes": 0.5,
        }
    },
     "site": {
        "location": "FIELD1",
        "controller": "irrigation1"
    }
}

###################################################################
# Flask routes
###################################################################
@app.route("/counter")
def counter():
    return json.dumps({"counter": app.controller.WaterMeter.Counter})

@app.route("/valves")
def valves():
    v = {}
    open_valves = app.controller.Arduino.getOpenValves()
    for valve in app.controller.Valves:
        v[valve.Number] = valve.Number in open_valves

    return json.dumps(v)

@app.route("/gpm")
def gpm():
    return json.dumps({"gpm": app.controller.WaterMeter.SavedGPM})

@app.route("/start", methods=["POST"])
def start():
    data = request.form
    print("DATA: %s"%(data))
    return json.dumps({"sucess": True})


###################################################################
# Controller routines
###################################################################
def writeState(name, conf):
    global config
    config["zones"][name] = conf
    with open(CONFIG_FILE, "w") as f:
        f.write(json.dumps(config, sort_keys=True, indent=4, separators=(',', ': ')))


def getNextDatetime(hour):
    # This function assumes local timezone which is currently hard coded
    now = datetime.datetime.now(pytz.timezone('US/Pacific'))
    next_time = datetime.time(hour, tzinfo=pytz.timezone('US/Pacific'))
    if now.time() >= next_time:
        # since we've already passed that time today, look at tomorrow
        day = (now + datetime.timedelta(hours=24)).date()
    else:
        # still early enough that we are looking at today
        day = now.date()
    return datetime.datetime.combine(day, next_time)


class Arduino(object):
    def __init__(self, log):
        self.Log = log
        self.Stream = None
        self._newSerial()
        self.Running = False

    def _newSerial(self):
        '''
        Reset the serial device using the DTR lines
        '''
        try:
            self.Stream.close()
        except:
            pass

        serial_devices = glob.glob("/dev/ttyUSB*")
        if len(serial_devices) < 1:
            self.Log.error("No Serial devices detected. Restarting ...")
            subprocess.call("sudo reboot", shell=True)

        self.SerialDevice = sorted(serial_devices)[-1]
        self.Stream = serial.Serial(self.SerialDevice, 57600, timeout=1)

        if self._sendData('I') == 'I':
            return
        # still not reset
        self.Log.error("Failed to reset Serial!!!")

    def resetSerial(self):
        try:
            self.Stream.close()
        except:
            pass

        # FIXME: match device to the actual
        subprocess.call("sudo ./usbreset /dev/bus/usb/001/002", shell=True, cwd=os.path.expanduser("~/"))
        time.sleep(2)
        self._newSerial()

    def _readResponse(self):
        try:
            response = self.Stream.readline().decode('utf-8').strip()
            while len(response) > 0 and response.startswith('D'):
                self.Log.debug(response)
                response = self.Stream.readline().decode('utf-8').strip()
        except Exception as e:
            self.Log.error("Serial exception: %s" % (e), exc_info=1)
            self.resetSerial()

        self.Log.debug("SERIAL - Response: '%s'"%(response))
        return response

    def _sendData(self, value):
        self._readResponse()
        v = bytes(value, 'utf-8')
        self.Log.debug("SERIAL - Sending: %s"%(v))
        self.Stream.write(v)
        return self._readResponse()

    def handleDebugMessages(self):
        self._readResponse()

    def getOpenValves(self):
        valves = self._sendData("V")
        return [int(c) for c in valves]

    def getWaterCounter(self):
        # This should only be called via the WaterMeter class
        try:
            return int(self._sendData("W"))
        except Exception:
            self.Log.error("Int conversion failed for Arduino.getWaterCounter()")
            return 0

    def toggleValve(self, valve):
        if self._sendData(str(valve)) == str(valve):
            return True
        return False

    def checkStartButton(self):
        return self._sendData('S') == 'S'

    def enterProgramMode(self):
        self.Running = True
        return self._sendData('P') == 'P'

    def leaveProgramMode(self):
        self.Running = False
        return self._sendData('p') == 'p'

    def isProgramRunning(self):
        return self.Running


class Valve(object):
    def __init__(self, number, log, conf, influx, arduino):
        self.Number = number
        self.Log = log
        self.Config = config
        self.Influx = influx
        self.Arduino = arduino
        self.Running = False

    @property
    def Duration(self):
        return self.Config["minutes"]

    def isOn(self):
        self.Running = self.Number in self.Arduino.getOpenValves()
        return self.Running

    def _on(self):
        if not self.Running:
            self.Arduino.toggleValve(self.Number)

    def on(self):
        self.Running = True
        self._on()
        self.Log.info("%s - %s is OPEN"%(datetime.datetime.now(), self.Number))

    def _off(self):
        if self.Running == True:
            self.Arduino.toggleValve(self.Number)

    def off(self):
        self.Running = False
        self._off()
        self.Log.info("%s - %s is OFF"%(datetime.datetime.now(), self.Number))

    def __repr__(self):
        return "Valve %s: %s"%(self.Number, "On" if self.Running else "Off")


class WaterMeter(object):
    def __init__(self, influx, arduino, log):
        self.Influx = influx
        self.Log = log
        self.Arduino = arduino
        self.LastWaterReading = time.time()
        self.LastGPMRequest = time.time()
        self.SavedGPM = 0.0

    @property
    def Counter(self):
        self.LastWaterReading = time.time()
        return self.Arduino.getWaterCounter()

    @property
    def GPM(self):
        now = time.time()
        if now - self.LastGPMRequest >= 60:
            self.LastGPMRequest = now
            last = self.LastWaterReading
            count = self.Counter
            # Caclulate gallon per minute and then divide by two since the counter triggers twice per gallon
            self.SavedGPM = float(count) / ((self.LastWaterReading - last) / 60.0) / 2.0

        return self.SavedGPM


class InfluxWrapper(object):
    def __init__(self, log, influx_config, site_config):
        self.Influx = InfluxDBClient(influx_config['host'],
                                     influx_config['port'],
                                     influx_config['login'],
                                     influx_config['password'],
                                     influx_config['database'],
                                     ssl=True,
                                     timeout=60)
        self.Log = log
        self.Points = []
        self.Location = site_config['location']
        self.Controller = site_config['controller']
        self.LastSent = datetime.datetime.now()
        self.Interval = influx_config['interval']
        self.MaxPoints = influx_config['max_points']

    def getTime(self):
        now = datetime.datetime.utcnow()
        return now.strftime('%Y-%m-%dT%H:%M:%SZ')

    def writePoints(self):
        ret = None

        # drop old points if there are too many
        if len(self.Points) > self.MaxPoints:
            self.Points = self.Points[self.MaxPoints:]

        for x in range(10):
            try:
                ret = self.Influx.write_points(self.Points)
            except Exception as e:
                self.Log.error("Influxdb point failure: %s"%(e))
                ret = 0
            if ret:
                self.Log.info("%s - Sent %d points to Influx"%(datetime.datetime.now(), len(self.Points)))
                self.LastSent = datetime.datetime.now()
                self.Points = []
                return ret

            time.sleep(0.2)

        self.Log.error("%s - Failed to send %d points to Influx: %s"%(datetime.datetime.now(), len(self.Points), ret))
        return ret

    def sendMeasurement(self, measurement, tags, value):
        point = {
            "measurement": measurement,
            "tags": {
                "location": self.Location,
                "controller": self.Controller
            },
            "time": self.getTime(),
            "fields": {
                "value": value
            }
        }
        point['tags'].update(tags)

        self.Points.append(point)

        now = datetime.datetime.now()
        if len(self.Points) >= self.MaxPoints or (now - self.LastSent).seconds >= self.Interval:
            return self.writePoints()
        return True

    def query(self, *args, **kwargs):
        return self.Influx.query(*args, **kwargs)


class IrrigationController(object):
    def __init__(self, log, valves, water_meter, influx, arduino, config):
        self.Log = log
        self.Valves = valves
        self.WaterMeter = water_meter
        self.Influx = influx
        self.Arduino = arduino
        self.LastGPMTime = time.time()

    def startCancelCheck(self):
        # FIXME: implement
        return

    def run(self):
        self.Arduino.handleDebugMessages()
        self.Log.info("%s - Loop" % (datetime.datetime.now()))
        self.Influx.sendMeasurement("water_gallons", {}, self.WaterMeter.GPM)

        open_valves = self.Arduino.getOpenValves()
        for valve in self.Valves:
            self.Influx.sendMeasurement("open_valve", {"valve":str(valve.Number)}, 1.0 if valve.Number in open_valves else 0.0)

        self.Influx.sendMeasurement("program_running", {}, self.Arduino.isProgramRunning())
        # self.refuelCheck(60)

def reboot(log):
    # if os.path.isfile(os.path.expanduser("~/.reboot")):
    #     os.remove(os.path.expanduser("~/.reboot"))

    # if not os.path.isfile(os.path.expanduser("~/.reboot2")):
    #     with open(os.path.expanduser("~/.reboot2"), "w") as f:
    #         f.write("%s\n"%(datetime.datetime.now()))

    #     log.error("############ REBOOTING ###########")
    #     subprocess.call("sudo reboot", shell=True)
    return


def main():
    log = logging.getLogger('ZoneIrrigationtLogger')
    log.setLevel(logging.DEBUG)
    log_file = os.path.realpath(os.path.expanduser(LOG_FILE))
    # FIXME: TimedFileHandler
    handler = logging.handlers.RotatingFileHandler(log_file, maxBytes=500000, backupCount=5)

    log.addHandler(handler)
    log.addHandler(logging.StreamHandler())
    log.info("%s - Zone Irrigation Controller STARTED"%(datetime.datetime.now()))

    reboot(log)

    # Setup influxdb
    with open(INFLUXDB_CONFIG_FILE) as f:
        influx_config = json.loads(f.read())

    # Handle start state
    global config

    if os.path.isfile(CONFIG_FILE):
        with open(CONFIG_FILE) as f:
            config = json.loads(f.read())
    else:
        log.error("No config file '%s' found. Defaulting to builtin config"%(CONFIG_FILE))

    log.info("%s - Initializing Influx"%(datetime.datetime.now()))
    influx = InfluxWrapper(log, influx_config, config['site'])

    log.info("%s - Initializing Arduino"%(datetime.datetime.now()))
    arduino = Arduino(log)

    log.info("%s - Setting up valve objects"%(datetime.datetime.now()))
    valves = []
    for number, conf in config["valves"].items():
        valves.append(Valve(int(number), log, conf, influx, arduino))

    log.info("%s - Initializing Water Meter"%(datetime.datetime.now()))
    water_meter = WaterMeter(influx, arduino, log)

    controller = IrrigationController(log, valves, water_meter, influx, arduino, config)
    app.controller = controller

    # import pdb
    # pdb.set_trace()

    ######################################################
    log.info("%s - ENTERING RUN LOOP"%(datetime.datetime.now()))
    try:
        scheduler = BackgroundScheduler()
        job = scheduler.add_job(controller.run, 'interval', minutes=LOOP_DELAY)
        scheduler.start()

        IS_PROD = os.environ.get("IS_PROD", False)
        app.run(debug=not IS_PROD, host="0.0.0.0", threaded=True)
    except Exception as e:
        log.error("Main loop failed: %s"%(e), exc_info=1)
        return 1
    return 1


if __name__ == "__main__":
    sys.exit(main())
