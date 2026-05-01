## 2026-04-26 GiY Minor GC 修改说明

本次只修改 GiY minor GC 相关路径，以及一个为 GiY/remembered set 构建所需的声明一致性问题。

### 1. GiY 专用 root scanning

修改文件：`ejsvm/GiY.cc`

新增 `giy_scan_roots_generational` 和 `giy_scan_stack`，在 `giy_minor_collect` 的 reserve 阶段和 patch 阶段都使用这个 GiY 专用 root scanner。

原因：

- 通用 `scan_roots` 会进入 `scan_Context`，并扫描 `context->function_table`。
- function table 里有大量常量池、inline cache、allocation site cache 等字段。对 GiY 的目标来说，这会扩大 minor GC 期间的 Old/非 Young 数据读取范围。
- GiY minor 当前需要聚焦执行栈、关键上下文寄存器、全局 root 和 remembered set。对象图继续通过 Young 内对象本身遍历。

这样做之后，GiY minor 不再通过通用 root scanner 扫描 function table，也避免把这部分老路径混进 Young-first traversal。

### 2. Remembered Set 扫描不再无条件写 Old slot

修改文件：`ejsvm/GiY.cc`

`giy_scan_remembered_set_slots` 现在只读取 RS 记录的 slot，并用 `GiYReserveTracer` 对 slot 中的 Young 指针做 reserve/mark，不再把原值无条件写回 slot。

`giy_patch_remembered_set_slots` 现在先读取旧值，计算 patch 后的新值，只有 `new_value != old_value` 时才写回。

原因：

- roadmap 要求 Old/DRAM 在 minor GC 中尽量不被触碰。
- RS slot 位于 Old Gen 时，旧代码即使指针没有变化也会写一次 Old slot。
- 新逻辑只在 Old slot 确实指向 Young，并且必须改成 Old dest pointer 时写回。这个写回属于 roadmap 允许的例外：更新 Old->Young 跨代引用。

Old slot 写回仍然走现有 `giy_store_ptr_slot` / `giy_store_jsvalue_slot`，在 x86_64 上使用 `_mm_stream_si64` 做 non-temporal store。

### 3. Young 内遍历和最终拷贝路径保持 GiY 结构

修改文件：`ejsvm/GiY.cc`

本次保留已有的核心结构：

- `copy_for_minor` 只在 Old Gen 预留目标地址，写 Young header 的 `forwarding_pointer`，并把 Young payload 压入 GiY GC stack。
- `giy_traverse_stack_and_copy` 从 GC stack 弹出 Young 对象，在 Young 内扫描并修正对象字段。
- 对象字段全部修正后，再调用 `giy_copy_live_object` 把完整对象一次性写入 Old Gen。
- `giy_copy_live_object` 在 x86/x86_64 路径使用 streaming store，最后由 `giy_minor_collect` 统一 `_mm_sfence`。

这符合 roadmap 的核心要求：对象图遍历和字段修正在 Young/Cache 中完成，Old/DRAM 只接收最终 materialize copy。

### 4. 修复 remembered set 构建声明冲突

修改文件：`ejsvm/types.h`

把 `write_barrier` 和 `write_barrier_ptr` 的声明移动到 `USE_REMEMBERED_SET` 的统一声明区，并在 C++ 下使用 `extern "C"`。accessor macro 里不再重复声明它们。

原因：

- GiY 构建时，`types.h` 的 accessor macro 先在 C++ 编译单元里声明 write barrier 为 C++ linkage。
- `cache_dram_manager.h` 后续声明同一函数为 C linkage，导致编译冲突。
- 实际 write barrier 需要被 C 文件和 C++ 文件共同调用，因此统一为 C linkage 是正确的。

这个修改只修声明/链接一致性，不改变 write barrier 行为，也不改变其他 GC 算法逻辑。

### 5. 验证

已执行：

```sh
make OPT_GC=giy -j2
```

结果：构建通过。构建过程中有仓库原有 warning，例如 `types.h` 的 multi-line comment warning、若干未使用变量/格式化 warning、`main.c` 对 `space_free_dram_manager_init` 的隐式声明 warning；本次没有处理这些非 GiY roadmap 范围的问题。

没有跑 benchmark。原因是本次先完成核心代码和构建验证；benchmark 输出目录需要按 roadmap 单独编号并写 readme，后续跑 benchmark 时再执行。

## 2026-04-26 弱引用语义补充修改

