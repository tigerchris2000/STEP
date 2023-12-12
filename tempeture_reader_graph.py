import sys
import matplotlib.pyplot as plt

xs = []
ys = []
with open(sys.argv[1], "r") as f:
    lines = f.readlines()
    for l in lines:
        x = l.split(" ")[0]
        y = l.split(" ")[1]
        xs.append(x)
        ys.append(y)

plt.plot(xs, ys)
plt.show()
