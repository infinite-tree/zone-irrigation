'''
Arduino interface
'''

import glob
import os
import serial
import subprocess
import time

PRODUCTION = os.getenv("PRODUCTION")


class FakeSerial(object):
    # FIXME: implement and connect to Arduino class
    def __init__(self, log, *args, **kwargs):
        self.Log = log
        self.Value = ''
        self.Map = {
            'I': "I",
            'W': "30.0",
            'V': "",
            '1': '1',
            '2': '2',
            '3': '3'
        }
        self.Numbers = "123456789"
    
    def close(self):
        return
    
    def write(self, value):
        self.Value = value.decode().strip()
        self.Log.debug("SERIAL: sent: '%s'"%self.Value)
        if self.Value in self.Numbers:
            if self.Value in self.Map['V']:
                self.Map['V'] = self.Map['V'].replace(self.Value, "")
            else:
                self.Map['V'] = self.Map['V'] + self.Value
        return
    
    def readline(self):
        resp = self.Map.get(self.Value, 'E')
        self.Log.debug("SERIAL: response: '%s'"%resp)
        return resp.encode()


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

        if PRODUCTION:
            serial_devices = glob.glob("/dev/ttyUSB*")
            if len(serial_devices) < 1:
                self.Log.error("No Serial devices detected. Restarting ...")
                subprocess.call("sudo reboot", shell=True)

            self.SerialDevice = sorted(serial_devices)[-1]
            self.Stream = serial.Serial(self.SerialDevice, 57600, timeout=1)
        else:
            self.Stream = FakeSerial(self.Log)

        if self._sendData('I') == 'I':
            return
        # still not reset
        self.Log.error("Failed to reset Serial!!!")

    def resetSerial(self):
        try:
            self.Stream.close()
        except:
            pass

        if PRODUCTION:
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
        ret = self._sendData("W")
        try:
            # int wont handle decimals in the string, but float handles with or without
            return int(float(ret))
        except Exception:
            self.Log.error("Int(%s) conversion failed for Arduino.getWaterCounter()"%ret)
            return 0

    def toggleValve(self, valve):
        if self._sendData(str(valve)) == str(valve):
            return True
        return False
    
    def openValve(self, valve):
        open_valves = self.getOpenValves()
        if not valve in open_valves:
            self.toggleValve(valve)
            return True
        return False
    
    def closeValve(self, valve):
        open_valves = self.getOpenValves()
        if valve in open_valves:
            self.toggleValve(valve)
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