本次按其他 GC 的语义修改 GiY 和 cache_cheney：强扫描阶段弱边不保活。

### 1. cache_cheney

修改文件：`ejsvm/cache_dram_manager.cc`

- `CacheCheney_Tracer::process_weak_edge` 改为空操作。
- `CacheCheney_Tracer::process_mark_stack` 改为调用 `scavenge()`。
- `weak_clear<CacheCheney_Tracer>` 后新增 `patch_live_inline_cache(ctx)`。

原因：

- weak edge 不应该在 root/function table 强扫描时把对象复制到 DRAM。
- weak_clear 会先清掉未存活的 inline cache 项。
- 对 weak_clear 后仍然保留的 inline cache 项，需要 patch `ic->pm` 和 `ic->prop_name`，否则它们可能还指向 cache/from-space 旧地址。

### 2. GiY

修改文件：`ejsvm/GiY.cc`

- `GiYReserveTracer` 和 `GiYPatchTracer` 的 `process_weak_edge` 改为空操作。
- 新增 `GiYWeakTracer` 供 `giy_weak_clear` 使用。
- `giy_weak_clear` 改为 `weak_clear<GiYWeakTracer>(ctx)`，并在最后按需 `_mm_sfence()`。
- weak_clear 后新增 `giy_patch_live_inline_cache(ctx)`，只修补仍然存活的 inline cache 项。

GiY 的约束：

- strong scan 不通过 weak edge 保活对象。
- weak_clear 如果因为 hidden class / string table / live inline cache 规则保留某个对象，`GiYWeakTracer` 会先在 Young/Cache 中完成遍历和字段修补，再 materialize 到 Old/DRAM。
- Old slot 更新仍走 `giy_store_ptr_slot` / `giy_store_jsvalue_slot`，保持 non-temporal store 路径。

### 3. 注意

这次修改只解决 weak edge 语义。GiY 是否需要处理 function table 中非弱的 Young 指针，是另一个问题。之前调查显示常量池和 allocation site cache 可能出现 Young 指针；后续更合适的方案是用写屏障记录 function table 中可能出现 Young 指针的强 slot，而不是让 GiY minor 回到通用 `scan_roots` 扫整个 function table。

### 4. 验证

已执行：

```sh
make OPT_GC=cache_cheney -j2
make OPT_GC=giy -j2
git diff --check
```

结果：均通过。warning 仍是仓库已有 warning，本次未处理非任务范围问题。

## 2026-04-27 GiY 最终重新验证

本轮目标是先暂时不考虑 weak 引用 strict 化，只确认 GiY 主流程是否满足 roadmap 中的核心原则，然后再跑 benchmark。

验证结果：
- `make OPT_GC=giy -j2` 成功；
- GiY 普通运行 `a.sbc` 到 `g.sbc`、`hello.sbc`、`f1.sbc`，退出码全部为 0；
- `GIY_MPROTECT_OLD=1` 下运行同一批样例，退出码全部为 0；
- stderr 中没有 `GiY mprotect old-space violation`，也没有 `Segmentation fault`。

mprotect 这轮验证到的含义：
- GiY strict core 阶段没有在非允许窗口访问 Old/DRAM；
- 允许窗口仍然是最终 copy 到 Old 和 patch Old slot；
- `weak_clear` 仍作为通用 VM metadata maintenance 放在 strict core 窗口外，本轮按用户要求暂不处理 weak 引用问题。

注意：我尝试用 cache_cheney 做运行对照，但 cache_cheney 构建后运行 `a.sbc` 出现退出码 139。为了不扩大修改其他 GC，本轮不把 cache_cheney 当作 GiY correctness baseline。

## 2026-04-27 benchmark 中发现的 GiY remembered set 容量问题

第一次完整 benchmark 输出到 `build.debug/benchmarks/out21`。结果是前 11 项通过，`Havlak` 失败：

```text
Error: Remembered set full, cannot add more remembered objects!
```

原因不是 weak 引用，也不是 mprotect 违规，而是 GiY remembered set 容量不足。GiY 为了避免 minor GC 读 Old slot，现在每条 remembered-set entry 同时存 slot 地址和值；原来的 24KB 预留空间被地址和值分摊后，实际容量只有 1536 个 entry，`Havlak` 会超过这个上限。

修复方式：
- `REMEMBERED_SET_CAPACITY_BYTES` 从 24KB 提高到 128KB；
- `HASH_TABLE_SIZE` 从 4096 提高到 8192；
- hash 探测上限从 8 提高到 32。

