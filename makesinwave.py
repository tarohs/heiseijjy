import sys
import matplotlib.pyplot as plt
import math

if len(sys.argv) != 3:
  print('usage: makesinwave.py SAMPLEFREQ WAVEFREQ')
  sys.exit(1)
sfreq = int(sys.argv[1])
wfreq = int(sys.argv[2])
print("sampling freq {}, wave freq {}".format(sfreq, wfreq));
x = []
y = []
n = int(sfreq / wfreq * 10 + .5)
for i in range(n):
  x = x + [i]
  yy = math.sin(2 * math.pi * i * wfreq / sfreq)
  y = y + [yy]
  iy = int(yy * 127 + 128 + .5)
  print('0x{:02x}, '.format(iy), end = '')
  if i % 8 == 7:
    print()

plt.plot(x, y)
plt.show()
