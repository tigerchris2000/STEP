import matplotlib.pyplot as plt
import sys
import time

if len(sys.argv) != 3:
    print("Input should be:")
    print("[location of usb device] [probe to call]")
    SystemExit()


file_location = sys.argv[1] + "probe" + sys.argv[2]
file = open(file_location, "r")

time = 0
times = []
temps = []
while True:
    line = file.readline()
    value = float(line)
    time.append(time)
    temps.append(value)
    plt.plot(times, temps)
    time.sleep(0.5)
    time = time + 0.5