这个修复仍符合 GiY 原则：只是扩大 cache 中的 GiY 元数据区域，不恢复扫描 Old，也不让 minor GC 读取 Old slot 当前值。

修复后验证：
- GiY 重新构建成功；
- 样例普通运行全部退出码 0；
- `GIY_MPROTECT_OLD=1` 样例全部退出码 0，未出现 old-space violation；
- 单独重跑 `Havlak`，退出码 0。

最终完整 benchmark 结果：`build.debug/benchmarks/out22`。

全部退出码为 0：

```text
Bounce      real 157.106
List        real 110.117
Sieve       real 120.998
Queens      real 113.883
Permute     real 381.902
Storage     real 320.245
Towers      real 180.324
Mandelbrot  real 237.101
Richards    real 937.403
CD          real 275.662
NBody       real 385.962
Havlak      real 496.786
```

结论：在暂不处理 weak 引用 strict 化的边界内，当前 GiY 通过样例、mprotect 核心验证和完整 benchmark。

代价：新的 remembered set 容量为 8192，benchmark 输出中的 GiY cache size 约为 320KB。也就是说，修复 `Havlak` 的容量问题牺牲了一部分 Young/cache 可用空间，但保留了“不在 minor GC 中读 Old slot”的 GiY 原则。

## 2026-04-27 Cheney 对照组完整 benchmark

用户明确说明 `cheneyGC` 是对照组。仓库中对应构建为：

```sh
make OPT_GC=cache_cheney
```

旧 `out13` 不能作为完整对照组，因为 `Havlak` 在 remembered set full 后退出。为得到可完整对比的对照组，我恢复了 cache_cheney 原本的弱边强处理语义，并把对照组 remembered set 预留调到 128KB、hash table 调到 8192，使其和 GiY `out22` 一样显示 cache size 320KB。

新的完整对照组输出为 `build.debug/benchmarks/out23`，12 项全部退出码 0。

和 GiY `out22` 的 runtime 对比：

```text
Benchmark    GiY out22   Cheney out23   GiY 相对 Cheney
Bounce       1.57099     1.59925        快 1.8%
List         1.10112     1.00671        慢 9.4%
Sieve        1.20906     1.21730        快 0.7%
Queens       1.13882     1.17852        快 3.4%
Permute      3.81898     3.81004        慢 0.2%
Storage      3.19236     2.07121        慢 54.1%
Towers       1.80293     1.83044        快 1.5%
Mandelbrot   2.37069     2.37038        基本持平
Richards     9.37380     9.35045        慢 0.25%
CD           2.75417     2.48455        慢 10.9%
NBody        3.85897     3.81718        慢 1.1%
Havlak       4.96096     4.23781        慢 17.1%
```

12 项 average runtime 总和：
- GiY：37.15285 秒；
- Cheney：34.97384 秒；
- 当前 GiY 总体约慢 5.9%。

解释：GiY 的 root scanning 已经明显更低，例如 Havlak 的 scan_roots 为 0.177 秒而 Cheney 为 5.709 秒；但 GiY 的 scavenge/materialize 成本更高，尤其 Storage、CD、Havlak，抵消了 root scanning 的收益。

## 2026-04-27 Cheney Root-Controlled 对照组

为了进一步控制变量，我又构造了一个新的 Cheney 对照组：保留 Cheney copy/scavenge 行为，但把 minor GC 的 root scanner 改成 GiY 风格：

- 不再调用通用 `scan_roots<CacheCheney_Tracer>(ctx)`；
- 改为 `scan_roots_generational(ctx)`；
- function_table 中的 constant pool 和 allocation site cache 强 slot 通过写入时记录；
- minor GC 只扫描这些记录过的 function-table strong slot，不扫描整张 function_table。

输出目录：`build.debug/benchmarks/out24`。12 项全部退出码 0。

三组 average runtime 总和：

```text
GiY out22                         37.15285 秒
Cheney full-root-scan out23       34.97384 秒
Cheney root-controlled out24      34.99316 秒
```

结论：
- `out24` 与 `out23` 几乎一样，说明把 Cheney 的 full function_table root scan 控住后，Cheney 总体性能没有明显改变；
- GiY 相对 root-controlled Cheney 仍约慢 5.8%；
- 因此当前 GiY 的主要差距不是 root scanning，而是后续 young traversal / materialize / scavenge 成本。

