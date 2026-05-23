# Important Experiment Results

## 2026-05-19: GiY RSet strategy decision

Goal: reduce confounding factors and make current GiY use the senior-style remembered set design.

Decision:
- Current GiY should use read-slot RSet by default.
- RSet stores old/init slot addresses only.
- During minor GC, GiY reads the current value from that old/init slot, reserves the young object if needed, and patches the slot immediately.
- GiY no longer keeps `remembered_set.values[]` in the default path.

Runtime full-suite result used for this decision:

| Scope | Saved-value GiY total | Read-slot GiY total | Total change | Saved-value GC | Read-slot GC | GC change |
|---|---:|---:|---:|---:|---:|---:|
| 13 benchmarks | 6730.506 s | 6646.009 s | read-slot +1.255% faster | 126.084 s | 147.527 s | read-slot 17.007% slower |
| Excluding Havlak | 5846.082 s | 5837.980 s | read-slot +0.139% faster | 84.055 s | 88.109 s | read-slot 4.823% slower |

Cache-miss full-suite result:

| Scope | Saved-value miss rate | Read-slot miss rate | Delta | Saved-value MPKI | Read-slot MPKI | MPKI change |
|---|---:|---:|---:|---:|---:|---:|
| 13 benchmarks | 9.937% | 11.400% | +1.463 pp | 0.0219 | 0.0253 | +15.654% |
| Excluding Havlak | 4.910% | 5.708% | +0.798 pp | 0.00763 | 0.00837 | +9.655% |

Interpretation:
- Read-slot RSet is simpler and closer to the senior implementation.
- It does not improve GC time; GC becomes slower because old slots must be read again during GC.
- It does not improve whole-program generic cache miss rate.
- The total runtime improvement is small after excluding Havlak, so the main reason to use it now is experimental control, not performance.

Implementation status:
- `GIY_RSET_READ_SLOT_AT_GC` now defaults to `1`.
- `GIY_RSET_READ_SLOT_AT_GC=false` is kept as the old saved-value comparison path.
- Default RSet local workspace after the change:
  - `buffer`: 128 KB
  - `values`: 0 KB
  - `hash`: 64 KB
  - total RSet workspace: 192 KB
- No `remembered_set_hash_indices` table is allocated in default read-slot mode.

Verification:
- Built default GiY with `make OPT_GC=giy CACHE_SIZE_KB=896 -B -j2`.
- Ran `./ejsvm a.sbc`: status 0.
- Ran `GIY_MPROTECT_OLD=1 ./ejsvm a.sbc`: status 0.
- Runtime initialization confirmed: `giy rset mode=read_slot buffer=128KB values=0KB hash=64KB`.

## 2026-05-20: Equal total workspace + equal usable young comparison

Goal:
- Compare CheneyGC and GiY with the same total GC Local Workspace and the same actual usable young space.

Configuration:

| GC | Total workspace | Extra local reservation | Young before aux | Young after aux |
|---|---:|---:|---:|---:|
| Cheney equal-young | 896 KB | 48 KB padding + 32 KB FT slot set | 684.24 KB | 604.24 KB |
| GiY | 896 KB | 48 KB GC stack + 32 KB FT slot set | 684.24 KB | 604.24 KB |

Output:
- `/home/qiancheng/ejs-new/build.debug/benchmarks/out_equal_young_896_20260520_1336`
- `summary.csv`
- `compare_equal_young.md`

Full-suite result:

| Metric | Cheney equal-young | GiY | GiY - Cheney |
|---|---:|---:|---:|
| Total time | 6580.231 s | 6597.105 s | +16.874 s (+0.256%) |
| Business time | 6460.666 s | 6454.527 s | -6.139 s (-0.095%) |
| GC full time | 119.565 s | 142.578 s | +23.013 s (+19.247%) |
| GC core time | 80.013 s | 99.642 s | +19.629 s (+24.532%) |
| scan_RS | 2.546 s | 24.817 s | +22.271 s |
| scavenge | 76.834 s | 74.311 s | -2.523 s (-3.284%) |
| Minor GC count | 1,032,930 | 1,045,148 | +12,218 (+1.183%) |
| Weighted GC miss rate | 0.793% | 3.232% | +2.438 pp |

Interpretation:
- 这个比较比“只保证 total workspace 相同”更加严格：
  - total workspace 相同；
  - 实际可用 young 空间也相同。
- 在可用 young 空间也对齐以后，minor GC 次数差距几乎消失。
- GiY 总时间只比 Cheney 慢 `0.256%`。
- GiY 的业务时间略快，但 GC 时间仍然更慢。
- 剩下的 GC 差距主要来自 remembered-set scanning：
  - GiY read-slot RSet 在 GC 时需要重新读取 old/init slot；
  - Cheney 的 scan_RS 仍然便宜很多。
- GiY 的 scavenge 本身略快于 Cheney，所以当前主要优化目标是 RSet scan，而不是 young traversal/copy。

## 2026-05-21：GiYSB 第一次完整 benchmark

测试的 GiYSB 设计：
- 把 old space 切成 small-old 和 large-old。
- 小对象阈值：aligned object size `<=256B`。
- 小对象通过 GC Local Workspace 中的 8KB staging buffer 复制。
- 大对象使用现有 GiY direct copy 路径。
- GiYSB 使用 FIFO worklist 顺序，让 small-old 的目标地址尽量跟 reserve 顺序一致。

配置：

| GC | Cache budget | Aux workspace | Young after aux |
|---|---:|---:|---:|
| GiY896 baseline | 896 KB | 80 KB | 604.24 KB |
| GiYSB896 | 896 KB | 88 KB | 596.24 KB |

Benchmark 输出：
- `/home/qiancheng/ejs-new/build.debug/benchmarks/out_giysb_896_profile0_20260520_234139`
- 调整后的结果：`summary_adjusted_richards_repeat1.csv`

说明：
- 第一次 profile-off Richards 样本是异常值，所以我单独重跑了一次 Richards。
- 调整后的比较使用 `Richards_repeat1`。

调整后的总结果：

| 指标 | GiY896 | GiYSB896 | GiYSB - GiY |
|---|---:|---:|---:|
| 总时间 | 6597.105 s | 6655.960 s | +58.855 s (+0.892%) |
| 业务时间 | 6454.527 s | 6489.367 s | +34.840 s (+0.540%) |
| GC full time | 142.578 s | 166.593 s | +24.015 s (+16.843%) |
| GC core time | 99.642 s | 122.534 s | +22.892 s (+22.974%) |
| scavenge | 74.311 s | 96.555 s | +22.244 s (+29.934%) |
| GC cache misses | 162,734,227 | 150,658,028 | -12,076,199 (-7.421%) |
| GC instructions | 631,647,661,430 | 786,304,319,303 | +154,656,657,873 (+24.485%) |

结论：
- GiYSB 是正确的，但第一版没有变快。
- cache misses 下降了，但 instruction count 和 copy work 增加更多。
- 主要怀疑原因：小对象被复制了两次，也就是 young -> staging -> old。
- split old 布局也可能损害 mutator locality，因为相关的小对象/大对象被分开放置了。

## 2026-05-21：记录语言规则

- 以后写入 `AIlog.md` 和 `important.md` 的实验记录、结论、设计说明默认使用中文。
- 英文术语可以保留，但解释和总结使用中文。
