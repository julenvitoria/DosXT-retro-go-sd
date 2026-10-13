# Make XTulator's timing.c use a virtual clock (advanced by the main loop from the instruction count).
import sys, re
p = sys.argv[1]
s = open(p).read()
old = "gettimeofday(&tv, NULL);\n\ttiming_cur = (uint64_t)tv.tv_sec * 1000000 + (uint64_t)tv.tv_usec;"
n = s.count(old)
assert n == 3, n
s = s.replace(old, "timing_cur = timing_now_us();")
hook = 'uint64_t timing_cur;\n'
assert hook in s
s = s.replace(hook, '''uint64_t timing_virtual_us = 0;
int timing_virtual_enable = 0;
static uint64_t timing_now_us(void) {
	if (timing_virtual_enable) return timing_virtual_us;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (uint64_t)tv.tv_sec * 1000000 + (uint64_t)tv.tv_usec;
}
''' + hook, 1)
open(p, "w").write(s)
print("patched", n, "time sources")
