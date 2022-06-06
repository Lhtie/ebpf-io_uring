#include <uapi/linux/ptrace.h>

BPF_HASH(read_req);
BPF_HASH(read_rec);
BPF_HASH(write_req);
BPF_HASH(write_rec);

int read_request(struct pt_regs *ctx) {
    u64 t = bpf_ktime_get_ns(), one = 1, key = 0;
    u64 *val = read_req.lookup(&key);
    if (val == NULL){
        read_req.update(&key, &t);
    } else  {
        *val += t;
    }
    key = 1;
    val = read_req.lookup(&key);
    if (val == NULL){
        read_req.update(&key, &one);
    } else {
        *val += 1;
    }
    return 0;
}

int read_receive(struct pt_regs *ctx){
    u64 t = bpf_ktime_get_ns(), one = 1, key = 0;
    u64 *val = read_rec.lookup(&key);
    if (val == NULL){
        read_rec.update(&key, &t);
    } else  {
        *val += t;
    }
    key = 1;
    val = read_rec.lookup(&key);
    if (val == NULL){
        read_rec.update(&key, &one);
    } else {
        *val += 1;
    }
    return 0;
}

int write_request(struct pt_regs *ctx) {
    u64 t = bpf_ktime_get_ns(), one = 1, key = 0;
    u64 *val = write_req.lookup(&key);
    if (val == NULL){
        write_req.update(&key, &t);
    } else  {
        *val += t;
    }
    key = 1;
    val = write_req.lookup(&key);
    if (val == NULL){
        write_req.update(&key, &one);
    } else {
        *val += 1;
    }
    return 0;
}

int write_receive(struct pt_regs *ctx){
    u64 t = bpf_ktime_get_ns(), one = 1, key = 0;
    u64 *val = write_rec.lookup(&key);
    if (val == NULL){
        write_rec.update(&key, &t);
    } else  {
        *val += t;
    }
    key = 1;
    val = write_rec.lookup(&key);
    if (val == NULL){
        write_rec.update(&key, &one);
    } else {
        *val += 1;
    }
    return 0;
}
