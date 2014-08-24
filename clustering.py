#!/usr/bin/env python

import matplotlib.pyplot as plt
import numpy as np
from sys import stdin

prev_lost = {}
succ_loss = {}
curr_loss = {}

conn_loss = {}

total_sent = {}
total_lost = {}

for line in stdin.readlines():
	s = line.split(",")
	tstamp = int(s[0])
	group = int(s[-1])
	conn = s[2].strip()
	dropped = int(s[-2])
	#qlen = int(s[1])

	if not group in prev_lost:
		prev_lost[group] = False
		succ_loss[group] = []
		total_sent[group] = 0
		total_lost[group] = 0
		curr_loss[group] = 0

		conn_loss[group] = {}

	total_sent[group] += 1
	total_lost[group] += dropped == 1

	if dropped == 1:
		prev_lost[group] = True
		curr_loss[group] += 1

		if not conn in conn_loss[group]:
			conn_loss[group][conn] = 0
		conn_loss[group][conn] += 1
	else:
		prev_lost[group] = False
		if curr_loss[group] > 0:
			succ_loss[group].append((curr_loss[group], conn_loss[group]))
		curr_loss[group] = 0
		conn_loss[group] = {}

for group, lost in curr_loss.iteritems():
	if lost > 0:
		succ_loss[group].append(lost)


print "%5s %7s %7s %7s %7s %7s %7s" % ("port", "sent", "lost", "rate", "succ", "inter", "intra")
for num, group in enumerate(sorted(succ_loss.keys())):

	counts = {}
	interstream = 0
	intrastream = 0
	s = 0
	for sample, conns in succ_loss[group]:
		if sample > 1:
			s += sample

			intra = len([d for d in conns.itervalues() if d > 1]) > 0
			inter = len(conns) > 1

			intrastream += sample * intra
			interstream += sample * inter

		if not sample in counts:
			counts[sample] = 0
		counts[sample] += 1

	ratio = s / float(total_lost[group])
	print "%5d %7d %7d %7s %7s %7d %7d" % (group, total_sent[group], total_lost[group],
			"%3.3f" % (total_lost[group] / float(total_sent[group]) * 100),
			"%3.3f" % (ratio * 100),
			interstream, intrastream)

	for number, count in counts.iteritems():
		print "\t%3d %7d %7s" % (number, count, "%3.3f" % ((count * number * 100)/float(total_lost[group])))
	print "\tsum %7d" % sum([k * v for k, v in counts.iteritems()])
	print ""
