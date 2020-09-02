#!/usr/bin/python3

import sys
import csv
import numpy as np
import matplotlib
import matplotlib.pyplot as plt

datafile = 'lebench-results.csv'
if len(sys.argv) > 1:
    datafile = sys.argv[1]
outfile = 'lebench-results.pdf'
if len(sys.argv) > 2:
    outfile = sys.argv[2]

data = []
benchmarks = []
with open(datafile) as csvfile:
    d = {}
    for row in csv.reader(csvfile):
        if row[0].startswith('Benchmark'): continue
        if row[0].startswith('ref'): continue
        d[row[0]] = [int(x.strip()) for x in row[1:4]]
        if row[0] not in benchmarks: benchmarks.append(row[0])
    data = d

ind = np.arange(len(benchmarks))
width = 0.35
matplotlib.rc('font', size=10, family='sans-serif')

fig, ax = plt.subplots(figsize=(8, 3.5))
ward_traditional_mitigation_cost = [data[bench][1] / data[bench][0] for bench in benchmarks]
ward_fast_mitigation_cost = [data[bench][2] / data[bench][0] for bench in benchmarks]
rects1 = ax.bar(ind - width/2, ward_traditional_mitigation_cost, width, label='Ward+Mitigations')
rects2 = ax.bar(ind + width/2, ward_fast_mitigation_cost, width, label='Ward')

ax.set_ylabel('Relative runtime')
ax.set_title('')
ax.set_xticks(ind)
ax.set_xticklabels(benchmarks, rotation='vertical')
ax.legend()
ax.plot([-1, len(benchmarks)], [1, 1], color='black', linestyle='--', linewidth=1)
fig.savefig(outfile, bbox_inches='tight')
