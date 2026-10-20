# usage: rfcmd.py <port> <seconds> <outfile> <command...>
import serial, sys, time
port, secs, out, cmds = sys.argv[1], float(sys.argv[2]), sys.argv[3], sys.argv[4:]
for _ in range(40):
    try:
        s = serial.Serial(port, 115200, timeout=0.3); break
    except Exception:
        time.sleep(0.5)
time.sleep(0.5)
s.reset_input_buffer()
for c in cmds:
    s.write((c + "\n").encode()); time.sleep(0.2)
end = time.time() + secs
with open(out, 'ab') as f:
    while time.time() < end:
        d = s.read(4096)
        if d:
            f.write(d); f.flush()
s.write(b"x\n")
