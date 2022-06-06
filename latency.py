from bcc import BPF, USDT
from time import sleep
import argparse
import os

def main(args):
    engine = args.engine
    infile = args.infile
    outfile = args.outfile

    usdt = USDT(path = "./testIOLatency")

    usdt.enable_probe(probe = "read_request", fn_name = "read_request")
    usdt.enable_probe(probe = "read_receive", fn_name = "read_receive")
    usdt.enable_probe(probe = "write_request", fn_name = "write_request")
    usdt.enable_probe(probe = "write_receive", fn_name = "write_receive")

    bpf = BPF(src_file = "usdt_trace.c", usdt_contexts = [usdt])
    os.system("./testIOLatency {} {} {}".format(engine, infile, outfile))

    delta, cnt = 0, 0
    for k, v in bpf["read_req"].items():
        if k.value == 0: 
            delta -= v.value
        else: cnt = v.value
    for k, v in bpf["read_rec"].items():
        if k.value == 0:
            delta += v.value
        else:
            if cnt != v.value:
                print("{} read failed".format(engine))
                exit()
    print("latency for {} IO read: {} ms".format(engine, round(delta / cnt / 1e6, 4)))

    delta, cnt = 0, 0
    for k, v in bpf["write_req"].items():
        if k.value == 0: 
            delta -= v.value
        else: cnt = v.value
    for k, v in bpf["write_rec"].items():
        if k.value == 0:
            delta += v.value
        else:
            if cnt != v.value:
                print("{} write failed".format(engine))
                exit()
    print("latency for {} IO write: {} ms".format(engine, round(delta / cnt / 1e6, 4)))

# sudo python3 latency.py --engine sync | io_uring | posix_aio | libaio --infile dict.txt --outfile output.txt

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", type=str)
    parser.add_argument("--infile", type=str)
    parser.add_argument("--outfile", type=str)
    args = parser.parse_args()
    main(args)