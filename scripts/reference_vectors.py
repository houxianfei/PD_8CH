"""Generate golden protocol vectors using the actual pinned afs_v1 RTL functions.

Only regeneration needs the reference checkout; ordinary tests use the committed .vh.
"""
import argparse
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--reference', default='E:/houxf/afs_fireware')
parser.add_argument('--icarus-bin', default='.tools/iverilog/bin')
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
build = root / 'sim/build'
build.mkdir(exist_ok=True)
functions = []
for name in ('USB_axi4lite.v', 'packet.v'):
    source = subprocess.check_output(
        ['git', '-C', args.reference, 'show', f'ae1a285:rtl/{name}']).decode('utf-8')
    functions.append(re.search(r'function \[7:0\] calculate_crc8;.*?endfunction', source, re.S).group())
assert re.sub(r'//[^\n]*|\s+', '', functions[0]) == re.sub(r'//[^\n]*|\s+', '', functions[1])
commands = [('WRITE_INTERVAL', 1, 0xF0, 1000), ('WRITE_START', 1, 0xF1, 0x301), ('WRITE_NUMBER', 1, 0xF2, 1),
            ('READ_CH7', 2, 7, 0), ('READ_INTERVAL', 2, 0xF0, 0)]
body = []
for name, op, addr, data in commands:
    value = (data << 24) | (addr << 16) | (op << 8) | 0xAA
    body.append(f'$display("localparam [63:0] REF_{name} = 64\'h%016h;", '
                f'{{calculate_crc8(0,64\'h{value:016x}),56\'h{value:014x}}});')
for mode in (1, 2, 3):
    header = (0xAA << 56) | (mode << 48)
    body.append(f'c=calculate_crc8(0,64\'h{header:016x});')
    if mode & 1:
        body.append("c=calculate_crc8(c,64'h0000111122223333);")
    if mode & 2:
        body.append("c=calculate_crc8(c,64'h4444555566667777);")
    body.append(f'$display("localparam [63:0] REF_HEADER_{mode} = 64\'h%016h;", '
                f"64'h{header:016x} | ({{56'd0,c}} << 8));")
oracle = build/'reference_oracle.v'
oracle.write_text('module reference_oracle;\n' + functions[0] + '\nreg [7:0] c;\ninitial begin\n' +
                  '\n'.join(body) + '\n$finish; end\nendmodule\n')
bin_dir = Path(args.icarus_bin).resolve()
exe = build/'reference_oracle.vvp'
subprocess.run([str(bin_dir/'iverilog.exe'), '-s', 'reference_oracle', '-o', str(exe), str(oracle)], check=True)
output = subprocess.check_output([str(bin_dir/'vvp.exe'), str(exe)], text=True)
vectors = '\n'.join(line for line in output.splitlines() if line.startswith('localparam'))
(root/'sim/reference_vectors.vh').write_text('// Generated from afs_v1 ae1a285 by scripts/reference_vectors.py.\n' + vectors + '\n')
print(vectors)