关键证据：
- Havlak scan_roots：Cheney full 5.709 秒，Cheney root-controlled 0.350 秒，GiY 0.177 秒；
- Storage scavenge：Cheney root-controlled 48.342 秒，GiY 156.700 秒；
- CD scavenge：Cheney root-controlled 7.675 秒，GiY 22.893 秒；
- Havlak scavenge：Cheney root-controlled 27.866 秒，GiY 69.168 秒。

新的优化方向：继续保持 GiY root scanning 的设计，不要回退到 full function_table scan；重点调查 GiY 的 edge log、object traversal、最终 copy/materialize 和 cache 空间压力。

## 2026-04-27 GiY mprotect 验证与修正

本次目标是用 `mprotect` 验证 GiY minor 的核心阶段是否真的避免了非必要 Old/DRAM 访问。

### 1. 验证机制

新增运行时开关：

```sh
GIY_MPROTECT_OLD=1
```

打开后，GiY minor 核心阶段会把整个 DRAM/Old 区设为 `PROT_NONE`。只有两个地方会临时放开保护：

- 最终把 live young object materialize 到 Old 的 copy 窗口。
- patch Old slot 时写入那个 slot 所在页。

如果其他阶段读写 Old，会收到 SIGSEGV，并打印当前 GiY 阶段名。

### 2. 修复一：年轻对象扫描不能读 Old Shape

mprotect 首先抓到的问题在 `young_traverse_copy` 阶段。

原因是通用 `NodeScanner::scan_object_properties` 会读取 `p->shape->...`。年轻对象的 `shape` 可能已经在 Old，因此这条路径违反 GiY 规则。

修复方式：

- 给 GiY 新增保守 JSObject scanner。
- 扫描 JSObject 时只读年轻对象自己的 header 和 payload。
- `shape` slot 只作为 pointer edge 处理，不 dereference。
- `eprop` 根据对象 header size 保守扫描。

这样 GiY 遍历年轻对象时不再依赖 Old `Shape/PropertyMap` 元数据。

### 3. 修复二：Remembered Set 不能在 reserve 阶段回读 Old slot

mprotect 第二次抓到的问题在 `remembered_set_reserve` 阶段。

旧 RS 只记录 Old slot 地址，所以 reserve 时必须读 `*slot`，这就是 Old read。

修复方式：

- GiY RS 新增 `values` 数组。
- 写屏障记录 slot 地址时，也记录“最新写入的值”。
- 如果 slot 后续写成 immediate、Old 或 NULL，就把已有记录的 value 更新为 0。
- reserve 阶段用记录值发现 Young 对象，不读 Old slot。
- patch 阶段根据记录值和 forwarding pointer 直接算新地址，只在需要时写 Old slot。

这符合 roadmap：RS 扫描阶段不碰 Old；真正的 Old 访问只剩必要的 Old slot patch。

### 4. 弱清理边界

本次 mprotect 窗口覆盖 GiY minor 的核心阶段：

- root reserve
- function table 强 slot reserve
- remembered set reserve
- young traversal/copy
- root patch
- function table patch
- remembered set patch

`weak_clear` 暂时在 mprotect 窗口外。

原因是当前通用 `weak_clear` 会维护字符串表、hidden class 图、shape 弱链和 inline cache 等全局弱结构，这些结构会遍历老元数据。要让弱清理也满足 0 Old read，需要单独实现 GiY 专用弱 slot 记录和清理机制；这不适合混入本次核心阶段验证。

### 5. 验证结果

已执行：

```sh
make OPT_GC=giy -j2
make OPT_GC=cache_cheney -j2
make OPT_GC=giy -j2
```

已执行 mprotect 样例：

```sh
GIY_MPROTECT_OLD=1 ./ejsvm a.sbc
GIY_MPROTECT_OLD=1 ./ejsvm b.sbc
GIY_MPROTECT_OLD=1 ./ejsvm c.sbc
GIY_MPROTECT_OLD=1 ./ejsvm d.sbc
GIY_MPROTECT_OLD=1 ./ejsvm e.sbc
GIY_MPROTECT_OLD=1 ./ejsvm f.sbc
GIY_MPROTECT_OLD=1 ./ejsvm g.sbc
GIY_MPROTECT_OLD=1 ./ejsvm ../ejsvm/js/hello.sbc
GIY_MPROTECT_OLD=1 ./ejsvm ../ejsvm/js/f1.sbc
```

