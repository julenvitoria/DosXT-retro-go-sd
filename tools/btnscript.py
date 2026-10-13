# Build a button script for the host harness (platform_host.c format "frame=HEXMASK,...").
# usage: btnscript.py <start_frame> NAME [NAME ...]   each NAME is tapped: 4 frames down, 4 up.
# NAME: up down left right a b game time x y   (or "wait:N" to skip N frames)
import sys
bits = dict(up=1, down=2, left=4, right=8, a=16, b=32, game=64, time=128, x=256, y=512)
f = int(sys.argv[1]); out = []
for n in sys.argv[2:]:
    if n.startswith("wait:"):
        f += int(n[5:]); continue
    out.append("%d=%x" % (f, bits[n])); out.append("%d=0" % (f + 4)); f += 8
print(",".join(out))
print(f, file=sys.stderr)
