#! /usr/bin/env python

"""
Script to turn on/off irrigastion valves
"""
import datetime
import json
import logging
import logging.handlers
import os
import pytz
import sys
import time

from flask import (Flask,
                   jsonify,
                   render_template,
                   request)
from apscheduler.schedulers.background import BackgroundScheduler

import arduinoInterface
import influxwrapper

DEFAULT_SERIAL_DEVICE = "/dev/ttyUSB0"
LOG_FILE = "~/logs/zone_irrigation.log"
CONFIG_FILE = os.path.expanduser("~/.zone-irrigation.config")
INFLUXDB_CONFIG_FILE = os.path.expanduser("~/.influxdb.config")

PUMP_CONTROL = "outlet_on"

MULTI_LOOPS = 2
ON_PAUSE = 20
OFF_PAUSE = 5

LOOP_DELAY = 1

app = Flask(__name__)

config = {
    "valves": 2,
     "site": {
        "location": "FIELD1",
        "controller": "irrigation1"
    },
    "status": {
        "running": False,
        "remaining": 0.0,
        "open_valves": []
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
    try:
        hours = int(data["hours"])
    except Exception as e:
        hours = 0

    print("HOURS: %d"%hours)
    ret = False
    if hours:
        ret = app.controller.start(hours)
    return json.dumps({"success": ret})

@app.route("/status", methods=["GET"])
def status():
    msg = app.controller.getStatusMessage()
    state = "OFF"
    if app.controller.isRunning():
        state = "RUNNING"
        if "start" in msg.lower():
            state = "STARTING"
        elif "stop" in msg.lower():
            state = "STOPPING"

    status = {
                "status": state,
                "message": msg,
                "percent": app.controller.getPercentComplete()

            }
    return json.dumps(status)

@app.route("/stop", methods=["POST"])
def stop():
    ret = app.controller.stop()
    return json.dumps({"success": ret})

@app.route("/pump_control", methods=["GET"])
def pump_control():
    return json.dumps({PUMP_CONTROL:app.controller.isRunning()})

@app.route('/', methods=['GET'])
def main():
    return render_template("index.html")

@app.errorhandler(404)
def not_found(e):
    message = "404 We couldn't find the page"
    return render_template("index.html", error_message=message)


###################################################################
# Controller routines
###################################################################
def writeConfigState():
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


def prettyTimeDelta(seconds):
    seconds = int(seconds)
    days, seconds = divmod(seconds, 86400)
    hours, seconds = divmod(seconds, 3600)
    minutes, seconds = divmod(seconds, 60)
    if days > 0:
        return '%d days & %d hours' % (days, hours)
    elif hours > 0:
        return '%d hours and %d minutes' % (hours, minutes)
    elif minutes > 0:
        return '%d minutes' % (minutes)
    else:
        return 'less than a minute'


class Valve(object):
    def __init__(self, number, log, influx, arduino):
        self.Number = number
        self.Log = log
        self.Influx = influx
        self.Arduino = arduino
        self.Running = False

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
        self.LastWaterReading = time.time() - 60
        self.LastGPMRequest = time.time() - 60
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
            # LastWaterReading is updated when ever Counter is accessed
            last = self.LastWaterReading
            count = self.Counter
            # Caclulate gallon per minute (counter returns gallons)
            self.SavedGPM = float(count) / ((now - last) / 60.0)

        return self.SavedGPM


class IrrigationController(object):
    def __init__(self, log, valves, water_meter, influx, arduino, config):
        self.Log = log
        self.Valves = valves
        self.WaterMeter = water_meter
        self.Influx = influx
        self.Arduino = arduino
        self.Config = config
        self.Config.setdefault("status", {"running": False, "remaining": 0.0, "open_valves": []})
        self.LastGPMTime = time.time()
        self.StatusMessage = "OFF"
        self.StartTime = None
        self.RunTime = None

    def getStatusMessage(self):
        return self.StatusMessage
    
    def isRunning(self):
        return self.Config["status"]["running"]
    
    def getPercentComplete(self):
        if self.StartTime is None:
            return 0

        elapsed = time.time() - self.StartTime
        return int((elapsed / self.RunTime)*100)
    
    def openAllValves(self):
        for valve in self.Valves:
            valve.on()
            if not valve.Number in self.Config["status"]["open_valves"]:
                self.Config["status"]["open_valves"].append(valve.Number)
    
    def closeAllValves(self):
        for valve in self.Valves:
            valve.off()
            if valve.Number in self.Config["status"]["open_valves"]:
                self.Config["status"]["open_valves"].remove(valve.Number)

    def start(self, hours):
        # Open the valves first
        self.openAllValves()
        
        # River pump will query /pump_control and start/stop based on the result
        self.StatusMessage = "Starting Pump"
        self.Config["status"]["running"] = True
        self.Config["status"]["remaining"] = float(hours*60*60)
        writeConfigState()
    
    def stop(self):
        self.StatusMessage = "Stopping"
        self.StartTime = None
        self.RunTime = None
        self.Config["status"]["running"] = False
        self.Config["status"]["remaining"] = 0.0
        writeConfigState()
        # Valves will close in the run loop after water has stopped flowing

    def run(self):
        self.Arduino.handleDebugMessages()
        self.Log.info("%s - Loop" % (datetime.datetime.now()))
        now = time.time()

        #
        # Handle runtime
        #
        if self.isRunning():
            # TWater just started flowing. Start the timer
            if self.WaterMeter.SavedGPM > 0 and self.StartTime is None:
                if not self.StartTime:
                    self.StartTime = now
                    self.RunTime = self.Config["status"]["remaining"]
            else:
                    self.StatusMessage = "Starting Pump"

            if not self.StartTime is None:
                # Update the remaining time
                remaining = self.RunTime - (now - self.StartTime)
                if remaining <= 0.0:
                    self.stop()
                else:
                    self.Config["status"]["remaining"] = remaining
                    self.StatusMessage = "%s remaining"%(prettyTimeDelta(remaining))
                    writeConfigState()


        # 
        # Handle Valves
        # 
        if self.Config["status"]["running"]:
            # If the valve is already open, this does nothing
            self.openAllValves()
        else:
            # The pump will eventually turn off when it reads /pump_control
            # Once the water is done flowing through the lines, the valves can be closed
            if self.WaterMeter.SavedGPM <= 0:
                # If the valve is already closed, this does nothing
                if self.Arduino.getOpenValves():
                    self.closeAllValves()
                    writeConfigState()
            else:
                self.StatusMessage = "OFF"


        # 
        # Send Data to Influx
        # 
        self.Influx.sendMeasurement("water_gallons", {}, self.WaterMeter.GPM)
        self.Influx.sendMeasurement("remaining_time", {}, self.Config["status"]["remaining"])
        self.Influx.sendMeasurement("remaining_percent", {}, self.getPercentComplete())

        open_valves = self.Arduino.getOpenValves()
        for valve in self.Valves:
            self.Influx.sendMeasurement("open_valve", {"valve":str(valve.Number)}, 1.0 if valve.Number in open_valves else 0.0)

        self.Influx.sendMeasurement("program_running", {}, self.isRunning())


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
    influx = influxwrapper.InfluxWrapper(log, influx_config, config['site'])

    log.info("%s - Initializing Arduino"%(datetime.datetime.now()))
    arduino = arduinoInterface.Arduino(log)

    log.info("%s - Setting up valve objects"%(datetime.datetime.now()))
    valves = []
    for number in range(1, config["valves"]+1):
        valves.append(Valve(int(number), log, influx, arduino))

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
        controller.run()
        job = scheduler.add_job(controller.run, 'interval', minutes=LOOP_DELAY)
        scheduler.start()

        IS_PROD = os.environ.get("PRODUCTION", False)
        app.run(debug=not IS_PROD, host="0.0.0.0", threaded=True)
    except Exception as e:
        log.error("Main loop failed: %s"%(e), exc_info=1)
        return 1
    return 1


if __name__ == "__main__":
    sys.exit(main())