结果：

- `a.sbc` 到 `g.sbc` 退出码为 0，没有 `GiY mprotect old-space violation`。
- `hello.sbc` 和 `f1.sbc` 退出码为 1，但没有 `GiY mprotect old-space violation`，也没有 Segmentation fault。

中间曾出现 `d.sbc` 普通崩溃，原因是 `build.debug/object.c` 是旧副本，缺少之前的 GiY function-table slot 记录。清理旧副本并重新构建后问题消失。

## 2026-04-27 hello/f1 退出码与 weak_clear 复查

### 1. hello.sbc / f1.sbc 为什么退出码是 1

这不是 GiY GC 错误，而是这两个样例文件还是旧版 SBC 文本格式。

当前 loader 需要：

- 第一行 `fingerprint 36`
- `numberOfInstructions`
- `numberOfConstants`
- 常量使用 `#index=value` 格式

旧文件的问题：

- 缺少 `fingerprint 36`
- 写的是 `numberOfInstruction`
- 缺少 `numberOfConstants`
- 常量直接写成 `"print"` / `"hello world"` 这种旧格式
- `f1.sbc` 还使用已经不存在的旧指令 `newargs`

已修复：

- `ejsvm/js/hello.sbc`
- `ejsvm/js/f1.sbc`

`f1.sbc` 中的 `newargs` 改成了 `newframe 2 1`，表示创建 2 个 local slot 的 frame，并生成 arguments 对象。

### 2. weak_clear 为什么不能简单放进严格 mprotect 窗口

我尝试过把 `weak_clear` 改成 GiY minor 专用的 Young-only 清理，但失败了：

- 处理 `property_map_roots` 的 Young 头节点会破坏 hidden-class/property map 图，`c.sbc` 在属性查找路径崩溃。
- 只处理 string table 的 Young 头节点也会破坏当前程序语义，`a-g` 出现退出 1 或段错误。

原因是当前 VM 的 `weak_clear` 不只是普通弱引用清理。它还维护：

- hidden-class / PropertyMap 图
- shape 弱链
- transition 弱边
- string interning 表
- inline cache

这些结构和 Old 元数据强耦合。局部 Young-only 清理会丢掉 VM 后续仍需要的 hidden-class/string metadata。

### 3. 当前采用的可靠处理

恢复通用 `weak_clear<GiYWeakTracer>(ctx)`，保证 VM 语义正确。

代码上把 guard 结束点移动到 `giy_weak_clear` 内，并标记阶段：

```text
weak_clear_generic_old_metadata
```

含义是：

- GiY 核心阶段仍然由 `mprotect(PROT_NONE)` 验证，确保 root/RS reserve、Young traversal/copy、root/RS patch 不读 Old。
- `weak_clear` 被明确标成 old metadata maintenance 阶段，不再混在 GiY 核心 traversal/copy 阶段里。
- 这个阶段为了正确维护 hidden-class/string 等全局元数据，当前仍需要读 Old。

如果后续导师要求 weak_clear 也严格 0 Old read，可靠方案不是局部改 generic weak_clear，而是新增 GiY 专用 weak remembered set：在写入 string table、PropertyMap weak edge、Shape weak edge、transition weak edge、inline cache 时记录弱 slot，然后 minor GC 只根据记录集清理或 patch 弱 slot。

### 4. 最终验证

已执行：

```sh
make OPT_GC=giy -j2
make OPT_GC=cache_cheney -j2
make OPT_GC=giy -j2
```

最终 mprotect 验证：

```text
a.sbc status=0
b.sbc status=0
c.sbc status=0
d.sbc status=0
e.sbc status=0
f.sbc status=0
g.sbc status=0
../ejsvm/js/hello.sbc status=0
../ejsvm/js/f1.sbc status=0
```

全部没有 `GiY mprotect old-space violation`，也没有 Segmentation fault。

## 2026-04-26 GiY inline cache 弱引用收紧

修改文件：`ejsvm/GiY.cc`

本次只调整 GiY 的 inline cache 弱引用处理，不改其他 GC。

### 修改内容

- 新增 `giy_is_live_weak_jsvalue`：判断 `ic->prop_name` 这种 JSValue 弱目标是否已经被强边保活。
- 新增 `giy_clear_inline_cache`：统一清掉 `ic->pm` 和 `ic->prop_name`。
- 修改 `giy_patch_live_inline_cache`：
  - `ic->pm` 未存活：清掉该 inline cache。
  - `ic->prop_name` 是堆对象且未存活：也清掉该 inline cache。
  - 两者都已经存活：只用 `GiYPatchTracer` patch 地址。

