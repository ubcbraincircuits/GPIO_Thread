#! /usr/bin/python
#-*-coding: utf-8 -*-

import ptCountermandPulse
import PTSimpleGPIO

class CountermandPulse (PTSimpleGPIO.Pulse):

    def __init__(self, gpio_pin, polarity, delay, duration, accuracy_level):
        self.task_ptr = ptCountermandPulse.newDelayDur(gpio_pin, polarity, delay, duration, accuracy_level)
        self.respectTheGIL = False

    def do_pulse_countermandable (self):
        return ptCountermandPulse.doCountermandPulse(self.task_ptr)
    
    def countermand_pulse (self):
        return ptCountermandPulse.countermand(self.task_ptr)

    def was_countermanded (self):
        return ptCountermandPulse.wasCountermanded(self.task_ptr)
