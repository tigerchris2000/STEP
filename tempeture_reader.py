import sys
import time

if len(sys.argv) != 4:
    print("Input should be:")
    print("[location of usb device] [probe to call] [duration in s]")
    SystemExit()


file_location = sys.argv[1] + "probe" + sys.argv[2]
data = open("data.txt", "w")

tracker = 0

for i in range(int(sys.argv[3]) * 2):
    file = open(file_location, "r")
    line = file.readline()
    time.sleep(0.5)
    tracker = tracker + 0.5
    data.write(str(tracker) + " " + line)
    file.close()

data.close()
