#!/usr/bin/python3
# -*- coding: utf-8 -*-
#********************************************************************
# ZYNTHIAN PROJECT: Zynmixer Python Wrapper
#
# A Python wrapper for zynmixer library
#
# Copyright (C) 2019-2022 Brian Walton <riban@zynthian.org>
#
#********************************************************************
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 2 of
# the License, or any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# For a full copy of the GNU General Public License see the LICENSE.txt file.
#
#********************************************************************

import ctypes
import logging

from zyngine import zynthian_controller
from zyngine import zynthian_engine
from zyngui import zynthian_gui_config

#-------------------------------------------------------------------------------
# Zynmixer Library Wrapper
#-------------------------------------------------------------------------------

class zynmixer(zynthian_engine):
	
	#	Function to initialize library
	def __init__(self):
		super().__init__()
		try:
			self.lib_zynmixer = ctypes.cdll.LoadLibrary('/zynthian/zynthian-ui/zynlibs/zynmixer/build/libzynmixer.so')
			self.lib_zynmixer.init()
			self.lib_zynmixer.getLevel.restype = ctypes.c_float
			self.lib_zynmixer.getBalance.restype = ctypes.c_float
			self.lib_zynmixer.getDpm.restype = ctypes.c_float
			self.lib_zynmixer.getDpmHold.restype = ctypes.c_float
		except Exception as e:
			self.lib_zynmixer = None
			print('Cannot init zynmixer library: %s' % str(e))

		self.zctrls = []
		self.learned_cc = [dict() for x in range(16)]
		for i in range(self.get_max_channels() + 1):
			strip_dict = {
				'level': zynthian_controller(self, 'level', None, {'midi_chan':i,'is_integer':False,'value_max':1.0,'value_default':0.8,'value':self.get_level(i),'graph_path':'level_{}'.format(i)}),
				'balance': zynthian_controller(self, 'balance', None, {'midi_chan':i,'is_integer':False,'value_min':-1.0,'value_max':1.0,'value_default':0.0,'value':self.get_balance(i),'graph_path':'balance_{}'.format(i)}),
				'mute': zynthian_controller(self, 'mute', None, {'midi_chan':i,'is_toggle':True,'value_max':1,'value_default':0,'value':self.get_mute(i),'graph_path':'mute_{}'.format(i)}),
				'solo': zynthian_controller(self, 'solo', None, {'midi_chan':i,'is_toggle':True,'value_max':1,'value_default':0,'value':self.get_solo(i),'graph_path':'solo_{}'.format(i)}),
				'mono': zynthian_controller(self, 'mono', None, {'midi_chan':i,'is_toggle':True,'value_max':1,'value_default':0,'value':self.get_mono(i),'graph_path':'mono_{}'.format(i)}),
				'phase': zynthian_controller(self, 'phase', None, {'midi_chan':i,'is_toggle':True,'value_max':1,'value_default':0,'value':self.get_phase(i),'graph_path':'phase_{}'.format(i)})
			}
			self.zctrls.append(strip_dict)

		self.midi_learn_zctrl = None
		self.midi_learn_cb = None
		self.MAX_NUM_CHANNELS = self.lib_zynmixer.getMaxChannels()


	def get_controllers_dict(self, processor):
		return self.zctrls[processor.midi_chan]


	def get_learned_cc(self, zctrl):
		for chan in range(16):
			for cc in self.learned_cc[chan]:
				if zctrl == self.learned_cc[chan][cc]:
					return [chan, cc]


	def send_controller_value(self, zctrl):
		try:
			getattr(self, 'set_{}'.format(zctrl.symbol))(zctrl.midi_chan, zctrl.value, False)
		except Exception as e:
			logging.warning(e)


	#	Destroy instance of shared library
	def destroy(self):
		if self.lib_zynmixer:
			self.lib_zynmixer.end()
			ctypes.dlclose(self.lib_zynmixer._handle)
		self.lib_zynmixer = None


	#	Get maximum quantity of channels (excluding main mix bus)
	#	returns: Maximum quantity of channels
	def get_max_channels(self):
		if self.lib_zynmixer:
			return self.lib_zynmixer.getMaxChannels()
		return 0


	#	Function to set fader level for a channel
	#	channel: Index of channel
	#	level: Fader value (0..+1)	
	#	level: Fader value (0..+1)
	#	level: Fader value (0..+1)	
	#	level: Fader value (0..+1)
	#	level: Fader value (0..+1)	
	def set_level(self, channel, level, update=True):
		if channel >= self.MAX_NUM_CHANNELS:
			channel = self.MAX_NUM_CHANNELS
		if self.lib_zynmixer:
			self.lib_zynmixer.setLevel(channel, ctypes.c_float(level))
		if update:
			self.zctrls[channel]['level'].set_value(level, False)
		self.send_update(channel, 'level', level)


	#	Function to set balance for a channel
	#	channel: Index of channel
	#	balance: Balance value (-1..+1)
	def set_balance(self, channel, balance, update=True):
		if channel >= self.MAX_NUM_CHANNELS:
			channel = self.MAX_NUM_CHANNELS
		if self.lib_zynmixer:
			self.lib_zynmixer.setBalance(channel, ctypes.c_float(balance))
		if update:
			self.zctrls[channel]['balance'].set_value(balance, False)
		self.send_update(channel, 'balance', balance)


	#	Function to get fader level for a channel
	#	channel: Index of channel
	#	returns: Fader level (0..+1)
	def get_level(self, channel):
		if self.lib_zynmixer:
			return self.lib_zynmixer.getLevel(channel)
		return 0


	#	Function to get balance for a channel
	#	channel: Index of channel
	#	returns: Balance value (-1..+1)
	def get_balance(self, channel):
		if self.lib_zynmixer:
			return self.lib_zynmixer.getBalance(channel)
		return 0


	#	Function to set mute for a channel
	#	channel: Index of channel
	#	mute: Mute state (True to mute)
	def	set_mute(self, channel, mute, update=False):
		if channel is not None and channel >= self.MAX_NUM_CHANNELS:
			channel = self.MAX_NUM_CHANNELS
		if self.lib_zynmixer:
			self.lib_zynmixer.setMute(channel, mute)
		if update:
			self.zctrls[channel]['mute'].set_value(mute, False)
		self.send_update(channel, 'mute', mute)


	#	Function to get mute for a channel
	#	channel: Index of channel
	#	returns: Mute state (True if muted)
	def	get_mute(self, channel):
		if self.lib_zynmixer:
			return self.lib_zynmixer.getMute(channel)
		else:
			return True


	#	Function to toggle mute of a channel
	#	channel: Index of channel
	def toggle_mute(self, channel):
		if channel >= self.MAX_NUM_CHANNELS:
			channel = self.MAX_NUM_CHANNELS
		if self.lib_zynmixer:
			self.lib_zynmixer.toggleMute(channel)


	#	Function to set phase reversal for a channel
	#	channel: Index of channel
	#	phase: Phase reversal state (True to reverse)
	def	set_phase(self, channel, phase, update=True):
		if channel >= self.MAX_NUM_CHANNELS:
			channel = self.MAX_NUM_CHANNELS
		if self.lib_zynmixer:
			self.lib_zynmixer.setPhase(channel, phase)
		if update:
			self.zctrls[channel]['phase'].set_value(phase, False)
		self.send_update(channel, 'phase', phase)


	#	Function to get phase reversal for a channel
	#	channel: Index of channel
	#	returns: Phase reversal state (True if phase reversed)
	def	get_phase(self, channel):
		if self.lib_zynmixer:
			return self.lib_zynmixer.getPhase(channel)
		else:
			return True


	#	Function to toggle phase reversal of a channel
	#	channel: Index of channel
	def toggle_phase(self, channel, update=True):
		if channel >= self.MAX_NUM_CHANNELS:
			channel = self.MAX_NUM_CHANNELS
		if self.lib_zynmixer:
			self.lib_zynmixer.togglePhase(channel)
		if update:
			self.zctrls[channel]['phase'].set_value(self.lib_zynmixer.getPhase(channel), False)


	#	Function to set solo for a channel
	#	channel: Index of channel
	#	solo: Solo state (True to solo)
	def	set_solo(self, channel, solo, update=True):
		if channel >= self.MAX_NUM_CHANNELS:
			channel = self.MAX_NUM_CHANNELS
		if self.lib_zynmixer:
			self.lib_zynmixer.setSolo(channel, solo)
		if update:
			self.zctrls[channel]['solo'].set_value(solo, False)
		self.send_update(channel, 'solo', solo)


	#	Function to get solo for a channel
	#	channel: Index of channel
	#	returns: Solo state (True if solo)
	def	get_solo(self, channel):
		if self.lib_zynmixer:
			return self.lib_zynmixer.getSolo(channel) == 1
		else:
			return True


	#	Function to toggle mute of a channel
	#	channel: Index of channel
	def toggle_solo(self, channel, update=True):
		if channel >= self.MAX_NUM_CHANNELS:
			channel = self.MAX_NUM_CHANNELS
		if self.lib_zynmixer:
			if self.get_solo(channel):
				self.set_solo(channel, False)
			else:
				self.set_solo(channel, True)
		if update:
			self.zctrls[channel]['solo'].set_value(self.lib_zynmixer.get_solo(channel), False)


	#	Function to mono a channel
	#	channel: Index of channel
	#	mono: Mono state (True to solo)
	def	set_mono(self, channel, mono, update=True):
		if channel >= self.MAX_NUM_CHANNELS:
			channel = self.MAX_NUM_CHANNELS
		if self.lib_zynmixer:
			self.lib_zynmixer.setMono(channel, mono)
		if update:
			self.zctrls[channel]['mono'].set_value(mono, False)
		self.send_update(channel, 'mono', mono)


	#	Function to get mono for a channel
	#	channel: Index of channel
	#	returns: Mono state (True if mono)
	def	get_mono(self, channel):
		if self.lib_zynmixer:
			return self.lib_zynmixer.getMono(channel) == 1
		else:
			return True


	#	Function to toggle mono of a channel
	#	channel: Index of channel
	def toggle_mono(self, channel, update=True):
		if channel >= self.MAX_NUM_CHANNELS:
			channel = self.MAX_NUM_CHANNELS
		if self.lib_zynmixer:
			if self.get_mono(channel):
				self.set_mono(channel, False)
			else:
				self.set_mono(channel, True)
		if update:
			self.zctrls[channel]['mono'].set_value(self.lib_zynmixer.getMono(channel), False)


	#	Function to check if channel has audio routed to its input
	#	channel: Index of channel
	#	returns: True if routed
	def is_channel_routed(self, channel):
		if self.lib_zynmixer:
			return (self.lib_zynmixer.isChannelRouted(channel) != 0)
		return False


	#	Function to get peak programme level for a channel
	#	channel: Index of channel
	#	leg: 0 for A-leg (left), 1 for B-leg (right)
	#	returns: Peak programme level
	def get_dpm(self, channel, leg):
		if self.lib_zynmixer:
			return self.lib_zynmixer.getDpm(channel, leg)
		return -200.0


	#	Function to get peak programme hold level for a channel
	#	channel: Index of channel
	#	leg: 0 for A-leg (left), 1 for B-leg (right)
	#	returns: Peak programme hold level
	def get_dpm_hold(self, channel, leg):
		if self.lib_zynmixer:
			return self.lib_zynmixer.getDpmHold(channel, leg)
		return -200.0


	#	Function to enable or disable digital peak meters
	#	chan: Mixer channel (256 for main mix bus)
	#	enable: True to enable
	def enable_dpm(self, chan, enable):
		if self.lib_zynmixer is None:
			return
		self.lib_zynmixer.enableDpm(chan, int(enable))


	#	Function to add OSC client registration
	#	client: IP address of OSC client
	def add_osc_client(self, client):
		if self.lib_zynmixer:
			return self.lib_zynmixer.addOscClient(ctypes.c_char_p(client.encode('utf-8')))


	#	Function to remove OSC client registration
	#	client: IP address of OSC client
	def remove_osc_client(self, client):
		if self.lib_zynmixer:
			self.lib_zynmixer.removeOscClient(ctypes.c_char_p(client.encode('utf-8')))


	#--------------------------------------------------------------------------
	# State management (for snapshots)
	#--------------------------------------------------------------------------

	def reset(self, channel):
		"""Reset mixer channel to default values"""

		if not isinstance(channel, int):
			return
		if channel >= self.MAX_NUM_CHANNELS:
			channel = self.MAX_NUM_CHANNELS
		self.zctrls[channel]['level'].reset_value()
		self.zctrls[channel]['balance'].reset_value()
		self.zctrls[channel]['mute'].reset_value()
		self.zctrls[channel]['mono'].reset_value()
		self.zctrls[channel]['solo'].reset_value()
		self.zctrls[channel]['phase'].reset_value()
			#for symbol in self.zctrls[channel]:
			#	self.zctrls[channel][symbol].midi_unlearn()


	# Reset mixer to default state
	def reset_state(self):
		for channel in range(self.get_max_channels() + 1):
			self.reset(channel)


	def get_state(self, full=True):
		"""Get mixer state as list of controller state dictionaries
		
		full : True to get state of all parameters or false for off-default values
		Returns : List of dictionaries describing parameter states
		"""
		state = {}
		for chan in range(self.get_max_channels() + 1):
			if chan < self.get_max_channels():
				key = 'chan_{:02d}'.format(chan)
			else:
				key = 'main'
			chan_state = {}
			for symbol in self.zctrls[chan]:
				zctrl = self.zctrls[chan][symbol]
				ctrl_state = zctrl.get_state(full)
				if ctrl_state:
					chan_state[zctrl.symbol] = ctrl_state
			if chan_state:
				state[key] = chan_state
		return state


	def set_state(self, state, full=True):
		"""Set mixer state
		
		state : List of mixer channels containing dictionary of each state value
		full : True to reset parameters omitted from state
		"""

		for chan, zctrls in enumerate(self.zctrls):
			if chan < self.get_max_channels():
				key = 'chan_{:02d}'.format(chan)
			else:
				key = 'main'
			for symbol, zctrl in zctrls.items():
				try:
					ctrl_state = state[key][symbol]
					if "value" in ctrl_state:
						zctrl.set_value(ctrl_state["value"], True)
					if "midi_learn_chan" in ctrl_state and "midi_learn_cc" in ctrl_state:
						self.set_midi_learn(zctrl, int(ctrl_state["midi_learn_chan"]), int(ctrl_state["midi_learn_cc"]))
				except:
					if full:
						zctrl.reset_value()


	def send_update(self, chan, ctrl, value):
		if self.processor_cb:
			self.processor_cb(chan, ctrl, value)
			
	#--------------------------------------------------------------------------
	# MIDI Learn
	#--------------------------------------------------------------------------

	def midi_control_change(self, chan, ccnum, val):
		if self.midi_learn_zctrl:
			self.learned_cc[chan][ccnum] = self.midi_learn_zctrl
			self.disable_midi_learn()
		elif zynthian_gui_config.midi_single_active_channel:
			for ch in range(16):
				try:
					self.learned_cc[ch][ccnum].midi_control_change(val)
				except:
					pass
		else:
			try:
				self.learned_cc[chan][ccnum].midi_control_change(val)
			except:
				pass


	def midi_unlearn_chan(self, chan):
		for zctrl in self.zctrls[chan].values:
			self.midi_unlearn(zctrl)


	def midi_unlearn(self, zctrl):
		for chan in self.learned_cc:
			for cc in self.learned_cc[chan]:
				if self.learned_cc[chan][cc] == zctrl:
					self.learned_cc[chan].pop(cc)


	def enable_midi_learn(self, zctrl):
		self.midi_learn_zctrl = zctrl


	def disable_midi_learn(self):
		self.midi_learn_zctrl = None
		if self.midi_learn_cb:
			self.midi_learn_cb()


	def set_midi_learn_cb(self, cb):
		self.midi_learn_cb = cb

#-------------------------------------------------------------------------------
