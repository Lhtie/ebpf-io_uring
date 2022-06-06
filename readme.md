# IO Performance Test

benchmark test for 4 basic IO method: Synchronized I/O, io_uring, posix async I/O, linux async I/O

## Requirements

### Generate read material

```sh
g++ gen.cpp -o gen && ./gen
```

`dict.txt`: randomly generated $128MB$ text

### Library installation

#### liburing

```sh
sudo dpkg -i packages/liburing2_2.1-2_amd64.deb
sudo dpkg -i packages/liburing-dev_2.1-2_amd64.deb
```

**for ubuntu 22.04 LTS**

```sh
sudo apt install liburing2 && sudo apt install liburing-dev
```

#### libaio

```sh
sudo apt install libaio-dev
```

#### supports for ebpf

* USDT

```sh
sudo apt-get install systemtap-sdt-dev
```

* bcc

```sh
sudo apt-get install bpfcc-tools linux-headers-$(uname -r)
```

### Runing

#### test IOPS

```sh
gcc testIOPS.c -o testIOPS -luring -laio -lrt -Wall -O2 -D_GNU_SOURCE
./testIOPS dict.txt -m=rRD
./testIOPS dict.txt -m=sRD
./testIOPS output.txt -m=rWR
./testIOPS output.txt -m=sWR
```

#### test latency

```sh
gcc testIOLatency.c -o testIOLatency -luring -laio -lrt -Wall -O2 -D_GNU_SOURCE
sudo python3 latency.py --engine sync --infile dict.txt --outfile output.txt
sudo python3 latency.py --engine io_uring --infile dict.txt --outfile output.txt
sudo python3 latency.py --engine posix_aio --infile dict.txt --outfile output.txt
sudo python3 latency.py --engine libaio --infile dict.txt --outfile output.txt
```

## Results

### IOPS

```sh
$ ./testIOPS dict.txt -m=rRD
sync: random read operates with bs 4 KB, IOPS: 5676 /seconds
io_uring: random read operates with bs 4 KB, IOPS: 25339 /seconds
posix_aio: random read operates with bs 4 KB, IOPS: 6887 /seconds
linux libaio: random read operates with bs 4 KB, IOPS: 24391 /seconds
```

```sh
$ ./testIOPS dict.txt -m=sRD
sync: sequential read operates with bs 4 KB, IOPS: 9230 /seconds
io_uring: sequential read operates with bs 4 KB, IOPS: 176313 /seconds
posix_aio: sequential read operates with bs 4 KB, IOPS: 5456 /seconds
linux libaio: sequential read operates with bs 4 KB, IOPS: 143955 /seconds
```

```sh
$ ./testIOPS output.txt -m=rWR
sync: random write operates with bs 4 KB, IOPS: 6432 /seconds
io_uring: random write operates with bs 4 KB, IOPS: 21622 /seconds
posix_aio: random write operates with bs 4 KB, IOPS: 5154 /seconds
linux libaio: random write operates with bs 4 KB, IOPS: 20240 /seconds
```

```sh
$ ./testIOPS output.txt -m=sWR
sync: sequential writeoperates with bs 4 KB, IOPS: 5869 /seconds
io_uring: sequential writeoperates with bs 4 KB, IOPS: 12838 /seconds
posix_aio: sequential writeoperates with bs 4 KB, IOPS: 4614 /seconds
linux libaio: sequential writeoperates with bs 4 KB, IOPS: 18703 /seconds
```

IOPS: number of $4KB$ data I/O per second

$64$ entries for each aio queue

![fig](\visualize\fig.jpg)

In conclusion, io_uring generally outperforms other IO methods in all types of IO tasks. 

linux aio method has similar throughput as io_uring, but it can only handle normal files stored in disk, and it is supposed to operate files open in O_DIRECT.

posix_aio method may have smaller throughput than traditional synchronized I/O method due to its massive number of context switch.

### IO Latency

```sh
$ sudo python3 latency.py --engine sync --infile dict.txt --outfile output.txt
...
latency for sync IO read: 0.1365 ms
latency for sync IO write: 0.1764 ms
```

```sh
$ sudo python3 latency.py --engine io_uring --infile dict.txt --outfile output.txt
...
latency for io_uring IO read: 2.9033 ms
latency for io_uring IO write: 6.8054 ms
```

```sh
$ sudo python3 latency.py --engine posix_aio --infile dict.txt --outfile output.txt
...
latency for posix_aio IO read: 6.1566 ms
latency for posix_aio IO write: 7.0417 ms
```

```sh
$ sudo python3 latency.py --engine libaio --infile dict.txt --outfile output.txt
...
latency for libaio IO read: 11.6784 ms
latency for libaio IO write: 11.676 ms
```

![fig2](visualize\fig2.jpg)

Average latency for totally $64$ reads and  $64$ writes

Comparing each aio methods, io_uring method has smaller latency.
