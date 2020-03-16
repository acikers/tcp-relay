#!/bin/env python
import sys
import numpy as np
from csv import DictReader as dr
from matplotlib import pyplot as plt
from matplotlib import mlab


if len(sys.argv) != 3 :
    print('Usage: ' + sys.argv[0] + ' input.csv output.pdf')
    sys.exit()

x = []
with open(sys.argv[1]) as csvfile:
    reader = dr(csvfile, fieldnames=['start_sec', 'start_nsec', 'finish_sec', 'finish_nsec', 'delta'])
    for row in reader:
        if row['delta'] != '0' or row['start_sec'] != 0 and row['start_nsec'] != 0:
            x.append(row['delta'])

nb = 50
x = np.asarray(x[100:len(x)-1], dtype='int')
#x = np.asarray(x, dtype='int')

p = np.array([50.0, 90.0, 99.0, 100.0])
perc = np.percentile(x, q=p)

plt.figure(figsize=(15,8))
n, bins, patches = plt.hist(x, nb, histtype='bar', facecolor='blue', alpha=0.6)
plt.locator_params(axis='x', nbins=nb)
plt.xlim(left=0.0)
plt.xticks(rotation=90)
plt.xlabel('Наносекунды')
plt.ylabel('# сообщений')
plt.vlines(perc[0], ymin=0, ymax=max(n), linestyles='dashed', color='green', label=perc[0])
plt.vlines(perc[1], ymin=0, ymax=max(n), linestyles='dashed', color='yellow', label=perc[1])
plt.vlines(perc[2], ymin=0, ymax=max(n), linestyles='dashed', color='orange', label=perc[2])
plt.vlines(perc[3], ymin=0, ymax=max(n), linestyles='dashed', color='red', label=perc[3])
plt.legend()
plt.savefig(sys.argv[2])
print('perc[\'99\']: ' + str(perc[2]))
