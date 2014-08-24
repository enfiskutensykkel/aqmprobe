#!/usr/bin/env python

import matplotlib.pyplot as plt
import numpy as np

prev_lost = {}
succ_loss = {}
curr_loss = {}

total_sent = {}
total_lost = {}

for line in open("stuff2.txt").readlines():
	s = line.split(",")
	tstamp = int(s[0])
	port = int(s[-1])
	#conn = s[2]
	conn = port
	dropped = int(s[-2])
	#qlen = int(s[1])

	if not conn in prev_lost:
		prev_lost[conn] = False
		succ_loss[conn] = []
		curr_loss[conn] = 0
		total_sent[conn] = 0
		total_lost[conn] = 0

	total_sent[conn] += 1
	total_lost[conn] += dropped == 1

	if dropped == 1:
		prev_lost[conn] = True
		curr_loss[conn] += 1
	else:
		prev_lost[conn] = False
		if curr_loss[conn] > 0:
			succ_loss[conn].append(curr_loss[conn])
		curr_loss[conn] = 0

for conn, lost in curr_loss.iteritems():
	if lost > 0:
		succ_loss[conn].append(lost)

for conn in sorted(succ_loss.keys()):
	ratio = sum(filter(lambda x: x > 1, succ_loss[conn])) / float(total_lost[conn])
	print conn, "%.3f" % ratio
