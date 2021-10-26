import random
import sys

# Generate random inputs of equal length.

for idx in range(1, 5):
	with open(f"{idx}", "w") as f:
		for i in range(200000):
			f.write(random.choice("ACGT-"))
