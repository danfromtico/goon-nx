#!/usr/bin/env python3
"""Sample a running xemu (gdbstub on :1234): EIP plus code addresses found on
the stack, written as counts to samples.txt every 10 s.
    python3 sampler.py SECONDS"""
import socket, struct, sys, time, collections

CODE_LO, CODE_HI = 0x00011000, 0x00239620       # DOAX .text .. XPP end

class RSP:
    def __init__(self, host='127.0.0.1', port=1234):
        self.s = socket.create_connection((host, port)); self.buf = b''
        self.s.settimeout(5)
    def _recv_packet(self):
        while True:
            while b'$' not in self.buf:
                self.buf += self.s.recv(65536)
            i = self.buf.index(b'$')
            j = self.buf.find(b'#', i)
            if j < 0 or len(self.buf) < j + 3:
                self.buf += self.s.recv(65536); continue
            data = self.buf[i+1:j]; self.buf = self.buf[j+3:]
            self.s.sendall(b'+')
            return data.decode('latin1')
    def cmd(self, c, expect_reply=True):
        pkt = c.encode('latin1')
        self.s.sendall(b'$' + pkt + b'#%02x' % (sum(pkt) & 0xff))
        if not expect_reply:
            return None
        while True:
            r = self._recv_packet()
            # a stop notification that arrived late is not the reply
            if c not in ('?',) and r[:1] in ('T', 'S', 'O') and not r.startswith('E'):
                continue
            return r
    def interrupt(self):
        self.s.sendall(b'\x03'); return self._recv_packet()
    def cont(self):
        pkt = b'c'; self.s.sendall(b'$c#%02x' % (sum(pkt) & 0xff))
        # wait for '+' ack only
        while b'+' not in self.buf:
            self.buf += self.s.recv(65536)
        self.buf = self.buf[self.buf.index(b'+')+1:]

def main():
    secs = float(sys.argv[1]) if len(sys.argv) > 1 else 300
    out = sys.argv[2] if len(sys.argv) > 2 else 'samples.txt'
    r = RSP()
    r.cmd('?')                          # the stub stops the VM on connect
    eips = collections.Counter(); stack = collections.Counter()
    end = time.time() + secs; last = time.time(); n = 0
    try:
        while time.time() < end:
            r.cont(); time.sleep(0.01)
            r.interrupt()
            g = r.cmd('g')
            regs = struct.unpack('<16I', bytes.fromhex(g[:128]))
            esp, eip = regs[4], regs[8]
            eips[eip] += 1; n += 1
            m = r.cmd('m%x,%x' % (esp, 0x400))
            if m and not m.startswith('E'):
                b = bytes.fromhex(m)
                for k in range(0, len(b) - 3, 4):
                    v = struct.unpack_from('<I', b, k)[0]
                    if CODE_LO <= v < CODE_HI:
                        stack[v] += 1
            if time.time() - last > 10:
                last = time.time()
                with open(out, 'w') as f:
                    f.write('# samples %d\n' % n)
                    for a, c in eips.most_common(): f.write('E %08X %d\n' % (a, c))
                    for a, c in stack.most_common(): f.write('S %08X %d\n' % (a, c))
                print('samples', n, 'distinct eip', len(eips), flush=True)
    finally:
        try: r.cont()
        except Exception: pass
        with open(out, 'w') as f:
            f.write('# samples %d\n' % n)
            for a, c in eips.most_common(): f.write('E %08X %d\n' % (a, c))
            for a, c in stack.most_common(): f.write('S %08X %d\n' % (a, c))

main()
