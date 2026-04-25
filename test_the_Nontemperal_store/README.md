# Simple NT Store Test

This folder provides a minimal benchmark for:
- pure C normal write
- non-temporal write via `_mm_stream_si128`

## Build

```bash
make
```

## Run

```bash
# default: 512 MiB, 30 iterations
./nt_store_bench

# custom: bytes iterations
./nt_store_bench 536870912 30
```

## Output

The program prints median/mean/min/max time and effective bandwidth for both modes,
then prints NT vs normal delta by median.

## Notes

- Test only on x86/x86_64.
- Data size should be larger than LLC for memory-write behavior.
- `NT stream write` uses `_mm_stream_si128` and `_mm_sfence`.