### 为什么这样做

之前的 live inline cache patch 使用 `GiYWeakTracer::process_edge`。这个 tracer 可以在 weak_clear 阶段 materialize Young 对象，适合 hidden class branch/terminal 的保留规则，但不适合 inline cache。

inline cache 是弱缓存。它不能因为 `ic->pm` 还活着，就把未被强边保活的 `ic->prop_name` 字符串也复制到 Old。更可靠的语义是：弱目标没活就清 cache；弱目标已经活，才修补地址。

### GiY 约束

- 强扫描阶段仍然弱边不保活。
- inline cache patch 阶段只修补已存活对象，不产生新的 materialize copy。
- 这样既不会留下 Young 旧地址，也不会让弱缓存扩大 Old copy 集合。

### 验证

已执行：

```sh
make OPT_GC=giy -j2
git diff --check
```

结果：通过。第一次构建遇到 `build.debug/types.h` 旧副本导致的 write barrier linkage 冲突；删除旧副本后重新构建通过。

## 2026-04-26 GiY function_table 强引用 slot 记录

修改文件：

- `ejsvm/GiY.cc`
- `ejsvm/cache_dram_manager.h`
- `ejsvm/codeloader.c`
- `ejsvm/object.c`

### 问题

GiY minor 为了避免通用 `scan_roots` 扫整个 `function_table`，改用了专用 root scanner。但 `function_table` 中仍有一些强引用可能指向 Young：

- constant pool：`ctop[i]`
- allocation site cache：`AllocSite::pm`
- allocation site cache：`AllocSite::shape`

这些不是弱引用，不能靠 `weak_clear` 解决。如果完全不处理，会有漏标 Young 对象的风险。

### 做法

新增 GiY 专用 function-table strong-slot remembered set。

这个 set 记录的是 slot 地址，而不是对象地址。记录时只在写入值指向 Young/Cache work area 时记录；如果值是 immediate、DRAM、init 或 NULL，就不记录。

记录接口：

```c
giy_record_ft_jsvalue_slot(JSValue *ptr, JSValue value)
giy_record_ft_ptr_slot(void **ptr, void *value)
```

这两个接口只在 `USE_GIY_MINOR` 下声明和使用，所以不会影响其他 GC。

### 记录点

`codeloader.c`：

- OBC `const_load` 写 `ctop[i]` 后记录 constant pool slot。
- SBC `load_number_sbc` / `load_string_sbc` / `load_regexp_sbc` 首次写 `ctop[index]` 后记录 slot。

`object.c`：

- `get_cached_shape` 写 `as->shape` 后记录。
- `create_simple_object_with_prototype` 首次写 `as->pm` / `as->shape` 后记录。
- `create_array_object` 首次写 `as->pm` / `as->shape` 后记录。

### 不记录什么

不记录 `InlineCache::pm` 和 `InlineCache::prop_name`。

原因：它们是弱引用。把它们放进 strong-slot remembered set 会重新让 inline cache 保活对象，破坏前面刚修正的弱引用语义。inline cache 继续由 `weak_clear` 和 `giy_patch_live_inline_cache` 负责。

### Minor GC 中的处理

reserve 阶段：

```text
scan GiY roots
reserve recorded function_table strong slots
scan normal remembered set
```

patch 阶段：

```text
patch GiY roots
patch recorded function_table strong slots
patch normal remembered set
clear function_table slot set
```

function_table strong slot 的 reserve 使用 `giy_reserve_edge`，只做 `copy_for_minor`，不写 slot，也不记录 edge log。patch 阶段才修 slot 地址。

### 为什么符合 GiY 原则

- 没有恢复扫描整个 `function_table`。
- 只处理实际写入过 Young 指针的强 slot。
- Young 对象图仍然在 `giy_traverse_stack_and_copy` 中完成遍历和字段修补。
- function_table patch 只是必要的外部 slot 地址修补，不会让 GiY 回到通用 Cheney 式 Old/非 Young 扫描。

### 验证

已执行：

```sh
make OPT_GC=giy -j2
make OPT_GC=cache_cheney -j2
```

结果：均通过。warning 仍是仓库已有 warning，本次未处理非任务范围问题。
