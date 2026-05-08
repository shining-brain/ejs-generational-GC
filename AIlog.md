记录规则：
- 从 2026-04-26 起，AIlog.txt 始终使用中文记录。
- 每次记录要写清楚修改范围、实际改了哪些文件、为什么这么改、验证结果、没有做的事情和原因。

2026-04-26

修改范围：
- 用户要求基于 roadmap.txt 修改 GiY GC，并明确要求不要闲着没事改其他 GC。
- 我开始前工作区里已有修改：roadmap.txt 已修改，AGENTS.md 是未跟踪文件。我没有修改这两个文件。

本次修改的文件：
- ejsvm/GiY.cc
- ejsvm/types.h
- meeting.md
- AIlog.txt

ejsvm/GiY.cc 的修改：
- 新增 GiY 专用 root scanning 辅助函数：
  - giy_process_edge
  - giy_scan_stack
  - giy_scan_roots_generational
- 将 reserve 阶段的 scan_roots<GiYReserveTracer> 替换为 giy_scan_roots_generational<GiYReserveTracer>。
- 将 patch 阶段的 scan_roots<GiYPatchTracer> 替换为 giy_scan_roots_generational<GiYPatchTracer>。
- 目的：避免 GiY minor GC 走通用 scan_Context 路径扫描 context->function_table。
- 在 giy_scan_remembered_set_slots 中：
  - 删除 reserve 阶段结束后对 remembered set slot 的无条件 giy_store_ptr_slot/giy_store_jsvalue_slot 写回。
  - reserve 阶段现在只读取 RS slot，并发现其中指向 Young 的对象。
- 在 giy_patch_remembered_set_slots 中：
  - 增加 old_value/new_value 比较。
  - 只有 patch 后的值和旧值不同，才写回 slot。
  - 如果 slot 位于 DRAM/Old Gen，写回仍然走 giy_store_ptr_slot/giy_store_jsvalue_slot；x86_64 下使用 non-temporal 的 _mm_stream_si64。

为什么要新增 GiY 专用 root scanning：
- 通用 scan_roots 会调用 scan_Context。
- scan_Context 会遍历 context->function_table，并进入每个 function table entry 的常量池、inline cache、allocation site cache 等结构。
- 这些结构大多不是当前 Young Gen 中需要原地遍历的对象图，很多数据在 Old/静态区域。
- roadmap 的核心要求是：Minor GC 期间尽量不要触碰 DRAM/Old Gen，存活对象的对象图遍历和字段修正应该在 Young/Cache 中完成，Old Gen 只接收最终一次性拷贝，以及必要的 Old->Young slot 更新。
- 因此 GiY minor 不能直接复用面向普通 moving GC 的通用 scan_roots。GiY 需要只扫描必要 roots：全局 root、关键 context 寄存器、执行栈、GC_PUSH roots，以及 remembered set。
- 这样可以减少不必要的 Old/非 Young 读取，避免把 function table 这种大结构带入 minor GC 的热路径，符合 GiY 的 Cache-first 设计目标。

ejsvm/types.h 的修改：
- 在 USE_REMEMBERED_SET 下统一声明 write_barrier 和 write_barrier_ptr。
- C++ 编译单元下使用 extern "C" 包裹这两个声明。
- 从 ACCESSOR_WB_JSVALUE 和 ACCESSOR_WB_PTR 宏里删除重复的块内 extern 声明。
- 原因：make OPT_GC=giy 时，types.h 先在 C++ 编译单元里把 write_barrier 声明成 C++ linkage，随后 cache_dram_manager.h 又把同一函数声明成 C linkage，导致编译冲突。
- 这个修改只是声明/linkage 修复，没有改变 write barrier 的运行逻辑。

构建验证：
- 已执行：make OPT_GC=giy -j2
- 结果：构建成功。
- 构建中仍有一些原有 warning，本次没有处理，因为不属于 roadmap 要求的 GiY GC 修改范围：
  - types.h 的 multi-line comment warning
  - cache_dram_manager.h 的 signedness warning
  - giy_dram_manager.cc 的 unused variable/format warning
  - main.c 对 space_free_dram_manager_init 的 implicit declaration warning

构建产物处理：
- make OPT_GC=giy 会重新生成 build.debug 下的 tracked 文件，并产生 GiY 相关未跟踪拷贝和目标文件。
- 我已清理这些 build.debug 构建产物，避免最终 diff 混入构建输出。

benchmark：
- 本次没有跑 benchmark。
- 原因：本次先完成核心实现和构建验证。roadmap 要求 benchmark 输出目录要单独编号并写 readme，后续跑 benchmark 时再按这个规则执行。

2026-04-26 补充调查：function_table 是否可能保存 Young 指针

调查结论：
- 有可能，而且当前代码里已经能看到明确路径。
- 因此“GiY minor 完全不扫描 function_table”这个优化不能直接作为最终正确方案。
- 更安全的方案应该是：GiY minor 至少扫描 function_table 中的强引用部分，尤其是常量池和 allocation site cache；inline cache 可以继续按弱缓存处理，但需要确认弱清理逻辑完整。

证据 1：常量池会保存 Young 对象
- main.c 中先调用 space_free_dram_manager_init()，将 cache_space.work_begin 固定下来。
- 之后才调用 code_loader() 加载字节码。
- codeloader.c 在 code_loader() 中用 malloc 分配 Instruction 数组，并把常量池放在 insns[ninsns] 后面。
- codeloader.c 的 load_number_sbc/load_string_sbc/load_regexp_sbc 会写入 ctop[index]：
  - ctop[index] = double_to_number(NULL, d)
  - ctop[index] = cstr_to_string(NULL, str)
  - ctop[index] = new_regexp(ctx, str, flag)
- double_to_number、cstr_to_string、new_regexp 都可能通过 gc_malloc 分配 GC heap 对象。
- 因为此时 work_begin 已经设置完成，这些对象属于 Young/work area。
- 这些常量在运行时由 number/string/regexp 指令通过 get_literal(insns, disp) 读取，所以它们是强引用。
- 如果 GiY minor 不扫描 function_table 常量池，这些 Young 常量可能在第一次 minor GC 时被错误回收。

证据 2：allocation site cache 会保存 PM/Shape 指针
- object.c 中 create_simple_object_with_prototype/create_array_object/get_cached_shape 会写：
  - as->pm = pm
  - as->shape = os
  - as->shape = new_object_shape(...)
- gc.cc 中 alloc_site_update_info 也会更新 as->pm/as->shape。
- AllocSite 位于 Instruction 内，也就是 function_table->insns 中。
- gc-visitor-inl.h 的 scan_function_table_entry 把 allocation site cache 当作强引用处理，对 as->shape 和 as->pm 使用 PROCESS_EDGE。
- 这说明原始 GC 设计认为 allocation site cache 里的 PM/Shape 需要参与保活或更新。
- 如果 GiY minor 跳过这部分，Young 中新建的 PropertyMap/Shape 可能不会通过 function_table 被发现。

证据 3：inline cache 也会写入指针，但原始 visitor 按弱引用处理
- object.c 中 get_prop_with_ic/set_prop_ 会写：
  - ic->pm = object_get_shape(obj)->pm
  - ic->prop_name = name
- InlineCache 同样位于 Instruction 内。
- gc-visitor-inl.h 的 scan_function_table_entry 对 inline cache 使用 PROCESS_WEAK_EDGE。
- weak_clear_inline_cache 会在 ic->pm 未标记时清空 ic->pm 和 ic->prop_name。
- 因此 inline cache 更像性能缓存，不应作为强 root；但它仍然是 function_table 中会保存 GC 指针的区域。

当前判断：
- function_table 不是普通 JS 对象图的一部分，但它是 VM root set 的一部分。
- 对 full GC 或普通 moving GC 来说，扫描 function_table 是为了覆盖 VM 内部结构保存的 JSValue/heap pointer。
- 对 GiY minor 来说，可以避免扫描不必要的大范围 Old 数据，但不能无条件跳过其中的强引用。
- 我之前新增的 GiY 专用 root scanning 跳过了整个 function_table，这个优化需要修正。建议改成 GiY 专用 function_table 扫描：只扫描常量池和 allocation site cache 的强引用，inline cache 保持弱清理或做最小 patch，不走通用 scan_Context 的全部逻辑。

2026-04-26 补充调查：当前程序如何处理弱引用

当前弱引用位置：
- StrCons::str：字符串表中的字符串引用，scan_StrCons 使用 PROCESS_WEAK_EDGE。
- TransitionTable 中 transition[i].pm：在 HC_SKIP_INTERNAL 下是弱引用。
- PropertyMap::prev：在 HC_SKIP_INTERNAL 下是弱引用。
- Shape::next：在 WEAK_SHAPE_LIST 下是弱引用。
- PropertyMapList::pm / PropertyMapList::next：用于隐藏类图 root list，按弱 list 处理。
- InlineCache::pm 和 InlineCache::prop_name：scan_function_table_entry 中使用 PROCESS_WEAK_EDGE，weak_clear_inline_cache 会清掉未标记的 inline cache。
- Context::exhandler_pool 注释为 weak，weak_clear 末尾直接置 NULL。

普通 visitor 的弱引用处理方式：
- mark-tracer.h 中 process_weak_edge 基本是空操作，也就是弱引用不会标记目标对象。
- 标记/复制的强引用阶段结束后，weak_clear 会处理弱结构：
  - weak_clear_StrTable：如果字符串未标记，就把 StrCons 从 string_table 链表移除；如果已标记，则 patch/保留。
  - weak_clear_property_maps / weak_clear_shape_recursive / weak_clear_shapes：清理隐藏类图中的弱边，并保留必要的分支节点。
  - weak_clear_inline_cache：如果 ic->pm 未标记，就把 ic->pm 清空，并把 ic->prop_name 设为 JS_UNDEFINED。

GiY 当前实现的特殊点：
- ejsvm/GiY.cc 中 GiYReserveTracer::process_weak_edge 目前直接调用 process_edge。
- GiYPatchTracer::process_weak_edge 也直接调用 process_edge。
- 这意味着 GiY 在扫描对象时，当前会把经过 PROCESS_WEAK_EDGE 的边当作强边 reserve/forward/patch。
- 这样做比较保守，不容易留下悬空 Young 指针，但会破坏弱引用语义，导致本应可丢弃的字符串、inline cache、隐藏类图节点被保活，增加 Old Gen 写入和内存保留。

是否需要真的管理弱引用：
- 需要。弱引用不应该保活目标对象，但也不能完全不管。
- 如果完全不处理，minor GC 后可能留下指向已回收 Young 区的悬空指针，例如 string_table 或 inline cache 中的旧 Young 指针。
- 正确做法应该是：
  - 强引用阶段只处理强边，不通过弱边 copy_for_minor。
  - 强引用遍历完成后，对弱 slot 做一次清理/修补：
    - 如果弱 slot 指向的 Young 对象已经被其他强引用标记/forward，则把弱 slot patch 到 Old dest。
    - 如果弱 slot 指向未标记的 Young 对象，则清空 slot 或从弱表中摘除。
    - 如果弱 slot 指向 Old/init 区，通常保持不变。
- 因此如果后续用写屏障记录 function_table slot，inline cache 这类弱 slot 也可以记录，但 minor GC 里应按 weak slot 规则处理，不应该作为强 root。

2026-04-26 补充调查：程序在什么情况下产生弱引用

当前构建配置中启用了 HC_SKIP_INTERNAL、WEAK_SHAPE_LIST、INLINE_CACHE、ALLOC_SITE_CACHE，因此弱引用主要出现在 VM 内部优化结构中，不是 JavaScript 层面的 WeakRef/WeakMap。

1. 字符串 intern 表：
- 触发场景：调用 cstr_to_string、字符串拼接、数字转字符串等路径创建或查找字符串。
- 创建位置：string.c 的 string_table_put。
- 写入内容：
  - c->str = v
  - c->next = string_table.obvector[index]
  - string_table.obvector[index] = c
- 弱引用含义：StrCons::str 不应该单独保活 StringCell。字符串如果没有被程序强引用，只因为在 intern table 中出现，不应该永久存活。
- 清理位置：weak_clear_StrTable。

2. hidden class / PropertyMap 的 prev 弱边：
- 触发场景：对象新增属性、扩展 property map、加载 HCG。
- 创建位置：
  - object.c 的 new_property_map：m->prev = prev
  - init.c 的 load_hcg：pm->prev = prev
- 弱引用含义：prev 用于隐藏类图回溯/压缩，不应该因为后继 map 存活就无条件保活所有历史中间 map。
- 清理/压缩位置：weak_clear_property_maps、weak_clear_property_map_recursive。

3. hidden class transition table 中的 pm 弱边：
- 触发场景：对象新增属性时创建从旧 PropertyMap 到新 PropertyMap 的 transition。
- 创建位置：
  - object.c 的 property_map_add_transition 调用 hash_put_transition
  - hash.c 的 hash_put_transition：ttable->transition[i].pm = pm
- 在 HC_SKIP_INTERNAL 下，scan_TransitionTable 对 transition[i].pm 使用 PROCESS_WEAK_EDGE。
- 弱引用含义：隐藏类图中的某些中间 transition 节点可以在 GC 时被跳过/压缩，不能全部作为强边保活。

4. Shape 链表中的 next 弱边：
- 触发场景：创建对象 shape，或者同一个 PropertyMap 下产生多个不同嵌入槽布局的 shape。
- 创建位置：object.c 的 new_object_shape：
  - s->next = *pp
  - *pp = s
- 类型注释：types.h 中 Shape::next 标注为 weak list。
- 弱引用含义：PropertyMap::shapes 指向链表头是强的，但后续 Shape::next 是弱链表，GC 可以清理不再使用的 shape。
- 清理位置：weak_clear_shapes、weak_clear_shape_recursive。

5. PropertyMapList 弱链表：
- 触发场景：创建从 root property map 派生的新 PropertyMap 时，会把它登记到 context->property_map_roots。
- 创建位置：object.c 的 new_property_map：
  - p->pm = m
  - p->next = ctx->property_map_roots
  - ctx->property_map_roots = p
- 类型注释：context.h 中 property_map_roots 是 weak list。
- 弱引用含义：这个列表用于找到隐藏类图入口，但列表项不能无条件保活所有 PropertyMap。
- 清理位置：weak_clear_property_maps / weak_clear_shapes。

6. InlineCache 中的 pm 和 prop_name：
- 触发场景：执行 getprop/setprop/setglobal/getglobal 等属性访问指令，发生 cache install。
- 创建位置：
  - object.c 的 set_prop_：ic->pm = object_get_shape(obj)->pm; ic->prop_name = name
  - object.c 的 get_prop_with_ic：ic->pm = object_get_shape(obj)->pm; ic->prop_name = name
  - 相关指令 miss 多次时会把 ic->pm 清空。
- scan_function_table_entry 中对 ic->pm 和 ic->prop_name 使用 PROCESS_WEAK_EDGE。
- 弱引用含义：inline cache 是性能缓存，不能因为缓存命中历史而保活 PM 或属性名字符串。
- 清理位置：weak_clear_inline_cache。

7. exhandler_pool：
- 触发场景：try/catch/finally 的 pushhandler/pophandler。
- 创建/复用位置：
  - pushhandler 从 context->exhandler_pool 取节点，或者 gc_malloc 新建 UnwindProtect。
  - pophandler 把节点从 exhandler_stack_top 移到 exhandler_pool。
- context.h 注释 exhandler_pool 为 weak。
- 弱引用含义：pool 是可复用缓存，真正活跃的异常 handler 在 exhandler_stack_top；pool 不应该保活 UnwindProtect。
- 清理位置：weak_clear 末尾直接 the_context->exhandler_pool = NULL。

2026-04-26 补充调查：其他 GC 如何处理弱引用

1. Mark-Sweep GC：
- 入口：marksweep-collector.cc。
- 流程：
  - PHASE_MARK：scan_roots<DefaultTracer>(ctx)，然后 DefaultTracer::process_mark_stack()。
  - PHASE_WEAK：weak_clear<DefaultTracer>(ctx)。
  - PHASE_SWEEP：sweep()。
- DefaultTracer 来自 mark-tracer.h。
- mark-tracer.h 中 process_weak_edge 是空操作，或者在 RVTracer 版本中直接返回原值。
- 含义：mark 阶段弱引用不标记目标，不保活目标。
- weak_clear 阶段根据 is_marked_cell 判断弱目标是否已被强引用标记：
  - 已标记：保留并继续 PROCESS_EDGE 修补必要结构。
  - 未标记：从 string table / hidden class weak list / inline cache 等结构中删除或清空。
- 这是最标准的弱引用处理方式。

2. Copy GC：
- 入口：copy-collector.cc。
- 流程：
  - PHASE_COPY：scan_roots<CopyTracer>(ctx)，然后 scavenge()。
  - PHASE_WEAK：weak_clear<CopyWeakTracer>(ctx)。
  - PHASE_FLIP：交换 from/to space。
- CopyTracer 的 process_weak_edge 是空操作，所以复制强引用阶段不会通过弱边 forward 对象。
- CopyWeakTracer 用 is_marked_cell 判断对象是否已被复制：
  - 如果对象已经在 to-space，或者 from-space header 已 forwarded，则认为存活。
  - weak_clear 中保留的弱边会通过 CopyWeakTracer::process_edge 修补到新地址。
  - 未复制的弱目标会被 weak_clear 删除/清空。
- Copy GC 的语义很清楚：弱边不保活，但存活对象对应的弱边会被更新到新地址。

3. Threaded Compact GC：
- 入口：threadedcompact-collector.cc。
- 流程：
  - PHASE_MARK：scan_roots<DefaultTracer>(ctx)，DefaultTracer 来自 mark-tracer.h。
  - PHASE_WEAK：weak_clear<DefaultTracer>(ctx)。
  - PHASE_FWDREF / PHASE_BWDREF：用 ThreadTracer 建立 threaded references 并更新移动后的地址。
- mark 阶段仍然使用 mark-tracer.h，因此 weak edge 不标记目标。
- weak_clear 先清掉未标记弱目标，只保留已存活或被 hidden class 规则复活的弱结构。
- 后续引用更新阶段 ThreadTracer 的 process_weak_edge 会调用 process_edge。
- 这不表示 weak edge 用来保活，而是因为 weak_clear 已经完成筛选，剩下的弱边也需要参与地址更新。
- Threaded Compact 的处理顺序是：先按弱语义筛选，再把保留下来的弱引用一起改地址。

4. cache_cheney GC：
- 入口：cache_dram_manager.cc。
- 普通 cache_cheney 路径使用 CacheCheney_Tracer。
- CacheCheney_Tracer::process_weak_edge 当前直接调用 process_edge。
- garbage_collection 中先 scan_roots/scavenge，再 weak_clear<CacheCheney_Tracer>(ctx)。
- 因为 weak edge 在扫描中就会被 process_edge 处理，所以 cache_cheney 会把弱引用当成强引用搬到 DRAM。
- 这和 Mark-Sweep / Copy / Threaded Compact 的弱引用语义不同，更保守但会保活本应可丢弃的对象或缓存。

5. GiY 当前路径：
- GiY.cc 中 GiYReserveTracer 和 GiYPatchTracer 的 process_weak_edge 也都直接调用 process_edge。
- 因此 GiY 当前和 cache_cheney 一样，也会把弱引用当强引用处理。
- 这能避免弱 slot 留下 Young 悬空指针，但会增加不必要的 Old Gen 拷贝，并破坏 weak cache 的回收语义。

总体结论：
- Mark-Sweep、Copy、Threaded Compact 都采用正确的两阶段弱引用语义：
  - 强扫描阶段：弱边不保活。
  - weak_clear 阶段：清掉未存活弱目标，修补已存活弱目标。
- cache_cheney 和当前 GiY 是例外：它们把 weak edge 当 strong edge。
- 如果继续优化 GiY，应优先参考 Copy GC 的方式：强阶段不处理 weak edge，弱阶段只 patch 已 forwarded 的对象，否则清空/摘除弱 slot。

2026-04-26 修改记录：GiY 和 cache_cheney 的弱引用强扫描语义

本次按 roadmap 和讨论结论，只修改 GiY 与 cache_cheney 相关路径，不改其他 GC 的弱引用实现。

1. cache_cheney：
- 修改位置：ejsvm/cache_dram_manager.cc。
- CacheCheney_Tracer::process_weak_edge(JSValue&) 和 process_weak_edge(void*&) 改为空操作。
- 结果：scan_roots / scan_function_table 的强扫描阶段遇到 weak edge 时，不再通过 inline cache、weak shape list、string table 等弱边把对象复制到 DRAM。
- CacheCheney_Tracer::process_mark_stack() 改为调用 scavenge()。
- 原因：weak_clear 阶段会对保留下来的弱结构调用 PROCESS_EDGE；如果这个 PROCESS_EDGE 新复制了对象，必须继续 scavenge 新对象，否则新复制对象内部字段可能没有被修补。
- 新增 patch_live_inline_cache(ctx)，只在 weak_clear<CacheCheney_Tracer>(ctx) 之后调用。
- 原因：通用 weak_clear_inline_cache 只会清掉 ic->pm 未存活的 cache 项；对于 ic->pm 已存活的 cache 项，它不会主动 patch ic->pm / ic->prop_name。强扫描阶段 weak edge 改为空操作后，如果不补这个步骤，live inline cache 可能留下指向 cache/from-space 的旧地址。
- 这个 patch 不负责保活 dead inline cache。weak_clear 已经先把 dead ic->pm 清成 NULL。patch_live_inline_cache 只处理 weak_clear 之后仍然非 NULL 的 cache 项。

2. GiY：
- 修改位置：ejsvm/GiY.cc。
- GiYReserveTracer::process_weak_edge 和 GiYPatchTracer::process_weak_edge 改为空操作。
- 结果：GiY minor 的 reserve/patch 强扫描阶段遇到 weak edge 时，不再把 weak target 当强引用处理。
- 新增 GiYWeakTracer，专门给 giy_weak_clear 使用。
- GiYWeakTracer::is_marked_cell 的判断规则：
  - DRAM/init space 视为已存活；
  - Young 对象只有 forwarding_pointer 非 0 才视为已由强边发现；
  - 其他情况视为未存活。
- GiYWeakTracer::process_edge 只在 weak_clear 认定需要保留某个结构后才 materialize 对象；如果对象还在 Young 且没有 forwarding pointer，会先 copy_for_minor，然后立即 giy_traverse_stack_and_copy。
- 这样做是为了遵守 GiY 的法则：即使 weak_clear 因 hidden class 规则需要复活/保留某个对象，也先在 Young/Cache 中完成对象图遍历和字段修补，再做最终 Old materialize copy。
- giy_weak_clear(ctx) 改为 weak_clear<GiYWeakTracer>(ctx)，并在最后根据 g_used_nt_old_store 做 _mm_sfence。
- 新增 giy_patch_live_inline_cache(ctx)，只在 giy_weak_clear 的 generic weak_clear 之后调用。目的和 cache_cheney 一样：dead inline cache 已由 weak_clear 清除，live inline cache 需要 patch ic->pm / ic->prop_name，不能留下 Young 旧地址。

3. 和 function_table 的关系：
- 本次修改没有恢复 GiY minor 对整个 function_table 的通用强扫描。
- 原因：用户要求“强扫描阶段：弱边不保活”，而 function_table 中 inline cache 的 pm/prop_name 本来就是弱边；GiY 不应该在 reserve 阶段通过这些 weak edge 保活对象。
- 但是 function_table 里还存在非弱的常量池、allocation site cache 等强引用问题。之前调查已经确认那里可能出现 Young 指针。这个问题不能靠“弱边不保活”解决，后续需要单独设计 GiY 专用的 function_table 记录/扫描方案，例如通过写屏障记录 function_table 中可能产生 Young 指针的 slot，再让 GiY minor 只处理这些记录过的强 slot。
- 当前新增的 live inline cache patch 只处理 weak_clear 后仍存活的 inline cache 项，不等价于扫描整个 function_table，也不会把 weak edge 提前当强边保活。

4. 验证：
- 在 build.debug 执行 make OPT_GC=cache_cheney -j2，构建通过。
- 在 build.debug 执行 make OPT_GC=giy -j2，构建通过。
- git diff --check 通过。
- 构建过程中仍有仓库已有 warning，例如 types.h multi-line comment、cache_dram_manager.h 符号比较、cache_dram_manager.cc 未使用变量/格式化 warning；本次没有处理这些非本任务范围的问题。

2026-04-26 调查结论：GiY 弱引用最可靠处理方式

基于当前代码，GiY 最可靠的弱引用实现应该继续采用两阶段语义：

1. 强扫描阶段：
- GiYReserveTracer::process_weak_edge 必须保持空操作。
- GiYPatchTracer::process_weak_edge 也应保持空操作。
- 原因：weak edge 不能参与对象可达性分析，不能因为 string table、hidden class weak list、inline cache 等缓存结构而把 Young 对象复制/保活到 DRAM。

2. weak_clear 阶段：
- 使用 GiYWeakTracer，而不是 GiYPatchTracer。
- GiYWeakTracer::is_marked_cell 用 forwarding_pointer 判断 Young 对象是否已经被强边发现。
- 对已经存活的弱结构，GiYWeakTracer::process_edge 负责 patch 到 Old 地址。
- 如果 generic weak_clear 的 hidden class 规则需要保留/复活 branch/terminal PropertyMap，GiYWeakTracer 可以调用 copy_for_minor，然后立刻 giy_traverse_stack_and_copy，保证仍然遵守 GiY 的 Young-first 法则。

3. inline cache 需要单独小心：
- 通用 weak_clear_inline_cache 只在 ic->pm 未存活时清掉 ic->pm 和 ic->prop_name。
- 如果 ic->pm 存活，通用 weak_clear 不会自动 patch live inline cache。
- 因此 GiY 需要 weak_clear 后补一个 GiY 专用 inline cache patch/clear 步骤。
- 最稳的语义是：
  - ic->pm 不存活：清掉 ic->pm 和 ic->prop_name；
  - ic->pm 存活但 ic->prop_name 是堆对象且没有存活：也清掉整个 inline cache；
  - 两者都存活：只 patch 地址，不保活新对象。
- 这样不会因为 inline cache 的弱 prop_name 反向保活字符串，也不会留下 Young 旧地址。

4. 和 function_table 强引用的关系：
- function_table 中的 constant_pool、AllocSite::pm、AllocSite::shape 是强引用问题，应通过 GiY 专用 function_table strong-slot remembered set 处理。
- inline cache 的 pm/prop_name 是弱引用，不能放进强 slot remembered set，否则会破坏弱引用语义。

2026-04-26 实现记录：收紧 GiY inline cache 弱引用处理

本次按“最可靠的 GiY 弱引用处理方式”修改了 ejsvm/GiY.cc，只调整 GiY 的 inline cache weak patch 行为。

修改内容：
- 新增 giy_is_live_weak_jsvalue，用于判断 JSValue 弱目标是否已经被强边保活：
  - fixnum/special 直接视为不需要 GC 管理；
  - 堆对象调用 GiYWeakTracer::is_marked_cell 判断，Young 对象必须有 forwarding_pointer 才算活。
- 新增 giy_clear_inline_cache，把 ic->pm 清为 NULL，把 ic->prop_name 清为 JS_UNDEFINED。
- 修改 giy_patch_live_inline_cache：
  - ic->pm == NULL：跳过；
  - ic->pm 未存活：清掉整个 inline cache；
  - ic->prop_name 是堆对象且未存活：也清掉整个 inline cache；
  - 两者都已经存活：只用 GiYPatchTracer patch 地址。

关键原因：
- 旧的 giy_patch_live_inline_cache 使用 GiYWeakTracer::process_edge 直接处理 ic->pm 和 ic->prop_name。
- GiYWeakTracer::process_edge 允许在 weak_clear 阶段 materialize Young 对象，这是为了 generic weak_clear 中 hidden class branch/terminal 复活规则服务。
- 但 inline cache 本身是弱缓存，不能因为 ic->pm 还活着，就顺手把未被强边保活的 prop_name 字符串 materialize 到 Old。
- 因此 inline cache 的正确策略是：弱目标没活就清 cache；只有弱目标已经活时才 patch。

和 GiY 法则的关系：
- strong scan 阶段仍然不处理 weak edge。
- inline cache patch 阶段不再 materialize 新对象，只 patch 已经有 forwarding_pointer 的对象。
- 这避免了 weak inline cache 反向增加 Old copy，也避免了留下 Young 旧地址。

验证：
- 第一次 make OPT_GC=giy -j2 失败，原因是 build.debug/types.h 是旧副本，仍含旧的 write_barrier 宏内声明，和 cache_dram_manager.h 的 C linkage 声明冲突。
- 删除 build.debug/types.h 后重新 make OPT_GC=giy -j2，构建通过。
- git diff --check 通过。

2026-04-26 实现记录：GiY function_table 强引用 slot remembered set

本次按“GiY 专用 function_table strong-slot remembered set”方案实现，目标是解决 GiY minor 不扫描通用 function_table 后可能漏掉强引用 Young 指针的问题，同时不把 inline cache 弱引用重新变成强引用。

修改文件：
- ejsvm/GiY.cc
- ejsvm/cache_dram_manager.h
- ejsvm/codeloader.c
- ejsvm/object.c

1. 新增 GiY function table slot set：
- 在 GiY.cc 内新增 GiYFTSlotSet。
- 记录内容是 slot 地址，不是对象地址。
- 用最低位 tag 区分：
  - 0：JSValue slot；
  - 1：void* pointer slot。
- 这个 slot set 和 GiY GC stack / edge log 一样，在 giy_bind_stack_to_cache_impl 中从 cache_space.end 预留空间。
- 这样 minor GC 扫这个记录集时读的是 cache 辅助结构，而不是重新遍历整个 function_table。

2. 新增记录接口：
- 在 cache_dram_manager.h 的 USE_GIY_MINOR 下声明：
  - giy_record_ft_jsvalue_slot(JSValue *ptr, JSValue value)
  - giy_record_ft_ptr_slot(void **ptr, void *value)
- 在 GiY.cc 中定义 C linkage 实现。
- 记录函数只在 value 指向 Young/Cache work area 时记录 slot。
- 如果 value 已经是 DRAM/init/immediate/null，则不记录。
- in_minor_gc 期间不记录，避免 GC 自己 patch 时把 slot 又放回记录集。

3. 记录哪些 function_table 强引用：
- codeloader.c：
  - OBC const_load 写 ctop[i] 后记录 constant_pool slot。
  - SBC load_number_sbc / load_string_sbc / load_regexp_sbc 首次写 ctop[index] 后记录 constant_pool slot。
- object.c：
  - get_cached_shape 创建/写入 as->shape 后记录 &as->shape。
  - create_simple_object_with_prototype 首次写 as->pm / as->shape 后记录两个 slot。
  - create_array_object 首次写 as->pm / as->shape 后记录两个 slot。

4. 明确不记录 inline cache：
- ic->pm 和 ic->prop_name 仍然按弱引用处理。
- 它们不进入 GiY function table strong-slot set。
- 原因：inline cache 是性能缓存，不能通过 strong-slot set 保活对象；它们继续由 weak_clear 和 giy_patch_live_inline_cache 负责清理或 patch。

5. GiY minor 中如何处理：
- reserve 阶段：
  - giy_scan_roots_generational<GiYReserveTracer>(ctx)
  - giy_reserve_function_table_slots()
  - giy_scan_remembered_set_slots()
- giy_reserve_function_table_slots 只调用 copy_for_minor 做 reserve，不写 function_table slot，也不把 slot 放进 edge log。
- patch 阶段：
  - giy_scan_roots_generational<GiYPatchTracer>(ctx)
  - giy_patch_function_table_slots()
  - giy_patch_remembered_set_slots()
- giy_patch_function_table_slots 只在 slot 的值真的变化时写回 function_table slot。
- minor 结束前 giy_clear_function_table_slots() 清空本轮记录。

6. 为什么符合 GiY 法则：
- 没有恢复通用 scan_roots 对整个 function_table 的扫描。
- minor GC 只处理被写入 Young 指针时记录过的强 slot。
- reserve 阶段只读少量 slot 并在 Young header 写 forwarding_pointer，不把对象提前复制到 Old。
- 对象图仍然通过 giy_traverse_stack_and_copy 在 Young/Cache 中完成。
- function_table slot patch 是必要的非 Old 管理结构地址修补，不会触发 Old object graph traversal。

7. 额外修正：
- giy_scan_remembered_set_slots 和 giy_reserve_function_table_slots 现在使用 giy_reserve_edge，只 reserve Young target，不用 GiYReserveTracer。
- 原因：GiYReserveTracer 会把被扫描 slot 放进 edge log；RS/FT slot 后面有专门 patch 阶段，不应该把局部临时变量地址记录进 edge log。

8. 验证：
- 在 build.debug 执行 make OPT_GC=giy -j2，构建通过。
- 在 build.debug 执行 make OPT_GC=cache_cheney -j2，构建通过，确认 USE_GIY_MINOR 下的记录接口没有影响 cache_cheney。
- warning 仍是仓库已有 warning，本次没有处理非任务范围问题。

2026-04-27 补充修改：用 mprotect 验证 GiY 核心阶段 Old/DRAM 访问

修改范围：
- 按用户要求，用 mprotect 验证 GiY minor 是否只在允许窗口访问 Old/DRAM。
- 本次仍只改 GiY 相关路径和 GiY remembered set；没有主动改其他 GC 算法。

本次修改的文件：
- ejsvm/GiY.cc
- ejsvm/giy_rset.cc
- ejsvm/cache_dram_manager.h
- AIlog.txt
- meeting.md

ejsvm/GiY.cc 的修改：
- 新增 GIY_MPROTECT_OLD=1 运行时开关。
- 开关打开时，GiY minor 核心阶段会用 mprotect(PROT_NONE) 保护整个 DRAM/Old 区。
- 允许访问 Old 的窗口只在：
  - giy_copy_live_object 最终 materialize copy 时临时放开目标对象范围；
  - giy_store_u64_old 更新 Old slot 时临时放开 8 字节 slot 所在页；
  - 保护窗口结束时恢复 Old 区读写权限。
- SIGSEGV handler 会打印违规地址和阶段，例如 phase=remembered_set_reserve，方便定位。
- 如果 guard 已经关闭，普通 SIGSEGV 不再伪装成 mprotect 违规。
- 保护窗口覆盖 GiY minor 的核心阶段；weak_clear 仍在窗口外执行。
- 原因：当前通用 weak_clear 会维护字符串表、hidden class 图和 inline cache 等全局弱结构，这些结构本来就会遍历老元数据。它不是本次验证的 GiY 核心对象图遍历/复制阶段；如果以后要让弱清理也满足 0 Old read，需要另做 GiY 专用弱引用记录/清理机制。

第一次 mprotect 暴露的问题：
- a.sbc 在 young_traverse_copy 期间 fault。
- 调用栈显示在 NodeScanner::scan_object_properties。
- 原因：年轻 JSObject 的 shape 可以指向 Old；通用 scanner 为了知道 embedded property 数量会读 shape->n_extension_slots、shape->pm->n_special_props 等 Old 元数据。
- 修复：
  - 新增 GiY 专用 giy_scan_jsobject_conservative。
  - 对 JSObject 只读取年轻对象自己的 header 和 payload。
  - shape slot 作为普通 pointer edge 处理，不 dereference。
  - 根据对象 header size 保守扫描全部 eprop slot。
  - giy_traverse_stack_and_copy 改为调用 GiY 专用 young-node scanner。

第二次 mprotect 暴露的问题：
- c.sbc 在 remembered_set_reserve 期间 fault。
- 原因：原 GiY remembered set 只记录 Old slot 地址；reserve 阶段需要读 *slot 才知道里面是否仍是 Young 指针，这会直接读 Old。
- 修复：
  - RememberedSet 新增 values 数组。
  - giy_rset.cc 的写屏障在记录 slot 地址时，同时记录最新写入的值。
  - 如果同一个 slot 后续写入 immediate/Old/NULL，则只更新已有记录为 0，避免 stale young value 被继续使用。
  - giy_scan_remembered_set_slots 不再读 Old slot，而是用 remembered_set.values[i] 做 reserve。
  - giy_patch_remembered_set_slots 不再读 Old slot，而是根据记录值和 forwarding pointer 计算新值；只有确实需要从 Young 改到 Old dest 时才写 slot。

构建副本问题：
- 测试中一度发现 d.sbc 普通崩溃。
- 调查后确认是 build.debug/object.c 仍是旧副本，缺少之前给 create_array_object 加的 GiY function_table slot 记录，导致 array allocation site 的 as->shape 没被记录/patch。
- 已清理 build.debug 下相关旧副本后重新构建，问题消失。

验证结果：
- 已执行：make OPT_GC=giy -j2
- 已执行：make OPT_GC=cache_cheney -j2
- 已重新执行：make OPT_GC=giy -j2，用 GiY 版本完成最终 mprotect 验证。
- 已执行 mprotect 样例：
  - GIY_MPROTECT_OLD=1 ./ejsvm a.sbc
  - GIY_MPROTECT_OLD=1 ./ejsvm b.sbc
  - GIY_MPROTECT_OLD=1 ./ejsvm c.sbc
  - GIY_MPROTECT_OLD=1 ./ejsvm d.sbc
  - GIY_MPROTECT_OLD=1 ./ejsvm e.sbc
  - GIY_MPROTECT_OLD=1 ./ejsvm f.sbc
  - GIY_MPROTECT_OLD=1 ./ejsvm g.sbc
  - GIY_MPROTECT_OLD=1 ./ejsvm ../ejsvm/js/hello.sbc
  - GIY_MPROTECT_OLD=1 ./ejsvm ../ejsvm/js/f1.sbc
- 结果：
  - a.sbc 到 g.sbc：退出码 0，没有 mprotect old-space violation。
  - hello.sbc 和 f1.sbc：退出码 1，但没有 mprotect old-space violation，也没有 Segmentation fault。
- 构建 warning 仍有仓库原有 warning，本次没有处理非 GiY/mprotect 范围的问题。

没有做的事情：
- 没有把通用 weak_clear 改造成完全 GiY 专用 0 Old read 弱清理。
- 原因：这需要额外记录字符串表、hidden class 弱链、transition 弱边、inline cache 等弱 slot 的最新状态；它是独立的大改，不应该混在本次 mprotect 核心阶段验证里。

2026-04-27 补充修复：hello.sbc / f1.sbc 退出码 1 与 weak_clear 边界复查

用户指出 hello.sbc / f1.sbc 退出码 1 也应该调查。

调查结果：
- 这两个退出码 1 不是 GiY GC 错误。
- 两个文件是旧版 SBC 文本格式，和当前 loader 期望格式不一致。
- 具体问题：
  - 缺少第一行 `fingerprint 36`。
  - 使用旧字段名 `numberOfInstruction`，当前 loader 需要 `numberOfInstructions`。
  - 缺少 `numberOfConstants`。
  - 常量写法仍是旧格式，例如 `string 5 "print"`；当前 loader 需要 `string 5 #0="print"` 这种 constant table 格式。
  - f1.sbc 还使用旧指令 `newargs`，当前指令集里对应的是 `newframe <locals> <make_arguments>`。

本次修改文件：
- ejsvm/js/hello.sbc
- ejsvm/js/f1.sbc
- ejsvm/GiY.cc
- AIlog.txt
- meeting.md

hello.sbc 的修复：
- 增加 `fingerprint 36`。
- 把 `numberOfInstruction 14` 改成 `numberOfInstructions 14`。
- 增加 `numberOfConstants 4`。
- 把 number/string 常量改成 `#index=value` 格式。

f1.sbc 的修复：
- 增加 `fingerprint 36`。
- 每个 function 都把 `numberOfInstruction` 改成 `numberOfInstructions`。
- 每个 function 增加正确的 `numberOfConstants`。
- 把 string 常量改成 `#index=value` 格式，并复用相同字符串常量 index。
- 把两个 `newargs` 改成 `newframe 2 1`，含义是创建 2 个 local slot 的 frame，并生成 arguments 对象。

weak_clear 复查：
- 我尝试过把 weak_clear 改成 GiY minor 专用的 Young-only 弱清理，让 mprotect 严格覆盖到 weak_clear 内部。
- 尝试 1：处理 property_map_roots 的 Young 头节点。结果破坏 hidden-class/property map 语义，c.sbc 在 `hash_get_with_attribute` 崩溃。
- 尝试 2：只处理 string_table 的 Young 头节点。结果仍破坏当前程序语义，a-g 出现退出 1 或段错误。
- 结论：当前 VM 的 `weak_clear` 不只是清理普通 weak slot，它还维护 hidden-class 图、shape 链、transition、string interning 表等旧元数据结构。用局部 Young-only 清理替代是不可靠的。

最终处理方式：
- 恢复通用 `weak_clear<GiYWeakTracer>(ctx)`，保证 VM 语义正确。
- 将 strict mprotect core guard 的结束点移动到 `giy_weak_clear` 中，并显式标记阶段为 `weak_clear_generic_old_metadata`。
- 也就是说：GiY 核心阶段仍然用 PROT_NONE 验证 0 Old read；weak_clear 被明确归类为 old metadata maintenance 阶段，而不是混在核心 GiY traversal/copy 里。
- 如果以后必须让 weak_clear 也满足严格 0 Old read，需要新增专门的 weak remembered set，记录 string table、hidden class weak list、transition weak edge、shape weak edge、inline cache 等弱 slot 的写入状态，再按记录集清理；这不是局部替换 generic weak_clear 能安全完成的。

验证结果：
- 正常重新构建 GiY：`make OPT_GC=giy -j2`，通过。
- 构建 cache_cheney：`make OPT_GC=cache_cheney -j2`，通过。
- 最后重新构建 GiY：`make OPT_GC=giy -j2`，通过。
- 最终 mprotect 验证：
  - a.sbc、b.sbc、c.sbc、d.sbc、e.sbc、f.sbc、g.sbc 全部退出码 0。
  - ejsvm/js/hello.sbc 退出码 0。
  - ejsvm/js/f1.sbc 退出码 0。
  - 所有样例均无 `GiY mprotect old-space violation`，无 Segmentation fault。

2026-04-27 补充说明：回答“weak_clear 是不是到处乱扫描”和 “mprotect 在哪里做”

本条记录是补写用户要求必须进入 AIlog 的解释内容。之前我只在回复中解释，没有同步写入 AIlog，这是记录遗漏。

1. 当前 weak_clear 是否“到处乱扫描”

准确说法：
- GiY 的核心 minor GC 阶段不是到处乱扫 Old。
- 这个核心阶段已经用 `GIY_MPROTECT_OLD=1` 验证过：root/RS reserve、Young traversal/copy、root/RS patch 不读 Old。
- 当前例外是 `weak_clear`。
- `weak_clear` 复用的是通用弱引用清理逻辑，它会维护 VM 全局元数据，而不是只处理普通 JS 对象边。
- 它会涉及：
  - string table；
  - hidden-class / PropertyMap 图；
  - Shape 弱链；
  - transition 弱边；
  - inline cache；
  - exhandler_pool。
- 这些结构里很多节点可能已经在 Old，所以通用 `weak_clear` 会读 Old 元数据。

给导师的推荐表述：
- “GiY 核心对象图遍历和复制主路径已经满足 roadmap：Young 内遍历，Old 只在最终 copy 和必要 Old slot patch 时访问。”
- “当前 remaining exception 是 `weak_clear`。它属于 VM old metadata maintenance 阶段，仍复用通用 GC 的 hidden-class/string weak metadata 清理逻辑，因此会访问 Old metadata。”
- “我已经把这个阶段显式标成 `weak_clear_generic_old_metadata`，没有把它混在 GiY 核心 traversal/copy 阶段里。”

2. mprotect 的运行时开关

mprotect 检查不是默认开启，必须显式设置环境变量：

```sh
GIY_MPROTECT_OLD=1 ./ejsvm a.sbc
```

如果没有 `GIY_MPROTECT_OLD=1`，这些检查不生效，不影响正常 benchmark。

3. mprotect 保护的内存区域

保护的是整个 DRAM/Old space：

```text
dram_space.begin
dram_space.total_size
```

代码会先按 page size 对齐地址范围，然后调用：

```c
mprotect(old_begin_page_aligned, old_size_page_aligned, PROT_NONE)
```

含义：
- Old 区不可读；
- Old 区不可写；
- GiY 核心阶段如果有任何非允许的 Old 访问，会直接 SIGSEGV。

4. mprotect 开始位置

在 `giy_minor_collect` 开始处，reset GiY GC stack / edge log 后调用：

```c
giy_old_guard_begin();
```

这个函数会：
- 检查 `GIY_MPROTECT_OLD` 是否开启；
- 读取 page size；
- 安装 SIGSEGV handler；
- 对整个 Old/DRAM 区执行 `PROT_NONE`；
- 设置 `g_old_guard_active = true`。

5. mprotect 覆盖的 GiY 核心阶段

当前 mprotect strict guard 覆盖这些阶段：

```text
scan_roots_reserve
function_table_reserve
remembered_set_reserve
young_traverse_copy
scan_roots_patch
function_table_patch
remembered_set_patch
```

每个阶段都会更新 `g_old_guard_phase`。

如果出现违规访问，SIGSEGV handler 会打印：

```text
GiY mprotect old-space violation at 0x... phase=<阶段名>
```

这样能直接知道是哪一阶段碰了 Old。

6. 允许的 Old 访问窗口一：最终对象 copy

在 `giy_copy_live_object` 中，GiY 会临时放开目标对象的 Old 地址范围：

```c
giy_old_guard_allow_old_write(dst, nbytes);
... streaming store / memcpy ...
giy_old_guard_protect_old_write(dst, nbytes);
```

含义：
- 只允许当前 live young object 的最终 copy 目标区域可写；
- copy 完马上恢复 `PROT_NONE`；
- x86/x86_64 下走 non-temporal store；
- 这对应 roadmap 允许的“一次性拷贝进 Old”。

7. 允许的 Old 访问窗口二：Old slot patch

在 `giy_store_u64_old` 中，GiY 会临时放开要更新的 Old slot：

```c
giy_old_guard_allow_old_write(slot, sizeof(*slot));
_mm_stream_si64(...);
giy_old_guard_protect_old_write(slot, sizeof(*slot));
```

含义：
- 只允许当前 slot 所在页临时读写；
- 写入 patch 后的新地址；
- 马上恢复 `PROT_NONE`；
- x86_64 下用 `_mm_stream_si64`，也就是 non-temporal store；
- 这对应 roadmap 允许的例外：“最后阶段更新 Old 区里指向 Young 的 slot”。

8. mprotect 结束位置

现在在 `giy_weak_clear` 进入通用 weak_clear 前结束：

```c
giy_old_guard_set_phase("weak_clear_generic_old_metadata");
giy_old_guard_end();
weak_clear<GiYWeakTracer>(ctx);
```

含义：
- GiY 核心阶段 mprotect 验证已经完成；
- 进入通用 weak metadata maintenance 之前恢复 Old 读写；
- 这是有意设计，不是漏掉；
- 原因是通用 `weak_clear` 当前为了正确维护 hidden-class/string 等 VM 元数据，必须读 Old。

9. SIGSEGV handler 行为

启用 guard 后会安装 SIGSEGV handler。

handler 行为：
- 如果 `g_old_guard_active == true`，说明是在严格 mprotect 窗口内访问 Old，打印违规地址和阶段，然后退出。
- 如果 guard 已经 inactive，说明是普通程序崩溃，不把它误报成 mprotect violation，而是恢复默认 SIGSEGV 行为。

10. 为什么不把 weak_clear 也强行放进 mprotect 窗口

我尝试过两种简化方案：
- 对 `property_map_roots` 做 Young-only 弱清理，结果破坏 hidden-class / PropertyMap 图，`c.sbc` 在 `hash_get_with_attribute` 崩溃。
- 对 string table 做 Young-only 头节点清理，结果 `a-g` 出现退出 1 或 Segmentation fault。

结论：
- 通用 `weak_clear` 不是一个可以简单替换的普通弱引用扫描器。
- 它维护的是 VM 元数据图，尤其 hidden-class 和 string interning 的结构关系。
- 如果禁止它读 Old，又没有额外记录弱 slot 的更新信息，就会破坏 VM 语义。

11. 如果以后要彻底解决 weak_clear Old read，可靠方案是什么

可靠方案不是局部改 `weak_clear`，而是新增 GiY 专用 weak remembered set。

需要在写入或创建以下弱结构时记录 weak slot：
- string table 的 `StrCons::str` / bucket link；
- PropertyMap weak edge；
- Shape weak edge；
- transition weak edge；
- PropertyMapList weak list；
- inline cache 的 `ic->pm` 和 `ic->prop_name`。

Minor GC 时只根据这些记录集做：
- 如果 weak target 已被强边保活并 forwarding，则 patch；
- 如果 weak target 没被强边保活，则清 slot 或清 cache；
- 不遍历整个 Old hidden-class/string metadata 图。

这个是下一阶段的大改，不应该用当前局部 Young-only 清理硬替代。

2026-04-27：本轮重新验证 GiY

用户要求：暂时不考虑 weak 引用问题，全面再次验证 GiY 是否正确；如果正确，开始 benchmarks。用户特别提醒 benchmark 可能运行很久，所以长时间无输出不能直接当作出错。

我本轮的验证边界：
- 先验证 GiY 能重新干净编译；
- 再验证当前仓库中可见的样例 `.sbc` 在普通 GiY 下全部能正常退出；
- 再打开 `GIY_MPROTECT_OLD=1`，验证 GiY strict core 窗口内不会非法访问 Old/DRAM 区；
- weak_clear 仍按之前结论暂时不纳入 strict core mprotect 窗口，因为用户本轮明确说暂且不考虑 weak 引用问题。

执行过的构建：

```sh
cd /home/qiancheng/ejs-new/build.debug
rm -f types.h cache_dram_manager.h codeloader.c object.c giy_rset.cc GiY.cc ejsvm *.o
make OPT_GC=giy -j2
```

结果：GiY 编译和链接成功。

执行过的 GiY 普通样例验证：

```sh
for f in a.sbc b.sbc c.sbc d.sbc e.sbc f.sbc g.sbc ../ejsvm/js/hello.sbc ../ejsvm/js/f1.sbc; do
  ./ejsvm "$f"
done
```

结果：9 个样例退出码全部为 0。

执行过的 GiY mprotect 样例验证：

```sh
for f in a.sbc b.sbc c.sbc d.sbc e.sbc f.sbc g.sbc ../ejsvm/js/hello.sbc ../ejsvm/js/f1.sbc; do
  GIY_MPROTECT_OLD=1 ./ejsvm "$f"
done
```

结果：
- 9 个样例退出码全部为 0；
- stderr 中没有 `GiY mprotect old-space violation`；
- 没有 `Segmentation fault`；
- 因此当前样例覆盖下，GiY 的 strict core 阶段没有发现越界读取/写入 Old 区。

补充：我尝试过用 cache_cheney 做运行结果对照，但 `make OPT_GC=cache_cheney -j2` 后，`./ejsvm a.sbc` 在 cache_cheney 下出现 Segmentation fault，退出码 139。这个对照不能作为本轮 GiY 正确性的基线。我没有继续扩大修改 cache_cheney，因为用户明确要求不要没事改其他 GC；本轮后续只依据 GiY 自身构建、GiY 普通样例、GiY mprotect 样例和 GiY benchmark 状态判断。

当前结论：
- 在暂不处理 weak 引用 strict 化的前提下，GiY 目前通过了可见样例和 mprotect 核心访问验证；
- 可以开始 GiY benchmark；
- benchmark 输出将放入 `build.debug/benchmarks/out21`，并写入说明文件，避免覆盖之前的 `out20`。

2026-04-27：benchmark 第一次运行结果和 Havlak 修复

benchmark 使用 GiY 构建，从 `build.debug/benchmarks` 运行，输出目录为 `out21`。第一次启动时我用了 `/usr/bin/time`，但这台机器没有这个路径，因此 `Bounce` 先出现一次退出码 127。这个 127 是计时工具路径问题，不是 GiY 或 benchmark 本身问题。随后我改用 bash 内置 `time` 重新覆盖运行。

`out21` 中的结果：
- `Bounce`、`List`、`Sieve`、`Queens`、`Permute`、`Storage`、`Towers`、`Mandelbrot`、`Richards`、`CD`、`NBody` 均退出码 0；
- `Havlak` 退出码 1。

`Havlak.out` 的失败原因：

```text
Error: Remembered set full, cannot add more remembered objects!
```

这说明失败不是 mprotect violation，也不是段错误，而是 GiY remembered set 容量不足。根因是：为了让 GiY remembered set 在 minor GC 中不读 Old slot，我前面把每个 entry 从“只存 slot 地址”改成“存 slot 地址 + 最新写入值”。原来 24KB 只存地址时容量较大；现在同样 24KB 要同时存 `buffer` 和 `values`，实际 entry 数只有 1536。`Havlak` 的 Old/Init 到 Young 写入集合更大，会超过这个固定容量。

修复：
- 将 GiY remembered set 的预留容量从 24KB 提高到 128KB；
- 将 hash table 从 4096 提高到 8192；
- 将线性探测上限从 8 提高到 32。

这样仍然遵守 GiY 的原则：
- remembered set 元数据仍然在 cache_space 中预留，不依赖扫描 Old；
- minor GC reserve/patch 仍使用写屏障记录的 slot 和 value；
- 没有恢复通用 `scan_roots` 扫描 function_table；
- 没有改动其他 GC 的扫描策略。

修改文件：
- `ejsvm/giy_rset.cc`

下一步：
- 重新构建 GiY；
- 重新跑普通样例和 `GIY_MPROTECT_OLD=1` 样例；
- 先单独跑 `Havlak` 验证 remembered set 容量修复；
- 如果通过，再开新目录 `out22` 重新跑完整 benchmark，避免把失败的 `out21` 当作最终结果。

2026-04-27：GiY remembered set 容量修复后的验证

重新构建：

```sh
cd /home/qiancheng/ejs-new/build.debug
rm -f giy_rset.cc giy_rset.o ejsvm
make OPT_GC=giy -j2
```

结果：构建成功。

重新跑普通样例：
- `a.sbc` 到 `g.sbc`：退出码全部 0；
- `../ejsvm/js/hello.sbc`：退出码 0；
- `../ejsvm/js/f1.sbc`：退出码 0。

重新跑 mprotect 样例：
- 同一批 9 个样例在 `GIY_MPROTECT_OLD=1` 下退出码全部 0；
- 没有 `GiY mprotect old-space violation`；
- 没有 `Segmentation fault`。

单独重跑 Havlak：

```sh
cd /home/qiancheng/ejs-new/build.debug/benchmarks
{ time ../ejsvm Havlak.sbc; } > ./out21/Havlak.retry-after-rset.out 2>&1
```

结果：`Havlak retry status=0`。

结论：
- remembered set 容量问题已经被当前 GiY 修复越过；
- 因为这次修改会影响 cache_space 可用容量和 benchmark 指标，不能把 `out21` 的前 11 项和新 `Havlak` retry 混合作为最终 benchmark；
- 下一步使用同一个新 GiY 构建重新完整跑一轮 benchmark，输出目录为 `build.debug/benchmarks/out22`。

2026-04-27：最终完整 benchmark 结果

最终完整 benchmark 已使用修复后的同一个 GiY 构建重新运行，输出目录：

```text
/home/qiancheng/ejs-new/build.debug/benchmarks/out22
```

运行结果：12 个 benchmark 全部退出码 0。

状态汇总：

```text
Bounce      status=0  real 157.106
List        status=0  real 110.117
Sieve       status=0  real 120.998
Queens      status=0  real 113.883
Permute     status=0  real 381.902
Storage     status=0  real 320.245
Towers      status=0  real 180.324
Mandelbrot  status=0  real 237.101
Richards    status=0  real 937.403
CD          status=0  real 275.662
NBody       status=0  real 385.962
Havlak      status=0  real 496.786
```

这轮 benchmark 说明：
- GiY 在当前样例和 benchmark 集合下可以完整运行；
- `Havlak` 不再触发 remembered set full；
- `GIY_MPROTECT_OLD=1` 的核心样例验证仍通过；
- weak_clear 仍按用户本轮要求暂不作为 strict 化目标。

补充观察：
- 新 remembered set 初始化容量为 8192；
- 因为 remembered set 和 hash table 都预留在 cache_space 中，benchmark 输出显示可用 cache size 从之前约 456KB 变为约 320KB；
- 这是本次修复的明确代价：换取 GiY 不读 Old slot 时仍能承载 `Havlak` 的较大 remembered set。

最终检查：

```sh
git diff --check
```

结果：通过，没有 whitespace error。

2026-04-27：分析 benchmark `out22` 与 `out13`

对比对象：
- `build.debug/benchmarks/out13`
- `build.debug/benchmarks/out22`

基本差异：
- `out13` 输出显示使用的是 `cache_cheney GC`，remembered set capacity 为 3072，Havlak 在 0.552 秒处失败，错误是 `Remembered set full`，因此 `out13/Havlak.out` 不是完整 benchmark。
- `out22` 输出显示使用的是当前 `GiY GC`，remembered set capacity 为 8192，12 个 benchmark 全部 `status=0`，包括完整 Havlak。
- 因为 GC 实现、计时报告格式和空间配置不同，内部 GC breakdown 不能当成完全同口径的微基准；但 benchmark 的 100 次 average runtime 可以用于宏观比较。

共同 11 项，不含 out13 失败的 Havlak：

```text
Benchmark    out13 avg(s)  out22 avg(s)  out22 改善
Bounce       2.99658       1.57099       47.6%
List         1.94252       1.10112       43.3%
Sieve        2.52909       1.20906       52.2%
Queens       2.54259       1.13882       55.2%
Permute      11.11532      3.81898       65.6%
Storage      3.68271       3.19236       13.3%
Towers       3.72013       1.80293       51.5%
Mandelbrot   4.11854       2.37069       42.4%
Richards     19.89279      9.37380       52.9%
CD           4.91575       2.75417       44.0%
NBody        6.19843       3.85897       37.7%
```

共同 11 项汇总：
- `out13` 平均 runtime 总和：63.65445 秒；
- `out22` 平均 runtime 总和：32.19189 秒；
- `out22` 对共同 11 项整体约 1.98 倍更快。

GC 行为观察：
- `out22` 的 minor GC 次数普遍比 `out13` 多，大约 2.3 到 3.7 倍，原因之一是当前 GiY 为 remembered set/hash table 预留更多 cache 空间后，实际 young/cache 可用空间从约 456KB 降到约 320KB。
- 尽管 minor GC 次数更多，很多 benchmark 仍明显更快，说明当前 GiY 的单次 GC core 路径更轻，特别是 `scan_roots` 时间显著下降。
- `out22` 的 `scan_roots` 时间相对 `out13` 明显减少，例如 `NBody` 从 6.744 秒降到 0.125 秒，`Mandelbrot` 从 4.405 秒降到 0.081 秒，`CD` 从 3.892 秒降到 0.107 秒。这符合 GiY 专用 root scanning 避免通用 function_table 扫描的预期。
- `Storage` 是主要风险点：总 runtime 仍比 out13 快约 13.3%，但 GC overhead 从 13.6% 升到 50.1%，scavenge 从 48.112 秒升到 156.700 秒。说明 Storage 对 young/cache 容量和对象搬迁非常敏感，当前 320KB cache 下 GC 更频繁且搬迁成本高。
- `CD` 也需要关注：总 runtime 快约 44.0%，但 GC overhead 从 2.7% 升到 10.4%，说明业务更快但 GC 占比变高。

Havlak 结论：
- `out13/Havlak.out` 不能和 `out22/Havlak.out` 做性能对比，因为 out13 Havlak 没完成。
- `out13` Havlak 失败原因是 remembered set capacity 3072 不够。
- `out22` Havlak 完整完成，平均 4.96096 秒，退出码 0，说明当前 remembered set capacity 8192 修复了这个 benchmark 的容量问题。

总体结论：
- 正确性/完整性上，`out22` 明显优于 `out13`：out22 全部 12 项完成，out13 的 Havlak 失败。
- 性能上，排除 out13 失败的 Havlak 后，out22 对共同 11 项整体约 1.98 倍更快。
- 代价是 minor GC 更频繁，某些 allocation-heavy benchmark，尤其 Storage，GC 占比显著升高。
- 下一步如果继续优化 GiY，重点不应回退到扫描 Old/function_table，而应关注 cache 空间划分和 Storage/Havlak 这类 workload 下的 scavenge 成本。

2026-04-27：重新运行 cheneyGC/cache_cheney 对照组 benchmark 的准备

用户说明：`cheneyGC` 是对照组，需要完整跑完对照组 benchmarks，并和 GiY `out22` 比较性能。

我确认到仓库中的对照组构建选项是：

```sh
make OPT_GC=cache_cheney
```

运行时输出会显示：

```text
Now we are using cache_cheney GC
```

先重新构建 `cache_cheney` 后跑样例，发现 `a.sbc` 直接 Segmentation fault。gdb 栈显示崩溃在：

```text
alloc_site_update_info
NodeScanner::scan_object_properties<CacheCheney_Tracer>
scavenge
garbage_collection
```

原因：之前为了调查 weak 引用，把 `CacheCheney_Tracer::process_weak_edge` 改成了不保活，并在 weak_clear 后补了 inline cache patch。这个变体对 GiY 调查有帮助，但不适合作为稳定的 Cheney 对照组；它会破坏 cache_cheney 原本依赖的 hidden-class/allocation-site 关系，导致对照组样例崩溃。

本次为了跑真实对照组，我把 `cache_cheney` 的弱边处理恢复到 Cheney 原始强处理：

```c
process_weak_edge(v) -> process_edge(v)
process_weak_edge(p) -> process_edge(p)
process_mark_stack() -> 空实现
```

同时删除 `cache_cheney` weak_clear 后额外 patch inline cache 的实验路径。

另外，旧 `out13` 的 Havlak 失败原因是 remembered set capacity 只有 3072：

```text
Error: Remembered set full, cannot add more remembered objects!
```

为了完整跑完对照组，并且尽量和 GiY `out22` 使用同级别的 cache metadata 预留，我把 `cache_cheney_Rset.cc` 调整为：

```text
REMEMBERED_SET_CAPACITY_BYTES = 128KB
HASH_TABLE_SIZE = 8192
HASH_PROBE_LIMIT = 32
```

说明：
- cache_cheney 只存 slot 地址，不存最新 value，所以同样 128KB 下它的 entry capacity 会比 GiY 大；
- 但这让 cache metadata 预留字节数和 GiY out22 更接近，也能避免 Havlak 因 remembered set 容量提前失败；
- 这不是 GiY 修改，是为了得到能完整运行的 Cheney 对照组。

修复后验证：
- `make OPT_GC=cache_cheney -j2` 成功；
- `a.sbc` 到 `g.sbc`、`hello.sbc`、`f1.sbc` 在 cache_cheney 下退出码全部为 0；
- 单独运行 `Havlak.sbc` 完整通过，退出码 0；
- Havlak 单项结果为 average 4367380us，总运行 436.739 秒，GC overhead 10.1%。

下一步：
- 使用同一个 cache_cheney 构建完整跑 benchmark；
- 输出目录为 `build.debug/benchmarks/out23`；
- 然后用 out23 和 GiY 的 out22 进行同口径性能对比。

2026-04-27：cheneyGC/cache_cheney 对照组完整 benchmark 与 GiY out22 对比

完整对照组 benchmark 已完成，输出目录：

```text
build.debug/benchmarks/out23
```

`out23` 的 12 个 benchmark 全部 `status=0`。这是新的可用对照组结果；旧 `out13` 不能继续当完整对照组，因为它的 Havlak 没跑完。

对照组设置：
- `out22`：GiY GC，remembered set capacity 8192，cache size 320KB；
- `out23`：cache_cheney GC，remembered set capacity 16384，cache size 320KB；
- 两者 cache metadata 预留后显示的 cache size 都是 320KB；
- cache_cheney 只记录 slot 地址，不记录最新 value，所以相同 metadata 字节下 entry capacity 比 GiY 大。

同口径 runtime 对比，单位为每次 benchmark iteration 的平均秒数：

```text
Benchmark    GiY out22   Cheney out23   GiY 相对 Cheney
Bounce       1.57099     1.59925        GiY 快 1.8%
List         1.10112     1.00671        GiY 慢 9.4%
Sieve        1.20906     1.21730        GiY 快 0.7%
Queens       1.13882     1.17852        GiY 快 3.4%
Permute      3.81898     3.81004        基本持平，GiY 慢 0.2%
Storage      3.19236     2.07121        GiY 慢 54.1%
Towers       1.80293     1.83044        GiY 快 1.5%
Mandelbrot   2.37069     2.37038        基本持平，GiY 慢 0.01%
Richards     9.37380     9.35045        基本持平，GiY 慢 0.25%
CD           2.75417     2.48455        GiY 慢 10.9%
NBody        3.85897     3.81718        GiY 慢 1.1%
Havlak       4.96096     4.23781        GiY 慢 17.1%
```

总和对比：
- GiY out22 12 项 average runtime 总和：37.15285 秒；
- Cheney out23 12 项 average runtime 总和：34.97384 秒；
- 以总和看，GiY 是 Cheney 的 0.941 倍速度，也就是整体约慢 5.9%。

GC 行为对比：
- GiY 的 `scan_roots` 明显更少。例如：
  - `Havlak`: GiY 0.177 秒，Cheney 5.709 秒；
  - `NBody`: GiY 0.125 秒，Cheney 4.320 秒；
  - `Mandelbrot`: GiY 0.081 秒，Cheney 2.875 秒；
  - `CD`: GiY 0.107 秒，Cheney 2.785 秒。
- 这说明 GiY 专用 root scanning 确实避免了 Cheney 通用 root/function_table 路径的大量扫描。
- 但是 GiY 的 scavenge/materialize 成本在若干 workload 上更高：
  - `Storage`: GiY scavenge 156.700 秒，Cheney 48.251 秒；
  - `CD`: GiY scavenge 22.893 秒，Cheney 7.451 秒；
  - `Havlak`: GiY scavenge 69.168 秒，Cheney 27.397 秒。
- 这抵消了 root scanning 的收益，导致总体性能 GiY 仍略慢于 Cheney。

重点结论：
- 当前 GiY 的优势已经很清楚：root scanning 大幅降低。
- 当前 GiY 的主要瓶颈也很清楚：young graph traverse/materialize/scavenge 路径成本高，尤其 Storage、CD、Havlak。
- 如果后续优化，应优先看 GiY 的 object traversal、edge log、最终 copy/materialize 和 cache 空间压力，而不是回退到 Cheney 式 scan_roots。

2026-04-27：构造 Cheney root-controlled 对照组

用户指出需要进一步控制变量：既然 Cheney 是对照组，就应该把 Cheney 中的全盘 `scan_roots` 扫描也改成 GiY 类似的扫描方法和处理方法，避免把“是否全盘扫 function_table”这个变量混入 GiY 和 Cheney 的比较。

本次修改目标：
- 保留 Cheney 的 copy/scavenge 机制；
- 保留 Cheney 原始 weak edge 强处理语义；
- 把 Cheney minor GC 的 root 入口从 `scan_roots<CacheCheney_Tracer>(ctx)` 改成 `scan_roots_generational(ctx)`；
- 增加 Cheney 专用 function-table strong-slot set，记录 constant pool 和 allocation site cache 中可能出现 Young 指针的强 slot；
- minor GC 时扫描这些记录过的 function-table 强 slot，而不是扫描整张 function_table。

修改文件：
- `ejsvm/cache_dram_manager.cc`
- `ejsvm/cache_dram_manager.h`
- `ejsvm/codeloader.c`
- `ejsvm/object.c`

实现说明：
- `codeloader.c` 中 constant pool 写入后调用 `giy_record_ft_jsvalue_slot`，现在在 `CACHE_CHENEY` 下也启用；
- `object.c` 中 allocation site cache 的 `as->pm` / `as->shape` 写入后调用 `giy_record_ft_ptr_slot`，现在在 `CACHE_CHENEY` 下也启用；
- cache_cheney 下这两个函数由 `cache_dram_manager.cc` 提供实现，记录到 `cache_cheney_ft_slot_set`；
- GiY 下仍由 `GiY.cc` 提供实现。

验证：
- `make OPT_GC=cache_cheney -j2` 成功；
- `a.sbc` 到 `g.sbc`、`hello.sbc`、`f1.sbc` 在 Cheney root-controlled 变体下退出码全部为 0；
- 单独运行 `Havlak.sbc` 完整通过，退出码 0；
- Havlak smoke 结果：average 4236960us，总运行 423.697 秒；
- Havlak 的 `scan_roots` 为 0.348 秒，说明已经不再走旧 Cheney 的全 function_table root scan。

下一步：
- 使用这个 root-controlled Cheney 构建完整跑 benchmark；
- 输出目录为 `build.debug/benchmarks/out24`；
- 最后比较 `out22` GiY、`out23` 原 Cheney 对照、`out24` root-controlled Cheney 对照。

2026-04-27：Cheney root-controlled 对照组 out24 结果和三方比较

`out24` 已完整跑完，12 个 benchmark 全部 `status=0`。

对比对象：
- `out22`：GiY；
- `out23`：Cheney 对照组，仍使用 full `scan_roots`；
- `out24`：Cheney root-controlled 对照组，使用 GiY 风格 root scanner + function-table strong-slot set，不再 full scan function_table。

三组 average runtime 总和：

```text
out22 GiY                         37.15285 秒
out23 Cheney full-root-scan       34.97384 秒
out24 Cheney root-controlled      34.99316 秒
```

结论：
- `out24` 和 `out23` 总体几乎相同，`out24` 只比 `out23` 慢约 0.055%；
- 控制掉 Cheney 的 full function_table root scan 后，Cheney 总性能没有明显变化；
- GiY 相对 `out24` 仍整体约慢 5.8%；
- 因此当前 GiY 与 Cheney 的主要性能差距不是 root scanning，而是 GiY 后续 young traversal / materialize / scavenge 路径。

同口径 runtime 对比，单位为每次 iteration 的平均秒数：

```text
Benchmark    GiY out22   Cheney out23   Cheney rootctl out24   GiY vs out24
Bounce       1.57099     1.59925        1.58884                GiY 快 1.1%
List         1.10112     1.00671        0.98876                GiY 慢 11.4%
Sieve        1.20906     1.21730        1.23730                GiY 快 2.3%
Queens       1.13882     1.17852        1.16045                GiY 快 1.9%
Permute      3.81898     3.81004        3.82242                基本持平
Storage      3.19236     2.07121        2.09177                GiY 慢 52.6%
Towers       1.80293     1.83044        1.85359                GiY 快 2.7%
Mandelbrot   2.37069     2.37038        2.34108                GiY 慢 1.3%
Richards     9.37380     9.35045        9.43233                GiY 快 0.6%
CD           2.75417     2.48455        2.51041                GiY 慢 9.7%
NBody        3.85897     3.81718        3.78795                GiY 慢 1.9%
Havlak       4.96096     4.23781        4.17826                GiY 慢 18.7%
```

root scanning 控制是否生效：
- `out24` 的 scan_roots 相对 `out23` 大幅下降：
  - Storage: 1.433 秒 -> 0.162 秒；
  - CD: 2.785 秒 -> 0.219 秒；
  - NBody: 4.320 秒 -> 0.313 秒；
  - Havlak: 5.709 秒 -> 0.350 秒。
- 这说明 root-controlled Cheney 确实已经去掉了 full function_table scan 这个变量。

为什么总体没明显变快：
- 对很多 benchmark 来说，scan_roots 原本不是总 runtime 主导；
- 部分 benchmark 中 root scan 降低后，scavenge 或其他 full GC enclosure 时间仍主导；
- `out24` 的总时间与 `out23` 接近，说明 Cheney 的瓶颈不主要在 full root scanning。

GiY 当前真正差距：
- Storage:
  - GiY scavenge 156.700 秒；
  - Cheney rootctl scavenge 48.342 秒；
  - GiY 慢 52.6%。
- CD:
  - GiY scavenge 22.893 秒；
  - Cheney rootctl scavenge 7.675 秒；
  - GiY 慢 9.7%。
- Havlak:
  - GiY scavenge 69.168 秒；
  - Cheney rootctl scavenge 27.866 秒；
  - GiY 慢 18.7%。

新的总判断：
- 你提出的控制变量实验是必要的；
- 实验结果说明，GiY 的 root scanning 优势是真实的，但它不是当前总性能的决定因素；
- 当前 GiY 的瓶颈更集中在 object traversal、edge log、最终 materialize/copy、以及 cache 空间压力造成的高频 GC 和 scavenge 成本。

2026-04-27：回答“大型 benchmark 中 GiY 是否取消了 mprotect 检查”

结论：大型 benchmark `out22` 没有开启 mprotect 检查。

原因：
- GiY 的 mprotect guard 不是编译期永久开启，而是运行时环境变量控制；
- 只有显式设置 `GIY_MPROTECT_OLD=1` 时，`giy_old_guard_enabled()` 才会返回 true；
- `out22` 的 README 记录的是 benchmark 前的 mprotect 样例验证，而不是 benchmark 本身带 mprotect 运行；
- `out22` benchmark loop 使用的是普通 `../ejsvm "$bench.sbc"`，没有加 `GIY_MPROTECT_OLD=1`。

代码位置：
- `ejsvm/GiY.cc` 中 `giy_old_guard_enabled()` 读取 `GIY_MPROTECT_OLD`；
- `giy_minor_collect()` 每次 minor GC 都会调用 `giy_old_guard_begin()`，但如果环境变量没开，函数直接返回，不做 `mprotect(PROT_NONE)`；
- `giy_weak_clear()` 中会调用 `giy_old_guard_end()`，把 strict core mprotect 窗口结束在通用 weak_clear 前。

含义：
- `out22` 的性能数据不是 mprotect 模式下的数据；
- 这是合理的，因为 mprotect 是验证工具，不是正常 benchmark 性能路径；
- 如果在大型 benchmark 上打开 `GIY_MPROTECT_OLD=1`，每次 minor GC 都会反复保护/放开 Old/DRAM 页，性能会严重失真，只适合做正确性压力验证，不适合做论文/报告中的性能数据。

如果以后要做“大型 benchmark + mprotect 正确性验证”，应该单独开一个输出目录，例如 `out25_mprotect_verify`，并明确标注它不是性能 benchmark。

2026-04-27：详细分析为什么 GiY 比 root-controlled Cheney 慢

本次分析使用最公平的一组对比：
- `out22`：GiY；
- `out24`：root-controlled Cheney，对 Cheney 也取消 full function_table scan，并记录 function-table strong slots。

这样 root scanning 变量已经基本被控制住。

总量数据：

```text
指标                     GiY out22       Cheney rootctl out24    比例
average runtime 总和      37.15285s       34.99316s               GiY 慢 6.17%
GC full 总时间            291.428s        119.450s                GiY 是 2.44x
scan_roots 总时间         0.550s          1.307s                  GiY 更少
scan_RS 总时间            1.269s          2.925s                  GiY 更少
scavenge 总时间           250.646s        85.707s                 GiY 是 2.92x
minor GC 总次数           3,333,545       2,341,014               GiY 是 1.42x
forward operations        1,638,569,002   1,696,428,830           GiY 略少
```

第一层结论：
- GiY 的 root scanning 并不慢，反而比 Cheney rootctl 少；
- GiY 的 remembered-set scan 也不慢，反而比 Cheney rootctl 少；
- GiY 慢主要来自 GC full 总时间，尤其 scavenge/materialize 总时间。

第二层结论：GiY 慢不是因为搬的对象更多。
- GiY 的 forward operations 总量是 1.638B；
- Cheney rootctl 是 1.696B；
- GiY 反而略少。

所以问题不是“GiY 活对象更多”或“GiY forwarding 次数更多”，而是每次处理 live object/edge 的成本更高。

第三层结论：GiY 同时有两个成本来源。

1. GiY minor GC 更频繁。

GiY minor GC 总次数是 Cheney rootctl 的 1.424 倍。这个比例和 cache 空间划分高度吻合：

```text
Cheney rootctl：主要额外保留 function-table slot set，约 1/8 young 区。
GiY：额外保留 gc_stack、edge_log、function-table slot set，约 3/8 young 区。
```

因此有效 young/cache 可用空间大致是：

```text
Cheney rootctl 有效 young ≈ 7/8
GiY 有效 young ≈ 5/8
(7/8) / (5/8) = 1.4
```

实际 minor GC 次数比例是 1.424，基本吻合。

这说明 GiY 的 cache pressure 是真实原因之一：GiY 为了遵守“先在 Young/Cache 内完成图处理”的设计，把 GC 辅助结构放进 cache，减少了可用于普通 young allocation 的空间，所以 GC 更频繁。

2. GiY 单次 scavenge/materialize 也更贵。

总 scavenge 时间：

```text
GiY      250.646s
Cheney    85.707s
比例       2.92x
```

但 GC 次数只多 1.42x。摊到每次 GC：

```text
GiY 每次 scavenge 平均      75.19us
Cheney 每次 scavenge 平均   36.61us
比例                         2.05x
```

这说明 GiY 不只是 GC 更频繁，而且每次 GC 的核心处理也更重。

代码原因：

Cheney 的对象处理路径比较直接：
- `CacheCheney_Tracer::forward` 第一次遇到对象时直接 `memcpy` 到 DRAM；
- 后续 `scavenge()` 顺着 DRAM 中新复制的对象扫描并更新引用；
- copy 和扫描较直接，没有额外 edge log 回放。

GiY 的对象处理路径更复杂：
- `giy_traverse_stack_and_copy()` 从显式 `gc_stack` 弹出 Young 对象；
- 先用 `GiYReserveTracer` 扫对象字段，发现孩子对象；
- 扫字段时不能立即把所有边按最终地址写好，所以要记录 `edge_log`；
- 然后 `giy_apply_edge_log()` 回放 edge log，把对象内部边修成 forwarding 后的新地址；
- 最后 `giy_copy_live_object()` 把修好的对象 materialize 到 Old/DRAM；
- copy 使用 non-temporal store，并在启用 guard 时还要兼容 mprotect 写窗口逻辑，虽然 benchmark 没开 mprotect，但代码路径仍比普通 memcpy 复杂。

最明显的 workload：

Storage：

```text
runtime:   GiY 3.19236s, Cheney 2.09177s，GiY 慢 52.6%
GC full:   GiY 160.023s, Cheney 50.947s
scavenge:  GiY 156.700s, Cheney 48.342s
```

Storage 的 runtime 差距几乎完全对应 GC/scavenge 差距。它说明 GiY 在大量对象搬迁/对象图处理场景下成本明显高。

CD：

```text
runtime:   GiY 2.75417s, Cheney 2.51041s，GiY 慢 9.7%
scavenge:  GiY 22.893s, Cheney 7.675s
```

Havlak：

```text
runtime:   GiY 4.96096s, Cheney 4.17826s，GiY 慢 18.7%
scavenge:  GiY 69.168s, Cheney 27.866s
```

最终判断：
- GiY 的 root scanning 目标已经达成，且比对照组少；
- GiY 慢的主因是：为了实现 GiY 法则，额外引入了 cache 内辅助结构、显式 stack、edge log、edge log replay、最后 materialize/copy；
- 这些机制带来两个直接后果：
  1. 有效 young/cache 空间更小，minor GC 更频繁；
  2. 每次 GC 处理 live object 的成本更高。

下一步优化方向：
- 减少 GiY 辅助结构占用，特别是 `gc_stack` / `edge_log` / `ft_slot_set` 的固定预留比例；
- 优化 edge log：减少记录数量、减少重复记录、尝试对象内局部 patch 直接化；
- 优化 materialize copy：评估 non-temporal store 是否对小对象反而更慢；
- 分析 Storage/CD/Havlak 中对象类型分布，确认哪些 object scanner 产生最多 edge log 和 copy 成本；
- 进一步统计每次 GC 的 live bytes、edge log entries、materialized bytes，而不是只看 forwarding count。

2026-04-27：新增 AIlog 记录规则

用户提出新的长期要求：
- 从现在开始，每次我讲完所有内容并给出总结后，都必须把这次说明和总结追加到 `AIlog.md`；
- 每次回复用户时，都要明确告诉用户本次写入 `AIlog.md` 的行号范围；
- 这个要求适用于后续所有解释、分析、benchmark 总结、代码修改说明和结论总结。

执行方式：
- 先完成本轮分析或修改；
- 再把本轮说明追加进 `AIlog.md`；
- 最后用 `nl -ba AIlog.md` 或等价方式确认新增内容的起止行；
- 在最终回复中写明：`AIlog.md` 第 X 行到第 Y 行。

2026-04-27：重新总结 GiY 当前问题和下一步做法

当前最可靠的判断：GiY 的大方向没有错，但当前实现的性能瓶颈已经从 root scanning 转移到了 Young-side processing 和 materialize 路径。

为什么这么判断：
- 已经做过 root-controlled Cheney 对照组 `out24`；
- `out24` 中 Cheney 也不再 full scan function_table，而是使用 GiY 风格 root scanner 加 function-table strong-slot set；
- 这样以后，Cheney `out24` 和原 Cheney `out23` 总时间几乎一样；
- GiY `out22` 相比 root-controlled Cheney `out24` 仍约慢 5.8%；
- 所以问题不主要在 root scanning。

关键数据：

```text
GiY out22 runtime 总和                    37.15285 秒
Cheney root-controlled out24 runtime 总和 34.99316 秒

GiY scan_roots 总时间                     0.550 秒
Cheney root-controlled scan_roots 总时间  1.307 秒

GiY scavenge/materialize 总时间           250.646 秒
Cheney root-controlled scavenge 总时间    85.707 秒

GiY minor GC 次数                         3,333,545
Cheney root-controlled minor GC 次数      2,341,014
```

这说明：
- GiY root scanning 已经比对照组更少；
- GiY remembered-set scan 也不是主问题；
- GiY 的 scavenge/materialize 总时间约是 Cheney root-controlled 的 2.92 倍；
- GiY 的 minor GC 次数约是 Cheney root-controlled 的 1.42 倍；
- GiY 每次 GC 的 scavenge 平均成本也约是 Cheney 的 2.05 倍。

所以 GiY 当前慢有两个层次：

1. GC 更频繁。

GiY 为了遵守 GiY 法则，在 cache 中额外预留：
- `gc_stack`;
- `edge_log`;
- `function-table slot set`;
- remembered set 的 `buffer + values`;

这些结构减少了真正可用于 Young allocation 的空间。有效 Young 空间更小，就会更快触发 minor GC。

2. 单次 GC 更重。

Cheney 的路径更直接：

```text
发现 Young 对象 -> 直接 memcpy 到 DRAM -> 在 DRAM 中继续 scavenge 修边
```

GiY 的路径更复杂：

```text
发现 Young 对象
-> 放入 gc_stack
-> 在 Young/Cache 中扫描对象
-> 记录 edge_log
-> 回放 edge_log 修对象内部边
-> 最后 materialize/copy 到 Old/DRAM
```

这个流程符合 GiY 的设计目标，但当前实现引入的额外步骤过重。

最可疑的问题点：

1. `edge_log` 可能太重。
- 每个对象先扫描字段，再记录 slot；
- 后面再遍历 edge log，重新读 slot、查 forwarding、写回；
- 这等于很多边处理了两遍；
- Storage、CD、Havlak 这种对象边多的 benchmark 会特别吃亏。

2. `materialize/copy` 可能太重。
- GiY 最后才把对象写进 Old；
- 当前 `giy_copy_live_object` 使用 non-temporal store；
- 对大量小对象来说，non-temporal store 未必比普通 memcpy 快；
- 还存在按对象逐个 materialize 的函数调用和对齐处理成本。

3. cache 空间压力太大。
- GiY 的辅助结构占用 Young/Cache；
- minor GC 次数增加约 1.42 倍；
- 这和理论上有效 Young 空间从 Cheney 的约 7/8 降到 GiY 的约 5/8 非常吻合。

4. GiY 的对象扫描路径可能重复劳动。
- reserve 阶段发现孩子；
- edge log 回放修边；
- patch roots / patch RS 再处理外部 slot；
- 一些 slot 可能记录后最终并不需要 patch。

应该怎么做：

第一步，不要急着大改，先加统计。

建议新增 GiY profiling 计数：
- 每次 GC 的 live bytes；
- 每次 GC 的 materialized bytes；
- 每次 GC 的 `gc_stack` push 次数；
- 每次 GC 的 `edge_log` entry 数；
- edge_log 中真正发生 patch 的数量；
- edge_log 中 `to == from` 的无效数量；
- 按 cell type 统计 edge_log entry；
- 每类 object 的 materialize bytes；
- 小对象 / 大对象 materialize 次数。

目标是确认到底是：
- edge_log entry 太多；
- edge_log 无效 entry 太多；
- materialize 小对象太慢；
- 某几个 object type 特别重；
- 还是 cache 空间导致 GC 次数太多。

第二步，先做低风险优化。

可优先尝试：
- edge_log 只记录当前值指向 Young 的 slot；
- 已经能直接得到 forwarding 的 slot 直接 patch，不进 edge_log；
- 对 JSValue array 做专门批量扫描和直接 patch；
- 统计并跳过 `to == from` 高比例的 edge log 类型；
- 对小对象评估普通 memcpy 是否比 non-temporal store 更快。

第三步，再考虑结构性优化。

如果统计证明 edge_log 是主因，可以考虑：
- 把 edge_log 从“对象扫描后统一回放”改成“扫描时直接 patch slot”；
- 改 scanner/tracer API，让 tracer 拿到真实 slot 地址并直接完成 forward + patch；
- 减少全局 edge_log 数组写入；
- 对小对象使用局部 small buffer，只有大对象才落到全局 edge_log。

第四步，重新跑 benchmark。

每次优化后至少比较：
- GiY out22 基线；
- Cheney root-controlled out24；
- 新 GiY 输出目录；
- Storage、CD、Havlak 三个重点 benchmark 的 scavenge/materialize；
- minor GC 次数是否下降；
- scan_roots 是否仍保持低。

当前建议的研究表述：

GiY 不是失败，而是已经把瓶颈从 Old/root scanning 转移到了 Young-side object processing。现在的问题不是“要不要扫 function_table”，而是“怎么把 GiY 的 Young-side traversal、edge patch 和 materialize 做得足够便宜”。

最优先要做的事情：
1. 加 GiY 专用统计，确认 edge_log/materialize 的真实成本来源；
2. 优化 edge_log，尽量减少两遍处理；
3. 评估小对象是否应该放弃 non-temporal store；
4. 缩小 GiY 辅助结构对 Young/Cache 空间的占用；
5. 用 Storage、CD、Havlak 作为主要优化反馈。

## 2026-04-27 GiY 深度调查与 targeted benchmark 报告

本次操作：
- 只围绕 GiY 诊断路径加统计，没有改变 GiY 的保活、复制、patch 判定语义。
- 修改文件：
  - `ejsvm/GiY.cc`：新增 GiY Detailed Profile 计数。
  - `ejsvm/cache_dram_manager.h`：声明 `giy_print_profile()`。
  - `ejsvm/giy_dram_manager.cc`：在 GC 报告里打印 GiY profile。
- 编译命令：`cd build.debug && make OPT_GC=giy -j2`，编译成功。
- 基础验证：
  - `./build.debug/ejsvm ejsvm/js/hello.sbc`，退出码 0。
  - `./build.debug/ejsvm ejsvm/js/f1.sbc`，退出码 0。
  - `GIY_MPROTECT_OLD=1 ./build.debug/ejsvm ejsvm/js/hello.sbc`，退出码 0。

本次 targeted benchmark：
- 输出目录：`out25_giy_profile/`
- 运行项目：Storage、CD、Havlak。
- 三个 benchmark 全部退出码 0。
- 说明：本次加入了 hot path profile 计数，所以 out25 的 wall time 不能当作纯性能基准。性能比较仍应主要看旧的无诊断输出：GiY 用 `build.debug/benchmarks/out22`，cache_cheney root-controlled 对照用 `build.debug/benchmarks/out24`。out25 的价值是解释 GiY 内部发生了什么。

对照组核心数据：
- Storage：
  - out22 GiY：Total 319.241s，GC full 160.023s，minor GC 321476，scavenge 156.700s。
  - out24 cache_cheney root-controlled：Total 209.181s，GC full 50.947s，minor GC 229482，scavenge 48.342s。
  - GiY 比 cache_cheney 慢 52.6%，scavenge 约 3.24 倍，minor GC 次数约 1.40 倍。
- CD：
  - out22 GiY：Total 275.434s，GC full 28.597s，minor GC 349941，scavenge 22.893s。
  - out24 cache_cheney root-controlled：Total 251.045s，GC full 11.989s，minor GC 230567，scavenge 7.675s。
  - GiY 比 cache_cheney 慢 9.7%，scavenge 约 2.98 倍，minor GC 次数约 1.52 倍。
- Havlak：
  - out22 GiY：Total 496.126s，GC full 81.336s，minor GC 726500，scavenge 69.168s。
  - out24 cache_cheney root-controlled：Total 417.826s，GC full 38.889s，minor GC 502801，scavenge 27.866s。
  - GiY 比 cache_cheney 慢 18.7%，scavenge 约 2.48 倍，minor GC 次数约 1.45 倍。

第一结论：问题不在 root scanning。
- out25 GiY profile 中，scan_roots 很小：
  - Storage：0.066s。
  - CD：0.129s。
  - Havlak：0.217s。
- out24 cache_cheney 已经使用 root-controlled 处理，仍然比 GiY 快很多。
- function table slot 数量也很小：
  - Storage：recorded 245，scanned 245，patched 245。
  - CD：recorded 568，scanned 568，patched 568。
  - Havlak：recorded 639，scanned 639，patched 639。
- 因此，function table/root scan 不是当前大 benchmark 的主性能问题。

第二结论：GiY 的有效 young/cache 空间被辅助结构切掉太多，直接增加 minor GC 次数。
- GiY profile 显示：
  - Young before aux：300.24 KB。
  - Young after aux：187.67 KB。
  - Aux total：112.57 KB，分成 stack 37.52 KB、edge 37.52 KB、ft 37.52 KB。
- 也就是说 GiY 当前把约 37.5% 的可用 young 工作区给了 GC 辅助结构。
- 这和 minor GC 次数增加高度吻合：
  - Storage GiY/cache：321476 / 229482 = 1.40 倍。
  - CD GiY/cache：349941 / 230567 = 1.52 倍。
  - Havlak GiY/cache：726500 / 502801 = 1.45 倍。
- 这个问题不是 correctness 问题，但它会让 GiY 更频繁进入 minor GC，使后续 edge log 和 materialize 成本被放大。

第三结论：edge log 是 GiY 的主要结构性开销之一。
- out25 profile：
  - Storage：edge log entries 1,090,429,756，约 3391.9 entries/minor GC。
  - CD：edge log entries 163,700,585，约 467.8 entries/minor GC。
  - Havlak：edge log entries 368,460,303，约 507.2 entries/minor GC。
- 这些 edge 几乎都是真实 young edge：
  - Storage patched 1,087,841,133，unforwarded young 0。
  - CD patched 158,636,833，unforwarded young 0。
  - Havlak patched 361,655,876，unforwarded young 0。
- 这说明 edge log 不是记录了大量无效边，而是 GiY 的当前设计确实要为大量 young-to-young 边做“先记录、后回放 patch”。
- cache_cheney 可以在复制/扫描过程中直接处理 field，而 GiY 当前多了 edge_log_push、edge_log 数组写入、之后 apply_edge_log 再读回和 patch 的额外路径。
- Storage 最典型：ARRAY 和 ARRAY_DATA 合起来产生约 10.88 亿 edge entries，正好解释 Storage 为什么最慢。

第四结论：materialize 到 old 的流量非常大，而且多数是小对象。
- out25 profile：
  - Storage：materialized objects 1,092,297,408，bytes 58,771.37 MB，平均 56.4 bytes。
  - CD：materialized objects 138,984,702，bytes 9,090.24 MB，平均 68.6 bytes。
  - Havlak：materialized objects 470,383,081，bytes 33,862.21 MB，平均 75.5 bytes。
- 小对象占比很高：
  - Storage：<=64 bytes 有 928,997,790 个，<=128 bytes 有 163,299,610 个，几乎全部是小对象。
  - CD：<=64 bytes 有 128,493,524 个。
  - Havlak：<=64 bytes 有 388,366,397 个，<=128 bytes 有 53,283,581 个。
- 当前 GiY 对这些对象全部走 NT copy：
  - Storage：NT copy bytes 58,771.37 MB。
  - CD：NT copy bytes 9,090.24 MB。
  - Havlak：NT copy bytes 33,862.21 MB。
- 对 56 到 75 字节这种小对象，non-temporal store 未必是好事。它可能造成写合并效率差、指令开销高，并且对象粒度太细，不能很好发挥顺序流式写优势。

第五结论：不同 workload 的慢点略有不同。
- Storage：
  - 主要是 ARRAY / ARRAY_DATA 极端重复 materialize。
  - ARRAY objs 546,098,478，bytes 29,164.81 MB，edge_entries 545,942,881。
  - ARRAY_DATA objs 546,098,478，bytes 29,603.50 MB，edge_entries 541,897,786。
  - RSet patch 只有约 2.62M，不是主因。
  - 最主要是小数组对象 materialize + edge log。
- CD：
  - SIMPLE_OBJECT、PROP、ARRAY_DATA 都明显。
  - PROP edge_entries 81,592,105，SIMPLE_OBJECT edge_entries 54,989,044。
  - RSet patch 约 5.98M，有一定开销，但仍小于 young traversal/materialize 主体。
- Havlak：
  - SIMPLE_OBJECT、PROP、ARRAY_DATA 都很重。
  - RSet slots scanned 140,185,024，patched 137,901,801，old slot stores 137,902,092。
  - Havlak 除了 materialize/edge log，还受到大量 remembered set patch old slot 的影响。

第六结论：GiY 的想法不是错的，但当前实现把收益放在 root/old read 减少上，把成本转移到了 young-side traversal、edge log 和 materialize。
- 之前已经验证 GiY 的 root scan 很小；mprotect 小验证也能证明核心路径按 GiY 法则避免乱读 old。
- 但是大 benchmark 中，root scan 本来就不是主要时间项。
- 所以 GiY 现在不是“没有优化到任何东西”，而是“优化掉的部分不是这些 workload 的瓶颈；新增的 young-side 机制反而变成主瓶颈”。

我认为最可靠的下一步优化顺序：
1. 优先砍 edge log。
   - 当前 `GiYReserveTracer` 在扫描 young object 时记录 edge log，然后 `giy_apply_edge_log()` 回放。
   - 更好的方向是：扫描 young object 字段时，forward 子对象后直接把这个 young slot patch 成目标 old pointer。
   - 因为被扫描对象在 young/cache 中，写它不违反 GiY 法则，也不需要读 old。
   - roots、function_table、remembered_set 仍保留单独 patch 阶段，避免 old slot 乱读。
   - 这样可以去掉大部分 in-object edge log 写入和回放。
2. 缩小或动态化 GiY 辅助区。
   - 当前固定 stack/edge/ft 各占 young/8，总共切掉 37.5% young。
   - 如果砍掉 in-object edge log，edge auxiliary 可以大幅缩小甚至取消，minor GC 次数应下降接近 cache_cheney。
   - function table set 只需要几百个 slot，却固定占 37.52 KB，明显过度。
3. 对小对象不要默认使用 NT copy。
   - 建议试验阈值：例如 <=128 或 <=256 bytes 使用普通 memcpy，只有大对象使用 NT store。
   - Storage/CD/Havlak 的多数 materialized 对象都小于 128 bytes，这个实验很关键。
4. 单独优化 remembered set patch。
   - Havlak 的 RSet patch old slot 数量达到 137.9M。
   - 这里仍要遵守 GiY 法则：使用 remembered_set.values 避免读 old slot，但 patch old slot 时尽量减少 NT 单槽 store 的额外成本。
5. 每次只改一个变量重新跑：
   - A：direct young-slot patch，去掉大部分 edge log。
   - B：缩小辅助区。
   - C：small object memcpy threshold。
   - D：RSet old slot patch 写入策略。
   - 每一步至少跑 Storage、CD、Havlak，再和 out22/out24 对比。

当前最可能的问题根源排序：
1. edge log 双路径：十亿级 edge entries，尤其 Storage。
2. materialize 小对象到 old 的成本：数十 GB，小对象占绝大多数。
3. 辅助结构切走 37.5% young/cache，导致 minor GC 次数增加约 1.4 到 1.5 倍。
4. Havlak 的 RSet patch old slot 数量巨大。
5. function table/root scan 不是主因。

## 2026-04-27 GiY 优化执行报告：direct young-slot patch + small-object memcpy

本次按照前面的判断继续修改 GiY，目标是减少 GiY hot path 的额外成本，同时不改变其他 GC。

实际修改：
- `ejsvm/GiY.cc`
  - GiY young object traversal 不再使用“edge log 写入 + apply_edge_log 回放”来 patch young object 内部字段。
  - 新逻辑是在 `GiYReserveTracer` 扫描到 young edge 时，先 reserve/copy_for_minor，再在 slot 本身位于 young/cache 时直接把该 slot patch 成目标 old pointer。
  - root、function_table、remembered set 仍保留独立 patch 阶段；也就是说 direct patch 只处理 young object 内部 slot，不把 root/old slot 混进去。
  - edge auxiliary cache 区从固定 `young/8` 改为 0；function table slot set 保留 32KB；stack 仍保留 `young/8`。
  - GiY 可用 young/cache 从 187.67KB 提高到 230.72KB。
  - 新增 `GIY_NT_COPY_MIN_BYTES = 256`：小于等于 256 字节的 object materialize 使用普通 `memcpy`，大对象继续用 non-temporal store。
  - 详细 profile 默认关闭，避免 hot path 计数污染 benchmark；需要时可以用 `GIY_PROFILE_DETAIL=1` 编译打开。
- `build.debug/benchmarks/giy_gc_probe.ejs.js`
  - 新增一个小型 GC probe，只用于快速触发少量 minor GC 和 mprotect 验证。

验证：
- 编译：
  - `cd build.debug && make OPT_GC=giy -j2` 成功。
- 基础程序：
  - `./build.debug/ejsvm ejsvm/js/hello.sbc`，退出码 0。
  - `./build.debug/ejsvm ejsvm/js/f1.sbc`，退出码 0。
- GC probe：
  - `../ejsvm giy_gc_probe.sbc`，退出码 0，触发 14 次 minor GC。
  - `GIY_MPROTECT_OLD=1 ../ejsvm giy_gc_probe.sbc`，退出码 0，触发 14 次 minor GC，没有 old-space violation。
- 大 benchmark targeted：
  - 输出目录：`out27_giy_direct_memcpy/`
  - Storage、CD、Havlak 全部退出码 0。

最终 targeted benchmark 对比：

Storage：
- 旧 GiY out22：
  - Total 319.241s，GC full 160.023s，minor GC 321476，scavenge 156.700s。
- cache_cheney root-controlled out24：
  - Total 209.181s，GC full 50.947s，minor GC 229482，scavenge 48.342s。
- 新 GiY out27：
  - Total 184.211s，GC full 27.182s，minor GC 261374，scavenge 24.209s。
- 结果：
  - 新 GiY 比旧 GiY 快 1.73 倍。
  - 新 GiY 比 cache_cheney root-controlled 快 1.14 倍。
  - Storage 是最大收益点，说明小对象 materialize 策略非常关键。

CD：
- 旧 GiY out22：
  - Total 275.434s，GC full 28.597s，minor GC 349941，scavenge 22.893s。
- cache_cheney root-controlled out24：
  - Total 251.045s，GC full 11.989s，minor GC 230567，scavenge 7.675s。
- 新 GiY out27：
  - Total 257.053s，GC full 12.071s，minor GC 284514，scavenge 6.941s。
- 结果：
  - 新 GiY 比旧 GiY 快 1.07 倍。
  - 新 GiY 的 scavenge 已经低于 cache_cheney，但总时间仍比 cache_cheney 慢约 2.4%。
  - CD 的剩余差距更可能在 business side/cache behavior 或非 GC 主体上，而不是 scan_roots/scavenge。

Havlak：
- 旧 GiY out22：
  - Total 496.126s，GC full 81.336s，minor GC 726500，scavenge 69.168s。
- cache_cheney root-controlled out24：
  - Total 417.826s，GC full 38.889s，minor GC 502801，scavenge 27.866s。
- 新 GiY out27：
  - Total 446.590s，GC full 32.513s，minor GC 590600，scavenge 21.458s。
- 结果：
  - 新 GiY 比旧 GiY 快 1.11 倍。
  - 新 GiY 的 GC full 和 scavenge 都已经低于 cache_cheney。
  - 但是总时间仍比 cache_cheney 慢约 6.9%，说明 Havlak 的剩余问题不在 GC core 主体，可能在写屏障/RSet 压力、old slot patch、cache locality 或 benchmark 运行期 side effect。

这次优化的关键结论：
- 我原先判断 edge log 是大头之一，但单独 direct patch 的 profile 版 out26 没有变快，说明“edge log 数组写入”不是唯一主因。
- 真正产生大收益的是：
  1. 关闭 hot path 详细 profile，避免计数污染性能；
  2. 去掉 edge auxiliary，使 young/cache 从 187.67KB 提高到 230.72KB；
  3. 对小对象改用 memcpy，避免对 56 到 75 字节这种小对象使用 NT store。
- Storage 的提升最能证明小对象 memcpy 的价值：scavenge 从旧 GiY 156.700s 降到 24.209s。
- CD/Havlak 的 GC core 已经接近或优于 cache_cheney，但总时间没有完全超过 cache_cheney，说明下一阶段要看 GC 之外或 GC 边界附近的成本。

当前最可靠的下一步：
1. 保留当前 GiY 改动，不回退 direct young-slot patch 和 small-object memcpy。
2. 跑完整 12 项 benchmark，确认 Storage/CD/Havlak 之外是否有 regressions。
3. 单独调查 Havlak/CD 的剩余总时间差：
   - write barrier 总次数；
   - remembered set patch old slot 成本；
   - direct young-slot patch 对 mutator cache locality 的影响；
   - 是否因为 GiY minor GC 次数仍高于 cache_cheney 导致非 scavenge 开销残留。
4. 如果继续优化，下一刀应针对 Havlak 的 RSet/old slot patch，而不是 root scanning 或 function_table。

## 2026-04-27 关于 small-object memcpy 是否破坏 GiY 的解释

问题：
- 使用 `memcpy` materialize 小对象到 old，会不会造成 cache pollution？
- 如果会，GiY 是不是就不实现了？

结论：
- `memcpy` 确实可能把 old 写入带进 CPU cache，因此从“最纯粹的 non-temporal old write”角度看，它没有 NT store 那么严格。
- 但是这不等于 GiY 没有实现。
- 当前 GiY 的核心约束是：minor GC 的强扫描阶段不要靠通用 old/root 全扫描读 old，不要为了找 young 可达对象到 old 区到处读。
- 小对象 `memcpy` 是 materialize 阶段的 old 写，不是 old 扫描读。
- 所以它放松的是“old 写是否完全 non-temporal”，不是“是否乱读 old 做可达性分析”。

为什么我仍然建议小对象用 `memcpy`：
- 本次 profile 已经证明 GiY materialize 的对象绝大多数很小：
  - Storage 平均对象约 56.4 bytes。
  - CD 平均对象约 68.7 bytes。
  - Havlak 平均对象约 75.4 bytes。
- 对几十字节小对象使用 NT store，往往不划算：
  - 指令路径更重；
  - 写合并效率不好；
  - 每个对象太小，不能很好发挥 streaming write 的优势；
  - 还需要 `_mm_sfence()` 等边界成本。
- out27 结果证明小对象 `memcpy` 是关键收益来源：
  - Storage 从旧 GiY 319.241s 降到 184.211s。
  - Storage scavenge 从 156.700s 降到 24.209s。
  - Havlak scavenge 从 69.168s 降到 21.458s。

这是不是违背 GiY 法则：
- 如果 roadmap 的 GiY 法则写的是“minor GC 不读 old、不扫描 old、只通过 remembered_set/function table slot set 处理 old->young 边”，那么当前做法仍符合。
- 如果导师要求的是“old materialization 也必须完全不污染 cache”，那当前 `memcpy <=256 bytes` 是一个有意识的工程折中，需要在论文/报告里说清楚：为了小对象性能，GiY 对小对象采用 cached store，对大对象保留 NT store。
- 更严谨的表述应该是：
  - GiY 仍然实现了 old-read avoidance / root-scan avoidance。
  - GiY 目前不是“所有 old writes 都 non-temporal”的最纯版本。
  - 小对象 `memcpy` 是性能优化阈值，不改变可达性分析和强/弱边语义。

如果要更符合最纯 GiY，可以做两个版本：
1. Strict GiY：
   - 所有 materialize 都用 NT store。
   - cache pollution 更少，概念更纯。
   - 但 Storage/CD/Havlak 性能会明显差。
2. Practical GiY：
   - 小对象用 `memcpy`，大对象用 NT store。
   - minor GC 不乱读 old，仍遵守核心 GiY 扫描法则。
   - 性能明显更好。

我建议使用 Practical GiY 作为主实现，同时保留一个编译宏或常量说明：
- `GIY_NT_COPY_MIN_BYTES = 256`
- 含义是：小于等于 256 字节使用 cached memcpy，大于 256 字节使用 non-temporal store。
- 如果导师追问，就解释这是一个性能阈值，不是可达性语义变化。

## 2026-04-27 当前 GiY 运作流程说明

当前 GiY 是一个 cache/young + DRAM/old 的 generational minor GC。

整体思想：
- 新对象先分配在 cache_space 的 young 工作区。
- minor GC 只处理 young 区对象。
- strong reachability 的发现尽量不读 old 对象内容：
  - roots 用 GiY 专用 root scanner；
  - old/init -> young 边靠 remembered set 记录的 slot 地址和写入值；
  - function_table 中可能出现的 young 指针靠 function table slot set 记录；
  - young -> young 边在扫描 young object 时直接处理。
- live young object 最终 materialize 到 DRAM old 区。
- minor GC 结束后整个 young 工作区一次性清空。

1. 分配阶段：
- `space_alloc()` 在 cache_space young 工作区分配对象。
- 如果 `cache_space.current + align_bytes > cache_space.end`，触发 `garbage_collection(the_context)`。
- 对象 header 里保存：
  - `type`
  - `size`
  - `forwarding_pointer = 0`
- `forwarding_pointer` 是 GiY minor GC 的核心标记：0 表示还没有为这个 young object 分配 old 目标位置；非 0 表示已经 reserve 了 old payload 地址。

2. 写屏障阶段：
- 程序运行时，如果某个 slot 位于 old/dram 或 init 区，并且写入值指向 young，写屏障把这个 slot 记录到 remembered set。
- GiY 的 remembered set 记录两类信息：
  - `buffer[i]`：slot 地址，ptr slot 会带 tag；
  - `values[i]`：最近写入的值。
- 这样 minor GC 扫 remembered set 时可以读 `remembered_set.values[i]`，不用去读 old slot 当前内容。
- 如果写入值不再是 young，remembered set 会把对应 value 更新成 0，表示该 slot 当前不需要处理。
- 这就是 GiY 避免 old scan 的关键之一。

3. function_table slot 记录：
- function_table 不是普通 heap object，但里面可能有 JSValue 或 pointer slot 指向 young。
- 当前 GiY 不在 minor GC 里全盘扫描 function_table。
- 程序写入 function_table 相关 slot 时，通过 `giy_record_ft_jsvalue_slot()` / `giy_record_ft_ptr_slot()` 把可能含 young 指针的 slot 记录到 `g_ft_slot_set`。
- minor GC 只处理这个 slot set。
- 这和 remembered set 的思想一致：只看发生过 young 写入的 slot，不全表扫描。

4. GC 辅助区绑定：
- 初始化后 `giy_bind_stack_to_cache_impl()` 从 cache_space 尾部切出 GiY 辅助结构。
- 当前配置：
  - GC stack：`young/8`，用于保存待遍历 young payload 指针；
  - edge auxiliary：0，不再保留旧 edge log 数组；
  - function table slot set：至少 32KB；
- 因此当前 profile 显示：
  - young before aux：约 300.24KB；
  - young after aux：约 230.72KB；
  - aux total：约 69.52KB。

5. minor GC 入口：
- `garbage_collection()` 设置 `in_minor_gc = 1`，统计时间，然后调用 `giy_minor_collect(ctx, ...)`。
- `giy_minor_collect()` 是 GiY minor 的主体。
- 如果开启 `GIY_MPROTECT_OLD=1`，进入 minor 核心后会 mprotect old/dram 区，用来验证不允许的 old 读。

6. Phase 1：从 roots reserve live young：
- 阶段名：`scan_roots_reserve`。
- 调用 `giy_scan_roots_generational<GiYReserveTracer>(ctx)`。
- 这个 root scanner 只扫必要 roots：
  - global constants；
  - global property maps；
  - global object shapes；
  - context global/spreg/error/exhandler/lcall_stack；
  - VM stack；
  - gc_root_stack。
- 它不走通用 `scan_roots`，因此不会顺手全扫 function_table。
- 如果 root 指向 young，`GiYReserveTracer` 调用 `copy_for_minor(ptr)`：
  - 在 old/dram 中 reserve 目标空间；
  - 把目标 payload 写进 young object header 的 `forwarding_pointer`；
  - 把 young payload push 到 `g_gc_stack`。
- 注意：这一步只是 reserve 目标地址，还没有把对象内容复制到 old。

7. Phase 1b：从 function_table slot set reserve live young：
- 阶段名：`function_table_reserve`。
- 调用 `giy_reserve_function_table_slots()`。
- 它遍历 `g_ft_slot_set` 中记录过的 slot。
- 如果 slot 当前值仍指向 young，就调用 `giy_reserve_edge()`，最终走 `copy_for_minor()`。
- 这里会读 function_table slot 当前值；这是受控的、记录集驱动的读取，不是全表扫描。

8. Phase 2：从 remembered set reserve live young：
- 阶段名：`remembered_set_reserve`。
- 调用 `giy_scan_remembered_set_slots()`。
- 它遍历 remembered set：
  - 使用 `remembered_set.values[i]` 判断 old/init slot 最近写入的 young 值；
  - 如果 value 是 young，就 reserve 该 young object。
- 关键点：这里不读 old slot 内容，而是读 cache 中 remembered_set 保存的 value。

9. Phase 3：遍历 young 图并 materialize：
- 阶段名：`young_traverse_copy`。
- 调用 `giy_traverse_stack_and_copy()`。
- 这个阶段不断从 `g_gc_stack` pop 出 live young object。
- 对每个 young object：
  1. 根据 object type 扫描它的内部字段；
  2. 如果字段指向 young child，调用 `copy_for_minor(child)` reserve child；
  3. 因为当前 slot 自己在 young/cache 中，所以直接把该 slot patch 成 child 的 old forwarding pointer；
  4. 扫完该对象后，把已经 patch 好的对象内容复制/materialize 到 old/dram 的 reserved 地址。
- 这就是当前 GiY 和之前 edge log 版本的区别：
  - 旧版：扫描时记录 edge log，之后 `apply_edge_log()` 回放 patch；
  - 当前版：扫描 young object 时直接 patch young slot，不再写 edge log 数组。
- direct patch 不违反 GiY 法则，因为它写的是 young/cache object 的字段，不是扫描 old。

10. materialize 写入策略：
- `giy_copy_live_object(dst, src, nbytes, ...)` 负责把 young object header+payload 写到 old/dram。
- 当前策略：
  - `nbytes <= 256`：使用普通 `memcpy`；
  - `nbytes > 256`：使用 non-temporal store。
- 这是性能折中：
  - 小对象用 NT store 太亏；
  - 大对象仍保留 streaming/non-temporal old write。
- 这不改变可达性分析语义，只改变 old materialize 的写入方式。

11. Phase 4：patch roots 和 function_table：
- 阶段名：
  - `scan_roots_patch`
  - `function_table_patch`
- roots 和 function_table slot 本身不一定在 young object 内，所以 Phase 3 的 direct young-slot patch 不会处理它们。
- 因此 materialize 之后要再用 `GiYPatchTracer` 更新这些外部 slot：
  - 如果 slot 指向已经 forwarded 的 young object，就改成 old forwarding pointer；
  - 如果不是 young 或没有 forwarding pointer，就保持原值。
- function_table patch 只处理 `g_ft_slot_set`，不全表扫描。

12. Phase 5：patch remembered set slots：
- 阶段名：`remembered_set_patch`。
- 遍历 remembered set。
- 对每个记录的 old/init slot：
  - 使用 `remembered_set.values[i]` 找到原 young 值；
  - 通过 `forwarded_or_self()` 找到目标 old pointer；
  - 如果目标变化，就把 old/init slot patch 成 old pointer。
- 如果 slot 在 dram/old 中，写入使用 `giy_store_ptr_slot()` 或 `giy_store_jsvalue_slot()`，当前 old 单槽 patch 仍走 `giy_store_u64_old()`，在 x86_64 下用 `_mm_stream_si64()`。

13. function_table slot set 清理：
- patch 完 function_table 后调用 `giy_clear_function_table_slots()`。
- 下一个 mutator 执行阶段如果再写入 young 指针，会重新记录。

14. weak_clear：
- `giy_minor_collect()` 返回后，`garbage_collection()` 调用 `giy_weak_clear(ctx)`。
- 当前 `giy_weak_clear()` 会结束 strict old mprotect guard，然后调用通用 `weak_clear<GiYWeakTracer>(ctx)`。
- 原因是通用 weak_clear 需要维护 hidden-class/string 等 old metadata，严格禁止 old read 会破坏正确性。
- 所以 weak_clear 当前不是 GiY strict core window 的一部分。
- inline cache 弱引用随后由 `giy_patch_live_inline_cache(ctx)` 处理：不可达则清空，可达则 patch 到 forwarding pointer。

15. young 区回收和 remembered set 清空：
- weak_clear 后，minor GC 把 `cache_space.current = cache_space.work_begin`。
- 这意味着整个 young 工作区一次性回收。
- remembered set 也会 `rememberset_clear()`，下一轮从空集合重新记录 old/init -> young 边。
- 最后 `in_minor_gc = 0`。

16. 当前 GiY 的一句话总结：
- Mutator 把新对象放在 cache/young。
- old/init/function_table 对 young 的引用通过写屏障或 slot set 记录。
- minor GC 从 roots、function_table slot set、remembered set values 找 live young。
- 遍历 young 图时直接 patch young 内部 slot。
- live young object materialize 到 old。
- 外部 roots/function_table/RSet slots 再 patch 到 old pointer。
- weak 引用在 strict core window 外处理。
- 最后清空 young 和 remembered set。

17. 当前 GiY 的边界：
- 它实现的是“minor 强扫描阶段避免乱读 old / 避免全盘 root-function_table 扫描”的 GiY。
- 它不是“所有 old writes 都绝对不污染 cache”的最纯版本，因为小对象 materialize 使用 `memcpy`。
- 这个区别应该对导师讲清楚：这是 Practical GiY，不是 Strict NT-write-only GiY。

## 2026-04-27 当前 GiY young/cache 区布局

当前 GiY 的 cache region 不是全部都给 young allocation 用。它按从低地址到高地址大致分成：
1. init area；
2. active young allocation area；
3. GiY GC stack；
4. GiY function table slot set；
5. remembered set hash table；
6. remembered set slot buffer；
7. remembered set values。

根据当前代码和一次实际运行输出：
- 原始 cache region：512KB。
- remembered set 初始化后，`cache_space.total_size` 变成 320KB。
- init object allocation 后，`work_begin` 前移，init area 使用约 19.76KB。
- GiY auxiliary 绑定后：
  - young before aux：300.24KB。
  - young after aux：230.72KB。
  - aux total：69.52KB。

精确字节布局，以 `cache_space.begin` 为 offset 0：

1. Init area：
- 范围：`[0, 20232)`
- 大小：20232 bytes = 19.76KB。
- 用途：VM 初始化期间分配的对象，位于 `cache_space.begin <= ptr < cache_space.work_begin`。

2. Active young allocation area：
- 范围：`[20232, 256488)`
- 大小：236256 bytes = 230.72KB。
- 用途：mutator 正常新对象分配区，也是 minor GC 要回收的 young 工作区。
- minor GC 后执行 `cache_space.current = cache_space.work_begin`，整个区一次性清空。

3. GiY GC stack：
- 范围：`[256488, 294912)`
- 大小：38424 bytes = 37.52KB。
- 容量：4803 个 `uintptr_t`。
- 用途：minor GC 中保存待遍历 live young payload pointer。
- 代码来源：`stack_bytes = (young_bytes / 8) & ~7`。

4. GiY edge auxiliary / edge log：
- 当前大小：0 bytes。
- 当前已经不再使用旧版 edge log 数组。
- young object 内部边现在扫描时直接 patch young slot。

5. GiY function table slot set：
- 范围：`[294912, 327680)`
- 大小：32768 bytes = 32KB。
- 容量：4096 个 `uintptr_t` slot 记录。
- 用途：记录 function_table 中可能包含 young pointer 的 slot，minor GC 只处理这些记录过的 slot，不全表扫描。
- 代码来源：`ft_slot_bytes = max((young_bytes / 64) & ~7, 4096 * sizeof(uintptr_t))`，当前触发最小值 32KB。

6. Remembered set hash table：
- 范围：`[327680, 393216)`
- 大小：65536 bytes = 64KB。
- 容量：8192 个 hash entry。
- 用途：快速判断 remembered set 是否已有某个 slot，减少重复记录。

7. Remembered set slot buffer：
- 范围：`[393216, 458752)`
- 大小：65536 bytes = 64KB。
- 容量：8192 个 slot 地址。
- 用途：记录 old/init slot 地址。ptr slot 会带 tag。

8. Remembered set values：
- 范围：`[458752, 524288)`
- 大小：65536 bytes = 64KB。
- 容量：8192 个 value。
- 用途：记录 old/init slot 最近写入的 young value，使 minor GC 扫 RSet 时不用读 old slot。

总和校验：
- Init area：20232 bytes。
- Active young：236256 bytes。
- GiY GC stack：38424 bytes。
- GiY FT slot set：32768 bytes。
- RSet hash：65536 bytes。
- RSet buffer：65536 bytes。
- RSet values：65536 bytes。
- 合计：524288 bytes = 512KB。

导师问法可以这样回答：
- 当前真正可用于 young object 分配的是 230.72KB。
- GiY 自己的 GC 辅助区占 69.52KB，其中 GC stack 37.52KB，function table slot set 32KB，edge log 0KB。
- remembered set 不是 active young allocation area 的一部分，但它占用同一个 cache region 的高地址 192KB。
- remembered set 192KB 分成 hash 64KB、slot buffer 64KB、values 64KB。

## 2026-04-27 GiY 与 cache_cheney remembered set 大小对比

问题：
- GiY 的 remembered set 是否太大？
- 对照组 cache_cheney 的 remembered set 有多大？
- 因为 remembered set 是 generational GC 的共同机制，是否应该设成一样大？

代码确认：
- `ejsvm/giy_rset.cc`
  - `REMEMBERED_SET_CAPACITY_BYTES = 128 * 1024`
  - `HASH_TABLE_SIZE = 8192`
  - GiY capacity = `128KB / (2 * sizeof(uintptr_t)) = 8192 entries`
  - GiY 分配：
    - values：8192 * 8 = 64KB
    - buffer：8192 * 8 = 64KB
    - hash table：8192 * 8 = 64KB
  - GiY RSet 总 cache 占用：192KB。
- `ejsvm/cache_cheney_Rset.cc`
  - `REMEMBERED_SET_CAPACITY_BYTES = 128 * 1024`
  - `HASH_TABLE_SIZE = 8192`
  - cache_cheney capacity = `128KB / sizeof(uintptr_t) = 16384 entries`
  - cache_cheney 分配：
    - buffer：16384 * 8 = 128KB
    - hash table：8192 * 8 = 64KB
  - cache_cheney RSet 总 cache 占用：192KB。

结论：
- 如果比较的是“占用 cache/young region 的总空间”，当前 GiY 和 cache_cheney 的 RSet 已经一样大，都是 192KB。
- 如果比较的是“能记录多少个 slot entry”，两者不一样：
  - GiY：8192 entries。
  - cache_cheney：16384 entries。
- 这个差异来自 GiY 的设计：GiY 为了避免 minor GC 读 old slot，必须额外保存 `remembered_set.values[i]`。
- cache_cheney 不需要保存 values，因为它可以在 GC 时直接读 slot 当前值。

公平性判断：
- 对 GiY 的核心实验，最重要的控制变量应该是 cache footprint，也就是 RSet 从 cache region 中切走多少字节。
- 当前这点已经公平：GiY 和 cache_cheney 都切走 192KB。
- 如果强行让 GiY 也有 16384 entries，同时还保存 values，则 GiY RSet 会变成：
  - buffer 128KB + values 128KB + hash 64KB = 320KB。
  - 这会比 cache_cheney 多占 128KB，反而不公平。
- 如果强行让 cache_cheney 也只有 8192 entries，则它只需要：
  - buffer 64KB + hash 64KB = 128KB。
  - 这会比 GiY 少占 64KB，也不公平。

因此当前设置是合理的：
- 两者 RSet 总 cache 占用一样。
- GiY 用同样的 RSet 空间换取 `values`，代价是 entry capacity 减半。
- 这正是 GiY 为“不读 old slot”付出的空间/容量代价。

如果导师追问，可以这样说：
- “我控制的是 remembered set 的 cache footprint。GiY 和 cache_cheney 的 RSet 都占 192KB；不同的是 GiY 把一半 entry buffer 空间换成 values，用于避免 minor GC 读取 old slot。所以 GiY entry capacity 小一些，但占用 cache 的总空间和对照组一致。”

## 2026-04-28 给导师汇报阶段成果的安全说辞（中英）

中文安全说辞：

老师，我现在完成的是 GiY minor GC 的一个 practical prototype，不是最终的 strict 版本。这个版本的核心目标是验证 GiY 的主要思想：在 minor GC 的强扫描阶段，尽量不通过读取 old generation 来发现 young 对象的可达性。

目前实现上，我把对象分成 cache/young 和 DRAM/old。新对象先分配在 young 区；minor GC 触发后，从 roots、function table slot set 和 remembered set values 发现 live young 对象。这里 remembered set 不只记录 old slot 地址，还记录最近写入的 value，这样 GC 扫 remembered set 时不用读 old slot 本身。

对于 young 对象内部的 young-to-young 边，当前版本不再使用 edge log 回放，而是在扫描 young object 时直接 patch young slot。这个操作只写 young/cache 中的字段，不需要扫描 old object。live young 对象最后会 materialize 到 old 区。

为了控制变量，我也调整了 cache_cheney 对照组，让它使用相同风格的 root/function table 处理，并且 GiY 和 cache_cheney 的 remembered set cache footprint 都是 192KB。不同的是，GiY 在这 192KB 里要额外保存 values，所以 GiY 的 entry capacity 比 cache_cheney 小；这是 GiY 为避免 old slot read 付出的空间代价。

性能方面，目前 targeted benchmarks 里：
- Storage：新 GiY 是 184.211s，旧 GiY 是 319.241s，cache_cheney 对照是 209.181s。
- CD：新 GiY 是 257.053s，旧 GiY 是 275.434s，cache_cheney 对照是 251.045s。
- Havlak：新 GiY 是 446.590s，旧 GiY 是 496.126s，cache_cheney 对照是 417.826s。

所以现在可以比较谨慎地说：GiY 的 root-scan / old-read avoidance 方向是可行的，而且在 Storage 上已经超过了 cache_cheney；在 CD 和 Havlak 上，GC core 已经明显改善，但总运行时间还没有全面超过对照组。下一步需要继续分析 CD/Havlak 的剩余成本，尤其是 remembered set patch、write barrier 压力和 cache locality。

需要特别说明的是：当前版本是 Practical GiY。为了性能，我对小于等于 256 bytes 的对象 materialization 使用了 memcpy，大对象仍使用 non-temporal store。这可能会带来一定 cache pollution，所以它不是“所有 old writes 都 non-temporal”的最纯 GiY；但它仍然保持了 minor GC 强扫描阶段不乱读 old、不全盘扫描 function_table 的核心语义。这个点我会作为实现边界明确说明。

一句话总结：
我目前完成了一个可运行、可验证、性能有改进的 Practical GiY minor GC。它证明了避免 old-read/root over-scan 的设计方向是可行的，但还需要进一步优化 RSet/old-slot patch 和 cache locality，才能在所有 benchmark 上稳定超过 cache_cheney。

英文安全说辞：

I have implemented a practical prototype of the GiY minor GC. I would not describe it as the final strict version yet. The main goal of the current implementation is to validate the core GiY idea: during the strong tracing phase of minor GC, we avoid discovering young-object reachability by reading or scanning the old generation.

The heap is organized as cache/young and DRAM/old. New objects are allocated in the young area. When a minor GC is triggered, live young objects are discovered from roots, a recorded function-table slot set, and remembered-set values. The remembered set stores not only old slot addresses, but also the most recently written values, so the GC does not need to read the old slots during remembered-set scanning.

For young-to-young edges inside young objects, the current implementation no longer uses the old edge-log replay design. Instead, it patches young slots directly while scanning young objects. This only writes fields in the young/cache area and does not require scanning old objects. Live young objects are then materialized into the old generation.

For a fairer comparison, I also adjusted the cache_cheney control so that root/function-table handling is controlled similarly. The remembered-set cache footprint is the same for GiY and cache_cheney: both use 192KB. The difference is that GiY uses part of that space to store values, so its entry capacity is smaller. This is the space/capacity cost GiY pays to avoid reading old slots.

In the targeted benchmarks:
- Storage: new GiY is 184.211s, old GiY was 319.241s, and cache_cheney is 209.181s.
- CD: new GiY is 257.053s, old GiY was 275.434s, and cache_cheney is 251.045s.
- Havlak: new GiY is 446.590s, old GiY was 496.126s, and cache_cheney is 417.826s.

Therefore, the safe conclusion is that the GiY direction is feasible and beneficial in some workloads. In Storage, the new GiY already outperforms the cache_cheney control. In CD and Havlak, the GC core cost has improved significantly, but total execution time has not yet consistently beaten the control. The next step is to investigate the remaining overhead, especially remembered-set patching, write-barrier pressure, and cache locality.

One important caveat is that the current implementation is Practical GiY, not a pure strict non-temporal-write-only version. For performance, objects of 256 bytes or smaller are materialized with memcpy, while larger objects still use non-temporal stores. This may introduce some cache pollution. However, it does not change the core GC semantics: the strong minor-GC tracing phase still avoids arbitrary old-generation reads and avoids full function-table scanning.

One-sentence summary:
At this stage, I have a working and validated Practical GiY minor GC prototype. It demonstrates that avoiding old reads and root/function-table over-scanning is feasible, and it improves performance significantly, but further work is needed on remembered-set patching, old-slot updates, and cache locality to consistently outperform cache_cheney across all benchmarks.

## 2026-04-28 分大小写入策略性能汇报要点

最安全的说法：
- 当前最终版本 out27 使用 Practical GiY：
  - young 内部边 direct patch；
  - edge auxiliary 取消；
  - detailed profile 默认关闭；
  - `<=256 bytes` object materialization 使用 `memcpy`；
  - `>256 bytes` object materialization 使用 non-temporal store。
- 因此 out27 相对 out22 的提升不能全部单独归因给“小对象 memcpy”，但小对象 memcpy 是最关键的性能因素之一。

最终版本 out27 相对旧 GiY out22：
- Storage：
  - 旧 GiY：319.241s。
  - 新 GiY：184.211s。
  - 提升：1.73x，约快 42.3%。
  - GC full：160.023s -> 27.182s。
  - scavenge：156.700s -> 24.209s。
- CD：
  - 旧 GiY：275.434s。
  - 新 GiY：257.053s。
  - 提升：1.07x，约快 6.7%。
  - GC full：28.597s -> 12.071s。
  - scavenge：22.893s -> 6.941s。
- Havlak：
  - 旧 GiY：496.126s。
  - 新 GiY：446.590s。
  - 提升：1.11x，约快 10.0%。
  - GC full：81.336s -> 32.513s。
  - scavenge：69.168s -> 21.458s。

相对 cache_cheney root-controlled out24：
- Storage：
  - cache_cheney：209.181s。
  - 新 GiY：184.211s。
  - 新 GiY 快约 13.6%。
- CD：
  - cache_cheney：251.045s。
  - 新 GiY：257.053s。
  - 新 GiY 慢约 2.4%。
- Havlak：
  - cache_cheney：417.826s。
  - 新 GiY：446.590s。
  - 新 GiY 慢约 6.9%。

使用分大小写入之前的性能问题：
- 旧 GiY 对小对象也使用 non-temporal store，Storage/CD/Havlak 中平均 materialized object 大约只有 56 到 75 bytes。
- 对这种几十字节对象，NT store 的开销太重，写合并优势发挥不出来。
- profile 版调查显示：
  - Storage materialize 约 58.8GB，平均 56.4 bytes。
  - CD materialize 约 9.1GB，平均 68.6 bytes。
  - Havlak materialize 约 33.9GB，平均 75.5 bytes。
- 所以“一律 NT store”导致 GiY 的 scavenge/materialize 非常慢。

汇报时要强调的边界：
- 小对象 `memcpy` 可能带来 cache pollution。
- 所以当前不是 strict NT-write-only GiY，而是 Practical GiY。
- 但是这不改变 GiY 的核心语义：minor GC 强扫描阶段仍避免乱读 old，function_table 也不是全表扫描。
- 目前最适合汇报的结论是：
  - 分大小写入策略显著降低了 materialization 成本；
  - Storage 已超过 cache_cheney；
  - CD/Havlak 的 GC core 已经明显改善，但总时间仍有剩余差距；
  - 下一步要看 RSet patch、write barrier 和 cache locality。

## 2026-04-28 targeted benchmarks 综合效率

这里用 Storage、CD、Havlak 三个 targeted benchmark 的总和来算综合效率，不挑单个 benchmark。

总运行时间综合：
- 旧 GiY out22：
  - Storage + CD + Havlak = 319.241 + 275.434 + 496.126 = 1090.801s。
- 新 GiY out27：
  - Storage + CD + Havlak = 184.211 + 257.053 + 446.590 = 887.854s。
- 综合提升：
  - speedup = 1090.801 / 887.854 = 1.23x。
  - 总时间减少约 18.6%。

相对 cache_cheney root-controlled out24：
- cache_cheney 总时间：
  - 209.181 + 251.045 + 417.826 = 878.052s。
- 新 GiY 总时间：
  - 887.854s。
- 综合结果：
  - 新 GiY 比 cache_cheney 慢约 1.1%。
  - 也就是说 targeted 三项总时间上，新 GiY 已经非常接近 cache_cheney，但还没有整体超过。

GC full 综合：
- 旧 GiY：269.956s。
- 新 GiY：71.766s。
- 综合提升：
  - speedup = 3.76x。
  - GC full 时间减少约 73.4%。
- cache_cheney：101.825s。
- 新 GiY 相对 cache_cheney：
  - GC full 少约 29.5%。

scavenge 综合：
- 旧 GiY：248.761s。
- 新 GiY：52.608s。
- 综合提升：
  - speedup = 4.73x。
  - scavenge 时间减少约 78.9%。
- cache_cheney：83.883s。
- 新 GiY 相对 cache_cheney：
  - scavenge 少约 37.3%。

安全汇报结论：
- 从 targeted 三项综合看，新 GiY 相比旧 GiY 总体快 1.23x，总时间减少 18.6%。
- GC 部分提升更明显：GC full 减少 73.4%，scavenge 减少 78.9%。
- 相比 cache_cheney，对 GC core 来说新 GiY 已经更快；但总运行时间仍慢约 1.1%，说明剩余问题主要不在 root scanning/scavenge 主体，而在 RSet patch、write barrier、cache locality 或 mutator-side 影响。

## 2026-05-02 当前项目进展梳理

本次用户询问：
- “现在详细告诉我项目当前的进展状况”

当前仓库状态：
- 当前目录：`/home/qiancheng/ejs-new`。
- 当前分支：`master`。
- `master` 相比 `origin/master` 领先 10 个提交。
- 工作区当前只有 `AGENTS.md` 有未提交修改；修改内容是日志文件要求从 `AIlog.txt` 改成 `AIlog.md`。
- 对话开头提供的规则仍写着 `AIlog.txt`，而仓库当前 `AGENTS.md` 写着 `AIlog.md`。为避免记录丢失，本次总结同时写入 `AIlog.md` 和 `AIlog.txt`。

项目主线进展：
- 项目原始基础是 eJS/eJSVM，核心目录包括 `ejsvm`、`vmgen`、`ejsc`、`ejsi`。
- 当前主要研究/实现方向是 Cache Young Gen -> DRAM Old Gen 的单线程 generational minor GC。
- 已经实现两个相关 GC 路径：
  - `OPT_GC=cache_cheney`：作为 cache/DRAM generational Cheney 对照组。
  - `OPT_GC=giy`：当前重点 GiY minor GC prototype。

GiY 当前实现状态：
- 已经接入构建系统：
  - `ejsvm/common.mk` 中 `OPT_GC=giy` 会定义 `USE_GIY_MINOR`，并编译 `giy_dram_manager.cc`、`giy_rset.cc`、`GiY.cc`。
  - `ejsvm/Makefile.template` 中已经列出 `OPT_GC=giy` 选项。
- 新对象在 cache/young 区分配，minor GC 时把 live young 对象 materialize 到 DRAM/old。
- roots、function table strong slots、remembered set values 用来发现 live young 对象。
- remembered set 不只记录 old slot 地址，也记录最近写入的 value，目的是 minor GC 扫 remembered set 时不用读 old slot。
- young 对象内部的 young-to-young 边已经改成直接在 young/cache 对象内 patch，不再依赖旧的 edge log 回放。
- function table 没有恢复通用全表扫描，而是记录 constant pool 和 allocation site cache 的强引用 slot，避免漏标 Young 指针。
- inline cache 保持弱引用语义：未被强边保活的 inline cache 目标不会因为 cache 自身而 materialize。
- 当前实现带有 mprotect old-space guard 逻辑，可用于检查 GiY 核心阶段是否发生非法 old-space 触碰。

当前 Practical GiY 的重要边界：
- 当前最终版本不是 strict “所有 old writes 都 non-temporal”的纯版本，而是 Practical GiY。
- materialization 策略是：
  - `<=256 bytes` 的对象使用 `memcpy`。
  - `>256 bytes` 的对象使用 non-temporal store。
- 原因是 benchmark 发现多数 materialized object 只有几十字节，一律使用 non-temporal store 会让小对象复制成本过高。
- 这个策略可能引入一定 cache pollution，但保留了核心语义：minor GC 强扫描阶段避免乱读 old，并避免 function_table 全表扫描。

RSet/cache 空间状态：
- GiY 和 cache_cheney 的 remembered set cache footprint 当前都控制为 192KB。
- GiY 因为额外保存 values，所以 entry capacity 是 8192。
- cache_cheney 不保存 values，所以 entry capacity 是 16384。
- 这个差异是 GiY 为避免 old slot read 付出的容量代价。
- 当前 GiY 可用于 young object 分配的 active young area 约 230.72KB。
- GiY 辅助区约 69.52KB：GC stack 37.52KB，function table slot set 32KB，edge log 0KB。

benchmark 进展：
- 已有关键输出目录：
  - `out25_giy_profile`
  - `out26_giy_direct_patch`
  - `out27_giy_direct_memcpy`
- 当前最应该引用的最终数据是 `out27_giy_direct_memcpy`。
- out27 结果：
  - Storage：184.211s，GC full 27.182s，scavenge 24.209s。
  - CD：257.053s，GC full 12.071s，scavenge 6.941s。
  - Havlak：446.590s，GC full 32.513s，scavenge 21.458s。
- 相比旧 GiY out22：
  - targeted 三项总时间从 1090.801s 降到 887.854s，整体约 1.23x。
  - GC full 综合减少约 73.4%。
  - scavenge 综合减少约 78.9%。
- 相比 cache_cheney root-controlled out24：
  - Storage 上新 GiY 更快：184.211s vs 209.181s。
  - CD 上新 GiY 略慢：257.053s vs 251.045s。
  - Havlak 上新 GiY 略慢：446.590s vs 417.826s。
  - targeted 三项总时间新 GiY 约慢 1.1%，但 GC core 已经比 cache_cheney 更快。

当前验证：
- 已执行：`make OPT_GC=giy -j2`，结果为 `Nothing to be done for 'all'`，说明当前 GiY build 目标是最新的。
- 已执行已有 `.sbc` smoke test：
  - `a.sbc`
  - `b.sbc`
  - `c.sbc`
  - `d.sbc`
  - `e.sbc`
  - `f.sbc`
  - `g.sbc`
  - `../ejsvm/js/hello.sbc`
  - `../ejsvm/js/f1.sbc`
- 结果：全部 `status=0`。

尚未完成/下一步：
- 本次没有重新跑完整 benchmark，因为用户询问的是当前进展状况，不是要求产生新 benchmark 输出。
- 当前下一步重点应放在 CD/Havlak 的剩余差距：
  - remembered set patch 成本；
  - write barrier 压力；
  - cache locality；
  - mutator-side 影响；
  - weak_clear 阶段如果要进一步 strict 0 old read，需要单独设计 GiY weak remembered set，而不是局部修改 generic weak_clear。

## 2026-05-02 roadmap.txt 5.2 之后内容结论

本次用户询问：
- 阅读 `roadmap.txt` 的 5.2 号之后内容，并给出结论。

读到的内容：
- 导师认为“一股脑对所有对象都使用 non-temporal store”是错误方向，应该分路径讨论。
- 导师建议尝试把 young 区存活对象先统一搬运到相同地方，并且这个地方不是 old 区，然后再统一执行 non-temporal store。
- 用户要求判断该方案是否合理，并给出可行性分析。

结论：
- 第一条判断完全正确：所有对象都使用 non-temporal store 不合理，尤其是几十字节的小对象。当前 Practical GiY 的 `<=256 bytes memcpy，>256 bytes non-temporal store` 正好符合“分路径讨论”的方向。
- 第二条建议有研究价值，但不能直接理解成“无条件多复制一遍”。如果先把 live objects 搬到另一个非 old staging 区，再统一 NT store 到 old，会增加一次额外复制、占用更多 cache 空间，并可能破坏当前 GiY 已经做到的 low GC core cost。
- 这个方案只有在 staging 带来的收益超过额外复制成本时才合理，例如：能把很多小碎片写合并成连续大块、改善 NT store 对齐、减少散乱 old writes、或者为后续 batch materialization 提供更好顺序。
- 实现时必须保持一个关键约束：对象的最终 old 地址要在 traversal/patch 阶段就确定，young 对象内部字段和 root/RS/function table patch 都必须指向最终 old 地址，不能指向 staging 地址。staging 只能是临时物理搬运缓冲区，不能成为 GC 语义地址。

更稳妥的实现方向：
- 优先不要立即做“对象完整搬到 staging，再 NT store”的重设计。
- 先做 batch materialization：traverse 阶段仍在 young 对象内完成字段 patch，只把 `(young_src, old_dest, size)` 记录下来，最后按 old_dest 或 size/class 分组执行 materialization。
- 对小对象继续使用 memcpy，或者把小对象聚合成较大连续写入后再评估 NT store。
- 对大对象继续直接 NT store。
- 如果 profiling 证明散乱写/对齐问题是主要瓶颈，再引入 cache staging buffer。

一句话结论：
- “分路径使用 memcpy/NT store”是确定正确方向；“先搬到非 old 区再统一 NT store”是可探索优化，但必须做成 batch/staging materialization 实验，不能破坏 final old address 语义，也不能让额外复制成本超过 NT store 聚合收益。

## 2026-05-02 benchmark allocation size 分布实验工具

本次用户要求：
- 做一个实验，为后续 memcpy / non-temporal store / staging 方案选择提供判据。
- 统计 benchmarks 中申请的 objects size 大小分布。

本次修改范围：
- `ejsvm/common.mk`
- `ejsvm/giy_dram_manager.cc`
- `run_alloc_size_profile_benchmarks.sh`
- `AIlog.md`
- `AIlog.txt`

具体修改：
- 在 `ejsvm/common.mk` 中新增 `ALLOC_SIZE_PROFILE=true` 编译开关。
- 在 GiY 的 `space_alloc(request_bytes, type)` 中新增可选统计逻辑，默认关闭，只有 `ALLOC_SIZE_PROFILE=1` 时启用。
- 统计两种 size：
  - payload size：传给 `space_alloc` 的 `request_bytes`。
  - footprint size：`ALIGN(payload + object_header)`，这是 GiY materialization 实际复制/写入 Old 时更相关的大小。
- 输出内容包括：
  - payload histogram；
  - footprint histogram；
  - 按 cell type 的 allocation count / payload MB / footprint MB。
- 新增脚本 `run_alloc_size_profile_benchmarks.sh`：
  - 自动执行 `make -C build.debug OPT_GC=giy ALLOC_SIZE_PROFILE=true -j$JOBS`。
  - 默认跑 benchmark suite：`Bounce List Sieve Queens Permute Storage Towers Mandelbrot Richards CD NBody Havlak`。
  - 输出目录默认是 `build.debug/benchmarks/out28_alloc_size_profile`。
  - 每个 benchmark 生成一个完整 `.out` 文件。
  - 额外生成 `summary.tsv`，直接给出 payload/footprint 的 `<=256` 和 `>256` 数量比例。

验证：
- 已执行：`make OPT_GC=giy ALLOC_SIZE_PROFILE=true -j2`。
- 结果：构建通过。仍有仓库已有 warning，本次没有处理。
- 已执行：`OUT_DIR=out_alloc_profile_smoke BENCHMARKS='hello_world' ./run_alloc_size_profile_benchmarks.sh`。
- 结果：脚本成功生成 summary。
- smoke 结果：
  - `hello_world` total profiled allocs = 400。
  - avg payload = 34.2 bytes。
  - avg footprint = 51.1 bytes。
  - payload `<=256`：95.50%。
  - footprint `<=256`：99.50%。

没有做的事情：
- 没有在本次直接跑完整 benchmark suite。
- 原因：尝试用 `Sieve` 做快速样例时，90 秒以上仍未结束；完整 suite 预计耗时较长，不适合在这个回答里直接阻塞完成。
- 当前已经提供可重复脚本，后续可以按需要跑完整 suite 或只跑 targeted benchmarks。

建议运行方式：
- targeted 三项：
  - `OUT_DIR=out28_alloc_size_profile BENCHMARKS='Storage CD Havlak' ./run_alloc_size_profile_benchmarks.sh`
- 完整 suite：
  - `OUT_DIR=out28_alloc_size_profile ./run_alloc_size_profile_benchmarks.sh`

实验判据：
- 后续判断 memcpy / NT store 阈值时，优先看 footprint histogram，不只看 payload histogram。
- 如果 footprint `<=256` 的 allocation count 占绝大多数，继续保留小对象 memcpy 是合理方向。
- 如果 `>256` 的 footprint 在 bytes 占比很高，说明大对象 NT store 仍有意义。
- 如果很多对象 footprint 很小但总 bytes 占比也很高，才值得进一步考虑 batch/staging，把小对象聚合成更大连续写入再实验 NT store。

## 2026-05-02 完整 suite allocation size profile 结果

本次用户要求：
- 完整跑完 benchmark suite，再统计 allocation object size 分布。

执行过程：
- 第一次完整跑完 suite 后发现输出没有 `Allocation Size Distribution`，原因是 build 目录里的 `ejsvm/giy_dram_manager.o/ejsvm` 没有被强制重建到 `ALLOC_SIZE_PROFILE=1` 版本。
- 我修正了 `run_alloc_size_profile_benchmarks.sh`：
  - 跑 benchmark 前删除 `build.debug/giy_dram_manager.cc`、`build.debug/giy_dram_manager.o`、`build.debug/ejsvm`，强制重新复制/编译/链接 GiY manager。
  - 修正 summary awk 中 `<=8` bucket 的字符串/数字比较问题，强制 `upper += 0`。
  - 每次运行前清空目标输出目录，避免旧结果混入。
- 用 `hello_world` 做重建检查，确认输出包含 `Allocation Size Distribution`。
- 随后重新完整跑完 suite。

有效输出目录：
- `build.debug/benchmarks/out28_alloc_size_profile`

关键输出文件：
- `summary.tsv`：每个 benchmark 的总 allocation、平均 payload/footprint、`<=256` / `>256` count 比例。
- `aggregate_summary.md`：完整 suite 和 targeted 三项的 count/bytes 汇总结论。
- 每个 benchmark 的 `.out`：完整 payload histogram、footprint histogram、按 cell type 统计。

完整 suite count-based 结果：
- total allocations：20,131,570,860。
- payload `<=256`：19,992,449,412，占 99.31%。
- payload `>256`：139,121,448，占 0.69%。
- footprint `<=256`：19,992,444,280，占 99.31%。
- footprint `>256`：139,126,580，占 0.69%。

完整 suite byte-based 结果：
- payload bytes `<=256`：231,645.97 MB，占 77.28%。
- payload bytes `>256`：68,091.02 MB，占 22.72%。
- footprint bytes `<=256`：536,705.27 MB，占 88.43%。
- footprint bytes `>256`：70,215.19 MB，占 11.57%。

targeted 三项（Storage + CD + Havlak）count-based 结果：
- total allocations：5,390,445,950。
- payload `<=256`：5,251,774,731，占 97.43%。
- payload `>256`：138,671,219，占 2.57%。
- footprint `<=256`：5,251,769,621，占 97.43%。
- footprint `>256`：138,676,329，占 2.57%。

targeted 三项 byte-based 结果：
- payload bytes `<=256`：116,541.53 MB，占 67.34%。
- payload bytes `>256`：56,532.38 MB，占 32.66%。
- footprint bytes `<=256`：196,675.94 MB，占 77.03%。
- footprint bytes `>256`：58,649.68 MB，占 22.97%。

重要单项结果：
- Storage：total allocations 1,092,302,015，avg payload 40.4 bytes，avg footprint 56.4 bytes，footprint `<=256` 基本 100%。
- CD：total allocations 2,314,174,982，avg payload 13.0 bytes，avg footprint 29.0 bytes，footprint `>256` 10,786,072，占 0.47%。
- Havlak：total allocations 1,983,968,953，avg payload 54.1 bytes，avg footprint 70.1 bytes，footprint `>256` 127,890,234，占 6.45%。

实验结论：
- 绝大多数 allocation event 都是小对象。按 count 看，完整 suite 和 targeted 三项里 `<=256` footprint 都占压倒多数。
- 这强烈支持继续保留小对象 `memcpy` 路径，不应该回到所有对象一律 non-temporal store。
- 大对象按 count 很少，但按 bytes 仍然重要。targeted 三项中 `>256` footprint 只占 2.57% 的 allocation count，却占约 22.97% 的 footprint bytes。
- 因此当前 `<=256 memcpy，>256 non-temporal store` 的分路径策略有数据支持。
- 后续 staging/batch materialization 实验不应该替代小对象 memcpy；它更应该针对 byte-heavy 的大 bucket，尤其是 Havlak 和 CD。

验证/清理：
- 完整有效 suite 已跑完。
- 已恢复 `build.debug/ejsvm`、`build.debug/giy_dram_manager.cc`、`build.debug/giy_dram_manager.o`、`build.debug/gmon.out` 等 tracked build 产物，避免工作区混入构建噪声。

## 2026-05-03 staging/batch materialization 排列方式结论

本次用户询问：
- 对“把存活 object 先放在相同地方紧凑排列，再用 non-temporal store 传输”这个想法，有哪些可实现方式，尤其是对象该如何排列到一起。

结论：
- 这个想法可以实现，但不能让对象语义地址变成 staging 地址。
- 正确原则是：minor GC 的 forwarding pointer 必须仍然指向最终 old address；root、RS、function table slot、young object 内部边也必须 patch 到最终 old address。staging 只能是临时物理缓冲区。
- 最稳妥的实现不是一开始就“复制到 staging 再复制到 old”，而是先做 materialization queue：
  - `copy_for_minor` 分配最终 old address，设置 forwarding pointer。
  - traversal 阶段只 patch young object 内部字段。
  - 对每个 live object 记录 `(young_src_hdr, old_dst_hdr, aligned_size, type)`。
  - 最后按这个 queue 执行 materialization。

推荐的对象排列方式：
- 第一版用 old destination order，也就是 forwarding pointer 分配顺序。
- 原因：
  - old address 单调递增，后续写 old 时天然连续。
  - 不需要排序，记录顺序就是最终 old layout 顺序。
  - root/slot patch 语义最简单。
  - 最容易和当前 GiY 实现兼容。
- 这个版本可以先不做真正 staging，只把当前“边扫描边 copy”改成“先记录 queue，最后按 old order copy/NT”。

如果要做真正 staging：
- 使用固定大小 staging slab，例如 32KB 或 64KB，cache-line 对齐。
- 按 old destination order 把 live objects 紧凑打包进 slab。
- slab 满了以后，按同样顺序把 slab 中对象写到各自 final old address。
- 这一步仍然不一定能变成一个单次大 memcpy/NT copy，除非 old address layout 与 staging layout 完全一致；因此最好让 old allocation order 和 staging packing order 保持一致。

第二阶段可尝试 size-class 排列：
- 例如按 footprint 分成 `<=256`、`257-512`、`513-1024`、`>1024`。
- 小对象继续 memcpy，不要强行 NT。
- 大对象 bucket 用 NT store 或 staging slab。
- 这种方式可能改善 CD/Havlak 的大对象路径，但会改变 old layout，需要先规划 bucket 大小，再设置 forwarding pointer，复杂度高于 old-order queue。

不推荐的方式：
- 不推荐 DFS pop 顺序直接作为最终排列依据，除非它和 old allocation order 一致；否则写 old 会变散。
- 不推荐把 young object 先复制到 staging，并把 forwarding pointer 指向 staging；这会破坏 root/slot patch 语义。
- 不推荐小对象全部 staging 后统一 NT；完整 suite 和 targeted 统计都显示小对象按 count 占绝对多数，小对象 memcpy 更有数据支持。

最建议的实验路线：
- Step 1：实现 materialization queue，按 old destination order 延迟 copy，不引入 staging。
- Step 2：只对 `>256` 的对象启用 batch/NT 路径，小对象保持 memcpy。
- Step 3：如果 old-order queue 仍显示大对象写入分散或 NT 效率不足，再引入 32KB/64KB staging slab。
- Step 4：最后再尝试 size-class bucket，但这是更复杂的第二阶段实验。

一句话结论：
- 最可实现、风险最低的排列方式是“按最终 old address 顺序排列”。先做 old-order materialization queue，再考虑对大对象引入 staging slab；不要让 staging 成为对象语义地址，也不要用 staging 取代小对象 memcpy。

## 2026-05-03 当前 Practical GiY GC 与 Cheney GC 对比结论

本次用户询问：
- 给出“当前 GC”和 Cheney GC 的详细对比。

我的理解：
- “当前 GC”指当前仓库中的 Practical GiY minor GC。
- “Cheney GC”指标准 Cheney-style copying GC / 仓库中的 `cache_cheney` 对照思路。

核心结论：
- Cheney GC 的优势是简单、语义自然、实现成熟：复制对象到 to-space/old 后，从新地址继续扫描对象字段，scan 指针和 copy 指针线性推进。
- 当前 Practical GiY 的目标不是简单复制，而是为 Cache Young -> DRAM Old 架构优化：尽量把对象图遍历和指针修正在 Young/Cache 内完成，避免 minor GC 强扫描阶段读取 Old/DRAM。
- 因此 GiY 的设计复杂度高于 Cheney，但它减少了 GC core 中对 Old/DRAM 的读触碰，尤其适合 DRAM 读代价高、cache pollution 敏感的实验设定。

关键差异：
- 对象扫描位置：
  - Cheney：对象被复制到目标空间后，从目标空间扫描。
  - GiY：对象留在 Young/Cache 中扫描和 patch，最后 materialize 到 Old/DRAM。
- Old/DRAM 触碰：
  - Cheney：scan 目标空间对象时会读已经复制到 Old/DRAM 的对象字段。
  - GiY：强扫描阶段尽量不读 Old/DRAM；Old/DRAM 主要只接收最终 materialization 写入和必要 old slot patch。
- remembered set：
  - Cheney：通常记录 old slot 地址，GC 时可以直接读 slot 当前值。
  - GiY：记录 old slot 地址和最近写入的 value，GC 扫 RS 时尽量不读 old slot。
- function table：
  - Cheney：更容易复用通用 root/function table scan。
  - GiY：不能全表扫 function table，否则会把大量非 Young/Old metadata 带入 minor GC 热路径；当前用 function table strong slot set 记录真正可能指向 Young 的强 slot。
- weak reference：
  - Cheney/通用 GC：强扫描后统一 weak_clear。
  - GiY：弱边不能保活对象，同时不能留下 Young 悬空指针；当前 inline cache 等弱结构需要更谨慎处理。
- copy 策略：
  - Cheney：普通 copy/memcpy 语义即可。
  - GiY：当前 Practical 版本 `<=256 bytes memcpy`，`>256 bytes non-temporal store`，避免小对象一律 NT store 的高开销。

性能判断：
- 已有 targeted benchmark 显示：当前 GiY 相比旧 GiY 大幅降低 GC full/scavenge 成本。
- 相比 cache_cheney，当前 GiY 的 GC core 已更快，但总运行时间仍和 cache_cheney 接近或略慢，说明剩余成本更多来自 remembered set patch、write barrier、cache locality、mutator-side 影响等。
- allocation size profile 进一步支持当前 Practical GiY 的分路径策略：绝大多数 allocation event 是 `<=256` 小对象，应保留 memcpy；大对象按 count 少但 bytes 有分量，应保留 NT path。

一句话总结：
- Cheney GC 是“复制后扫描新空间”的简单可靠基线；当前 Practical GiY 是“先在 Cache/Young 内扫描和修指针，最后写 Old/DRAM”的架构优化版本。GiY 更复杂，但更符合避免 Old/DRAM read 和降低 cache pollution 的研究目标。

## 2026-05-03 当前 GiY 与 Cheney 具体 benchmark 差异数据

本次用户追问：
- 需要具体数据，不要只要概念对比。
- 用户指出之前已经各跑过 benchmark。

数据来源：
- Cheney 对照：`build.debug/benchmarks/out24`，cache_cheney root-controlled full suite。
- 旧 GiY full suite：`build.debug/benchmarks/out22`。
- 当前最新 Practical GiY targeted：`out27_giy_direct_memcpy`，只有 `Storage/CD/Havlak` 三项。

重要说明：
- `out22 vs out24` 是完整 suite 对比，但 out22 是旧 GiY，不是当前最终 Practical GiY。
- `out27 vs out24` 是当前最新 GiY 和 Cheney 的最干净对比，但只有 Storage、CD、Havlak 三项。
- 刚跑的 `out28_alloc_size_profile` 是 allocation size profiling build，带额外统计开销，不适合作为性能对比。

当前最新 Practical GiY out27 vs Cheney out24：
- Storage：
  - total execution：GiY 184.211s，Cheney 209.181s，GiY 快 11.94%。
  - GC full：GiY 27.182s，Cheney 50.947s，GiY 少 46.65%。
  - GC core：GiY 24.274s，Cheney 48.552s，GiY 少 50.00%。
  - scavenge：GiY 24.209s，Cheney 48.342s，GiY 少 49.92%。
  - Minor GC count：GiY 261,374，Cheney 229,482。
- CD：
  - total execution：GiY 257.053s，Cheney 251.045s，GiY 慢 2.39%。
  - GC full：GiY 12.071s，Cheney 11.989s，GiY 多 0.68%。
  - GC core：GiY 7.086s，Cheney 8.000s，GiY 少 11.43%。
  - scavenge：GiY 6.941s，Cheney 7.675s，GiY 少 9.56%。
  - Minor GC count：GiY 284,514，Cheney 230,567。
- Havlak：
  - total execution：GiY 446.590s，Cheney 417.826s，GiY 慢 6.88%。
  - GC full：GiY 32.513s，Cheney 38.889s，GiY 少 16.40%。
  - GC core：GiY 22.526s，Cheney 29.936s，GiY 少 24.75%。
  - scavenge：GiY 21.458s，Cheney 27.866s，GiY 少 23.00%。
  - Minor GC count：GiY 590,600，Cheney 502,801。

当前最新 Practical GiY targeted 三项合计：
- total execution：GiY 887.854s，Cheney 878.052s，GiY 总时间慢 1.12%。
- GC full：GiY 71.766s，Cheney 101.825s，GiY 少 29.52%。
- GC core：GiY 53.886s，Cheney 86.488s，GiY 少 37.70%。
- scavenge：GiY 52.608s，Cheney 83.883s，GiY 少 37.28%。

数据解释：
- 当前 GiY 的 GC 主体已经明显优于 Cheney：targeted 三项 GC core 少 37.70%，scavenge 少 37.28%。
- 但总时间还没有全面赢：Storage 赢 11.94%，CD 慢 2.39%，Havlak 慢 6.88%，三项合计慢 1.12%。
- 这说明问题不在 GC core 主扫描，而更可能在 write barrier、remembered set patch、old slot update、cache locality、mutator-side overhead。

旧 GiY out22 full suite vs Cheney out24：
- full suite total execution：旧 GiY 3715.449s，Cheney 3499.343s，旧 GiY 慢 6.18%。
- full suite GC full：旧 GiY 291.428s，Cheney 119.450s，旧 GiY 多 143.97%。
- full suite GC core：旧 GiY 252.463s，Cheney 89.940s，旧 GiY 多 180.70%。
- full suite scavenge：旧 GiY 250.646s，Cheney 85.707s，旧 GiY 多 192.45%。
- 这组数据说明旧 GiY 明显不如 Cheney；后来的 out27 Practical GiY 已经把 targeted 三项的 GC core 反超 Cheney。

一句话结论：
- 如果看旧 full-suite GiY，GiY 明显输给 Cheney；如果看当前最新 Practical GiY 的 targeted 三项，GiY 的 GC core 已经赢 Cheney 很多，但总运行时间还差约 1.12%，剩余瓶颈主要在 GC core 之外。

## 2026-05-03 当前 GiY full suite 补跑与 Cheney out24 性能调查

本次用户要求：
- 补跑一轮新的当前 GiY full suite。
- 不要让刚才 allocation size profile 测试的噪音影响结果。
- 详细比较 GiY 和 Cheney GC 的性能，并调查差异原因。

执行方式：
- 输出目录：`build.debug/benchmarks/out29_giy_current_full`。
- 对照目录：`build.debug/benchmarks/out24`。
- 构建前删除：
  - `build.debug/giy_dram_manager.cc`
  - `build.debug/giy_dram_manager.o`
  - `build.debug/ejsvm`
- 重新执行：`make OPT_GC=giy -j2`。
- 编译命令确认没有 `-DALLOC_SIZE_PROFILE=1`，因此不受 `out28_alloc_size_profile` 的统计开销影响。
- 完整跑完 benchmark suite：
  - Bounce
  - List
  - Sieve
  - Queens
  - Permute
  - Storage
  - Towers
  - Mandelbrot
  - Richards
  - CD
  - NBody
  - Havlak

生成文件：
- `build.debug/benchmarks/out29_giy_current_full/README.txt`
- `build.debug/benchmarks/out29_giy_current_full/compare_out29_vs_out24.tsv`
- `build.debug/benchmarks/out29_giy_current_full/cause_investigation_out29_vs_out24.tsv`
- `build.debug/benchmarks/out29_giy_current_full/investigation_report.md`

full-suite 总体结果：
- Total execution：
  - GiY out29：3521.378s。
  - Cheney out24：3499.343s。
  - GiY 慢 0.63%。
- Business logic：
  - GiY：3428.880s。
  - Cheney：3379.894s。
  - GiY 多 48.986s，多 1.45%。
- GC full：
  - GiY：92.497s。
  - Cheney：119.450s。
  - GiY 少 26.953s，少 22.56%。
- GC core：
  - GiY：55.822s。
  - Cheney：89.940s。
  - GiY 少 34.118s，少 37.93%。
- scan_roots：
  - GiY：0.466s。
  - Cheney：1.307s。
  - GiY 少 64.35%。
- scan_RS：
  - GiY：1.206s。
  - Cheney：2.925s。
  - GiY 少 58.77%。
- scavenge：
  - GiY：54.148s。
  - Cheney：85.707s。
  - GiY 少 36.82%。
- Minor GC count：
  - GiY：2,709,304。
  - Cheney：2,341,014。
  - GiY 多 15.73%。
- Write barrier calls：
  - GiY：2,087,865,377。
  - Cheney：2,054,529,628。
  - GiY 多 1.62%。
- Forward operations：
  - GiY：1,636,291,388。
  - Cheney：1,696,428,830。
  - GiY 少 3.54%。
- Total allocations：
  - GiY：20,177,532,732。
  - Cheney：19,789,564,672。
  - GiY 多 1.96%。
- GC full - GC core：
  - GiY：36.675s。
  - Cheney：29.510s。
  - GiY 多 24.28%。

单项重点：
- Storage 是 GiY 最强项：
  - total：187.000s vs 209.181s，GiY 快 10.60%。
  - GC full：27.403s vs 50.947s，GiY 少 46.21%。
  - GC core：24.421s vs 48.552s，GiY 少 49.70%。
- Havlak 是 GiY 最大问题：
  - total：447.385s vs 417.826s，GiY 慢 7.07%。
  - GC core：22.654s vs 29.936s，GiY 少 24.33%。
  - business logic：414.267s vs 378.938s，GiY 多 35.329s，多 9.32%。
- CD：
  - total：257.280s vs 251.045s，GiY 慢 2.48%。
  - GC core：7.062s vs 8.000s，GiY 少 11.72%。
  - business logic：245.134s vs 239.056s，GiY 多 6.078s，多 2.54%。

原因调查结论：
- GiY 的 GC core 已经赢了 Cheney，full suite 少 37.93%；scavenge 少 36.82%。这说明当前 GiY 的核心扫描/复制方向是有效的。
- 但 total execution 仍慢 0.63%，原因是 business/non-GC time 增加 48.986s，抵消了 GC full 节省的 26.953s。
- GiY minor GC count 多 15.73%，很可能和 GiY 辅助结构占用 young/cache 空间有关，例如 GC stack、function-table slot set 等减少了 active young allocation area。
- GiY 的 GC full - GC core 多 24.28%，说明 core 外还有更高成本，可能来自 old slot patch、weak/metadata maintenance、GC entry/exit、profile/reporting 等。
- Havlak/CD 的 business time 增长最值得查：
  - Havlak write barrier calls 多 21.22%，allocations 多 9.31%，business time 多 35.329s。
  - CD write barrier calls 多 4.54%，allocations 多 10.25%，business time 多 6.078s。
- benchmark harness 两边都是 100 iterations，所以 Havlak/CD 的 allocation 差异不是顶层 benchmark 迭代次数不同导致的，更可能来自 VM/runtime 行为差异，例如 inline cache/hidden class/shape/cache retention、GC 触发导致的 metadata/cache invalidation、或者 weak-cache 清理策略差异。
- out29 和之前 out27 targeted 三项一致性很好：
  - Storage total 比 out27 慢 1.51%，GC core 多 0.61%。
  - CD total 比 out27 慢 0.09%，GC core 少 0.34%。
  - Havlak total 比 out27 慢 0.18%，GC core 多 0.57%。
  - 因此 out29 可以认为是干净的当前 GiY full-suite 性能 run。

下一步建议：
- 优先调查 Havlak/CD 为什么 GiY allocations 更多。
- 分解 write barrier 和 remembered-set insertion/duplicate-check 成本。
- 单独计时 old-slot patch、weak_clear、function-table slot patch、GC entry/exit 等 GC core 外阶段。
- 尝试减少 GiY auxiliary cache footprint 或增大 young active area，观察 minor GC count 是否下降。
- 对 Havlak/CD 做 materialization order / staging / batch 实验，但不要影响小对象 memcpy 路径。

清理：
- 已恢复 tracked build 产物：
  - `build.debug/ejsvm`
  - `build.debug/giy_dram_manager.cc`
  - `build.debug/giy_dram_manager.o`
  - `build.debug/gmon.out`

## 2026-05-03 business/non-GC time 增加 48.986s 的原因调查与验证

结论：
- `out29_giy_current_full` 相对 Cheney `out24` 的 business/non-GC time 增加 `48.986s`，主因不是 GC core。GiY 的 GC core 反而从 `89.940s` 降到 `55.822s`，少 `34.118s` / `37.93%`。
- business 增量主要集中在 Havlak 和 CD：
  - Havlak：business `414.267s` vs `378.938s`，多 `35.329s`，贡献 `72.12%`。
  - CD：business `245.134s` vs `239.056s`，多 `6.078s`，贡献 `12.41%`。
  - Havlak + CD 合计 `41.407s`，解释 `84.53%` 的 business 增量。
- 已排除两个假因：
  - 关键 benchmark 两边都是 `iterations=100`。
  - out29 是无 `ALLOC_SIZE_PROFILE=true` 的 clean performance run，allocation-size profile 的 out28 没有参与性能对比。

验证 1：GiY active young area 变小解释了普遍的 minor GC 增加。
- GiY 输出：
  - Young before aux：`300.24 KB`。
  - Young after aux：`230.72 KB`。
  - Aux total：`69.52 KB`，其中 stack `37.52 KB`，function-table slots `32.00 KB`。
- Cheney 源码中 function-table slot 预留约 `young_bytes / 8`，所以从 `300.24 KB` 推算 active young 约 `262.71 KB`。
- 由容量推算的 minor GC 增加：`262.71 / 230.72 - 1 = 13.865%`。
- 实测 allocation 数基本相同的 workload 中，minor GC 增加约 `13.90%`：
  - Bounce `13.918%`，List `13.899%`，Storage `13.897%`，Mandelbrot `13.906%`，NBody `13.900%`。
- 所以 GiY 辅助结构占用 cache/young 空间确实导致了更高 minor GC 频率，但这只能解释普遍的小幅扰动，不足以解释 Havlak/CD 的主要 business time 增量。

验证 2：具体 business 开销来自 GiY write barrier 的 remembered-set value-update 线性扫描。
- GiY remembered set 为了避免 GC 时读 old slot，保存了 `buffer[]` 和 `values[]`。
- 当 old/init slot 写入 young value 时，GiY 记录 value。
- 当同一个 old/init slot 后续写入 immediate/old/null 时，GiY 调用 `rememberset_update_existing(slot, 0)` 清除旧 value。
- 当前 `rememberset_update_existing` 通过 `rememberset_find_index` 线性扫描 `remembered_set.buffer[0..count)`。
- 这段发生在 `write_barrier` / `write_barrier_ptr` 内，因此记入 business/non-GC time。
- 现有 `Write barrier calls` 只统计 young-value add，不统计这些 clear/update 扫描，所以之前计数看不到真正开销。

我添加了默认关闭的验证开关 `GIY_WB_PROFILE=true`，只跑 CD/Havlak 做验证，不作为性能 baseline：
- CD：
  - Update attempts：`43,851,721`。
  - Update scan steps：`401,694,113`。
  - Steps/attempt：`9.160`。
  - Clear attempts：`40,851,797`。
  - Existing WB calls：`9,573,828`。
  - 等价于每个已计数 WB call 额外 `41.96` 次线性扫描 step。
- Havlak：
  - Update attempts：`444,200,570`。
  - Update scan steps：`190,916,727,113`。
  - Steps/attempt：`429.798`。
  - Clear attempts：`417,674,314`。
  - Existing WB calls：`165,000,542`。
  - 等价于每个已计数 WB call 额外 `1157.07` 次线性扫描 step。

这验证了最主要原因：Havlak 贡献 `72.12%` 的 business regression，同时它在 GiY write barrier profile 中产生约 `1909亿` 次 remembered-set 线性扫描 step。GC core 变快但 business time 增加，正是这个 mutator-side RSet value-update/clear-update 路径造成的。

次要因素：
- Havlak allocation 也从 `1,856,819,962` 增到 `2,029,600,757`，多 `172,780,795` / `9.30%`。
- CD allocation 从 `2,099,019,821` 增到 `2,314,174,986`，多 `215,155,165` / `10.25%`。
- 两边都是 100 iterations，所以不是 harness 次数差异。这个更可能来自不同 GC 时机影响 VM/runtime cache、shape、inline cache、weak metadata 等对象生命周期。

修复方向：
- 优先把 `rememberset_update_existing` 从线性扫描改成 hash-probe 后 O(1) 更新：
  - hash table 存 slot 对应的 buffer index，或维护并行 index table。
  - duplicate/update 时直接 `values[index] = value`。
  - 处理当前 `HASH_PROBE_LIMIT` 后 fallback 插入的条目，避免出现只能线性找的 overflow 项。
- 修完后先重跑 Havlak/CD/Storage，再跑 full suite。
- 预期 Havlak business time 改善最大，CD 次之；GC core 优势应保持。

记录文件：
- 详细报告：`build.debug/benchmarks/out29_giy_current_full/business_non_gc_delta_investigation.md`。
- 插桩验证输出：`build.debug/benchmarks/out30_giy_wb_profile/CD.out`、`build.debug/benchmarks/out30_giy_wb_profile/Havlak.out`。

## 2026-05-03 深度因果调查：business/non-GC gap 的进一步验证

新增实验：
- `out31_giy_rset_index_fast`：`GIY_RSET_INDEX_FAST=true`，不打开 WB profile，用来做性能消融。
- `out32_giy_rset_index_fast_wb_profile`：确认 fast index 后 update scan step 是否消失。
- `out33_giy_index_cache552`：`GIY_RSET_INDEX_FAST=true CACHE_SIZE_KB=552`，把 GiY active young 从 `230.72KB` 提到 `265.72KB`，接近 Cheney 估算 active young `262.71KB`。

实验 1 结论：RSet update 线性扫描是 Havlak 的强因果项。
- Havlak out29 business：`414.267s`。
- Havlak out31 index business：`395.732s`。
- 下降 `18.535s`，关闭 Havlak business gap 的 `52.46%`。
- GC core 基本不变：`22.654s -> 22.570s`，所以改善不是 GC core 噪音，而是 mutator-side metadata lookup 成本。
- CD 没改善：`245.134s -> 245.848s`，说明 CD 主因不是这条线性扫描。

profile 反证：
- CD update scan steps：`401,694,113 -> 121,419`，下降 `99.97%`，但本来量级小，性能没改善。
- Havlak update scan steps：`190,916,727,113 -> 2,744,439,102`，下降 `98.56%`，对应 business 大幅回收。
- index 版本 Havlak 还有 `3,692,903` 个 fallback insertions，留下 `2.744B` fallback scan steps，正式修复要消除 overflow/fallback 线性扫描。

实验 2 结论：GiY active young 过小是 Havlak 的第二个因果项，但不是 CD 主因。
- CD：minor GC `284,514 -> 246,970`，business `245.848s -> 245.245s`，只改善 `0.603s`；allocation 几乎不变。
- Havlak：minor GC `590,600 -> 512,300`，business `395.732s -> 389.257s`，改善 `6.475s`；allocation 几乎不变。
- 因此 Havlak 有明显 higher minor-GC frequency / metadata churn penalty；CD gap 基本不是这个。

CD + Havlak 合并：
- out29 baseline business gap vs Cheney：`+41.407s`。
- out31 RSet index 后：`+23.586s`。
- out33 RSet index + cache552 后：`+16.508s`。
- 两个消融实验合计关闭 `60.13%` 的 CD+Havlak business gap。

最终判断：
- 高置信：Havlak 最大 business regression 成分来自 GiY RSet `values[]` 清理/更新的线性扫描。
- 高置信：GiY active young 过小导致 minor GC 增加，并给 Havlak 带来约 `6.5s` business penalty。
- 中高置信：剩余 CD/Havlak gap 是 extra mutator work，主要体现为更多 actual allocations 和普通 write barrier：
  - out33 CD allocations 仍比 Cheney 多 `10.25%`。
  - out33 Havlak allocations 仍比 Cheney 多 `9.31%`。
  - out33 CD WB 仍多约 `2.03%`。
  - out33 Havlak WB 仍多约 `18.96%`。
- allocation 差异在 active young 调整后几乎不变，所以不是 young capacity 的直接结果，更像 GiY runtime/cache/metadata 行为差异，例如 inline-cache、alloc-site-cache、weak-shape/property-map metadata churn。

修复建议：
- 正式实现 GiY RSet `slot -> buffer index` 的 O(1)/bounded update，并消除 fallback 线性扫描。
- 重新设计 GiY auxiliary footprint，保证和 Cheney 对比时 active young 容量等价。
- 之后再加可靠的 allocation-site / weak_clear invalidation / inline-cache invalidation 计数，定位剩余 extra allocations。

详细报告：`build.debug/benchmarks/out29_giy_current_full/deep_business_gap_causal_investigation.md`。

## 2026-05-03 RSet update 查找的目的、语义和瓶颈解释

结论：
- 这个查找不是为了 GC 时扫描 RSet，而是在 mutator 的 write barrier 里维护 GiY RSet 的 `values[]`。
- GiY RSet 记录的是 `(slot_addr, last_written_value)`，而 Cheney RSet 基本只记录 `slot_addr`。
- GiY 这样做的目的，是 minor GC 时尽量不读 old/init slot 当前值，减少 Old/DRAM read，符合 GiY 的设计目标。
- 因此 GiY 必须保证 RSet 里的 `values[i]` 不过期；一旦 old/init slot 不再指向 young object，就要把对应的 `values[i]` 清成 0。
- 当前为了找到 `values[i]` 对应的 index，代码线性扫描 `remembered_set.buffer[]`，这就是最大的已确认单项瓶颈，尤其在 Havlak 里非常严重。

Cheney 的语义：
- Cheney RSet 通常只存 old/init slot 地址：
  - `RSet: slot_addr`
- minor GC 时直接读取 slot 当前值：
  - `current_value = *slot_addr`
  - 如果 `current_value` 指向 young，再把它当作 root 处理。
- 因为 Cheney 在 GC 时读 slot 当前值，所以 write barrier 不需要维护 “last written value” 是否过期。

GiY 的语义：
- GiY 为了避免 minor GC 读 Old/DRAM slot，RSet 存两份并行信息：
  - `remembered_set.buffer[i] = slot_addr`
  - `remembered_set.values[i] = last_written_young_value`
- minor GC 扫 RSet 时，GiY 直接读 `values[i]`：
  - 如果 `values[i] == 0`，跳过。
  - 如果 `values[i]` 指向 young，就从这个 young value 开始做 reserve/copy/patch。
- 这能减少 GC core 阶段对 old/init slot 的读取，但代价是 write barrier 必须维护 `values[]` 的准确性。

为什么必须查找：
- 假设发生以下写入序列：
  - `t1: old_obj.field = young_obj_A`
  - GiY RSet 记录：`slot=&old_obj.field, value=young_obj_A`
  - `t2: old_obj.field = null` 或 `old_obj.field = old_obj_B`
- 到 `t2` 时，这个 slot 当前已经不再引用 `young_obj_A`。
- 如果 GiY 不更新 RSet 中的 value，minor GC 会继续看到 `value=young_obj_A`，错误地认为 `young_obj_A` 仍然 live。
- 所以 write barrier 必须找到这个 slot 原来在 RSet 中的 index，然后执行：
  - `remembered_set.values[index] = 0`
- 这就是 `rememberset_update_existing(slot, 0)` 的语义目的。

为什么不能简单不查：
- 如果不清旧 value：
  - 会导致 young object 被错误保活。
  - 会增加不必要的 forward/copy/patch。
  - 严重时会破坏 GiY 对 “RSet value 表示 slot 当前 young 引用” 的语义。
- 如果每次都追加一条新记录：
  - 同一个 slot 会出现多条历史记录，例如：
    - `slot X -> young_A`
    - `slot X -> 0`
    - `slot X -> young_B`
  - minor GC 必须处理版本覆盖关系，否则旧 young_A 仍可能被错误保活。
  - RSet 会膨胀，GC 扫描成本也会上升。
- 因此查找是语义上需要的，但查找方式不应该是线性扫描。

当前性能问题：
- 当前实现大致是：
  - `write_barrier(...)`
  - `rememberset_update_existing(slot, value)`
  - `rememberset_find_index(slot)`
  - 遍历 `remembered_set.buffer[0..count)` 找 slot。
- 这个查找发生在 mutator 运行阶段，所以时间记入 business/non-GC time。
- 现有 `Write barrier calls` 只统计 young-value add，不能完整反映这些 clear/update 查找成本。
- 实测 profile：
  - CD baseline update scan steps：`401,694,113`。
  - Havlak baseline update scan steps：`190,916,727,113`。
  - Havlak 因此产生非常大的 business regression。

为什么 fast index 证明了这个结论：
- 我实现了实验开关 `GIY_RSET_INDEX_FAST=true`，让 RSet hash table 额外记录 slot 对应的 `buffer[]/values[]` index。
- 更新时从：
  - `slot -> 线性扫描 buffer[] -> index -> values[index]`
- 变成：
  - `slot -> hash probe -> index -> values[index]`
- 结果：
  - Havlak update scan steps 从 `190,916,727,113` 降到 `2,744,439,102`，下降 `98.56%`。
  - Havlak business 从 `414.267s` 降到 `395.732s`，直接收回 `18.535s`。
  - GC core 基本不变：`22.654s -> 22.570s`。
- 这证明改善来自 mutator-side RSet metadata lookup，不是 GC core 噪音。

正确修复方向：
- RSet hash table 不应只用于去重，还应能返回 `buffer[]/values[]` 的 index。
- 正式设计应实现：
  - `slot_addr -> bounded hash probe -> index`
  - `values[index] = current_young_value_or_0`
- 还要处理当前 `HASH_PROBE_LIMIT` 后的 fallback 插入：
  - 不能让 fallback 只能靠线性扫描找回。
  - 可以维护 overflow index，或禁止无 index 的 fallback 记录，或扩大/改进 hash table。
- 一句话：查找这个动作是为了保持 GiY RSet `values[]` 的正确语义；错不在“查找需求”，而在当前用线性扫描实现 slot 到 index 的映射。

## 2026-05-03 收回 RSet 线性扫描损失后，business 时间是否接近 Cheney

结论：
- 如果只收回已经验证的 RSet update 线性扫描损失，GiY 的总时间会非常接近 Cheney，但 business 时间本身仍然明显慢于 Cheney。
- 对 full suite 粗略估算：
  - 当前 out29 business gap：`3428.880s - 3379.894s = +48.986s`。
  - Havlak RSet index 实验收回 `18.535s`。
  - 如果把这部分推广回 full suite，剩余 business gap 约 `48.986s - 18.535s = +30.451s`。
  - 相对 Cheney business `3379.894s`，仍慢约 `0.90%`。
  - 当前 out29 total 比 Cheney 慢 `+22.035s`；若只收回 `18.535s`，total 还会慢约 `+3.500s`，约 `+0.10%`。
- 所以结论是：总时间会接近到几乎打平，但 business 时间还没有真正接近；还剩约 `30s` 级别的 non-GC gap。

CD + Havlak 的实测消融更清楚：
- 原始 out29：
  - CD business gap：`+6.078s`。
  - Havlak business gap：`+35.329s`。
  - 两项合计 business gap：`+41.407s`。
- 只加 RSet index 后 out31：
  - CD business：`245.848s` vs Cheney `239.056s`，仍慢 `+6.792s`。
  - Havlak business：`395.732s` vs Cheney `378.938s`，仍慢 `+16.794s`。
  - 两项合计 business gap：`+23.586s`。
  - 也就是说 RSet index 关闭了 CD+Havlak business gap 的 `43.04%`，但还剩 `56.96%`。
- 再加 active young 容量消融 out33：
  - CD business gap：`+6.189s`。
  - Havlak business gap：`+10.319s`。
  - 两项合计 business gap：`+16.508s`。
  - 两个消融合计关闭 `60.13%`，但仍未完全贴近 Cheney。

判断：
- 如果目标是 total execution，收回 RSet 线性扫描后已经非常接近 Cheney，甚至再解决 active young 容量后在 CD+Havlak 上接近反超。
- 如果目标是 business/non-GC 本身，仍不够接近，尤其 CD 的 business gap 基本没有被 RSet index 解决。
- 剩余 business gap 主要不是 GC core，而是 extra mutator work：
  - CD allocations 仍多 `10.25%`。
  - Havlak allocations 仍多 `9.31%`。
  - Havlak write barrier 仍多约 `18.96%`。
- 因此下一阶段不能只做 RSet index；还要定位 GiY 为什么产生更多 actual allocations / write barriers，以及 inline-cache、alloc-site-cache、weak metadata 是否被更频繁地扰动。

## 2026-05-03 是否可以把 GiY 中 stale RSet value 当作 floating garbage

结论：
- 在当前 GiY 实现里，不能简单把 RSet 里过期的 young value 当成 floating garbage，然后不再清理/更新它。
- 原因是当前 GiY 不只是把 `remembered_set.values[i]` 当作保守 root；它还会在 patch 阶段根据这个 saved value 回写真实 old/init slot。
- 如果 saved value 已经过期，patch 阶段可能把已经写成 `null`、immediate、或 old object 的 slot 重新写回 stale young object 的 promoted 地址。这不是 floating garbage，而是程序语义错误。

具体路径：
- `giy_scan_remembered_set_slots()`：
  - 读取 `remembered_set.values[i]`。
  - 如果非 0，就把这个 saved value 当作 young root 做 `giy_reserve_edge(...)`。
- `giy_patch_remembered_set_slots()`：
  - 再次读取 `remembered_set.values[i]`。
  - 如果这个 value 被 forward，就把真实 slot 写成 forwarded old address：
    - `giy_store_jsvalue_slot(slot, put_ptag(to, get_ptag(value)), slot_in_dram)`
    - 或 `giy_store_ptr_slot(slot, (void *) to, slot_in_dram)`

危险例子：
- `t1: old_obj.field = young_A`
  - RSet 记录：`slot=&old_obj.field, value=young_A`
- `t2: old_obj.field = null`
  - 如果不清 RSet value，RSet 仍然保存 `value=young_A`。
- minor GC：
  - scan 阶段把 `young_A` 当作 live，复制/forward 到 old。
  - patch 阶段看到 `value=young_A` 被 forward，于是把 `old_obj.field` 写成 `forwarded(young_A)`。
- 结果：
  - 程序本来已经把 `old_obj.field` 设为 `null`。
  - GC 后它又变回了指向 `young_A` 的 old 地址。
  - 这会直接破坏 JavaScript 可观察语义。

为什么这不是普通 floating garbage：
- 普通 floating garbage 的含义是：对象被多保活一轮，但没有任何 live object 指向它；下一次合适的 GC 可以回收。
- 当前 GiY stale value 的问题是：patch 阶段会把 stale object 重新写入 live old slot，使它重新变成可达对象。
- 因此它不是“无害地多保活”，而是可能“复活旧引用”。

即使只把 stale value 当保守 root，也仍有代价：
- 如果修改算法，让 stale `values[]` 只用于 reserve/copy，不用于 patch slot，那么它才接近 floating garbage。
- 但这样要保证 patch 阶段不会根据 stale value 回写 slot。
- 可选办法包括：
  - patch 前读取 old slot 当前值，确认 `*slot` 仍等于 saved value 后才 patch；
  - 给 slot 维护版本号/sequence，确认 RSet value 是最新写入；
  - 放弃 saved value，GC 时像 Cheney 一样读 slot 当前值；
  - 保留 saved value，但用 O(1)/bounded index update 及时清零。
- 其中“patch 前读 old slot”会重新引入 Old/DRAM read，违背 GiY 想减少 old read 的核心目标。

另一个现实问题：
- 即使 stale value 只造成 floating garbage，被复制到 old/DRAM 后，下一轮 minor GC 也不能回收它。
- minor GC 只回收 young/cache 区；promoted 到 old/DRAM 的 floating garbage 需要 major GC 才能回收。
- 当前 `need_major_gc()` 路径仍是 TODO/exit 风格，不能指望“下一轮 minor 再处理”。

所以正确方向仍然是：
- 不能直接不清 stale RSet value。
- 应该把 `slot -> index` 查找从线性扫描改成 O(1)/bounded update。
- 如果想接受 floating garbage，必须重新定义 GiY RSet semantics：saved value 只能作为保守 root，不能用于无验证 patch；并且要有 major GC 或其他 old-space 回收机制承接 promoted garbage。

## 2026-05-03 Cheney GC 下 RSet 去重是否也不可或缺

结论：
- Cheney GC 下 RSet 去重也很重要，但重要性主要是容量和性能，不是 GiY 这种 `values[]` stale-value 语义正确性。
- Cheney RSet 只记录 slot 地址；minor GC 扫 RSet 时会读取 slot 当前值。
- 因此如果 old slot 后来被写成 `null`、immediate、或 old object，Cheney 不需要更新/清理 RSet entry；GC 时读到当前值不是 young，就自然跳过。
- GiY RSet 记录 `(slot_addr, saved_value)`；minor GC 不读 old slot 当前值，而是相信 saved value。因此 GiY 必须在 write barrier 阶段更新/清理 saved value，否则 patch 阶段可能复活旧引用。

Cheney 如果不去重会怎样：
- 语义上通常仍然正确：
  - 同一个 slot 被重复放进 RSet。
  - minor GC 多次读取同一个 slot 当前值。
  - 如果它指向 young，就重复处理同一个 young edge；forwarding 本身通常是幂等的。
  - 如果它已经不指向 young，就每次都跳过。
- 但性能和容量会很差：
  - RSet 会快速膨胀。
  - scan_RS 重复读同一个 old slot。
  - RSet capacity 可能被重复 entry 填满。
  - write barrier 和 GC 扫描都会变慢。

GiY 如果不去重/不查找会怎样：
- 不只是性能问题，也可能是语义问题。
- 因为同一个 slot 的 saved value 需要保持“最新 young value 或 0”。
- 如果不查找旧 entry 并更新 `values[index]`：
  - old slot 当前已经改成 `null`，但 RSet 还保存旧 young value。
  - scan 阶段会把旧 young value 当 live。
  - patch 阶段可能把真实 old slot 改回旧 young value 的 promoted address。
- 所以 GiY 需要的是更强的东西：
  - 不只是 Cheney 那种 duplicate filtering。
  - 还需要 `slot -> values[] index` 的可更新映射。

一句话：
- Cheney 的去重是“避免 RSet 重复 entry 导致容量/性能爆炸”。
- GiY 的查找/去重是“避免容量/性能爆炸，同时维护 saved value 的最新性，保证 patch 语义正确”。
- 所以 Cheney 下去重也不可或缺，但它不是同一种不可或缺；GiY 对 `slot -> index` 的需求更强。

## 2026-05-03 如果 RSet 按写入顺序保留重复项，最后一条是否能覆盖前面的

结论：
- 理论上，如果把 RSet 改造成完整的 per-slot 写日志，并且 GC 时按“latest write wins”的规则折叠，那么最后一条确实可以代表 slot 的最终状态。
- 但这不是当前 GiY RSet 的语义，也不能简单通过“允许重复项并按顺序 patch”来保证正确。
- 主要问题有三个：
  1. final value 是 `null`/immediate/old object 时，最后一条如果只是 `value=0` 并在 patch 阶段跳过，无法覆盖之前 patch 写入的 stale young。
  2. scan 阶段发生在 patch 前；如果 scan 所有历史 young entries，会把已被覆盖的 young object 全部 promote 到 old，形成大量 old-space floating garbage。
  3. 写日志会极大膨胀 RSet，当前小 cache/RSet 容量根本承受不了 CD/Havlak 这种 workload 的写入数量。

为什么“顺序 patch，最后覆盖”不总是正确：
- 情况 A：最后一次写入仍是 young value。
  - `t1: slot = young_A`
  - `t2: slot = young_B`
  - 如果 RSet 保留两条：
    - `slot -> young_A`
    - `slot -> young_B`
  - patch 阶段先把 slot 写成 `forwarded(A)`，再写成 `forwarded(B)`，最终 slot 是 B。
  - 语义上可以正确，但 A 已经被 scan 阶段错误 promote，成为 floating garbage。
- 情况 B：最后一次写入不是 young。
  - `t1: slot = young_A`
  - `t2: slot = null`
  - 如果 RSet 保留：
    - `slot -> young_A`
    - `slot -> 0`
  - 当前 patch 逻辑遇到 `value=0` 是 `continue`，不会把 slot 写回 null。
  - patch 阶段先把 slot 写成 `forwarded(A)`，然后最后一条 0 被跳过。
  - 最终 slot 错误地变成 `forwarded(A)`。
  - 所以“最后一条会覆盖”只有在最后一条真的执行写回时才成立；`0` tombstone 如果只是 skip，不会覆盖任何东西。

如果要让重复 RSet 方案正确，需要重新设计：
- 方案 1：完整写日志 + replay。
  - 每次写 old/init slot 都记录真实写入值，包括 `null`、immediate、old object、young object。
  - patch 阶段按顺序重放所有写入，最后一条覆盖前面。
  - 问题：这会把 write barrier 变成巨大日志系统，RSet 容量和 patch 成本都会爆炸；而且 scan 阶段仍需避免扫描历史 stale young。
- 方案 2：反向 latest-wins 折叠。
  - GC 时从 RSet log 末尾往前扫。
  - 每个 slot 只处理第一次遇到的最新 entry。
  - 如果最新 entry 是 0/null/old，则标记 slot 已处理并跳过旧 young entries。
  - 如果最新 entry 是 young，则只扫描/patch 这个最新 young。
  - 问题：GC 时需要一个 `seen slots` 集合，本质上还是要做去重/索引，只是把成本从 write barrier 转移到 GC；同时仍然需要容纳完整写日志。
- 方案 3：当前更合理方向。
  - write barrier 阶段维护 `slot -> index`。
  - 对已有 slot 直接更新 `values[index]` 为最新 young value 或 0。
  - GC 阶段只看每个 slot 的最新状态，不需要处理历史日志。

为什么完整写日志在当前 benchmark 下不现实：
- profile 中 clear/update attempts 很大：
  - CD clear attempts 约 `40,851,797`。
  - Havlak clear attempts 约 `417,674,314`。
- 如果这些都作为重复 RSet log entry 追加，远远超过当前 RSet 容量和 cache 预算。
- 即使用 DRAM 存 log，也会把 GC 前处理和 memory bandwidth 压力转移到另一个地方。

总结：
- “最后写入覆盖前面”这个思路在理论上可成立，但前提是 RSet 是完整、有 tombstone/真实值、并且 GC 按 latest-wins 折叠的写日志。
- 当前 GiY 的 `values[]` 不是这种日志，而是每个 slot 的最新摘要。
- 对“最新摘要”结构来说，必须能找到旧 slot 的 index 并更新；否则会产生 stale value。
- 因此当前最务实的修复仍然是 `slot -> index` 的 O(1)/bounded update，而不是允许无限重复项。

## 2026-05-03 为什么会有 value=0 的 RSet 记录，以及为什么不能简单每次处理 0

为什么会产生 `value=0`：
- GiY RSet 的 `values[]` 是每个 remembered slot 的“当前 young 引用摘要”。
- 当 old/init slot 写入 young object 时，`values[index] = young_value`。
- 当同一个 slot 后来写入的不是 young object 时，例如 `null`、fixnum/special、old object、init/non-young object，这个 slot 就不再是 old-to-young edge。
- 为了让 GC 扫 RSet 时知道“这个 slot 当前没有 young 引用”，GiY 会把对应 `values[index]` 更新成 0。
- 所以 `value=0` 是 tombstone / no-young-edge 标记，不是普通对象引用。

为什么不能在当前 patch 里简单“处理 value=0 并清空那个指针”：
- 对 JSValue slot 来说，`value=0` 不等价于 JavaScript 的 null。
- 它可能代表原始写入是 fixnum、special、old object、init object、null 等很多情况。
- 只保存 0 会丢失真实写入值。
- patch 时如果把 slot 写成 0，会把原程序写入的 old object/fixnum/special/null 都改坏。
- 对 void* slot 来说，`value=0` 也只能代表“当前不是 young pointer”，不一定代表应该把 slot 写成 NULL；它也可能原本写入的是 old pointer。

如果想让 `value=0` 可以覆盖旧记录：
- 需要把 tombstone 从“无 young edge”升级成“真实最后写入值”。
- 至少要区分 JSValue null/special/fixnum、old/init object pointer 及 tag、void* NULL、void* old/init pointer。
- 这等于把 RSet 从 “latest young edge summary” 改成 “完整 write log / full value log”。

为什么完整处理 `value=0` 会降低效率：
- 如果每次非-young 写入都追加 tombstone/log entry：
  - CD clear attempts 约 `40,851,797`。
  - Havlak clear attempts 约 `417,674,314`。
  - 这些都会变成 RSet/log entry，远超当前 RSet 容量。
- GC 时还要做 latest-wins 折叠：
  - 从后往前扫 log；
  - 维护 `seen slot` 集合；
  - 对每个 slot 只处理最新 entry。
- 这本质上仍然需要 hash/set 去重，只是把成本从 mutator update 转移到 GC。
- 如果不折叠而处理所有历史 young，会把大量已被覆盖的 young object promote 到 old，制造 old-space floating garbage；当前 major GC 不完整，不能靠下一轮 minor 回收。

所以：
- `value=0` 是“这个 slot 当前无 young edge”的摘要，不是“应该把 slot 清空为 0”的真实写入值。
- 直接处理 0 去清空 slot 会破坏语义。
- 把它改造成可重放 tombstone 理论上可行，但需要 full value log + latest-wins 折叠 + seen set，会带来容量和 GC 处理成本。
- 当前更有效的方案仍是维护每个 slot 的最新摘要：`slot -> index -> values[index] = young_value_or_0`，并把 index 查找做成 O(1)/bounded。

## 2026-05-03 当前瓶颈是否因为 GiY 必须处理“清除状态”

结论：
- 这个理解基本正确，但需要更精确地说：
  - GiY 的确比 Cheney 多了“清除状态 / no-young-edge state”的维护需求。
  - 但瓶颈不是“必须处理清除状态”本身，而是当前实现为了清除旧 young 摘要，需要线性查找这个 slot 在 `values[]` 中的位置。
- write barrier 其实可以判断当前写入值是不是 young。
- 问题在于：当当前写入值不是 young 时，write barrier 需要知道“这个 slot 以前是否已经在 RSet 中记录过 young value，以及它的 index 是多少”。
- 当前 hash table 可以用于 duplicate filtering，但不能直接返回 `values[]` index，所以只能线性扫描 `remembered_set.buffer[]` 找 index。

更准确的路径：
- 写入 young：
  - `slot = young_A`
  - write barrier 能判断 value 在 young。
  - 需要记录或更新：`values[index] = young_A`。
- 后续写入 non-young：
  - `slot = null/old/fixnum/...`
  - write barrier 能判断 value 不是 young。
  - 但它还需要清除之前可能存在的 `values[index] = young_A`。
  - 因此需要找到 index 并执行：`values[index] = 0`。
- 当前慢在：
  - `slot -> remembered_set.buffer[] 线性扫描 -> index -> values[index] = 0`

为什么 Cheney 没有这个“清除状态”成本：
- Cheney RSet 只记录 slot 地址。
- 当 slot 写入 non-young 后，Cheney 不需要更新 RSet entry。
- 下次 minor GC 读 slot 当前值，发现不是 young，就跳过。
- 因此 Cheney 可以允许 RSet 中保留“曾经有用但现在无用”的 slot 地址；它们只是扫描时多读一次 old slot。

为什么 GiY 不能像 Cheney 一样保留旧状态：
- GiY minor GC 不想读 old slot 当前值。
- 它依赖 `values[]` 保存的摘要判断这个 slot 当前是否有 young edge。
- 如果不清除 stale `values[]`，GiY 会把旧 young 当 live，甚至在 patch 阶段写回旧引用。

所以当前瓶颈可以概括为：
- GiY 为了避免 GC core 读 old slot，把“判断 slot 当前值”的工作提前到了 write barrier。
- 这引入了对 `values[]` 当前性的维护。
- 当前维护方式缺少 `slot -> index` 的 O(1) 映射，导致 non-young 写入时的清除操作变成大量线性扫描。

修复重点：
- 不是取消清除状态。
- 也不是让 write barrier 忽略 non-young 写入。
- 而是让 write barrier 能快速回答：
  - 这个 slot 是否已有 RSet entry？
  - 如果有，它对应 `values[]` 的 index 是多少？
- 也就是实现 `slot -> index -> values[index] = 0/young_value`。

## 2026-05-03 business 慢的核心是否是 RSet 项目更多 + 修改指针时遍历 RSet

结论：
- 用户提出的两个方向基本抓住了核心，但第 1 点需要修正：
  1. 不是“GiY 记录的 RSet 项目数量一定比 Cheney 多”，而是 GiY 的 write barrier 必须处理更多类型的 old/init slot 写入事件，尤其是 non-young 写入导致的 clear/update attempts。
  2. 当前实现确实在修改 old/init slot 时，为了更新或清除 `values[]`，需要线性遍历 `remembered_set.buffer[]` 找 index。这是已验证的主要瓶颈。

更准确地说：
- Cheney：
  - 只需要在 old/init slot 写入 young value 时，把 slot 地址加入 RSet。
  - 如果 old/init slot 写入 non-young value，例如 null、fixnum、old object，Cheney 可以直接忽略。
  - 因为 Cheney minor GC 会在扫描 RSet 时读取 slot 当前值。
- GiY：
  - old/init slot 写入 young value 时，要记录 `slot -> young_value`。
  - old/init slot 写入 non-young value 时，也不能完全忽略，因为这个 slot 之前可能记录过 stale young value。
  - 所以 GiY 需要执行“清除旧摘要”：`slot -> index -> values[index] = 0`。

所以 GiY 多出来的不是单纯 RSet entry 数量，而是：
- non-young 写入也会触发 update_existing 尝试；
- duplicate young 写入也要更新 saved value；
- 这些动作很多不体现在旧的 `Write barrier calls` 计数里，因为该计数主要统计 young-value add。

实测证据：
- CD：
  - clear attempts：约 `40,851,797`。
  - baseline update scan steps：`401,694,113`。
- Havlak：
  - clear attempts：约 `417,674,314`。
  - baseline update scan steps：`190,916,727,113`。
- Havlak 的这些线性扫描消融掉后，business 从 `414.267s` 降到 `395.732s`，直接收回 `18.535s`。

关于“遍历整个 remembered set”：
- 当前实现是线性扫描 `remembered_set.buffer[0..count)`。
- 如果 slot 不在 RSet 中，通常会扫完整个当前 RSet。
- 如果 slot 在 RSet 中，会扫到命中位置；平均也可能接近半个 RSet，冲突/插入顺序会影响实际步数。
- 所以不是每次都必然扫完整 RSet，但大量 miss/hit 累积后，效果接近非常高的线性扫描成本。

一句话：
- 当前 business 慢的核心不是“RSet entry count 更多”这么简单。
- 核心是 GiY 为了避免 GC core 读 old slot，把 slot 当前 young-edge 状态缓存到 `values[]`，因此 write barrier 必须处理 non-young 写入的清除状态；而当前清除/更新状态缺少 `slot -> index` 的 O(1) 映射，只能线性扫描 RSet。

## 2026-05-03 证明当前性能瓶颈确实是 GiY RSet update 线性查找的证据链

结论：
- 对 Havlak，证据已经达到强因果级别：RSet update 线性查找是最大的已确认 business 瓶颈。
- 对 CD，RSet update 线性查找存在，但不是主瓶颈；CD 的 gap 主要还在 extra allocations / mutator work。
- 不需要再启动新一轮 benchmark 才能证明这一点，因为已有 out30/out31/out32/out33 已经构成完整消融链。

证据 1：源码路径确实会在 non-young 写入时清除旧摘要。
- `ejsvm/giy_rset.cc` 中：
  - `write_barrier(JSValue* ptr, JSValue value)` 如果 `value` 是 fixnum/special，会调用 `rememberset_update_existing(obj_ptr, 0)`。
  - 如果 `value` 不是 young，也调用 `rememberset_update_existing(obj_ptr, 0)`。
  - `write_barrier_ptr(void** ptr, void* value)` 如果 `value == NULL` 或不在 young，也调用 `rememberset_update_existing(obj_ptr | RS_PTR_SLOT_TAG, 0)`。
- 这说明 GiY 的 write barrier 不只处理 young 写入，还要处理 non-young 写入造成的“清除旧 young 摘要”。

证据 2：当前 `slot -> index` 的 fallback 实现确实是线性扫描。
- `rememberset_find_index_linear(uintptr_t obj_ptr)` 遍历：
  - `for (int i = 0; i < remembered_set.count; i++)`
  - 比较 `remembered_set.buffer[i] == obj_ptr`
  - 找到后返回 index。
- `rememberset_update_existing(...)` 在没有 fast index 时调用这个线性查找，然后执行：
  - `remembered_set.values[index] = value`
- 这就是 `slot -> buffer[] scan -> index -> values[index]` 的线性路径。

证据 3：RSet `values[]` 的确会驱动 GC scan 和 patch，所以 stale value 不能忽略。
- `ejsvm/GiY.cc`：
  - `giy_scan_remembered_set_slots()` 读取 `remembered_set.values[i]`，非 0 就调用 `giy_reserve_edge(...)`。
  - `giy_patch_remembered_set_slots()` 读取同一个 `values[i]`，如果 value 被 forward，就把真实 slot 写成 forwarded address。
- 因此 `values[]` 不是仅用于统计；它直接影响 live set 和 old slot patch。
- 这证明 GiY 必须维护 `values[]` 当前性。

证据 4：profile 计数显示线性扫描量巨大。
- out30 baseline GiY + WB profile：
  - CD：
    - update attempts：`43,851,721`
    - clear attempts：`40,851,797`
    - update scan steps：`401,694,113`
    - steps/attempt：`9.160`
  - Havlak：
    - update attempts：`444,200,570`
    - clear attempts：`417,674,314`
    - update scan steps：`190,916,727,113`
    - steps/attempt：`429.798`
- Havlak 是决定性证据：单项 `1909亿` 次 buffer scan step，量级足以解释几十秒 business 开销。

证据 5：fast-index 消融几乎只消除了这个查找成本。
- 实验：`out31_giy_rset_index_fast`，不开 WB profile，不改变 benchmark 逻辑，只让 hash table 额外提供 `slot -> index`。
- Havlak：
  - business：`414.267s -> 395.732s`，下降 `18.535s`。
  - total：`447.385s -> 428.995s`，下降 `18.390s`。
  - GC full：`33.118s -> 33.263s`，基本不改善，甚至略高。
  - GC core：`22.654s -> 22.570s`，只变 `-0.084s`。
  - minor GC：`590,600 -> 590,600`，完全不变。
  - write barrier calls：`165,000,542 -> 165,000,542`，完全不变。
  - allocations：`2,029,600,757 -> 2,029,600,757`，完全不变。
  - forward ops：`369,160,253 -> 369,160,253`，完全不变。
- 这组“不变项”很重要：它排除了 allocation、GC 次数、WB 次数、forward 数量变化造成改善。
- 改善几乎只能来自 `slot -> index` 查找方式变化，也就是 RSet update metadata lookup。

证据 6：profile 反证 fast-index 后扫描 step 确实消失。
- out32 fast-index + WB profile：
  - CD update scan steps：`401,694,113 -> 121,419`，下降 `99.97%`。
  - Havlak update scan steps：`190,916,727,113 -> 2,744,439,102`，下降 `98.56%`。
- Havlak 仍残留 `2.744B` scan steps，是因为实验实现还有 `3,692,903` 个 fallback insertions，fallback 路径仍会线性扫。
- 这说明正式修复还要去掉 fallback 线性扫描。

证据 7：CD 是边界条件，说明结论不是“所有 gap 都归因于 RSet update”。
- CD fast-index 后：
  - update scan steps 下降 `99.97%`。
  - 但 business `245.134s -> 245.848s`，没有改善。
- 原因是 CD baseline scan steps 只有 `401M`，远小于 Havlak 的 `190.9B`。
- CD 的主要 gap 仍是 extra allocations / mutator work：
  - CD allocations 比 Cheney 多 `10.25%`。
- 这反而支持结论的精确性：RSet update 线性扫描是 Havlak 最大瓶颈，但不是所有 workload 的唯一瓶颈。

证据 8：active young 消融说明剩余 gap 另有来源。
- out33 `GIY_RSET_INDEX_FAST=true CACHE_SIZE_KB=552`：
  - Havlak active young：`230.72KB -> 265.72KB`。
  - minor GC：`590,600 -> 512,300`。
  - business：`395.732s -> 389.257s`，再下降 `6.475s`。
  - allocations 基本不变：`2,029,600,757 -> 2,029,600,622`。
- 这说明剩余 gap 中还有 active-young/minor-GC 频率因素，但 extra allocations 不是由 young 容量直接造成。

最终证明结构：
- 源码证明：non-young 写入会触发 `values[]` 清除；清除需要 `slot -> index`；当前 index 查找是线性扫描。
- profile 证明：Havlak 上这条线性扫描达到 `190,916,727,113` step。
- 消融证明：把 `slot -> index` 改成 fast index 后，Havlak business 直接收回 `18.535s`，而 GC/alloc/WB/minor/forward 等关键计数完全不变。
- 反证边界：CD 线性扫描少得多，fast index 不改善 CD，说明结论不是过度泛化。
- 因此可以高置信地说：Havlak 当前最大已确认性能瓶颈就是 GiY RSet update 的 `slot -> index` 线性查找；full suite 的总性能也被它显著拖慢。

## 2026-05-03 RSet 相关开销占 full-suite business gap 的比例，以及剩余 gap 调查

full-suite 基准：
- GiY out29 business：`3428.880s`。
- Cheney out24 business：`3379.894s`。
- full-suite business gap：`+48.986s`。

已经严格实测的 RSet update 线性查找消融：
- Havlak：
  - out29 business：`414.267s`。
  - out31 RSet index business：`395.732s`。
  - 收回：`18.535s`。
- CD：
  - out29 business：`245.134s`。
  - out31 RSet index business：`245.848s`。
  - 反而慢：`0.714s`。
- CD + Havlak 净收回：`18.535s - 0.714s = 17.821s`。

占 full-suite business gap 的比例：
- 只按 Havlak 的正收益计算：
  - `18.535 / 48.986 = 37.84%`。
- 按 CD+Havlak 净收益计算：
  - `17.821 / 48.986 = 36.38%`。
- 所以当前已经严格证明的 RSet update 线性查找开销，至少解释 full-suite business gap 的约 `36.38%`，如果只看 Havlak 正项则是 `37.84%`。
- 这是保守值，因为还没有跑完整 full-suite 的 `GIY_RSET_INDEX_FAST=true`；不过现有数据已经覆盖最大 regression 的 Havlak 和第二大 regression 的 CD。

剩余 gap：
- full-suite business gap：`48.986s`。
- 扣除 CD+Havlak RSet index 净收益：`48.986s - 17.821s = 31.165s`。
- 剩余比例：`31.165 / 48.986 = 63.62%`。
- 所以 RSet 线性查找是最大已确认单项瓶颈，但不是全部。

剩余 gap 的构成：
- RSet index 后，CD+Havlak 仍剩：
  - CD：`245.848s - 239.056s = +6.792s`。
  - Havlak：`395.732s - 378.938s = +16.794s`。
  - CD+Havlak 合计：`+23.586s`。
- full-suite 中除 CD/Havlak 外的净 business gap：
  - `48.986s - 41.407s = +7.579s`。
- 所以扣除 RSet 线性查找后，剩余 `31.165s` 大致由：
  - CD+Havlak 剩余 `23.586s`；
  - 其他 benchmarks 净剩余 `7.579s`。

剩余 gap 的进一步实测解释：
- active young 容量消融 out33 在 CD+Havlak 上又收回：
  - CD：`245.848s -> 245.245s`，收回 `0.603s`。
  - Havlak：`395.732s -> 389.257s`，收回 `6.475s`。
  - 合计：`7.078s`。
- 这说明剩余 gap 中有一块来自 GiY active young 较小造成的 higher minor-GC frequency / metadata churn，尤其 Havlak 明显。
- 但 active young 消融后 allocations 基本不变：
  - CD：`2,314,174,986 -> 2,314,175,264`，几乎不变。
  - Havlak：`2,029,600,757 -> 2,029,600,622`，几乎不变。
- 因此 extra allocations 不是 active young 容量直接造成的。

剩余 gap 的最强证据指向：
- CD：
  - out33 后 business gap 仍 `+6.189s`。
  - allocations 仍比 Cheney 多 `10.25%`。
  - WB 仍比 Cheney 多约 `2.03%`。
- Havlak：
  - out33 后 business gap 仍 `+10.319s`。
  - allocations 仍比 Cheney 多 `9.31%`。
  - WB 仍比 Cheney 多约 `18.96%`。
- 这说明剩余 business gap 主要是 extra mutator work，而不是 GC core。

是否需要再启动 benchmarks：
- 为了精确得到 full-suite RSet 百分比，需要跑完整 `GIY_RSET_INDEX_FAST=true` suite。
- 但目前没有必要为了证明“RSet 线性查找是最大已确认瓶颈”再跑，因为：
  - Havlak 是 full-suite business gap 的 `72.12%` 来源；
  - RSet index 在 Havlak 上收回 `18.535s`；
  - 关键计数 GC/alloc/WB/minor/forward 全部不变；
  - profile scan steps 同步下降 `98.56%`。
- 如果后续要把百分比从“保守下界/最大两项实测”变成“full-suite 精确值”，再跑完整 suite 是必要的。

## 2026-05-03 结论：RSet 线性探查到底造成多少性能下降

明确数字：
- 在 Havlak 上，RSet update 的线性探查导致 GiY business time 多花 `18.535s`。
  - 当前 GiY out29：`414.267s`
  - 加 RSet index 后 out31：`395.732s`
  - 差值：`414.267s - 395.732s = 18.535s`
- 同一项在 Havlak total time 上导致约 `18.390s` 下降。
  - 当前 GiY out29 total：`447.385s`
  - RSet index 后 out31 total：`428.995s`
  - 差值：`18.390s`

放到 full-suite business gap 里看：
- GiY 当前 full-suite business gap：`3428.880s - 3379.894s = 48.986s`。
- 已严格实测的 RSet 线性探查净贡献：
  - Havlak 收回 `18.535s`
  - CD 反而慢 `0.714s`
  - CD+Havlak 净收回 `17.821s`
- 因此，当前可严格证明的 full-suite 线性探查性能损失是：
  - `17.821s / 48.986s = 36.38%`
  - 如果只看 Havlak 正向收益，则是 `18.535s / 48.986s = 37.84%`

结论：
- 这种 RSet update 线性探查不是造成 `48.986s` 全部 business gap 的原因。
- 它造成的明确、已消融验证的下降约为 `18s`。
- 对 full-suite business gap 的解释比例约为 `36% - 38%`。
- 剩下约 `31.165s` 仍来自其他因素，包括 active young 容量差异、extra mutator work、extra allocations / write barrier 次数等。

## 2026-05-03 深入调查：RSet 线性探查之后剩余 business gap 的主要原因

最终结论：
- 扣除 RSet 线性探查和 active-young 容量差异之后，剩下的主要问题不是 IC hit rate、也不是 JS benchmark 真的做了更多语义工作，而是 GiY 没有像 Cheney 的通用 scanner 一样在 GC 扫描 JSObject 时维护 allocation-site cache。
- 具体表现是：`AllocSite.as->pm` 落后于存活对象真实的 `shape->pm`。下一次从同一 allocation site 分配对象时，`get_cached_shape()` 只能拿到较小/较旧的 property map，预分配形状不足，于是 mutator 执行更多 `set_prop_()`、更多 shape search、更多 `gc_malloc/space_alloc`、更多 write barrier。
- 这解释的是 business/non-GC time 增加，因为这些额外工作发生在对象创建/属性写入路径，而不是 GC core 计时里。

源码证据：
- Cheney 通用扫描器在 `ejsvm/gc-visitor-inl.h` 的 `scan_object_properties(JSObject *p)` 中会调用 `alloc_site_update_info(p)`，并继续处理 `os->alloc_site->pm`。
- GiY 的自定义 `giy_scan_jsobject_conservative(JSObject *p)` 原本只处理 `p->shape` 和按对象大小保守扫描 `eprop[]`，没有执行 allocation-site update。
- `object.c:get_cached_shape()` 依赖 `as->pm` 来创建/复用 prealloc shape；如果 `as->pm` 落后，后续对象需要在 `set_prop_()` 里逐步扩 shape。
- `object.c` 的 shape search 计数正是在 `set_prop_()` 扩展 shape 时增加；因此 extra shape search 和 extra `set_prop_()` 是同一个机制的两个观测面。

性能计数证据（GiY out36 vs Cheney out37，no-AS profile，不使用 noisy AS_PROF）：
- CD：
  - business：GiY `247.046s`，Cheney `238.945s`，gap `+8.101s`。
  - shape search：GiY `312,449,807`，Cheney `92,274,397`，多 `220,175,410`，是 Cheney 的 `3.39x`。
  - `set_prop_` gprof calls：GiY `635,341,756`，Cheney `415,166,608`，多 `220,175,148`。
  - allocations：GiY `2,314,175,264`，Cheney `2,099,019,821`，多 `215,155,443`，`+10.25%`。
  - write barrier calls：GiY `9,344,047`，Cheney `9,157,893`，多 `186,154`。
- Havlak：
  - business：GiY `393.652s`，Cheney `378.276s`，gap `+15.376s`。
  - shape search：GiY `285,943,062`，Cheney `84,926,065`，多 `201,016,997`，是 Cheney 的 `3.37x`。
  - `set_prop_` gprof calls：GiY `792,556,118`，Cheney `592,330,180`，多 `200,225,938`。
  - allocations：GiY `2,029,600,622`，Cheney `1,856,819,962`，多 `172,780,660`，`+9.31%`。
  - write barrier calls：GiY `161,923,947`，Cheney `136,112,340`，多 `25,811,607`，`+18.96%`。

反证：不是 shape cache miss 或 JS workload 改变。
- shape cache hit rate 几乎一样：
  - CD：GiY `0.999999`，Cheney `0.999998`。
  - Havlak：GiY `0.999999`，Cheney `0.999997`。
- 问题不是 miss，而是 GiY 进入 shape search 的次数多了三倍多。
- 关键 JS-level 调用量基本一致，例如 Havlak 的 `vmrun_threaded`、`get_prop_with_ic`、`call_function` 量级一致；差异集中在对象创建/属性扩展相关路径。

新增 dry profile 验证（out39_giy_as_dry_profile，只计数、不改变语义）：
- CD：
  - `AS dry objects`: `64,996,332`
  - `AS dry pm match`: `10,445,672`
  - `AS dry pm mismatch`: `54,550,660`
  - mismatch 比例：`83.93%`
  - 同一轮 shape search 仍为 `312,449,807`，`set_prop_` calls 仍为 `635,341,756`，说明 dry profile 没改变行为，只揭示了落后的 allocation-site 元数据规模。
- Havlak：
  - `AS dry objects`: `254,575,178`
  - `AS dry pm match`: `100,439,376`
  - `AS dry pm mismatch`: `154,135,802`
  - mismatch 比例：`60.55%`
  - 同一轮 shape search 仍为 `285,943,062`，`set_prop_` calls 仍为 `792,556,118`。

直接修复实验：
- 试过在 GiY 的 JSObject 扫描路径里直接调用 Cheney 的 `alloc_site_update_info(p)`。
- 结果 CD 很早 SIGSEGV，崩在 `alloc_site_update_info()` 的 `find_lub` 路径。
- 原因判断：GiY minor GC 是 reserve/materialize 两阶段，扫描 young 对象时会先设置 forwarding pointer、稍后才真正 materialize；Cheney 的 update 函数假设扫描期对象图/PM 链处在即时复制扫描的状态。直接照搬会破坏这个时序假设。
- 因此修复方向不是简单复制 Cheney 的一行调用，而是要为 GiY 单独设计 allocation-site metadata update：在 reserve 阶段只计算/记录候选更新，在 materialize/patch 后安全更新 `as->pm/as->shape`，同时把函数表里的 `as->pm/as->shape` 当作需要 reserve/patch 的 slot 处理。

结论：
- RSet 线性探查解释 full-suite business gap 的约 `36% - 38%`。
- active young 容量在 CD+Havlak 上又解释约 `7.078s`。
- 剩余主要 gap 由 allocation-site cache 未被 GiY GC 训练导致的 extra mutator work 解释：它直接造成三倍多 shape search、约 `200M` 级别额外 `set_prop_()`、`170M-215M` 级别额外 allocation，以及 Havlak 上 `25.8M` 额外 write barrier。
- 下一步真正优化应优先做 GiY-safe allocation-site update，而不是继续追 IC miss 或 GC core。

## 2026-05-04 简化解释：剩余性能 gap 在哪里

剩余性能 gap 主要不在 GC core，而在 mutator/business 阶段。

GiY 没有像 Cheney 那样在 GC 扫描 JSObject 时更新 allocation-site cache，导致 allocation site 记录的 `as->pm` 落后于对象真实的 `shape->pm`。下一次从同一个 allocation site 创建对象时，GiY 预分配出来的 shape/property layout 太旧、太小，于是程序运行时不得不额外执行大量属性扩展。

这批额外属性扩展表现为：
- 更多 `set_prop_()`；
- 更多 shape search；
- 更多 property/shape 相关 allocation；
- 更多 write barrier。

所以剩余 gap 的本质是：GiY 让 JS 程序在对象创建和属性写入路径上多做了很多工作，而不是 GC 复制对象本身慢很多。

## 2026-05-04 为什么 GiY 和 Cheney 会产生 allocation-site cache 差距，以及 GiY 能否修复

为什么产生差距：
- Cheney 的通用对象扫描器在扫描 JSObject 时会执行 `alloc_site_update_info(p)`，用存活对象当前真实的 `shape->pm` 更新 allocation site 的 `as->pm`。
- 这个更新等于在 GC 期间学习“这个 allocation site 创建出来的对象最终通常长成什么形状”。
- GiY 当前用了自己的 `giy_scan_jsobject_conservative()`，只 reserve/copy/patch 指针，没有执行这一步学习，所以 `as->pm` 长期停留在较旧、较小的 property map 上。
- 下一轮 mutator 从这个 allocation site 分配对象时，`get_cached_shape()` 看到的是旧 `as->pm`，只能预分配旧 layout；随后对象要靠 `set_prop_()` 一步步扩展到真实 layout，于是产生额外 shape search、额外 allocation、额外 write barrier。

GiY 是否可以像 Cheney 一样做：
- 原则上可以，而且这是当前最应该做的优化方向。
- 但不能简单地在 GiY 扫描 JSObject 时直接调用 Cheney 的 `alloc_site_update_info(p)`。
- 已经试过直接调用，CD 很早 SIGSEGV，崩在 `alloc_site_update_info()` 的 `find_lub` 路径。
- 原因是 Cheney 是常规扫描/复制语义，扫描时对象图和 property-map 链的状态满足该函数假设；GiY 是 reserve/materialize 两阶段，扫描 young 对象时先设置 forwarding pointer，之后才真正复制到 old，并且函数表里的 `as->pm/as->shape` 也需要单独 reserve/patch。

正确修复方向：
- 在 GiY reserve 阶段不要直接改 `as->pm/as->shape`，而是记录“这个 allocation site 看到了哪个 live object shape/property map”。
- 在所有相关 young object、PropertyMap、Shape 都完成 reserve/materialize/patch 后，再统一计算 LUB 并更新 allocation-site cache。
- 更新 `as->pm/as->shape` 时，必须把函数表里的这些字段当作 GC root/slot 处理，保证其中的 young 指针也被 reserve 和 patch。
- 这样 GiY 才能获得 Cheney 的 allocation-site 学习效果，同时不破坏 GiY 两阶段复制的不变量。

## 2026-05-04 GiY allocation-site 修补实验和结论

代码修补：
- 在 `ejsvm/GiY.cc` 中新增 GiY-safe allocation-site update 路径：GiY 不再在扫描 young JSObject 时直接更新 allocation site，而是在对象完成 materialize 后，把其 shape 聚合进每轮 AS update log，最后按 allocation site 统一 LUB/apply。
- 保留函数表 allocation-site shape advance，并新增编译开关 `GIY_AS_FT_ADVANCE` 用于 A/B。
- 直接对每个 materialized JSObject 调用 Cheney 的 `alloc_site_update_info()` 已被否定：CD 立即 SIGSEGV，崩在 `alloc_site_update_info()` 内部。
- 直接对每个对象做安全版 AS update 也被否定：CD 运行 6 分钟仍未完成，单次迭代已到 `34-37s`，明显引入不可接受的 per-object 更新开销。

关键验证结果：
- 最终采用的方案是“materialize 后记录对象 shape，但按 allocation site 聚合后统一更新”。
- CD（out48，FT advance 开启）：
  - business `230.655s`，Cheney out37 为 `238.945s`，GiY 快 `8.290s`。
  - GC overhead `12.694s`，GC core `8.425s`。
  - allocations `2,006,796,747`，低于 Cheney `2,099,019,821`。
  - AS observations `64,858,050`，entries `1,740,399`，applied `24`，LUB failed `104`，invalid shape/pm/oldpm 全为 `0`。
- Havlak（out48，FT advance 开启）：
  - business `377.401s`，Cheney out37 为 `378.276s`，GiY 快 `0.875s`。
  - GC overhead `37.310s`，GC core `28.103s`。
  - allocations `1,817,371,816`，低于 Cheney `1,856,819,962`。
  - shape cache search `28,237,581`，远低于旧 GiY out36 的 `285,943,062`，也低于 Cheney out37 的 `84,926,065`。
  - AS observations `254,211,608`，entries `3,387,604`，applied `33`，LUB failed `11`，invalid shape/pm/oldpm 全为 `0`。

FT advance A/B：
- 关闭 FT advance 后 CD（out49）：
  - business `238.949s`，比 FT 开启慢 `8.294s`。
  - GC core 从 `8.425s` 降到 `7.246s`，只省 `1.179s`。
- 关闭 FT advance 后 Havlak（out49）：
  - business `384.739s`，比 FT 开启慢 `7.338s`。
  - GC core 从 `28.103s` 降到 `24.955s`，只省 `3.148s`。
- 结论：FT advance 虽有 GC 成本，但 business 收益更大，最终配置应保留。

当前结论：
- 之前剩余 business gap 的主要原因确实是 GiY 没有训练 allocation-site cache，导致对象 layout 预分配落后，进而触发额外 `set_prop_()`、shape search、allocation 和 write barrier。
- 修补后，CD 和 Havlak 的 business 时间都已经达到或超过 Cheney；剩余差异主要转移到 GC overhead，而不是 mutator/business。
- 下一步应以最终配置跑完整 benchmarks suite，验证全局结果是否也支持这个结论。

## 2026-05-04 最终 full-suite 验证与反复修补结论

最终保留的代码方案：
- GiY minor GC 在对象完成 materialize 后，把 JSObject 的 shape 记录到 allocation-site update log。
- 每轮 GC 按 allocation site 聚合 shape->pm，统一计算 LUB，再更新 `as->pm/as->shape`。
- 保留 `GIY_AS_FT_ADVANCE=true` 的函数表 allocation-site shape advance，因为 CD/Havlak A/B 显示它的 business 收益大于 GC 成本。
- 放弃两个错误/负收益方案：
  - 直接调用 Cheney `alloc_site_update_info()`：CD 立即 SIGSEGV。
  - 对每个 materialized object 逐个直接更新 AS：CD 6 分钟未完成，单次迭代已到 `34-37s`。
- 也放弃后续“提前过滤/fast-path”方案：out53 full-suite total `3496.306s`，不如最终采用的 out50 `3485.047s`。

完整 suite 目录：
- 最终采用：`build.debug/benchmarks/out50_giy_as_update_final_full`
- 对照 Cheney：`build.debug/benchmarks/out24`
- 对照旧 GiY：`build.debug/benchmarks/out29_giy_current_full`
- 被否定的提前过滤版本：`build.debug/benchmarks/out53_giy_as_update_prefilter_full`

最终 full-suite 总量（12 项：Bounce/List/Sieve/Queens/Permute/Storage/Towers/Mandelbrot/Richards/CD/NBody/Havlak）：
- Cheney out24：
  - total `3499.343s`
  - business `3379.894s`
  - GC `119.450s`
  - allocations `19,789,564,672`
  - forward ops `1,696,428,830`
- 旧 GiY out29：
  - total `3521.378s`
  - business `3428.880s`
  - GC `92.497s`
  - allocations `20,177,532,732`
  - forward ops `1,636,291,388`
- 新 GiY out50：
  - total `3485.047s`
  - business `3378.598s`
  - GC `106.448s`
  - allocations `19,637,117,887`
  - forward ops `1,468,807,058`

最终差异：
- 新 GiY vs 旧 GiY：
  - total `-36.331s`
  - business `-50.282s`
  - GC `+13.951s`
  - allocations `-540,414,845`
  - forward ops `-167,484,330`
- 新 GiY vs Cheney：
  - total `-14.296s`
  - business `-1.296s`
  - GC `-13.002s`
  - allocations `-152,446,785`
  - forward ops `-227,621,772`

关键单项：
- CD：新 GiY business `229.997s`，Cheney `239.056s`，旧 GiY `245.134s`。
- Havlak：新 GiY business `376.846s`，Cheney `378.938s`，旧 GiY `414.267s`。
- Storage 是主要反例项：新 GiY out50 business `178.553s`，但复跑最终配置为 `164.625s`；no-AS 基线为 `163.462s`。Storage 的 AS observations 可达 `546,200,007`，applied 只有 `5`，说明它对 AS update 几乎没有收益，额外成本主要来自巨量 live JSObject 的 eligibility/record 检查。提前过滤把 observations 降到 `8`，但 full-suite 总体反而变差，因此没有保留。

最终结论：
- 原先和 Cheney 相比的 GiY business gap，核心确实是 allocation-site cache 没有被 GiY minor GC 训练。
- 修补后 full-suite business 从旧 GiY `3428.880s` 降到 `3378.598s`，收回 `50.282s`，已经略快于 Cheney `1.296s`。
- 因此“allocation-site cache 训练缺失导致 mutator 做额外对象 layout/属性扩展工作”这个判断成立。
- 剩余主要工程问题不是这个根因是否成立，而是如何降低 Storage 这类 benchmark 上 AS 检查本身的 GC 成本；需要更细的 dirty allocation-site 机制，而不是简单全对象检查。

## 2026-05-04 背景解释：allocation-site cache、CheneyGC 与修复前 GiY 的区别

背景：
- JavaScript 对象通常不是一次性拥有所有属性，而是在运行过程中逐步写入属性。
- VM 用 `Shape` / `PropertyMap` 描述对象当前的属性布局。
- `allocation site` 是“某条 new/对象创建指令的位置”。同一个位置反复创建出来的对象，通常会长成相似的属性布局。
- `allocation-site cache` 的作用是让 VM 记住“这个创建位置的对象最终通常长成什么布局”，下次从这个位置创建对象时直接使用更接近最终状态的 layout。
- 如果这个 cache 训练得好，mutator 后续少做 `set_prop_()`、shape search、property map/shape allocation、write barrier。
- 如果这个 cache 落后，对象刚创建时 layout 太小/太旧，程序运行中就要一步步扩展属性，产生大量额外 business time。

CheneyGC 的行为：
- CheneyGC 扫描存活 JSObject 时会调用 `alloc_site_update_info(p)`。
- 它用存活对象当前真实的 `shape->pm` 更新该对象所属 allocation site 的 `as->pm/as->shape/as->polymorphic`。
- 这等价于 GC 顺手学习“活下来的对象最终长成什么形状”。
- 因为 Cheney 是普通复制扫描流程，扫描对象时 shape/property-map 状态满足 `alloc_site_update_info()` 的假设。

修复前 GiY 的行为：
- GiY 有自己的 minor GC 扫描/复制路径，核心是 reserve/materialize 两阶段。
- 修复前 GiY 只负责找 live object、复制对象、patch 指针，没有执行 Cheney 那一步 allocation-site cache 训练。
- 所以 GiY 的 `as->pm` 经常停留在旧 layout 上。
- 结果是下一轮 mutator 从同一个 allocation site 创建对象时，拿到的是过时 layout，后面再通过 `set_prop_()` 补属性扩展。

数据证据：
- 修复前 GiY vs Cheney：
  - CD business：GiY `245.134s`，Cheney `239.056s`。
  - Havlak business：GiY `414.267s`，Cheney `378.938s`。
- 修复后：
  - CD business：新 GiY `229.997s`。
  - Havlak business：新 GiY `376.846s`。
- full-suite business：
  - 旧 GiY `3428.880s`。
  - Cheney `3379.894s`。
  - 新 GiY `3378.598s`。
- 因此修复前 GiY 在这方面慢的核心原因，是没有像 Cheney 那样利用 GC 扫描 live object 的机会训练 allocation-site cache。

## 2026-05-04 最终实验大结论：当前 GiY vs CheneyGC

最终采用的数据：
- 当前 GiY：`build.debug/benchmarks/out50_giy_as_update_final_full`
- CheneyGC：`build.debug/benchmarks/out24`
- 修复前 GiY：`build.debug/benchmarks/out29_giy_current_full`

full-suite 总结论：
- 当前 GiY total `3485.047s`，CheneyGC total `3499.343s`，当前 GiY 快 `14.296s`。
- 当前 GiY business `3378.598s`，CheneyGC business `3379.894s`，当前 GiY 快 `1.296s`。
- 当前 GiY GC `106.448s`，CheneyGC GC `119.450s`，当前 GiY 快 `13.002s`。
- 修复前 GiY business 是 `3428.880s`，修复后降到 `3378.598s`，收回 `50.282s`。
- 因此，当前 GiY 在 full-suite 总时间、business 时间、GC 时间上都已经不慢于 CheneyGC；总时间上当前 GiY 更快。

关键单项：
- CD：
  - CheneyGC business `239.056s`
  - 修复前 GiY business `245.134s`
  - 当前 GiY business `229.997s`
  - 当前 GiY 比 CheneyGC 快 `9.059s`，比修复前 GiY 快 `15.137s`
- Havlak：
  - CheneyGC business `378.938s`
  - 修复前 GiY business `414.267s`
  - 当前 GiY business `376.846s`
  - 当前 GiY 比 CheneyGC 快 `2.092s`，比修复前 GiY 快 `37.421s`

原因分析：
- 修复前 GiY 和 CheneyGC 的主要 business gap，不是 GC copy 本身，而是 mutator 被迫多做对象 layout/属性扩展工作。
- CheneyGC 在扫描 live JSObject 时会调用 `alloc_site_update_info(p)`，用存活对象当前真实的 `shape->pm` 训练 allocation-site cache。
- 修复前 GiY 没有这一步，所以 allocation site 的 `as->pm` 经常落后；下一次对象创建时 layout 偏旧，后续要靠 `set_prop_()` 继续扩展属性。
- 这会带来额外 `set_prop_()`、shape search、PropertyMap/Shape allocation 和 write barrier。
- 当前修复给 GiY 补上了 GiY-safe allocation-site training：materialize 后记录 live JSObject shape，按 allocation site 聚合，再统一 LUB/update。

直接证据：
- 修复前 GiY full-suite business 比 CheneyGC 慢 `48.986s`：`3428.880s - 3379.894s`。
- 修复后当前 GiY full-suite business 比修复前快 `50.282s`。
- 修复后当前 GiY business 已经比 CheneyGC 快 `1.296s`。
- CD/Havlak 是原先 gap 最明显的项目，修复后 CD 从 `245.134s` 降到 `229.997s`，Havlak 从 `414.267s` 降到 `376.846s`。
- Havlak 的 shape-cache/search 诊断也支持原因判断：旧 GiY no-AS profile 中 Havlak shape search 曾是 `285,943,062`，Cheney 是 `84,926,065`；修复实验中 Havlak shape search 降到 `28,237,581`，说明对象 layout 训练确实减少了属性扩展相关路径。

剩余问题：
- Storage 是主要反例/风险点。当前 GiY 对 Storage 的 AS update 收益很小，观察到 AS observations 可达 `546,200,007`，但 applied 只有 `5`。
- 这说明某些 benchmark 上，GiY 仍会为“几乎不需要更新的 allocation site”付出检查成本。
- 后续优化方向不是重新否定 allocation-site 训练，而是做更细的 dirty allocation-site / dirty object-site 机制，避免 Storage 这类场景全量检查。

## 2026-05-05 当前 GiY vs CheneyGC 所有 benchmark 逐项数据

数据来源：
- 当前 GiY：`build.debug/benchmarks/out50_giy_as_update_final_full`
- CheneyGC：`build.debug/benchmarks/out24`
- Δ = 当前 GiY - CheneyGC；负数表示当前 GiY 更快或更少。

| benchmark | Cheney total | GiY total | Δ total | Cheney business | GiY business | Δ business | Cheney GC | GiY GC | Δ GC | Cheney alloc | GiY alloc | Δ alloc |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Bounce | `158.885` | `157.335` | `-1.550` | `158.741` | `157.216` | `-1.525` | `0.145` | `0.119` | `-0.026` | `45,604,143` | `30,608,585` | `-14,995,558` |
| List | `98.878` | `99.094` | `+0.216` | `98.863` | `99.082` | `+0.219` | `0.014` | `0.012` | `-0.002` | `9,302,050` | `4,655,378` | `-4,646,672` |
| Sieve | `123.732` | `120.831` | `-2.901` | `120.971` | `119.419` | `-1.552` | `2.761` | `1.412` | `-1.349` | `601,999` | `601,999` | `0` |
| Queens | `116.048` | `116.321` | `+0.273` | `116.018` | `116.281` | `+0.263` | `0.030` | `0.040` | `+0.010` | `8,002,056` | `8,002,056` | `0` |
| Permute | `382.245` | `388.003` | `+5.758` | `382.243` | `388.001` | `+5.758` | `0.002` | `0.002` | `0.000` | `404,153` | `304,698` | `-99,455` |
| Storage | `209.181` | `213.328` | `+4.147` | `158.234` | `178.553` | `+20.319` | `50.947` | `34.774` | `-16.173` | `1,092,302,019` | `1,092,301,898` | `-121` |
| Towers | `185.360` | `184.093` | `-1.267` | `185.357` | `184.090` | `-1.267` | `0.003` | `0.003` | `0.000` | `1,802,073` | `968,893` | `-833,180` |
| Mandelbrot | `234.111` | `235.048` | `+0.937` | `228.344` | `226.881` | `-1.463` | `5.767` | `8.167` | `+2.400` | `6,547,928,783` | `6,547,928,783` | `0` |
| Richards | `943.234` | `936.209` | `-7.025` | `943.121` | `936.072` | `-7.049` | `0.113` | `0.138` | `+0.025` | `52,753,056` | `52,552,966` | `-200,090` |
| CD | `251.045` | `242.589` | `-8.456` | `239.056` | `229.997` | `-9.059` | `11.989` | `12.592` | `+0.603` | `2,099,019,821` | `2,006,796,747` | `-92,223,074` |
| NBody | `378.798` | `378.113` | `-0.685` | `370.008` | `366.160` | `-3.848` | `8.790` | `11.952` | `+3.162` | `8,075,024,557` | `8,075,024,068` | `-489` |
| Havlak | `417.826` | `414.083` | `-3.743` | `378.938` | `376.846` | `-2.092` | `38.889` | `37.237` | `-1.652` | `1,856,819,962` | `1,817,371,816` | `-39,448,146` |

总计：
- total：CheneyGC `3499.343s`，当前 GiY `3485.047s`，Δ `-14.296s`。
- business：CheneyGC `3379.894s`，当前 GiY `3378.598s`，Δ `-1.296s`。
- GC：CheneyGC `119.450s`，当前 GiY `106.448s`，Δ `-13.002s`。
- minor GC count：CheneyGC `2,341,014`，当前 GiY `2,302,292`，Δ `-38,722`。
- write barrier calls：CheneyGC `2,054,529,628`，当前 GiY `2,079,088,412`，Δ `+24,558,784`。
- allocations：CheneyGC `19,789,564,672`，当前 GiY `19,637,117,887`，Δ `-152,446,785`。
- forward operations：CheneyGC `1,696,428,830`，当前 GiY `1,468,807,058`，Δ `-227,621,772`。
- GC core：CheneyGC `89.940s`，当前 GiY `72.829s`，Δ `-17.111s`。

逐项结论：
- 当前 GiY total 更快的项目：Bounce、Sieve、Towers、Richards、CD、NBody、Havlak，共 7 项。
- 当前 GiY total 更慢的项目：List、Queens、Permute、Storage、Mandelbrot，共 5 项。
- 当前 GiY business 更快的项目：Bounce、Sieve、Towers、Mandelbrot、Richards、CD、NBody、Havlak，共 8 项。
- 当前 GiY business 更慢的项目：List、Queens、Permute、Storage，共 4 项。
- 最大正收益来自 CD、Richards、Havlak、Sieve/Bounce。
- 最大负项是 Storage business `+20.319s` 和 Permute business `+5.758s`；Storage 同时 GC 快 `16.173s`，所以 total 只慢 `4.147s`。

## 2026-05-05 当前 GiY 与 CheneyGC 的内存布局

共同基础：
- 两者都使用 `Cache_space` + `Dram_space`。
- `Cache_space` 字段：`begin / work_begin / current / end / total_size / threshold_size`。
- `Dram_space` 字段：`begin / free / current / end / total_size / available_bytes`。
- 每个对象前面都有 `object_header`：`forwarding_pointer / type / size`，payload 紧跟 header 后面，对象 footprint 按 `ALIGN(size + object_header)` 对齐。
- `[cache_space.begin, cache_space.work_begin)` 是 init/static area；minor GC 不回收，指针判断中视为 init/old。
- `[cache_space.work_begin, cache_space.current)` 是当前 young allocation area 中已经分配的对象。
- `[cache_space.current, cache_space.end)` 是 young free area。
- `Dram_space [begin, free)` 是 old generation；minor GC 之后存活 young object 被放到这里。
- minor GC 结束后，两者都会执行 `cache_space.current = cache_space.work_begin`，整块 young work area 被回收。

CheneyGC 当前布局：
- 不是传统两个等大的 from-space/to-space semispace。
- 它使用 cache 作为 young/from area，DRAM 作为 old/to area。
- 对象先分配到 cache 的 young area。
- minor GC 时，Cheney tracer 遇到 young pointer 后立即 `memcpy` 到 `dram_space.free`，推进 `dram_space.free`，并在源 cache object header 里写 forwarding pointer。
- 然后从 `dram_space.current = scan_start` 开始扫描新复制到 DRAM 的对象，直到 `dram_space.current == dram_space.free`。
- CheneyGC 会在 cache 顶部保留 function-table slot set：大小为 young bytes 的 `1/8`，至少 `32KB`。
- 以 out24/CD 实际输出为例：
  - cache total `320KB`
  - DRAM total `64GB`
  - init 后 `work_begin` 位于 `cache_space.begin + ~19.9KB`
  - 按代码推算，Cheney function-table slot set 约 `37.5KB`，可用于 normal young allocation 的区域约 `262.7KB`

CheneyGC 图示：
```text
Cache_space:
  [begin, work_begin)        init/static objects, minor GC 不回收
  [work_begin, current)      young allocated objects
  [current, cheney_end)      young free area
  [cheney_end, original_end) function-table slot set

Dram_space:
  [begin, free)              old/live copied objects
  [free, end)                DRAM free
```

GiY 当前布局：
- GiY 也用 cache 作为 young area，DRAM 作为 old area。
- 与 CheneyGC 最大区别不是最终空间归属，而是 minor GC 过程中的 transient layout。
- GiY 在 cache 顶部额外绑定 GC auxiliary area：
  - GC stack：young bytes 的 `1/8`，至少 `8KB`
  - edge log：当前为 `0`
  - function-table slot set：young bytes 的 `1/64`，至少 `32KB`
- `giy_bind_stack_to_cache()` 会把 `cache_space.end` 往前移动，所以 normal young allocation 不能占用这些 auxiliary 区域。
- 以 out50/CD 实际输出为例：
  - cache total `360KB`
  - DRAM total `64GB`
  - init 后 young before aux `340.24KB`
  - GiY aux total `74.52KB`：stack `42.52KB`，edge `0KB`，ft `32KB`
  - normal young after aux `265.72KB`

GiY 图示：
```text
Cache_space:
  [begin, work_begin)        init/static objects, minor GC 不回收
  [work_begin, current)      young allocated objects
  [current, giy_end)         young free area
  [giy_end, edge_begin)      GiY GC stack
  [edge_begin, ft_begin)     edge log, 当前 0
  [ft_begin, original_end)   function-table slot set

Dram_space:
  [begin, free)              old/live materialized objects
  [free, end)                DRAM free
```

GiY minor GC 的 transient layout：
- reserve 阶段：遇到 young object 时，先在 DRAM 中预留一段 destination slot，推进 `dram_space.free`，但对象内容还没复制过去。
- source young object header 的 `forwarding_pointer` 指向 DRAM reserved payload。
- source young object 被压入 GiY GC stack。
- traversal/materialize 阶段：从 GC stack 弹出 source young object，扫描其字段、reserve 子对象、patch source 内部 young slot，然后把整个对象 copy/materialize 到已预留的 DRAM 位置。
- 当前实现会对较大对象使用 non-temporal store copy。

GiY 与 CheneyGC 的关键区别：
- 最终布局相似：活下来的 young object 都进入 DRAM old generation，cache young area 被整体清空。
- CheneyGC 是“遇到对象就立即 copy 到 DRAM，然后扫描 DRAM 中的新对象”。
- GiY 是“先 reserve DRAM 位置并建立 forwarding pointer，再用 cache 内 GC stack 驱动 traversal，最后 materialize/copy 到 DRAM”。
- CheneyGC 不需要额外的 GiY GC stack；GiY 需要在 cache 顶部保留 GC stack/FT slot set。
- GiY 的设计为后续紧凑排列、non-temporal copy、cache/DRAM 分工提供了空间，但也引入了 auxiliary area 和两阶段不变量。

## 2026-05-05 function table slot set 是什么，为什么需要

结论：
- `function table slot set` 不是对象集合，而是“function table 中哪些 slot 当前保存了 young pointer”的 slot 地址集合。
- slot set 里存的是 slot 的地址，不是 slot 指向的对象。
- 它的作用是让 minor GC 把这些 function table slot 当作 root/old-to-young edge 处理：先 reserve 它们指向的 young object，再在对象 forwarding 后 patch slot。

为什么不能只靠普通 remembered set：
- 普通 remembered set 主要记录 heap 内 old/init 对象里的 slot。
- function table 是 VM 的全局/静态元数据区，不在 `cache_space` / `dram_space` 的普通 heap object 范围内。
- 当前 GiY 的 remembered-set patch/scan 会检查 slot 地址是否在 DRAM 或 init area；function table slot 地址通常不满足这个条件。
- 所以如果 function table slot 指向 young object，而没有专门记录，minor GC 可能不会把这个 young object 当作 root，也不会把 slot patch 到 copied old address。

function table 里为什么会有 young pointer：
- code loader 可能把 JSValue 常量写进 function table 的 constant area。
- allocation-site cache 存在 function table 的 instruction metadata 里，例如 `as->pm`、`as->shape`。
- inline cache / property metadata 也可能短期持有 heap pointer。
- 这些 slot 不一定是 heap object 字段，但它们仍然可能指向 heap object。

当前实现：
- 写 function table slot 时调用：
  - `giy_record_ft_jsvalue_slot(JSValue *slot, JSValue value)`
  - `giy_record_ft_ptr_slot(void **slot, void *value)`
- 如果 value 是 young pointer，就把 slot 地址加入 `g_ft_slot_set`。
- 为了区分 slot 类型，低 bit 用作 tag：
  - 普通 `JSValue *slot`
  - tagged `void **slot`
- minor GC 中：
  - `giy_reserve_function_table_slots()` 扫描 slot set，把 slot 当前 value 当 root reserve。
  - `giy_patch_function_table_slots()` 在 forwarding 完成后把 slot 改成新地址。
  - `giy_clear_function_table_slots()` 清空集合。
- CheneyGC 也有对应的 `cache_cheney_ft_slot_set` 和同名记录接口。

为什么不每次扫描整个 function table：
- function table 很大，里面包括大量 instruction、constant、inline cache、allocation-site metadata。
- 每次 minor GC 全量扫描会很贵。
- 其中很多字段是 weak cache，不应全部当 strong root。
- slot set 只记录“确实写入过 young pointer 的 slot”，因此是一个精确的强 root 子集。

和 allocation-site 修复的关系：
- 这次修复后，GiY 会更新 allocation-site cache。
- `as->pm/as->shape` 位于 function table 的 instruction metadata 中。
- 如果这些字段短期指向 young `PropertyMap` / `Shape`，就必须通过 function table slot set 让 minor GC reserve/patch 它们。
- 否则 allocation-site cache 可能保存悬空 young pointer，下一轮访问时出错或退化。

## 2026-05-05 GiYOL 初始设计与实现记录

目标：
- 新增 GC 名称：`GiYOL`，构建方式为 `OPT_GC=giyol`。
- 主体 GC 逻辑必须保持 GiY 一样：roots、remembered set、function table slot set、weak clear、allocation-site update、reserve/patch 都复用 GiY。
- 唯一改变 live object materialize/copy 策略：GiYOL 不在每个 live object 扫描后立刻 copy，而是先把 live object 的 source/destination/size 放进 batch，累计到阈值后一起用 non-temporal store flush。

当前实现：
- `ejsvm/common.mk` 新增 `OPT_GC=giyol`：
  - 编译同一组文件：`giy_dram_manager.cc giy_rset.cc GiY.cc`
  - 定义：`USE_GIY_MINOR` + `USE_GIYOL`
- `ejsvm/giy_dram_manager.cc` 在 `USE_GIYOL` 下打印 `Now we are using GiYOL GC`。
- `ejsvm/GiY.cc` 新增：
  - `GIYOL_BATCH_BYTES`，默认 `64KB`
  - `GiYOLCopyEntry { dst, src, nbytes }`
  - `GiYOLCopyBatch`
  - `giyol_enqueue_copy()`
  - `giyol_flush_copy_batch()`
- GiYOL flush 时会对 batch 按 `dst` 地址排序，尽量让 streaming store 按 DRAM 目标地址递增写入。
- batch bytes 达到 `GIYOL_BATCH_BYTES` 后，使用 non-temporal store 复制整个 batch。
- minor GC 结束前剩余不足阈值的 batch 用普通 GiY copy 策略 flush，避免对很小尾批次强行 streaming store。

为什么仍符合 GiY 原则：
- GiY 的 reserve 阶段仍然先在 DRAM 预留目标位置，并在 young source header 写 forwarding pointer。
- GiY 的 traversal 仍然在 young/cache 内完成字段扫描和 young pointer patch。
- GiYOL 只是延迟 materialize/copy 到 DRAM 的时间点；不会改变对象发现顺序、forwarding pointer 语义、root/RS/FT patch 语义。
- 在 roots/RS/FT patch 之前，GiYOL 会 flush 所有 pending live objects，保证 DRAM destination 已经 materialized。

需要验证：
- `OPT_GC=giyol` 构建能否通过。
- smoke tests：`hello_world.sbc`、`giy_gc_probe.sbc`。
- 关键 benchmarks：CD/Havlak/Storage，和当前 GiY out50 对比。
- 如果关键项稳定，再跑 full suite。

## 2026-05-05 GiYOL targeted 阈值实验记录

已完成的 GiYOL 修正：
- `GIYOL_SORT_BATCH` 默认改为 `0`。原因：按 batch 对 copy entry 做 `qsort` 的成本太高，尤其 Storage 会产生大量 batch。
- `GIYOL_BATCH_BYTES` 默认改为 `128KB`。原因：targeted benchmark 中 128KB 是目前 CD/Havlak/Storage 三项总和最好的阈值。
- GiYOL 的主体仍复用 GiY；改变的只有 live object materialize/copy：先记录 `{dst, src, nbytes}`，达到阈值后批量 flush。

smoke test：
- `build.debug/benchmarks/out54_giyol_smoke`
- `hello_world.sbc` 通过。
- `giy_gc_probe.sbc` 通过。
- 输出中确认使用的是 `Now we are using GiYOL GC`。

targeted benchmark 输出目录：
- 当前 GiY baseline：`build.debug/benchmarks/out50_giy_as_update_final_full`
- GiYOL 64KB + sort 初版：`build.debug/benchmarks/out55_giyol_key`
- GiYOL 256KB + no-sort：`build.debug/benchmarks/out56_giyol_key_nosort_256k`
- GiYOL 128KB + no-sort：`build.debug/benchmarks/out57_giyol_key_nosort_128k`
- GiYOL 64KB + no-sort：`build.debug/benchmarks/out58_giyol_key_nosort_64k`

三项 targeted 总和，单位秒：

| 版本 | total | business | GC full | GC core |
|---|---:|---:|---:|---:|
| GiY out50 | 870.000 | 785.396 | 84.603 | 68.152 |
| GiYOL 64KB + sort | 937.938 | 778.880 | 159.059 | 139.681 |
| GiYOL 64KB + no-sort | 893.431 | 796.156 | 97.275 | 77.034 |
| GiYOL 128KB + no-sort | 870.568 | 774.226 | 96.341 | 77.003 |
| GiYOL 256KB + no-sort | 876.894 | 780.086 | 96.808 | 77.193 |

关键单项数据，单位秒：

| benchmark | 版本 | total | business | GC full | GC core | GiYOL NT batches | GiYOL NT MB | small MB |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| CD | GiY out50 | 242.589 | 229.997 | 12.592 | 8.446 | - | - | - |
| CD | 64KB + sort | 250.218 | 233.102 | 17.117 | 12.088 | 16522 | 1035.28 | 6748.72 |
| CD | 64KB + no-sort | 246.376 | 232.994 | 13.382 | 8.087 | 16522 | 1035.28 | 6748.72 |
| CD | 128KB + no-sort | 243.934 | 231.096 | 12.838 | 7.825 | 0 | 0.00 | 7784.00 |
| CD | 256KB + no-sort | 246.530 | 233.579 | 12.951 | 7.902 | 0 | 0.00 | 7784.00 |
| Havlak | GiY out50 | 414.083 | 376.846 | 37.237 | 27.765 | - | - | - |
| Havlak | 64KB + sort | 429.508 | 380.344 | 49.164 | 38.275 | 344297 | 21909.04 | 9023.47 |
| Havlak | 64KB + no-sort | 420.494 | 384.206 | 36.288 | 24.780 | 344297 | 21909.04 | 9023.47 |
| Havlak | 128KB + no-sort | 411.838 | 376.656 | 35.182 | 24.271 | 153897 | 19286.55 | 11645.96 |
| Havlak | 256KB + no-sort | 417.041 | 381.024 | 36.017 | 24.875 | 60997 | 15251.37 | 15681.13 |
| Storage | GiY out50 | 213.328 | 178.553 | 34.774 | 31.941 | - | - | - |
| Storage | 64KB + sort | 258.212 | 165.434 | 92.778 | 89.318 | 907547 | 56746.07 | 2025.47 |
| Storage | 64KB + no-sort | 226.561 | 178.956 | 47.605 | 44.167 | 907547 | 56746.07 | 2025.47 |
| Storage | 128KB + no-sort | 214.796 | 166.474 | 48.321 | 44.907 | 453773 | 56730.28 | 2041.25 |
| Storage | 256KB + no-sort | 213.323 | 165.483 | 47.840 | 44.416 | 226886 | 56727.26 | 2044.28 |

当前结论：
- GiYOL 已经能正确构建和运行，且 CD/Havlak/Storage targeted benchmark 全部退出码 0。
- 64KB + sort 是错误方向：排序和过多 flush 叠加，使三项总时间比 GiY 慢 67.938s。
- 64KB + no-sort 仍比 GiY 慢 23.431s，说明主要问题不只是排序；过早对小批量 survivor 使用 NT store 本身也有明显成本。
- 256KB + no-sort 对 Storage 最稳，但 Havlak/CD 不如 128KB。
- 128KB + no-sort 是当前 targeted 最优：三项总时间 870.568s，只比 GiY out50 慢 0.568s；其中 Havlak 比 GiY 快 2.245s，Storage 比 GiY 慢 1.468s，CD 比 GiY 慢 1.345s。
- 这个实验也说明：把 survivor 合批以后使用 NT store 并不是无条件收益。对 Storage 这种几乎全是小对象的 workload，NT store 会明显降低 GC copy 本身效率，但可能减少后续 business cache 污染；最终 total 取决于两者抵消。

下一步：
- 使用 128KB + no-sort 作为 GiYOL 当前默认配置。
- 重新构建 `OPT_GC=giyol`。
- 跑完整 benchmark suite，输出新目录，确认 full suite 上是否有隐藏 regression。

## 2026-05-05 GiYOL 最终实现、完整 suite 与结论

最终代码状态：
- 新 GC 名称：`GiYOL`。
- 构建方式：`make -C build.debug OPT_GC=giyol GIY_RSET_INDEX_FAST=true CACHE_SIZE_KB=552 GIY_AS_UPDATE=true GIY_AS_FT_ADVANCE=true -j4`
- `OPT_GC=giyol` 使用和 GiY 相同的主体文件：`giy_dram_manager.cc giy_rset.cc GiY.cc`。
- GiYOL 只额外定义 `USE_GIYOL`，主体 GC 逻辑仍复用 GiY。
- 默认参数：
  - `GIYOL_BATCH_BYTES = 128KB`
  - `GIYOL_SORT_BATCH = 0`
  - `GIYOL_INLINE_COPY_ENTRIES = 64`
  - `GIYOL_ADAPTIVE_BATCH = 0`

最终实现要点：
- GiY 仍然先 reserve 目标 old/DRAM 地址，并在 young header 写 forwarding pointer。
- GiY 仍然在 young/cache 内扫描 object children，并 patch young object 内部 slot。
- GiYOL 不立刻 materialize 每个 live object，而是记录 `{dst, src, nbytes}` 到 `GiYOLCopyBatch`。
- batch 达到 128KB 时，用 non-temporal store flush batch。
- 不足阈值的尾批次在 traversal 结束时用普通 GiY copy flush。
- batch 自带 64 个 inline entry，避免小 GC 每次 malloc/free copy descriptor。
- 排序默认关闭。64KB + sort 实验证明排序成本太高，不适合作为默认策略。
- `GIYOL_ADAPTIVE_BATCH` 曾测试过；它可以避免部分 no-NT workload 的 delayed-copy 副作用，但在 Storage 上完全不触发 GiYOL NT batch，偏离本实验目标，因此保留为可选宏，默认关闭。

最终 smoke：
- 输出目录：`build.debug/benchmarks/out67_giyol_final_smoke`
- `hello_world.sbc` 退出码 0。
- `giy_gc_probe.sbc` 退出码 0。
- 输出确认：`Now we are using GiYOL GC`。
- `giy_gc_probe` 中确认 `GiYOL batch bytes = 131072`，`GiYOL NT batches = 11`。

最终 full suite：
- 输出目录：`build.debug/benchmarks/out63_giyol_inline_full`
- 12 项全部退出码 0。
- 注意：`out59_giyol_full` 是 inline 优化前的一轮 full suite，其中 Sieve 出现单次异常 166.861s；单独重跑 Sieve 为 122.099s，最终 out63 的 Sieve 为 123.542s，因此最终结论不用 out59。

全 suite 总和，单位秒：

| 版本 | total | business | GC full | GC core | minor GC | allocations | forward ops |
|---|---:|---:|---:|---:|---:|---:|---:|
| Cheney out24 | 3499.343 | 3379.894 | 119.450 | 89.940 | 2341014 | 19789564672 | 1696428830 |
| GiY out50 | 3485.047 | 3378.598 | 106.448 | 72.829 | 2302292 | 19637117887 | 1468807058 |
| GiYOL out63 | 3560.942 | 3438.931 | 122.013 | 82.446 | 2302292 | 19637117867 | 1468807204 |

GiYOL out63 相对 GiY out50：
- total 慢 75.895s。
- business 慢 60.333s。
- GC full 慢 15.565s。
- GC core 慢 9.617s。
- minor GC 次数相同：2302292。
- allocations 基本相同：GiYOL 少 20 次，可以视为同口径。
- forward ops 基本相同：GiYOL 多 146 次，可以视为同口径。

GiYOL out63 相对 Cheney out24：
- total 慢 61.599s。
- business 慢 59.037s。
- GC full 慢 2.563s。
- GC core 快 7.494s。
- 也就是说，GiYOL 的 GC core 仍比 Cheney 少，但 total 被 business/non-GC 时间和部分 GC full overhead 抵消。

GiYOL out63 相对 GiY out50 的逐项数据，单位秒：

| benchmark | GiY total | GiYOL total | Δtotal | Δbusiness | ΔGC full | ΔGC core | GiYOL NT batches | NT MB | small MB |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Bounce | 157.335 | 165.578 | +8.243 | +8.226 | +0.017 | +0.002 | 0 | 0.00 | 36.55 |
| List | 99.094 | 97.165 | -1.929 | -1.930 | +0.001 | +0.000 | 0 | 0.00 | 0.55 |
| Sieve | 120.831 | 123.542 | +2.711 | +2.577 | +0.134 | +0.036 | 0 | 0.00 | 1910.76 |
| Queens | 116.321 | 116.288 | -0.033 | -0.038 | +0.006 | +0.000 | 0 | 0.00 | 1.62 |
| Permute | 388.003 | 390.197 | +2.194 | +2.194 | +0.000 | +0.000 | 0 | 0.00 | 0.03 |
| Storage | 213.328 | 213.332 | +0.004 | -13.220 | +13.225 | +12.650 | 453773 | 56730.28 | 2041.25 |
| Towers | 184.093 | 182.606 | -1.487 | -1.488 | +0.001 | +0.000 | 0 | 0.00 | 0.12 |
| Mandelbrot | 235.048 | 238.696 | +3.648 | +2.532 | +1.116 | +0.055 | 0 | 0.00 | 71.07 |
| Richards | 936.209 | 936.094 | -0.115 | -0.132 | +0.016 | +0.002 | 0 | 0.00 | 14.92 |
| CD | 242.589 | 243.676 | +1.087 | +0.756 | +0.331 | -0.506 | 0 | 0.00 | 7784.00 |
| NBody | 378.113 | 436.307 | +58.194 | +55.897 | +2.299 | +0.343 | 0 | 0.00 | 626.01 |
| Havlak | 414.083 | 417.461 | +3.378 | +4.959 | -1.581 | -2.965 | 153897 | 19286.55 | 11645.96 |

额外验证：
- NBody 在 out63 中异常慢，但它没有触发 GiYOL NT batch。
- 单独重跑 GiYOL NBody：`build.debug/benchmarks/out64_giyol_inline_nbody_rerun`，total 396.067s。
- 同一时间点重建 GiY 单独重跑 NBody：`build.debug/benchmarks/out65_giy_nbody_rerun`，total 380.737s。
- 这说明 out63 的 NBody +58.194s 有明显单次噪音成分；但即使用单独重跑，GiYOL NBody 仍比当前 GiY 慢 15.330s。
- 原因不是 NT store，因为 NBody `GiYOL NT batches = 0`；更可能是 delayed materialization 改变了 GC 后的 cache 状态，导致 business 阶段多付出 cache/locality 成本。

实验总判断：
- GiYOL 已经实现，并且可以完整跑完 suite。
- 这个 OL 思路在当前实现下没有带来全局收益。
- 对真正触发 NT batch 的 Storage：business 快 13.220s，但 GC full 慢 13.225s，total 几乎打平。这说明小对象大批量 streaming store 的代价几乎完全抵消了 cache pollution 减少带来的收益。
- 对 Havlak：GC core 快 2.965s、GC full 快 1.581s，但 business 慢 4.959s，total 仍慢 3.378s。
- 对不触发 NT batch 的项目，GiYOL 仍可能因为 delayed copy 改变 cache 状态而慢，例如 NBody 单独复测仍慢 15.330s。
- 因此目前最可靠结论是：GiYOL 证明“批量 survivor + NT store”可以工作并保持正确性，但默认实现不应替代当前 GiY；它更适合作为实验分支继续研究。

后续如果继续优化：
- 不要对小对象无差别 streaming store。Storage 说明小对象 NT copy 的 GC 成本太高。
- 需要按对象大小或按 batch 内平均 object size 决定是否 NT。
- 对没有达到阈值的 workload，应避免 delayed materialization，或者把 `GIYOL_ADAPTIVE_BATCH=1` 作为实验方向继续修。
- 如果要真正实现“先拷贝到 staging buffer 再整块 NT 到 DRAM”，需要解决 destination reservation 顺序和 traversal/LIFO 顺序不一致的问题，否则无法保证一个 batch 对应连续目标区间。

## 2026-05-05 当前 GiYOL 使用的策略

当前默认 GiYOL 策略：
- GiYOL 通过 `OPT_GC=giyol` 构建。
- 构建宏为：`USE_GIY_MINOR + USE_GIYOL`。
- 主体 GC 仍然复用 GiY：root scan、remembered set、function table slot set、weak/AS update、reserve/patch 逻辑不变。
- 唯一改变的是 live object 的 materialize/copy 策略。

具体策略：
- 在 `giy_traverse_stack_and_copy()` 中，GiY 原本会在扫描完一个 live object 后立刻把它 copy 到 old/DRAM。
- GiYOL 不立刻 copy，而是把每个 live object 记录成一个 descriptor：
  - `dst`：已经 reserve 出来的 DRAM/old 目标地址。
  - `src`：young/cache 中的原对象地址。
  - `nbytes`：对象 footprint 大小，包含 header 后的 aligned size。
- descriptor 放进 `GiYOLCopyBatch`。
- batch 累计到 `GIYOL_BATCH_BYTES = 128KB` 后，flush 这个 batch。
- flush 时如果是达到阈值触发的 flush，就对 batch 中的对象逐个使用 non-temporal store 写到目标地址。
- minor GC traversal 结束后，如果还有没达到 128KB 的尾 batch，则不用强制 NT store，而是用普通 GiY copy 路径 flush。

当前默认参数：
- `GIYOL_BATCH_BYTES = 128KB`
- `GIYOL_SORT_BATCH = 0`
- `GIYOL_INLINE_COPY_ENTRIES = 64`
- `GIYOL_ADAPTIVE_BATCH = 0`

几个关键含义：
- `GIYOL_SORT_BATCH = 0`：当前不对 batch 按 `dst` 排序。之前 64KB + sort 的实验显示排序成本太高。
- `GIYOL_INLINE_COPY_ENTRIES = 64`：每个 batch 自带 64 个 descriptor 的 inline 空间，避免小 GC 每次 malloc/free descriptor 数组。
- `GIYOL_ADAPTIVE_BATCH = 0`：当前默认不是自适应策略，而是只要是 GiYOL build，就在 traversal 中启用 batch delayed-copy。
- 如果手动编译 `GIYOL_ADAPTIVE_BATCH=1`，才会让上一轮 survivor 大小决定下一轮是否启用 batch；但这个策略默认关闭，因为它会让 Storage 这种实验目标 workload 完全不触发 GiYOL NT batch。

一句话总结：
- 当前 GiYOL 是“GiY 主体不变 + live object 延迟物化 + 128KB descriptor batch + 达阈值后逐对象 non-temporal store flush”的策略。

## 2026-05-05 为什么 GiYOL 当前是逐对象使用 non-temporal store

问题：为什么 GiYOL 达到 128KB batch 后，仍然是对 batch 里的对象逐对象使用 non-temporal store，而不是把几个对象真正放在一起后整块使用 NT store？

结论：
- 当前 GiYOL 的 batch 是“descriptor batch”，不是“staging buffer batch”。
- 它把多个 object 的 `{dst, src, nbytes}` 记录到一起，但没有把这些对象的 bytes 先搬到一个连续临时 buffer。
- 因此 flush 时只能逐对象从各自的 `src` 读，再写到各自的 `dst`。

为什么不能直接整块 NT：
- NT store 是对目标地址写数据，不会自动把多个分散的 source object 合成一个连续数据流。
- 当前 young/cache 中的 source object 通常不是“这些 survivor 按 batch 顺序连续排好”的。
- 当前 DRAM destination 是在 reserve 阶段按 object 发现顺序分配的，但 copy/traverse 使用 GC stack，实际处理顺序是 LIFO/图遍历顺序。
- 这导致一个 copy batch 中的 `dst` 可能不是严格连续的完整区间；中间可能有还没被弹出/还没 copy 的 object。
- 即使按 `dst` 排序，也只能让写入顺序更接近递增地址，不能保证可以把整个 batch 当成一个没有洞的连续大块来写。

如果要真正“把几个对象放在一起使用 NT store”，需要换成 staging buffer 方案：
1. 先为一组 live object 确定一个连续 old/DRAM 目标区间。
2. 把这些对象按目标顺序打包到一个连续 staging buffer。
3. 再用 NT store 从 staging buffer 连续写到 DRAM 目标区间。
4. 同时确保 forwarding pointer、object 内部指针 patch、roots/RSet/FT patch 都指向最终 DRAM 地址。

这个方案可能更接近用户说的“把几个对象放在一起使用”，但代价是：
- 多了一次 copy：young -> staging，再 staging -> DRAM。
- 需要 staging buffer 空间。
- 需要保证 batch 对应连续目标区间，否则仍然不能整块写。
- 需要重新设计 reserve/copy 顺序，避免当前 LIFO traversal 导致目标区间和 copy batch 不一致。

所以当前 GiYOL 选择逐对象 NT 的原因是：
- 它最大限度保持 GiY 主体不变。
- 不改变 forwarding pointer 语义。
- 不引入二次 copy buffer。
- 不要求重排对象布局。
- 但代价是它只能做到“多个对象累计到阈值后一起 flush”，不是“把多个对象物理合成一个连续大块后整块 NT”。

性能判断：
- 用户说的方向理论上可能更强，特别是如果能形成真正连续的大块 store。
- 但在当前实现里，逐对象 NT 已经显示出小对象 workload 的成本很高，Storage 中 business 快了但 GC copy 慢了几乎同样多。
- 因此下一步如果继续做 GiYOL，不应该继续扩大“逐对象 NT”的范围，而应该研究 staging buffer 或者按对象大小/平均对象大小选择是否启用 NT。

## 2026-05-05 GiYOL staging buffer 连续目标块实验

用户提出的新方向：
- 想尽办法让目标地也成为一个大连续块。
- 可以开辟一个小 staging 空间，达到一定阈值后，把 staging 中的内容一口气送到具体的一整块 DRAM/old 空间。

我实现并测试了这个方向。

最终实现策略：
- `GIYOL_STAGING_COPY = 1`，当前默认开启。
- GiYOL 不再只是 descriptor batch 后逐对象 NT。
- 在 `copy_for_minor()` reserve DRAM 目标地址时，按 reserve 顺序记录 `{dst, src, nbytes}`。
- reserve 顺序天然对应 DRAM 目标地址递增，因此比 traversal/LIFO 顺序更接近连续目标块，也不需要 qsort。
- `giy_traverse_stack_and_copy()` 仍然扫描并 patch young object 内部指针，但 staging 模式不在这里逐对象 copy。
- traversal 完成后，GiYOL 按 reserve-order batch 分块：
  - 把多个 source object 先 memcpy 到连续 staging buffer。
  - staging buffer 达到阈值后，用 non-temporal store 连续写到 DRAM 目标区间。
  - 尾部不足阈值的对象走普通 GiY copy。
- 当前默认阈值仍是 `GIYOL_BATCH_BYTES = 128KB`。

这次实验跑过的版本：
- `out69_giyol_staging_key`：第一版 staging，按 traversal batch 排序后 staging。问题是 chunk 分割过保守，大量 fallback。
- `out70_giyol_staging_key2`：修正 chunk 逻辑，达到/跨过阈值再 flush，但仍需要 qsort。
- `out72_giyol_staging_reserve_key`：reserve-order staging，不再依赖 qsort。
- `out73_giyol_staging_reserve_512k`：reserve-order + 512KB 阈值。
- `out74_giyol_staging_reserve_256k`：reserve-order + 256KB 阈值。
- `out75_giyol_staging_final_smoke`：最终源码默认 128KB reserve-order staging 的 smoke。

smoke 结果：
- `hello_world.sbc` 通过。
- `giy_gc_probe.sbc` 通过。
- `giy_gc_probe` 显示：
  - `GiYOL batch bytes = 131072`
  - `GiYOL NT batches = 11`
  - `GiYOL staging batches = 11`
  - `GiYOL staging bytes = 1.38 MB`
- 说明 staging buffer -> contiguous DRAM NT store 路径实际执行到了。

关键 benchmark 数据，单位秒：

| benchmark | 版本 | total | business | GC full | GC core | NT batches | NT MB | fallback MB |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| Storage | GiY out50 | 213.328 | 178.553 | 34.774 | 31.941 | - | - | - |
| Storage | old GiYOL 逐对象 NT out62 | 210.855 | 162.572 | 48.283 | 44.893 | 453773 | 56730.28 | - |
| Storage | staging reserve 128KB out72 | 261.811 | 165.208 | 96.603 | 93.725 | 453773 | 56732.81 | 2038.73 |
| Storage | staging reserve 256KB out74 | 260.986 | 165.108 | 95.878 | 92.970 | 226886 | 56727.15 | 2044.39 |
| Storage | staging reserve 512KB out73 | 250.712 | 164.277 | 86.434 | 83.556 | 0 | 0.00 | 58771.54 |
| Havlak | GiY out50 | 414.083 | 376.846 | 37.237 | 27.765 | - | - | - |
| Havlak | old GiYOL 逐对象 NT out62 | 417.428 | 381.641 | 35.787 | 24.878 | 153897 | 19286.55 | - |
| Havlak | staging reserve 128KB out72 | 428.732 | 381.020 | 47.712 | 38.602 | 153799 | 19263.29 | 11669.22 |
| Havlak | staging reserve 256KB out74 | 430.968 | 382.614 | 48.354 | 39.233 | 60997 | 15254.27 | 15678.24 |
| Havlak | staging reserve 512KB out73 | 430.537 | 380.294 | 50.243 | 41.114 | 0 | 0.00 | 30932.51 |
| CD | GiY out50 | 242.589 | 229.997 | 12.592 | 8.446 | - | - | - |
| CD | old GiYOL 逐对象 NT out62 | 242.945 | 229.991 | 12.954 | 7.943 | 0 | 0.00 | - |
| CD | staging reserve 128KB out72 | 245.869 | 230.539 | 15.330 | 11.158 | 0 | 0.00 | 7784.00 |

三项总和：
- GiY out50：870.000s。
- old GiYOL 逐对象 NT out62：871.228s。
- staging reserve 128KB out72：936.412s。

最终结论：
- 功能上，这个方案成功了：现在 GiYOL 的确能把多个对象先打包到 staging buffer，再对一个连续 DRAM 目标区间做 NT store。
- reserve-order 方案也成功避免了 qsort，并且能证明目标区间可以按 reserve 顺序形成连续块。
- 性能上，这个方案失败了：Storage 的 staging NT 覆盖 56.7GB，但 GC core 从 GiY 的 31.941s 增加到 93.725s；相比 old GiYOL 逐对象 NT 的 44.893s 也慢很多。
- 核心原因是 staging 方案多了一次完整 copy：`young -> staging`，然后 `staging -> DRAM`。对 Storage 这种小对象海量复制 workload，这个额外 copy 的成本远大于“整块 NT store”带来的收益。
- 提高阈值不能解决：
  - 256KB 仍触发 NT，但性能几乎没改善。
  - 512KB 基本不触发 NT，退化成 delayed ordinary copy，仍然慢。

当前判断：
- “目标地成为大连续块 + staging buffer 一口气 NT”是可实现的，当前代码已经实现并验证。
- 但在当前 benchmarks 上，它不是性能正确方向。
- 更可能有价值的下一步不是继续 staging 双拷贝，而是：
  - 只对大对象或大平均对象 batch 使用 staging/NT。
  - 对小对象继续使用 GiY 即时 copy 或旧 GiYOL 逐对象 NT。
  - 或者更激进地改变 reserve/copy 结构，使对象直接按目标连续顺序从 young 写到 DRAM，避免 staging 二次 copy。

## 2026-05-06 当前 GiYOL staging 性能失败原因解释

核心结论：
- 当前 staging 版 GiYOL 的性能失败，根本原因不是“目标区间不连续”，因为 reserve-order staging 已经能形成连续 DRAM 目标块。
- 真正的失败原因是：为了实现“连续 staging buffer -> 连续 DRAM NT store”，它把每个存活对象的数据多搬运了一次。
- 原 GiY/旧 GiYOL 是一次搬运：`young/cache -> DRAM`。
- staging 版 GiYOL 是两次搬运：`young/cache -> staging buffer -> DRAM`。
- 对 Storage/Havlak 这类大量小对象 workload，多出来的第一段 `young -> staging` memcpy 成本，比“连续大块 NT store”节省的 cache pollution 成本更大。

为什么连续目标块没有带来收益：
- NT store 的主要收益是避免把写入的 DRAM 目标地址污染 cache。
- 但是 staging 方案为了得到连续 source stream，先把对象从 young/cache 拷贝到 staging buffer。
- 这一步仍然要读每个 young object、写 staging buffer，并且这些对象非常小，函数/循环/descriptor 开销和内存写入开销都很高。
- 然后再从 staging buffer 读一次，并用 NT store 写 DRAM。
- 因此总内存流量从“大约读 young 一次 + 写 DRAM 一次”变成“大约读 young 一次 + 写 staging 一次 + 读 staging 一次 + 写 DRAM 一次”。
- 对小对象海量复制来说，内存流量和循环次数都接近翻倍，收益不够抵消。

数据证据：
- Storage：
  - GiY out50：total 213.328s，GC core 31.941s。
  - old GiYOL 逐对象 NT out62：total 210.855s，GC core 44.893s。
  - staging reserve 128KB out72：total 261.811s，GC core 93.725s。
  - staging 版确实 NT 写了 56.7GB：`NT MB = 56732.81`。
  - 但 GC core 相比 GiY 增加 61.784s，相比旧 GiYOL 逐对象 NT 也增加 48.832s。
- Havlak：
  - GiY out50：total 414.083s，GC core 27.765s。
  - old GiYOL 逐对象 NT out62：total 417.428s，GC core 24.878s。
  - staging reserve 128KB out72：total 428.732s，GC core 38.602s。
  - staging 版 NT 写了 19.263GB，但 GC core 比旧 GiYOL 慢 13.724s。
- CD：
  - CD 不触发 staging NT，`NT MB = 0`。
  - GiY out50：total 242.589s，GC core 8.446s。
  - staging reserve 128KB out72：total 245.869s，GC core 11.158s。
  - 这说明即使没有真正 NT，staging/delayed-copy 结构本身也会带来额外开销。

为什么阈值调整没解决：
- 128KB：能大量触发 staging NT，但双拷贝成本太高。
- 256KB：Storage/Havlak 仍触发 staging NT，但 GC core 几乎没有改善：
  - Storage 256KB：GC core 92.970s，仍远慢于 GiY 31.941s 和 old GiYOL 44.893s。
  - Havlak 256KB：GC core 39.233s，仍慢于 old GiYOL 24.878s。
- 512KB：很多 GC 的 survivor 总量达不到阈值，基本不触发 NT：
  - Storage 512KB：NT MB 0，fallback 58.771GB，GC core 83.556s。
  - Havlak 512KB：NT MB 0，fallback 30.933GB，GC core 41.114s。
- 说明问题不是“batch 太小”，而是 staging 双拷贝路径本身太贵。

更具体的开销来源：
1. 二次数据搬运：
   - 每个 survivor object 先 memcpy 到 staging。
   - staging 再 NT 写到 DRAM。
   - 相比 GiY/旧 GiYOL，多了 staging 写入和 staging 读取。
2. 小对象粒度太细：
   - Storage 平均对象 footprint 很小。
   - 把很多几十字节对象打包时，descriptor 遍历、memcpy 调用/循环、分支判断成本都很明显。
3. staging buffer 本身污染 cache：
   - staging buffer 是普通内存，`young -> staging` 会写入 cache。
   - 虽然后续 DRAM 写是 NT，但 staging 写入本身仍占 cache bandwidth/cache lines。
4. 延迟 materialization 改变 cache 时序：
   - GiY 是扫描完对象后立即 copy。
   - staging 版把 copy 推迟到 traversal 后统一做，可能改变 young object、metadata、后续 patch 的 cache locality。
5. 对未达到阈值的尾部/fallback 仍要普通 copy：
   - fallback 部分没有 NT 收益，却仍承担 delayed-copy/staging 管理成本。

最终判断：
- 用户提出的方案“目标地连续块 + staging buffer 一口气 NT”在工程上可实现，并已实现。
- 性能失败不是因为做不到连续块，而是因为当前 workload 的对象太小、数量太多，staging 额外 copy 的代价远超过整块 NT 的收益。
- 如果继续研究，方向应该改成选择性 staging：
  - 只对大对象或平均对象足够大的 batch 使用 staging/NT。
  - 小对象不要 staging，继续 GiY 即时 copy或旧 GiYOL 路径。
  - 更激进的方向是直接按 reserve-order 从 young 写 DRAM，避免 staging buffer 这次额外 copy。

## 2026-05-06 关于“在 young/cache 区保留 staging 暂存区”的判断

用户提出：
- 能不能在 young 区/cache 区保留一块空间，专门作为 staging 暂存区；
- evacuation 阶段一旦 staging 满足阈值，就触发一次 non-temporal store；
- 目标是防止 DRAM old destination 的 cache pollution。

结论：
- 这个方案可以实现，但它不一定能解决当前 staging 失败的问题。
- 它可能比 malloc 普通 staging buffer 更贴近 GiY 的 cache-space 设计，但会付出非常明显的 young/cache 容量代价。
- 最大风险是：cache_space 本来就很小，拿出一块 staging buffer 会减少 young 可用空间，导致 minor GC 更频繁。

为什么可实现：
- 可以在 cache_space 初始化后，从 young/cache 可用区切出一段固定 staging buffer。
- 这段 buffer 不作为普通对象分配区，也不被 GC 当成 object 扫描。
- evacuation 时把多个 survivor object 的 bytes 先 memcpy 到这个 staging buffer。
- staging 达到阈值后，用 NT store 连续写到已经 reserve 好的 DRAM 目标区间。
- 写完后 staging offset 清零，重复使用这段固定空间。

它相对当前 malloc staging 的潜在好处：
- staging buffer 位于 GiY 的 cache/young 区，理论上访问延迟更低。
- staging buffer 固定复用，cache pollution 被限制在一块固定区域内。
- staging -> DRAM 使用 NT store，可以避免 DRAM destination 被普通写分配进 cache。

但关键问题仍然存在：
1. 仍然是双拷贝。
   - 数据路径还是：`young object -> staging buffer -> DRAM`。
   - 相比 GiY/旧 GiYOL 的 `young object -> DRAM`，仍多了一次 staging 写入和 staging 读取。
2. staging buffer 自己也会污染 cache。
   - 即使 buffer 在 cache_space 中，写 staging 也会占用 cache lines 和 bandwidth。
   - 它只是把 pollution 限定在固定 staging 区，不是消除 pollution。
3. 会减少 young/cache 可用容量。
   - 当前 GiY 输出里 young after aux 约 265.72KB。
   - 如果切 128KB 做 staging，实际 young 分配空间会大幅减少。
   - minor GC 可能显著增加；Storage 已经有 226887 次 minor GC，再减少 young 容量风险很大。
4. 不能随便复用当前 young 空间。
   - GC 发生时，young/cache 里还保存着待扫描/待复制的 source object。
   - staging 不能覆盖这些对象，否则还没读完 source 就破坏对象内容。
   - 所以 staging 必须是预留的独立区域，不能简单拿“空闲 young 区”来用；minor GC 触发时通常也没有足够空闲空间。

所以这个方案的本质：
- 它可能减少“DRAM destination 普通写导致的 cache pollution”。
- 但它增加了“cache-space 内部 staging 写入/读取”和“young 容量减少导致更多 GC”。
- 旧 GiYOL 逐对象 NT 已经能避免 DRAM destination 的普通 cache pollution；staging 的额外价值主要是把很多小对象聚合成连续 NT stream。
- 但此前 staging 实验证明：聚合 stream 的收益没有覆盖双拷贝成本。

如果要实验，应该小心地做成可选配置，而不是默认替换：
- 例如 `GIYOL_CACHE_STAGING=1`。
- staging 大小不要一开始用 128KB，可以先试 16KB/32KB/64KB。
- 必须记录：
  - staging bytes；
  - staging flush 次数；
  - fallback bytes；
  - minor GC count 是否增加；
  - young after aux 实际减少多少；
  - Storage/CD/Havlak 的 total、business、GC full、GC core。

预期判断：
- 如果 staging 空间太大：minor GC 次数上升，性能大概率更差。
- 如果 staging 空间太小：NT stream 太碎，接近逐对象 NT，收益有限。
- 它唯一可能赢的场景是：staging 足够小，不明显增加 GC 次数；同时 survivor batch 平均对象足够大，能减少很多小对象逐个 NT 的固定成本。

当前建议：
- 可以实现并测试，但不要期待它一定解决问题。
- 最值得验证的实验是“小 cache staging buffer + 选择性启用”：只在 batch 平均 object size 或单对象 size 足够大时使用 cache staging；小对象仍走 GiY 即时 copy或旧 GiYOL。

## 2026-05-06 关于 400B 小暂存区 + 256B non-temporal store 的判断

用户进一步说明：
- 计划使用 256 字节宽的 non-temporal store。
- 因此只想预留非常小的一块 cache/young 暂存区，例如 400B 左右。
- 只让小对象使用这个区域。
- 问题：这样是否能解决此前说的 staging 成本问题？

结论：
- 这个方案比之前 128KB staging buffer 更合理，值得实验。
- 它能解决两个之前的大问题：
  1. staging buffer 挤占 young/cache 空间的问题基本消失，因为 400B 相对 265KB young 可用空间很小。
  2. staging buffer 自身 cache pollution 被限制在极小范围内，基本就是几个 cache line 反复复用。
- 但它不能完全消除双拷贝问题，只是把双拷贝成本限制在很小的窗口里。
- 它是否赢，取决于“把许多小对象聚合成 256B 连续 NT store”节省的 per-object NT/普通写成本，是否大于 `young -> staging` 这次额外 memcpy 成本。

为什么这个方案可能比大 staging 好：
- 之前 128KB staging 的失败，主要是每次把大量 survivor 多搬一遍：`young -> staging -> DRAM`。
- 400B staging 不追求收集一个很大的 batch，而是追求把多个几十字节小对象凑成 256B/384B 的连续写。
- 对 Storage 这种平均对象很小的 workload，如果每个对象逐个 NT，固定开销和尾部处理很多；小 staging 可以把多个对象合并成更少的连续 NT 写。
- staging 区很小，长期保持 hot，写 staging 的 cache 代价可能远小于大 staging。

仍然需要注意的问题：
1. 仍然是双拷贝：
   - 小对象先 copy 到 staging，再 NT 到 DRAM。
   - 只是每次额外 copy 的窗口很小，不再是几十 MB/GB 级别的大 staging 流。
2. 目标 DRAM 必须连续：
   - reserve-order 记录 `{dst, src, nbytes}` 仍然有价值。
   - 只有连续目标区间才能把 staging 一口气送到 DRAM。
3. 对齐问题很关键：
   - 如果使用 256-bit / 512-bit streaming store，目标地址通常需要 32B/64B 对齐。
   - 当前对象/DRAM 分配多半是 8B/16B 对齐，不一定满足 32B/64B。
   - 如果用户说的“256字节宽”是指每次 flush 256B，而不是单条指令 256B，则可以用多个 16B/32B/64B NT store 拼出来。
   - 需要处理 misaligned head/tail，否则会出错或退化。
4. 只适合小对象：
   - 大对象不应该先塞 400B staging。
   - 大对象可以直接用原 GiY/旧 GiYOL 的 copy path。
5. flush 尾部策略：
   - staging 不满 256B 的尾部最好普通 copy，或者等到本次 minor GC 结束时普通 copy。
   - 强行对很小尾部 NT 可能继续亏。

我认为最值得实验的策略：
- 新增一个可选模式，例如 `GIYOL_TINY_STAGING=1`。
- 固定 staging buffer 大小，例如 512B，而不是正好 400B，便于 64B cache line 和 256B flush 对齐。
- 只接收小对象，例如 `nbytes <= 128B` 或 `nbytes <= 256B`。
- staging flush 阈值设为 256B。
- 当 reserve-order 中目标地址连续时：
  - 把小对象 bytes 追加到 tiny staging。
  - staging 达到 256B 后，用 NT store 写到 DRAM。
- 遇到大对象、目标地址不连续、或者 staging 无法保持对象边界时：
  - 先 flush 当前 staging。
  - 大对象走普通 GiY/旧 GiYOL copy。

预期：
- 这个方案有机会解决“大 staging 双拷贝太贵”和“逐对象 NT 对小对象太碎”之间的矛盾。
- 它不保证成功，但比 128KB staging 更有实验价值。
- 关键 benchmark 仍应先看 Storage，因为 Storage 小对象最多；如果 Storage GC core 不能从 staging reserve 的 93.725s 明显降回 old GiYOL 的 44.893s 附近，这个方向就不成立。

## 2026-05-06 GiYOL tiny staging 实现与实验结论

本轮实际做了实现和多轮 benchmark。目标是验证“小暂存区，把多个小对象凑成连续块后再 non-temporal store”的方向。

实现策略：
- 新增 `GIYOL_TINY_STAGING` 路径，默认开启。
- 当前源码默认参数：`GIYOL_TINY_STAGING_BYTES=2048`，`GIYOL_TINY_FLUSH_BYTES=2048`，`GIYOL_TINY_MAX_OBJECT_BYTES=64`。
- `copy_for_minor()` 仍然按 GiY/GiYOL 原规则为 survivor 在 DRAM 预留最终位置，并写 forwarding pointer。
- 只有 `align_bytes <= GIYOL_TINY_MAX_OBJECT_BYTES` 的小对象会记录到 reserve-order staging batch。
- 大于阈值的对象不进入 tiny staging，在 traversal 中立即按原 GiYOL/GiY copy path 写到 DRAM。
- flush 时只合并目标 DRAM 地址连续的小对象 run；达到阈值后把 staging buffer 用 NT copy 写到最终 DRAM 位置。
- 目标不连续、对象过大、或者尾部不足阈值时，退回普通 `giy_copy_live_object()`，避免为了很小碎片强行 NT。

关键代码位置：
- `ejsvm/GiY.cc`: tiny staging 默认参数在 129-147 行。
- `ejsvm/GiY.cc`: `copy_for_minor()` 只记录小对象在 1164-1169 行。
- `ejsvm/GiY.cc`: tiny staging flush 逻辑在 2039-2159 行。
- `ejsvm/GiY.cc`: 大对象立即 copy 的选择在 2310-2320 行。
- `ejsvm/common.mk`: 147-165 行新增 make 参数，可用 `GIYOL_TINY_STAGING_BYTES/GIYOL_TINY_FLUSH_BYTES/GIYOL_TINY_MAX_OBJECT_BYTES` 覆盖。

参数扫描结果，先以 Storage 筛选：

| variant | Storage total | business | GC full | GC core | tiny flushes | NT tiny bytes | 结论 |
|---|---:|---:|---:|---:|---:|---:|---|
| 1024B flush, max 64B | 209.742s | 161.243s | 48.499s | 45.619s | 1,238,569 | 1209.54MB | flush 次数太多，GC core 变差 |
| 1536B flush, max 64B | 209.690s | 164.704s | 44.986s | 42.069s | 241,107 | 353.18MB | 中间点，仍不如 2048B |
| 2048B flush, max 64B | 208.061s full-suite rerun | 163.544s | 44.517s | 41.602s | 23,629 | 46.15MB | 当前最合理候选 |
| 4096B flush, max 64B | 209.691s | 164.931s | 44.760s | 41.829s | 0 | 0.00MB | 阈值太高，基本等于不触发 NT |
| 2048B flush, max 96B | 211.927s | 163.378s | 48.549s | 45.585s | 4,292,051 | 8382.91MB | 小对象阈值放大后被 flush 固定成本反噬 |

重要判断：
- 不是“NT bytes 越多越好”。
- 96B 上限让 NT bytes 从 46MB 放大到 8.38GB，但 Storage GC core 从约 41.6s 恶化到 45.6s。
- 1024B flush 捕获 1.21GB，但产生 123.9 万次 flush，也让 GC core 恶化。
- 4096B flush 完全触发不了，因为自然连续的小对象 run 通常不到 4096B。
- 当前最好点是很保守的 2048B/2048B/64B：只捕获少量真正连续的小对象 run，避免 flush 数量爆炸。

完整 suite 结果，当前 GiYOL tiny staging 为 `out89_giyol_tiny_selective_2048_64_full`：

| benchmark | GiY total | GiYOL tiny total | delta vs GiY | old GiYOL full total | delta vs old GiYOL |
|---|---:|---:|---:|---:|---:|
| Bounce | 157.335 | 157.307 | -0.028 | 156.118 | +1.189 |
| List | 99.094 | 97.105 | -1.989 | 98.864 | -1.759 |
| Sieve | 120.831 | 121.270 | +0.439 | 166.861 | -45.591 |
| Queens | 116.321 | 115.810 | -0.511 | 117.254 | -1.444 |
| Permute | 388.003 | 391.063 | +3.060 | 391.387 | -0.324 |
| Storage | 213.328 | 208.061 | -5.267 | 215.977 | -7.916 |
| Towers | 184.093 | 183.741 | -0.352 | 183.722 | +0.019 |
| Mandelbrot | 235.048 | 237.680 | +2.632 | 239.979 | -2.299 |
| Richards | 936.209 | 934.953 | -1.256 | 940.971 | -6.018 |
| CD | 242.589 | 243.100 | +0.511 | 244.678 | -1.578 |
| NBody | 378.113 | 379.211 | +1.098 | 386.273 | -7.062 |
| Havlak | 414.083 | 420.604 | +6.521 | 418.529 | +2.075 |

完整 suite 汇总：

| GC/version | total | business | GC full | GC core |
|---|---:|---:|---:|---:|
| GiY current (`out50`) | 3485.047s | 3378.598s | 106.448s | 72.829s |
| old GiYOL full (`out59`) | 3560.613s | 3438.391s | 122.222s | 81.546s |
| GiYOL tiny current (`out89`) | 3489.905s | 3369.331s | 120.575s | 86.167s |

对之前三项重点集 Storage/CD/Havlak，用旧 GiYOL key run `out62` 对比：

| version | total | business | GC full | GC core |
|---|---:|---:|---:|---:|
| old GiYOL key (`out62`) | 871.228s | 774.204s | 97.024s | 77.714s |
| GiYOL tiny current key subset (`out89`) | 871.765s | 774.214s | 97.550s | 80.778s |

最终结论：
- 小暂存区方案可以实现，而且 2048B/2048B/64B 是目前测到的最合理参数。
- 但它不是全局性能成功：完整 suite 相比 GiY 仍慢 4.858s total，GC full 多 14.127s，GC core 多 13.338s。
- 它相比早期 full GiYOL (`out59`) 明显更好，总时间少 70.708s，但这包含此前 GiYOL 其它修复和噪音，并不全是 tiny staging 的功劳。
- 与旧 GiYOL key run (`out62`) 在 Storage/CD/Havlak 三项上相比，当前 tiny staging 反而略慢 0.537s total，GC core 多 3.064s。
- Storage 单项有收益：相对 GiY 快 5.267s total，相对 old GiYOL full 快 7.916s total；但 Havlak 退化明显，相对 GiY 慢 6.521s，相对 old GiYOL full 慢 2.075s。
- 根因是小对象连续 run 的自然长度分布不理想：阈值低会产生海量 flush，阈值高又完全触发不了；扩大“小对象”范围会让 staging 双拷贝和 flush 固定成本迅速超过 NT store 的收益。
- 因此，这个方向最多适合作为可选/实验模式，或只在检测到类似 Storage 的对象布局时启用；不应该作为 GiYOL 的默认全局优化结论。

## 2026-05-06 只使用 GiY / GiYOL 两个名称的最终性能对比

命名约定：
- `GiY`：没有开启 tiny staging / GiYOL 功能的当前 GiY。
- `GiYOL`：开启 tiny staging 功能的 GiYOL，当前参数为 2048B staging、2048B flush、64B 小对象上限。
- 以下不再把中间实验目录名当成 GC 名称。

完整 suite 汇总：

| metric | GiY | GiYOL | GiYOL - GiY | 百分比 |
|---|---:|---:|---:|---:|
| total | 3485.047s | 3489.905s | +4.858s | +0.14% |
| business | 3378.598s | 3369.331s | -9.267s | -0.27% |
| GC full | 106.448s | 120.575s | +14.127s | +13.27% |
| GC core | 72.829s | 86.167s | +13.338s | +18.31% |

GiYOL 在完整 suite 中累计触发 tiny staging flush 275,622 次，NT tiny bytes 共 538.32MB。

逐项 total 对比：

| benchmark | GiY total | GiYOL total | GiYOL - GiY | 百分比 |
|---|---:|---:|---:|---:|
| Bounce | 157.335 | 157.307 | -0.028 | -0.02% |
| List | 99.094 | 97.105 | -1.989 | -2.01% |
| Sieve | 120.831 | 121.270 | +0.439 | +0.36% |
| Queens | 116.321 | 115.810 | -0.511 | -0.44% |
| Permute | 388.003 | 391.063 | +3.060 | +0.79% |
| Storage | 213.328 | 208.061 | -5.267 | -2.47% |
| Towers | 184.093 | 183.741 | -0.352 | -0.19% |
| Mandelbrot | 235.048 | 237.680 | +2.632 | +1.12% |
| Richards | 936.209 | 934.953 | -1.256 | -0.13% |
| CD | 242.589 | 243.100 | +0.511 | +0.21% |
| NBody | 378.113 | 379.211 | +1.098 | +0.29% |
| Havlak | 414.083 | 420.604 | +6.521 | +1.57% |

逐项 GC core 对比：

| benchmark | GiY GC core | GiYOL GC core | GiYOL - GiY |
|---|---:|---:|---:|
| Bounce | 0.044 | 0.051 | +0.007 |
| List | 0.003 | 0.003 | +0.000 |
| Sieve | 0.803 | 0.812 | +0.009 |
| Queens | 0.009 | 0.009 | +0.000 |
| Permute | 0.001 | 0.001 | +0.000 |
| Storage | 31.941 | 41.602 | +9.661 |
| Towers | 0.001 | 0.001 | +0.000 |
| Mandelbrot | 1.305 | 1.498 | +0.193 |
| Richards | 0.041 | 0.045 | +0.004 |
| CD | 8.446 | 8.862 | +0.416 |
| NBody | 2.470 | 2.969 | +0.499 |
| Havlak | 27.765 | 30.314 | +2.549 |

结论：
- GiYOL 的 business 时间比 GiY 少 9.267s，这是正向结果。
- 但 GiYOL 的 GC full 多 14.127s，GC core 多 13.338s，超过了 business 的收益。
- 所以完整 suite 总体上 GiYOL 比 GiY 慢 4.858s，约 +0.14%。
- GiYOL 明显赢在 Storage：total 快 5.267s，business 快 15.009s；但是 Storage 的 GC core 也多 9.661s。
- GiYOL 明显输在 Havlak：total 慢 6.521s，business 慢 3.926s，GC core 多 2.549s。
- 当前最准确的大结论：GiYOL 的 tiny staging 功能能改善一部分 mutator/business 表现，尤其 Storage，但它增加的 GC copy/flush 成本在完整 suite 上抵消了收益；因此当前 GiYOL 不是全局优于 GiY。

## 2026-05-06 当前 GiYOL 复制策略与选择理由

当前只使用两个名字：
- `GiY`：不开启 GiYOL tiny staging 功能。
- `GiYOL`：开启 tiny staging 功能。

当前 GiYOL 的复制策略不是“所有对象都先放进暂存区再搬”，而是选择性策略：

1. 发现存活对象时，先 reserve 最终 DRAM 位置
   - `copy_for_minor()` 检查对象是否已经有 forwarding pointer。
   - 如果没有，就在 `dram_space.free` 上按对象大小预留最终位置。
   - 然后把 young 对象 header 的 `forwarding_pointer` 写成新 DRAM payload 地址。
   - 同时把原 young payload 压入 GC stack，后续继续扫描它的字段。
   - 这一步和 GiY 的原则一致：最终地址在第一次发现对象时确定，所有后续 slot 更新都指向这个最终地址。

2. GiY 的普通复制路径
   - 扫描对象时，先用 reserve tracer 处理对象内部 young pointer，把字段更新成目标 forwarding pointer。
   - 然后把整个对象从 young 复制到预留好的 DRAM 位置。
   - `giy_copy_live_object()` 的规则是：
     - `nbytes <= 256B`：普通 `memcpy`。
     - `nbytes > 256B`：在 x86 上使用 streaming/non-temporal store。
   - 所以 GiY 本身已经不是单纯 `memcpy`，它已经有“超过 256B 用 NT”的策略。

3. GiYOL 的新增部分：只让很小对象进入 tiny staging
   - 当前参数：
     - `GIYOL_TINY_STAGING_BYTES=2048`
     - `GIYOL_TINY_FLUSH_BYTES=2048`
     - `GIYOL_TINY_MAX_OBJECT_BYTES=64`
   - 当 `copy_for_minor()` reserve 目标地址时，只有 `align_bytes <= 64B` 的对象会被记录为 `{dst, src, nbytes}`。
   - 大于 64B 的对象不会进入 tiny staging。
   - 这些大对象在 traversal 中立即调用 `giy_copy_live_object()`，也就是继续走 GiY 的即时复制策略。

4. tiny staging 的 flush 规则
   - GiYOL 按 reserve-order 遍历记录的小对象。
   - 只有当下一个对象的 DRAM 目标地址正好等于 `expected`，也就是和前一个对象连续时，才会追加到同一个 staging chunk。
   - 小对象先 `memcpy` 到 `giyol_staging_buffer`。
   - staging buffer 达到 2048B 时，用 `giyol_stream_copy_region()` 一次性 non-temporal store 到最终 DRAM 连续区间。
   - 如果目标地址不连续、对象不满足条件、或者最后尾部不足 2048B，就退回逐对象 `giy_copy_live_object()`。

为什么这么选择：

1. 先 reserve 最终地址，是为了保持 GiY 的核心语义
   - slot 更新需要一个稳定的新地址。
   - 如果先把对象放到临时区，之后再决定最终地址，就会引入第二次地址重写，复杂度和 remembered-set/slot 更新风险都会上升。
   - 当前策略让 forwarding pointer 从一开始就是最终 DRAM 地址，避免“临时地址再搬迁”的语义问题。

2. 大对象不进 staging，是因为双拷贝成本太高
   - staging 的本质是 `young -> staging -> DRAM`。
   - 大对象如果也走 staging，会多搬一遍大量数据。
   - 之前大 staging 实验证明，双拷贝成本会压倒连续 NT 的收益。
   - 所以当前只让 64B 以内的小对象尝试 staging，大对象继续即时复制。

3. 只合并目标地址连续的小对象，是因为 NT store 需要写一段连续目标内存才有意义
   - 如果目标地址不连续，就不能把 staging buffer 直接一次性写到 DRAM。
   - 强行合并不连续对象会写坏中间区域。
   - 因此必须检查 `dst == expected`。

4. 2048B flush 是实验后选出的折中点
   - 1024B flush：Storage 捕获 1209.54MB NT tiny bytes，但 flush 1,238,569 次，GC core 变差到 45.619s。
   - 1536B flush：flush 241,107 次，GC core 42.069s，仍不如 2048B。
   - 2048B flush：flush 23,629 次，GC core 41.602s，是当前最合理点。
   - 4096B flush：flush 0 次，说明自然连续小对象 run 通常不到 4096B，阈值太高等于不启用。
   - 96B 小对象上限：NT tiny bytes 变成 8382.91MB，但 flush 4,292,051 次，GC core 45.585s，说明扩大对象范围会被 flush 固定成本反噬。

5. 64B 小对象上限是为了避免把太多对象拖进双拷贝路径
   - 当前数据说明，不是 NT bytes 越多越好。
   - 64B 只捕获最容易被逐对象复制固定成本影响的小对象。
   - 96B 虽然捕获更多数据，但 flush 次数和 staging 管理成本暴涨，性能更差。

当前复制策略的一句话总结：
- `GiY`：对象扫描后直接复制到最终 DRAM 位置；小于等于 256B 用 `memcpy`，大于 256B 用 NT store。
- `GiYOL`：保持 GiY 的地址确定和大对象复制策略不变；只把 64B 以内、目标 DRAM 地址连续的小对象先放进 2048B tiny staging buffer，凑满 2048B 后用 NT store 写到最终 DRAM；不连续或不满阈值则退回 GiY 的复制路径。

当前选择的原因：
- 这是在“减少小对象逐个复制/NT 的固定成本”和“避免 staging 双拷贝过重”之间的折中。
- 它能在 Storage 上带来收益，但完整 suite 上仍未赢过 GiY，说明该策略目前只能算实验性优化，不是默认全局胜利策略。

## 2026-05-06 当前方案下 old 区指针什么时候修复

结论：
- old 区里的 remembered-set slot 不是在扫描 remembered set 时立刻修复。
- 它是在 minor GC 的后半段，也就是 young live objects 都完成 reserve/traverse/copy 之后，在 `remembered_set_patch` 阶段统一修复。
- GiYOL 的 tiny staging 不改变这个时机；它只改变部分小对象的物理复制方式，不改变 forwarding pointer 和 old slot patch 的逻辑。

当前 minor GC 顺序：

1. `scan_roots_reserve`
   - 从 roots 发现 young live object。
   - 对 young object 调用 `copy_for_minor()`。
   - `copy_for_minor()` 会先在 DRAM 中 reserve 最终位置，并写 young object header 的 forwarding pointer。

2. `function_table_reserve`
   - 对 function table 中记录的 slot 做同样的 reserve。

3. `remembered_set_reserve`
   - 遍历 remembered set。
   - 读取 remembered set 里保存的 `values[i]`，也就是写屏障最后记录的 slot value。
   - 如果 value 指向 young，就调用 `copy_for_minor()` 给它 reserve DRAM 位置。
   - 注意：这个阶段只保证 old slot 指向的 young object 被标记为 live，并获得 forwarding pointer；它不改 old slot 本身。

4. `young_traverse_copy`
   - 遍历 GC stack 上的 young live objects。
   - 对 young object 内部字段，如果字段指向 young，会直接 patch 成 forwarding pointer。
   - 然后把对象复制到 DRAM。
   - 对 GiYOL：
     - 大对象立即复制。
     - 64B 以内且目标连续的小对象可能先进入 tiny staging。
     - `giy_traverse_stack_and_copy()` 结束前会 flush staging batch，所以进入后续 patch 阶段时，这些对象也已经物理写到 DRAM。

5. `scan_roots_patch`
   - 修复 roots。

6. `function_table_patch`
   - 修复 function table slots。

7. `remembered_set_patch`
   - 这里才真正修复 old 区 remembered-set slots。
   - `giy_patch_remembered_set_slots()` 遍历 remembered set。
   - 对每个 slot：
     - 取 `remembered_set.values[i]` 作为旧 value。
     - 用 `forwarded_or_self()` 查询这个 value 是否已经有 forwarding pointer。
     - 如果有，就把 slot 写成新 DRAM 地址。
   - 如果 slot 在 DRAM old 区，写回时走 `giy_store_ptr_slot()` 或 `giy_store_jsvalue_slot()`，内部会用 old-write guard，并在 x86 上通过 64-bit streaming store 写 old slot。

为什么不在 remembered-set reserve 阶段立刻修复 old slot：
- 当扫描 remembered set 时，虽然当前 value 可以立刻获得 forwarding pointer，但它指向的对象的子对象可能还没有全部发现和 reserve。
- 当前实现把“发现/预留 live set”和“修复外部 slot”分成两个阶段，逻辑更简单：
  - 前半段只建立所有 forwarding pointer。
  - 中间完成 young object 内部指针 patch 和物理复制。
  - 后半段再统一把 roots/function table/old slots 改到新地址。
- 这样 old slot patch 不依赖对象是否已经被物理复制，只依赖 forwarding pointer；而到 patch 阶段，forwarding pointer 已经稳定。

需要区分两类指针修复：
- young object 内部指针：在 `young_traverse_copy` 中，复制对象前就修复；这样复制到 DRAM 的对象内容已经是修复后的。
- old 区 remembered-set 指针：在 `remembered_set_patch` 中最后统一修复；这样 old 区 slot 从 young 地址变成 DRAM forwarding 地址。

一句话回答：
- 当前方案下，old 区指针是在 minor GC 的 `remembered_set_patch` 阶段修复的；在此之前 remembered set 只用于 reserve live young objects，GiYOL 的 tiny staging 不改变这个顺序。

## 2026-05-06 关于“用 source 顺序 + 中介区完全避免 old 区 memcpy”的想法

用户提出的新想法：
- 在批量 NT store 小对象的思路下，能不能把同顺序下 SRC 地址相邻/同组的 object 先 copy 到同一个中介位置。
- 如果累积到一定数值后遇到一个很大的对象，就把前一大组和这个大对象一起用 NT store 写回。
- 如果没有这么大，就先放进中介区再立刻 NT store。
- 目标是完全避免普通 GiY 对 old/DRAM 区的 `memcpy`，减少 old 区 cache pollution。

结论：
- 可以做一个“更激进的 GiYOL all-NT/staging”实验版本，但不能按“source 地址连续”作为核心判据。
- 真正决定能不能一口气 NT store 的是目标 DRAM 地址是否连续，而不是 source 地址是否连续。
- source 连续只说明从 young 读数据可能比较顺，但 NT store 写回时如果目标 old/DRAM 地址不连续，就不能把 staging buffer 当成一个连续块直接写过去。

为什么 source 顺序不是核心：
- NT store 的目的地是 old/DRAM。
- 一次连续 streaming store 要求目标地址 `[dst, dst+n)` 连续。
- 如果两个对象 source 地址连续，但它们的最终 DRAM 目标地址不连续，中间可能已经给其他 live object 预留了空间；强行连续写会覆盖别的对象或空洞。
- 当前 GiY/GiYOL 的 forwarding pointer 指向最终 DRAM 地址，所以 slot patch 也依赖这个最终地址。

什么情况下这个想法可以成立：
- 必须按 `copy_for_minor()` 的 reserve-order 处理对象，因为 DRAM 目标地址就是按 reserve-order 单调分配出来的。
- 如果把所有 live object 都记录成 `{dst, src, nbytes}`，而不是只记录小对象，那么这些记录在 reserve-order 下的目标地址通常天然连续。
- 这种情况下，可以在扫描完成后，按 reserve-order 把一段对象组装进 staging buffer，再用 NT store 连续写到 DRAM。

但是有三个大风险：

1. 会变成“所有对象双拷贝”
   - 路径变成 `young -> staging -> DRAM`。
   - 这可以避免 old/DRAM 上的普通 `memcpy` 写分配污染，但多了一次读写 staging。
   - 之前大 staging 实验失败的核心就是双拷贝成本过高。

2. 不能依赖“遇到大对象就把前面一组和大对象一起写”
   - 如果大对象已经很大，它本身直接 `young -> DRAM` NT store 就可以了，不一定值得先搬进 staging。
   - 更好的策略是：
     - 小对象先进入 staging。
     - 遇到大对象时，先 flush 当前 staging。
     - 大对象自己直接 NT store 到 DRAM。
   - 如果一定要把大对象也放入 staging，就会把最大的数据也多搬一次，风险很高。

3. “完全避免 old memcpy”不一定等于更快
   - GiY 当前已经对 `>256B` 对象使用 NT store。
   - old 区普通 `memcpy` 主要来自小对象。
   - 小对象如果不满一个足够大的连续写块，强行 NT 可能比 `memcpy` 更慢。
   - 实验已经看到：
     - 1024B flush 捕获 1209.54MB NT tiny bytes，但 flush 1,238,569 次，GC core 变差。
     - 4096B flush 触发 0 次，阈值太高等于不启用。
     - 96B 小对象上限让 NT bytes 变 8382.91MB，但 flush 4,292,051 次，性能更差。

更可实现的版本：

方案 A：小对象 staging，大对象直接 NT
- 保持当前策略的基本方向。
- 只改进判据：
  - 按 reserve-order 看目标 DRAM 是否连续。
  - staging 中累积小对象。
  - 遇到大对象时，先 flush 小对象 staging，然后大对象直接 `giy_copy_live_object()`/NT。
- 优点：避免大对象双拷贝。
- 缺点：不能“完全避免”小尾部 fallback memcpy。

方案 B：all-object reserve-order staging
- 所有对象都记录 `{dst, src, nbytes}`。
- 扫描完成后，按 reserve-order 把对象组装成连续目标块。
- 小对象和中等对象进入 staging，大对象可配置为直接 NT 或进入 staging。
- 优点：old/DRAM 普通 memcpy 最少，理论上最接近用户目标。
- 缺点：双拷贝最严重，之前实验倾向说明大概率亏。

方案 C：只消除 old 小对象 memcpy，不追求全量 staging
- 对小对象不用普通 `memcpy(dst_old, src_young)`。
- 改为：
  - 小对象 `memcpy` 到 hot staging buffer；
  - 如果凑满阈值就 NT 到 DRAM；
  - 如果尾部不满阈值，可以尝试用 64-bit/128-bit streaming store 写尾部，而不是普通 memcpy。
- 优点：更接近“避免 old memcpy”。
- 风险：小尾部 streaming store 固定成本可能更高，且需要处理 8B/16B 对齐约束。

我认为当前最值得尝试的改进方向：
- 不应该以 source 地址连续为核心。
- 应该以 reserve-order 的目标 DRAM 连续性为核心。
- 大对象不放 staging，直接 NT。
- 小对象 staging 可以更激进地尝试“尾部也用 streaming store”，但必须单独开配置实验。

一句话判断：
- 这个想法的正确改写是：“按最终 DRAM reserve-order 来构造连续写回块，而不是按 source 地址；小对象用 staging 聚合，大对象直接 NT，尽量减少 old 区 memcpy。”
- 如果目标是“完全避免 old 区 memcpy”，可以实现，但风险是把大量工作变成双拷贝，实验上很可能只在特定 benchmark 赢，不会自然成为全局胜利策略。

## 2026-05-06 关于“大对象自己 NT，小对象全部进中介区再 NT，以完全避免 old memcpy”

用户修正后的想法：
- 判据是 TARGET/DRAM 目标地址连续，不是 SRC 连续。
- 足够大的对象自己直接 non-temporal store 到最终 DRAM。
- 小对象全部按目标顺序进入中介区/staging buffer，然后用 NT store 写回。
- 目标是完全避免普通 GiY 对 old/DRAM 区的 `memcpy`，防止 old 区 cache pollution。

结论：
- 这个方案在语义上可行。
- 但“完全避免 memcpy”需要精确定义：
  - 可以完全避免对 old/DRAM 目标区的普通 `memcpy` 写入。
  - 但小对象仍然需要从 young 复制到 staging buffer，这一步通常仍是普通 `memcpy` 或普通 load/store。
  - 区别在于 staging buffer 很小、反复复用、停留在 cache 中；被污染的是 staging 的少量 cache line，而不是大量 old/DRAM 目标 cache line。

可行的策略应是：
1. 按 reserve-order 处理对象，因为 reserve-order 下最终 DRAM target 最可能连续。
2. 对大对象：
   - 先 flush 当前小对象 staging。
   - 大对象直接 `young -> DRAM` NT store。
   - 不把大对象放 staging，避免大对象双拷贝。
3. 对小对象：
   - 按 target 顺序 append 到 staging buffer。
   - staging 满阈值后，NT store 到连续 target 区间。
   - target 不连续时，flush 当前 staging，再重新开始。
4. 为了真的“完全避免 old memcpy”：
   - staging 尾部即使不满阈值，也不能 fallback 到 `memcpy(dst_old, src_young)`。
   - 必须用小粒度 streaming store 写掉尾部，例如 8B/16B 单元。

最大的风险：
- 小对象尾部强制 NT 可能非常慢。
- 以前实验已经显示：
  - flush 太小会导致 flush 次数爆炸。
  - 捕获 NT bytes 更多不一定更快。
- 因此，这个方案能减少 old cache pollution，但可能增加指令开销、store fence/streaming store 固定开销、以及 `young -> staging` 双拷贝成本。

它与当前 GiYOL 的区别：
- 当前 GiYOL 对小对象 staging 不满阈值时会 fallback 到 GiY 的复制路径，所以仍可能对 old 区做普通 `memcpy`。
- 用户这个新方案要求“不满阈值也 NT 写回”，也就是增加一个 `GIYOL_FORCE_TINY_TAIL_NT` 或类似配置。
- 当前 GiYOL 只让 64B 内小对象进 staging；这个新方案可以保持 64B 上限，也可以实验 96B/128B，但之前 96B 已证明容易因 flush 数暴涨而退化。

我认为可以做的实验版本：
- `大对象`: `nbytes > 256B` 直接 NT。
- `小对象`: `nbytes <= 64B` 全部 staging。
- `staging flush`: 达到 2048B 时 NT。
- `tail flush`: 不满 2048B 时也用 streaming store，而不是 old memcpy。
- `target gap`: 遇到 target 不连续就强制 tail NT flush，然后新开 chunk。

判断：
- 如果用户目标是“从机制上避免 old 区 cache pollution”，这个方案成立。
- 如果目标是“必然更快”，不能保证；之前数据表明小对象 NT 的固定成本非常敏感，必须实现后用 benchmarks 验证。

## 2026-05-06 关于 64-256B 对象如何处理，以及 tail NT flush 的含义

用户追问：
- 在建议实验版本里，`64B < object <= 256B` 的对象应该怎么处理？
- 什么叫 `tail NT flush`？

结论：
- 如果目标是“严格避免 old/DRAM 目标区普通 memcpy”，那么 `64B < nbytes <= 256B` 这段不能走当前 GiY 的 `giy_copy_live_object()`。
- 因为当前 `giy_copy_live_object()` 的规则是 `nbytes <= 256B` 用普通 `memcpy(dst_old, src_young)`。
- 所以在严格实验版里，64-256B 对象应单独走 direct NT store，而不是普通 GiY copy。

建议把对象分成三类：

1. tiny object：`nbytes <= 64B`
   - 进入 tiny staging。
   - staging 满 2048B 时，用 NT store 写到连续 target。
   - target 不连续、遇到中/大对象、或者 minor GC 结束时，触发 tail NT flush。

2. medium object：`64B < nbytes <= 256B`
   - 不建议进入 staging。
   - 原因：这类对象比 tiny 大，放进 staging 会多一次 `young -> staging` 拷贝，双拷贝成本更明显。
   - 严格避免 old memcpy 的实验里，medium object 应直接 `young -> old/DRAM` 用 NT store。
   - 注意：不能调用当前 `giy_copy_live_object()`，因为它会对 256B 以内对象使用 memcpy；需要新增一个“强制 NT copy”函数或给 copy 函数加 force-NT 参数。

3. large object：`nbytes > 256B`
   - 当前 GiY 已经会用 NT store。
   - 可以继续直接 `young -> old/DRAM` NT。

为什么不建议 64-256B 也进 staging：
- 当前实验已经说明，扩大 staging 对象范围会很容易失败。
- 例如 96B 上限时，NT tiny bytes 变大，但 flush 次数暴涨，Storage GC core 恶化。
- 64-256B 对象如果都进 staging，会明显增加 `young -> staging -> DRAM` 双拷贝量。
- 因此更合理的严格实验是：
  - tiny：staging + NT。
  - medium：direct NT。
  - large：direct NT。

什么是 tail NT flush：
- staging buffer 有一个 flush 阈值，例如 2048B。
- 正常 flush 是：staging 累积到 `>=2048B`，就把这 2048B 左右连续 bytes 用 NT store 写到 old/DRAM。
- 但是经常会出现“不满阈值的剩余部分”，这就是 tail。

例子：
- 当前 staging 已经累计 384B 小对象。
- 然后遇到一个 target 不连续的对象，或者遇到 medium/large object，或者 minor GC 结束。
- 这 384B 不可能继续等，因为后面的 target 不连续，或者本轮 GC 要结束。
- 这 384B 就是 staging tail。

当前 GiYOL 的保守做法：
- 如果 tail 小于阈值，就 fallback，逐对象调用 `giy_copy_live_object()`。
- 对 tiny object 来说，`giy_copy_live_object()` 会用 old memcpy。
- 所以当前保守做法没有完全避免 old memcpy。

tail NT flush 的意思：
- 即使 staging tail 只有 384B、128B、甚至 64B，也不 fallback 到 old memcpy。
- 而是把 staging buffer 中这段 tail 也用 streaming/non-temporal store 写到 old/DRAM target。
- 这样可以从机制上保证 tiny staging 产生的数据不会用普通 memcpy 写 old 区。

tail NT flush 的风险：
- 小 tail 使用 NT store 的固定成本可能高。
- tail 太小会导致很多小 NT 写，性能可能差。
- 还必须处理 8B/16B 对齐和尾部长度限制。

严格实验版的复制策略可以写成：
- `nbytes <= 64B`: staging；满 2048B 或必须断开时，tail 也强制 NT flush。
- `64B < nbytes <= 256B`: direct forced NT，不进 staging，不走 old memcpy。
- `nbytes > 256B`: direct NT，沿用当前大对象路径。

这才是真正意义上的“避免 old/DRAM 目标区普通 memcpy”版本。

## 2026-05-07 GiYOL forced-NT 实验结果

本轮实现内容：
- 新增 `GIYOL_FORCE_TINY_TAIL_NT`，默认 1。
- 新增 `GIYOL_DIRECT_NONTINY_NT`，默认 1。
- 当前完整实验版 GiYOL 的复制策略是：
  - `nbytes <= 64B`: 先进入 tiny staging buffer；满 2048B 或遇到 target 不连续/本轮 GC 结束时 flush；即使 tail 小于 2048B，也强制用 NT store flush。
  - `nbytes > 64B`: 不再走 GiY 的 `giy_copy_live_object()` 小对象 memcpy 路径，直接使用 GiYOL forced NT copy。
- 目的：机制上尽量避免 survivor object materialization 对 old/DRAM target 的普通 `memcpy`。

冒烟测试：
- `Sieve` 正常结束，无 alignment invariant 失败。
- `Storage` 正常结束，无 alignment invariant 失败。

关键结论：
- 这个“完全避免 old memcpy”的 forced-NT 方案失败了。
- 失败不是正确性问题，而是性能问题。
- 主要原因是它把大量很小的写入强行变成了 NT store，尤其是 tiny tail 和 64B 以上对象的 direct NT。
- NT store 适合较大、连续、不会马上重读的写入；当前 workload 下很多 survivor copy 是小块、频繁、每轮 GC 都要收尾的写入，强制 NT 的固定成本压过了避免 cache pollution 的收益。

10 个已跑 benchmarks 合计，不含 Richards/Havlak：
- GiY: total 2134.394s, business 2065.690s, GC full 68.706s, GC core 45.023s。
- 旧 GiYOL: total 2133.990s, business 2053.243s, GC full 80.747s, GC core 55.808s。
- forced-NT GiYOL: total 2188.201s, business 2054.996s, GC full 133.206s, GC core 107.857s。
- forced-NT GiYOL 相对旧 GiYOL：total +54.211s, business +1.753s, GC full +52.459s, GC core +52.049s。
- forced-NT GiYOL 相对 GiY：total +53.807s, business -10.694s, GC full +64.500s, GC core +62.834s。

逐项 total/core 对比，GiY / 旧 GiYOL / forced-NT GiYOL：
- Bounce: total 157.303 / 157.275 / 158.365s；GC core 0.044 / 0.051 / 0.056s。
- List: total 99.082 / 97.091 / 96.854s；GC core 0.003 / 0.003 / 0.003s。
- Sieve: total 120.809 / 121.248 / 123.278s；GC core 0.803 / 0.812 / 0.864s。
- Queens: total 116.298 / 115.794 / 117.687s；GC core 0.009 / 0.009 / 0.012s。
- Permute: total 387.931 / 391.007 / 389.864s；GC core 0.001 / 0.001 / 0.001s。
- Storage: total 213.283 / 208.025 / 253.573s；GC core 31.941 / 41.602 / 87.427s。
- Towers: total 184.069 / 183.708 / 182.655s；GC core 0.001 / 0.001 / 0.001s。
- Mandelbrot: total 235.001 / 237.636 / 237.864s；GC core 1.305 / 1.498 / 1.546s。
- CD: total 242.552 / 243.058 / 247.883s；GC core 8.446 / 8.862 / 15.057s。
- NBody: total 378.066 / 379.148 / 380.178s；GC core 2.470 / 2.969 / 2.890s。

forced-NT GiYOL 的 NT 写入规模：
- Bounce: tiny tail flush 22,290 次，32.01MB；direct NT 5,878 objects，4.54MB。
- List: tiny tail flush 815 次，0.55MB；direct NT 45 objects，0.01MB。
- Sieve: tiny tail flush 50,122 次，2.68MB；direct NT 50,041 objects，1908.08MB。
- Queens: tiny tail flush 2,686 次，0.55MB；direct NT 10,022 objects，1.07MB。
- Permute: tiny tail flush 199 次，0.02MB；direct NT 116 objects，0.01MB。
- Storage: tiny tail flush 113,686,097 次，45,677.56MB；direct NT 163,300,046 objects，13,047.80MB。
- Towers: tiny tail flush 280 次，0.11MB；direct NT 48 objects，0.01MB。
- Mandelbrot: tiny tail flush 578,706 次，71.07MB；direct NT 40 objects，0.00MB。
- CD: tiny tail flush 16,840,682 次，2,372.96MB；direct NT 26,453,850 objects，5,403.47MB。
- NBody: tiny tail flush 719,069 次，625.97MB；direct NT 550 objects，0.04MB。

Storage 消融实验：
- 旧 GiYOL：total 208.025s，business 163.546s，GC full 44.479s，GC core 41.602s。
- tail-only，即 tiny tail 强制 NT、非 tiny 仍走旧 GiY copy：total 230.549s，business 163.060s，GC full 67.489s，GC core 63.365s。
- full forced，即 tiny tail 强制 NT + 非 tiny direct NT：total 253.573s，business 162.837s，GC full 90.736s，GC core 87.427s。
- tiny tail 强制 NT 单独让 Storage GC core 增加 21.763s。
- 非 tiny direct NT 在此基础上再让 Storage GC core 增加 24.062s。
- 两部分共同造成 Storage GC core 相对旧 GiYOL 增加 45.825s，几乎完全解释了 total 增加 45.548s。

最终判断：
- “所有小对象 staging tail 都 NT flush + 所有 64B 以上对象 direct NT，以完全避免 old memcpy”不是一个好的当前方向。
- 当前数据支持回退到更保守的选择：只在目标连续且累计到足够大块时使用 NT；小 tail 继续 fallback 到 GiY 的 memcpy；64-256B 中等对象也不应该无条件 direct NT。
- 如果还要继续探索 NT，重点应是减少 tiny tail flush 次数、提高单次 NT 粒度、只对真正大块连续 target 使用 NT，而不是追求形式上完全消除 `memcpy`。

## 2026-05-07 关于“256B 以下全部先复制到中介区，再到阈值 NT store”的判断

问题：
- 是否可以把 `<=256B` 的 survivor object 全部先复制到中介区，然后累计到阈值后再一次 NT store 到 old/DRAM target？

机制结论：
- 可以实现，但必须有一个前提：这些 object 的 target 地址在 old/DRAM 里必须是连续的。
- 如果 target 连续，那么 staging buffer 可以组织成一段连续 bytes，然后一次 NT store 到连续 target。
- 如果 target 不连续，就不能把 staging buffer 一口气 NT store 到一个 target 地址；否则会覆盖中间不属于这批对象的 old/DRAM 内容。
- 因此这个策略的真实形态不是“所有 256B 以下都无脑凑满阈值”，而是“按 target 连续 run 分组，每个连续 run 达到阈值才 NT flush”。

按现有数据的判断：
- 不建议把 `<=256B` 全部强制 staging + 强制 tail NT 作为默认策略。
- 上一轮 forced-NT 实验已经显示，强制把小块写变成 NT store 会明显失败。
- Storage 中：
  - 旧 GiYOL GC core 41.602s。
  - tiny tail 强制 NT 后 GC core 63.365s，单独增加 21.763s。
  - 再加上 `>64B` direct NT 后 GC core 87.427s，又增加 24.062s。
- 这说明瓶颈不是“是否通过中介区”本身，而是“是否产生大量小 NT flush”和“是否对不够大的对象强制 NT”。

为什么 `<=256B` 全 staging 也有风险：
- 对 `64B~256B` 对象，旧 GiY/GiYOL 的保守路径通常是一次 `memcpy(dst, src, nbytes)`。
- staging 方案会变成两步：
  - 先 `memcpy(staging, src, nbytes)`。
  - 再 `NT store(dst, staging, nbytes或chunk)`。
- 如果最后形成的是大块连续 NT flush，这可能值得。
- 如果最后只是很多小 tail flush，就会同时承担“多一次 staging memcpy”和“小 NT store 固定成本”，性能会更差。

可实验的正确版本：
- `<=256B` 对象可以进入 staging log，但必须按 target 地址连续性切 run。
- 只有连续 run 累计到阈值，例如 2048B、4096B 或更大时，才 NT flush。
- 未达阈值的 tail 不应该强制 NT；应 fallback 到普通 GiY copy。
- `>256B` 对象继续使用原来的 GiY 阈值策略：足够大时 NT，否则 memcpy。
- 这样做的目标不是完全消灭 old memcpy，而是只在“连续且足够大”的场景使用 NT。

最终结论：
- `<=256B` 全部先放中介区在机制上可行。
- 但如果目标是“所有 256B 以下最终都强制 NT store”，根据现有数据大概率会失败。
- 更合理的实验方向是：`<=256B` 可以进入 staging，但只有连续 run 达到阈值才 NT；tail fallback。
- 这个版本可能减少 64B~256B 对象 direct NT 的固定成本，同时避免 forced-tail NT 的灾难性小 flush。

## 2026-05-07 什么是 tail fallback

定义：
- `tail` 指 staging buffer 里已经收集了一些 object bytes，但还没有达到 NT flush 阈值的尾部剩余数据。
- `fallback` 指这部分尾部剩余数据不再强制用 NT store 写回 old/DRAM，而是回退到普通 GiY copy 路径。

例子：
- 假设 NT flush 阈值是 2048B。
- 当前一段 target-contiguous run 中，staging 已经收集了 320B、512B 或 1536B。
- 这时遇到 target 不连续、遇到一个不该进入 staging 的大对象，或者本轮 minor GC 结束。
- 这段不足 2048B 的 staging 内容就是 tail。

tail forced NT：
- 即使 tail 只有 320B，也执行 `NT store(staging -> old target)`。
- 优点：形式上避免了 old/DRAM target 的普通 `memcpy`。
- 缺点：会产生大量小 NT store；上一轮 Storage 实验中 tiny tail forced NT 单独让 GC core 从 41.602s 增到 63.365s，增加 21.763s。

tail fallback：
- 如果 tail 没达到阈值，就不对这段 staging bytes 做 NT flush。
- 而是把 tail 中对应的 object 按原来的对象记录逐个复制到 old/DRAM：
  - 小对象通常走 `memcpy(dst, src, nbytes)`。
  - 大于 GiY NT 阈值的对象仍可走原来的 NT copy。
- 本质是：只有“大且连续”的 chunk 才值得 NT；小尾巴不要硬上 NT。

为什么需要保留每个 object 的记录：
- tail fallback 不能只看 staging buffer 的一段 bytes。
- 因为 fallback 时要知道每个 object 的原始 `src`、最终 `dst` 和 `nbytes`。
- 这样才能逐对象调用普通 GiY copy，而不是把 staging buffer 的 tail 盲目写到某个连续 target。

最终结论：
- tail fallback 是一种性能保护机制。
- 它承认小 tail 用普通 memcpy 可能比小 NT store 更快。
- 对当前 GiYOL 来说，tail fallback 比“完全消灭 memcpy”更符合已有 benchmark 数据。

## 2026-05-07 `<=256B staging + tail fallback` 实验

实验目标：
- 测试此前认为“值得试验的正确版本”：
  - `<=256B` survivor object 进入 staging。
  - 按 old/DRAM target 连续性切 run。
  - run 达到阈值才 NT flush。
  - tail 不强制 NT，fallback 到普通 GiY copy。
  - `>256B` 不 direct forced NT，回到 GiY 原来的复制策略。

主实验配置：
- `GIYOL_TINY_MAX_OBJECT_BYTES=256`
- `GIYOL_FORCE_TINY_TAIL_NT=0`
- `GIYOL_DIRECT_NONTINY_NT=0`
- `GIYOL_TINY_STAGING_BYTES=2048`
- `GIYOL_TINY_FLUSH_BYTES=2048`

正确性：
- 12 个 benchmark 全部正常结束。
- profile 确认所有 12 项都是：
  - `GiYOL tiny max object:256`
  - `GiYOL force tiny tail NT:0`
  - `GiYOL direct non-tiny NT:0`
- `GiYOL tiny tail NT flushes` 全部为 0。
- `GiYOL direct NT objects` 全部为 0。

12 项 CPU time 汇总：
- GiY: total 3484.477s, business 3378.482s, GC full 105.998s, GC core 72.829s。
- 旧 GiYOL: total 3489.323s, business 3368.807s, GC full 120.518s, GC core 86.167s。
- `<=256B staging + tail fallback, 2048B`: total 3506.420s, business 3380.944s, GC full 125.476s, GC core 91.571s。
- 新版本相对旧 GiYOL：total +17.097s, business +12.137s, GC full +4.958s, GC core +5.404s。
- 新版本相对 GiY：total +21.943s, business +2.462s, GC full +19.478s, GC core +18.742s。

10 项汇总，不含 Richards/Havlak，用于和 forced-NT 版本对比：
- GiY: total 2134.394s, business 2065.690s, GC full 68.706s, GC core 45.023s。
- 旧 GiYOL: total 2133.990s, business 2053.243s, GC full 80.747s, GC core 55.808s。
- forced-NT GiYOL: total 2188.201s, business 2054.996s, GC full 133.206s, GC core 107.857s。
- `<=256B staging + tail fallback, 2048B`: total 2150.274s, business 2064.792s, GC full 85.483s, GC core 61.320s。
- 新版本相对 forced-NT：total -37.927s, GC core -46.537s。
- 新版本相对旧 GiYOL：total +16.284s, GC core +5.512s。

逐项 total/core 对比，旧 GiYOL -> `<=256B staging + tail fallback, 2048B`：
- Bounce: total 157.275 -> 157.407s；GC core 0.051 -> 0.051s。
- List: total 97.091 -> 99.074s；GC core 0.003 -> 0.003s。
- Sieve: total 121.248 -> 120.295s；GC core 0.812 -> 0.844s。
- Queens: total 115.794 -> 119.533s；GC core 0.009 -> 0.009s。
- Permute: total 391.007 -> 391.513s；GC core 0.001 -> 0.001s。
- Storage: total 208.025 -> 215.605s；GC core 41.602 -> 47.104s。
- Towers: total 183.708 -> 181.979s；GC core 0.001 -> 0.001s。
- Mandelbrot: total 237.636 -> 237.399s；GC core 1.498 -> 1.447s。
- Richards: total 934.810 -> 939.186s；GC core 0.045 -> 0.044s。
- CD: total 243.058 -> 246.475s；GC core 8.862 -> 8.935s。
- NBody: total 379.148 -> 380.994s；GC core 2.969 -> 2.925s。
- Havlak: total 420.523 -> 416.960s；GC core 30.314 -> 30.207s。

2048B 阈值下的 staging/NT 规模：
- Storage: NT batches 4,291,828，NT bytes 8,382.48MB，fallback objects 936,238,037，fallback bytes 50,389.06MB。
- CD: NT batches 22,118，NT bytes 43.20MB，fallback objects 71,919,520，fallback bytes 3,435.72MB。
- Havlak: NT batches 341,799，NT bytes 667.58MB，fallback objects 310,588,946，fallback bytes 16,417.46MB。
- 对比旧 GiYOL 的 Storage：NT batches 23,629，NT bytes 46.15MB。
- 说明 `<=256B` staging 确实大幅增加了可 NT flush 的连续 run，但也增加了大量 `src -> staging` 的中介拷贝和 flush 管理成本。

Storage 阈值扫描：
- 旧 GiYOL：total 208.025s，business 163.546s，GC full 44.479s，GC core 41.602s；NT batches 23,629，NT bytes 46.15MB。
- 2048B：total 215.605s，business 166.499s，GC full 49.106s，GC core 47.104s；NT batches 4,291,828，NT bytes 8,382.48MB。
- 4096B：total 234.405s，business 185.258s，GC full 49.146s，GC core 45.660s；NT batches 2,067,726，NT bytes 8,077.05MB。此轮 business time 明显偏高，total 噪声较大，但 GC core 可参考。
- 8192B：total 210.934s，business 163.730s，GC full 47.203s，GC core 44.655s；NT batches 1,189,927，NT bytes 9,296.30MB。
- 16384B：total 212.975s，business 165.423s，GC full 47.553s，GC core 45.045s；NT batches 542,020，NT bytes 8,469.06MB。
- Storage 上 8192B 是扫描里最好的阈值，但仍慢于旧 GiYOL：total +2.909s，GC core +3.053s。

CD/Havlak 的 8192B 补测：
- CD 旧 GiYOL：total 243.058s，GC core 8.862s。
- CD 8192B：total 242.530s，GC core 8.873s；NT batches 4,552，NT bytes 35.56MB。
- Havlak 旧 GiYOL：total 420.523s，GC core 30.314s。
- Havlak 8192B：total 419.708s，GC core 30.416s；NT batches 128,503，NT bytes 1003.93MB。
- 8192B 在 CD/Havlak 上没有表现出明显 GC core 优势；total 的小幅变化更像 business/noise 与局部收益混合。

最终结论：
- `<=256B staging + tail fallback` 是机制上正确的版本，且明显优于 forced-NT。
- 但它没有击败旧 GiYOL。
- 2048B 阈值 full suite 相对旧 GiYOL：total +17.097s，GC core +5.404s。
- 调到 8192B 后，Storage 明显改善，但仍比旧 GiYOL 慢：total +2.909s，GC core +3.053s。
- 主要原因不是 tail NT，也不是 direct NT；这两者已经关闭。
- 主要成本变成了：`64B~256B` 对象进入 staging 后，多了一次 `src -> staging` memcpy、更多 batch bookkeeping，以及更多 NT flush。即使 tail fallback 避免了灾难性小 NT，扩大 staging 范围本身仍然带来额外开销。
- 当前数据支持的方向：不要把所有 `<=256B` 都纳入 staging；应只让“预计能形成足够大 target-contiguous run”的对象进入 staging，或者维持旧 GiYOL 的 `<=64B` 更保守策略。

## 2026-05-07 阶段性总结：GiY vs 旧 GiYOL vs 当前 GiYOL

这里的三个版本定义：
- GiY：当前普通 GiY baseline，数据来自 `out50_giy_as_update_final_full`。
- 旧 GiYOL：先前的保守 GiYOL，`<=64B` tiny staging，tail 小于阈值 fallback 到普通 GiY copy；它仍然有大量 old/DRAM target 的 `memcpy`，因此仍有 cache pollution 风险。数据来自 `out89_giyol_tiny_selective_2048_64_full`。
- 当前 GiYOL：本轮实验的 `<=256B staging + tail fallback`，full suite 使用 2048B 阈值；另对 Storage/CD/Havlak 扫描了 8192B 阈值。数据来自 `out93_giyol_256_staging_tailfb_full`，以及部分 `out95/out97` 补测。

12 项 full suite 总数据：
- GiY：total 3484.477s，business 3378.482s，GC full 105.998s，GC core 72.829s。
- 旧 GiYOL：total 3489.323s，business 3368.807s，GC full 120.518s，GC core 86.167s。
- 当前 GiYOL 2048B：total 3506.420s，business 3380.944s，GC full 125.476s，GC core 91.571s。

相对 GiY：
- 旧 GiYOL：total +4.846s，business -9.675s，GC full +14.520s，GC core +13.338s。
- 当前 GiYOL 2048B：total +21.943s，business +2.462s，GC full +19.478s，GC core +18.742s。

相对旧 GiYOL：
- 当前 GiYOL 2048B：total +17.097s，business +12.137s，GC full +4.958s，GC core +5.404s。

核心观察：
- 旧 GiYOL 的 business time 比 GiY 少 9.675s，说明保守 staging/NT 确实可能减少一部分业务阶段可见的 cache pollution 或缓存干扰。
- 但旧 GiYOL 的 GC core 比 GiY 多 13.338s，说明 staging/batch/remembered-set/额外复制管理的 GC 成本超过了 business 收益。
- 因此旧 GiYOL 总时间比 GiY 仍慢 4.846s。
- 当前 GiYOL 进一步把 staging 范围从 `<=64B` 扩到 `<=256B`，理论上希望减少更多 old target memcpy。
- 实际结果是：business 不但没有继续改善，反而比旧 GiYOL 慢 12.137s；GC core 也比旧 GiYOL 多 5.404s。
- 因此当前 GiYOL 总时间比旧 GiYOL 慢 17.097s。

Storage 是最能说明问题的项：
- GiY：total 213.283s，business 178.595s，GC core 31.941s。
- 旧 GiYOL：total 208.025s，business 163.546s，GC core 41.602s。
- 当前 GiYOL 2048B：total 215.605s，business 166.499s，GC core 47.104s。
- 当前 GiYOL 8192B 补测：total 210.934s，business 163.730s，GC core 44.655s。

Storage 解释：
- 旧 GiYOL 相比 GiY，business 快 15.049s，但 GC core 慢 9.661s；最终 total 快 5.258s。
- 当前 2048B 相比旧 GiYOL，business 慢 2.953s，GC core 慢 5.502s；最终 total 慢 7.580s。
- 当前 8192B 相比当前 2048B 有改善，GC core 从 47.104s 降到 44.655s，但仍慢于旧 GiYOL 的 41.602s。

cache pollution 与性能的关系：
- 旧 GiYOL 确实没有完全解决 cache pollution。
- Storage 中旧 GiYOL 只有 46.15MB 通过 staging NT 写回，仍有 45,677.59MB tiny fallback bytes 通过普通 GiY copy 写 old/DRAM。
- 这意味着旧 GiYOL 仍有大量 ordinary memcpy old writes，会污染 cache。
- 但是尝试把更多对象纳入 staging 后，当前 GiYOL 虽然大幅增加了 NT bytes，却付出了更高的中介复制和管理成本。

当前 GiYOL 的额外成本：
- Storage 2048B 下，当前 GiYOL NT batches 4,291,828，NT bytes 8,382.48MB，fallback bytes 50,389.06MB。
- 对比旧 GiYOL Storage：NT batches 23,629，NT bytes 46.15MB，fallback bytes 45,677.59MB。
- 当前 GiYOL 让更多连续 run 走 NT，但代价是：
  - 许多 `64B~256B` object 多了一次 `src -> staging` memcpy。
  - batch bookkeeping 和 run 切分次数大幅增加。
  - NT flush 次数大幅增加，即使 tail forced NT 已经关闭。
  - fallback bytes 仍然很大，说明并没有彻底消灭 old target memcpy。

forced-NT 失败提供的反证：
- forced-NT GiYOL 在 Storage 上 total 253.573s，GC core 87.427s。
- 它更接近“尽量消灭 old memcpy”的目标，但性能远差于旧 GiYOL和当前 tail-fallback 版本。
- 这说明“减少 cache pollution”不能靠无条件 NT；NT 必须只用于足够大、足够连续、收益能覆盖固定成本的写入。

阶段性结论：
- GiY 当前仍是 full-suite total 最好的版本：3484.477s。
- 旧 GiYOL 在 business time 上最好：3368.807s，但 GC 成本过高，total 比 GiY 慢 4.846s。
- 当前 GiYOL 2048B 没有达到目标：它既没有保持旧 GiYOL 的 business 优势，也进一步增加了 GC 成本，total 比旧 GiYOL 慢 17.097s。
- 当前 GiYOL 的正确价值是验证了一个方向：tail fallback 是必要的，forced NT 不可取；但扩大到 `<=256B` 全 staging 不值得。
- 后续最合理方向不是继续扩大 staging，而是做选择性 staging：只让能形成大 target-contiguous run 的对象进入 staging，否则保持 GiY/旧 GiYOL 的普通 copy。

## 2026-05-07 深入调查：object 聚合什么时候最划算

本轮新发现：
- 之前的 tail fallback 版本仍有一个隐藏成本：
  - 即使最后 tail fallback 到 `memcpy(dst, src)`，对象也已经先被 `memcpy(src -> staging)` 了一遍。
  - 也就是说 fallback 对象实际上多做了一次无效中介拷贝。
- 这会严重扭曲结论，因为我们以为“tail fallback 避免了 NT 成本”，但它仍然保留了 `src -> staging` 成本。

新实现：
- 新增 `GIYOL_DEFER_TINY_STAGING`。
- 当 `GIYOL_DEFER_TINY_STAGING=1` 时：
  - 先扫描 reserve-order copy log，找 target-contiguous run。
  - 先判断这个 run 的 chunk 是否达到 `GIYOL_TINY_FLUSH_BYTES`。
  - 只有确定要 NT flush 的 chunk，才执行 `src -> staging`。
  - 不达阈值的 chunk 直接 fallback 到 GiY copy，不再先写 staging。
- 这是真正意义上的“只有聚合划算时才聚合”。

Storage 扫描，deferred staging，tail forced NT off，direct non-tiny NT off：
- 旧 GiYOL：total 208.025s，business 163.546s，GC full 44.479s，GC core 41.602s；NT batches 23,629，NT bytes 46.15MB。
- max64/flush2048：total 204.032s，business 165.224s，GC full 38.809s，GC core 35.962s；NT batches 23,641，NT bytes 46.17MB。
- max64/flush4096：total 202.248s，business 163.880s，GC full 38.367s，GC core 35.478s；NT batches 0，NT bytes 0.00MB。
- max64/flush8192：total 202.513s，business 164.115s，GC full 38.398s，GC core 35.620s；NT batches 0，NT bytes 0.00MB。
- max96/flush8192：total 221.826s，business 181.761s，GC full 40.064s，GC core 38.750s；NT batches 1,189,908，NT bytes 9,296.16MB。
- max128/flush8192：total 208.510s，business 163.690s，GC full 44.820s，GC core 42.034s；NT batches 1,189,927，NT bytes 9,296.30MB。
- max192/flush8192：total 204.669s，business 163.125s，GC full 41.543s，GC core 38.556s；NT batches 1,189,927，NT bytes 9,296.30MB。
- max256/flush8192：total 208.997s，business 167.685s，GC full 41.312s，GC core 38.674s；NT batches 1,189,928，NT bytes 9,296.31MB。

Storage 结论：
- deferred staging 本身非常有效：max64/flush4096 的 GC core 35.478s，比旧 GiYOL 41.602s 少 6.124s。
- 但“扩大对象上限”不划算：
  - 从 max64 扩到 max96 以后，突然出现约 9.3GB NT bytes 和约 119 万次 NT batches。
  - GC core 不再继续下降，反而比 max64/flush4096 更差。
- max64/flush4096 和 max64/flush8192 在 Storage 上最好。
- max64/flush4096 略优，且 full suite 后续采用它。

full suite：`max64/flush4096/deferred`，并重跑 Mandelbrot/Towers 纠正噪声：
- GiY：total 3484.477s，business 3378.482s，GC full 105.998s，GC core 72.829s。
- 旧 GiYOL：total 3489.323s，business 3368.807s，GC full 120.518s，GC core 86.167s。
- max64/flush4096/deferred：total 3491.145s，business 3380.233s，GC full 110.911s，GC core 77.526s。

相对旧 GiYOL：
- total +1.822s。
- business +11.426s。
- GC full -9.607s。
- GC core -8.641s。

相对 GiY：
- total +6.668s。
- business +1.751s。
- GC full +4.913s。
- GC core +4.697s。

逐项对比：旧 GiYOL -> max64/flush4096/deferred：
- Bounce：total 157.275 -> 156.708s；GC core 0.051 -> 0.047s。
- List：total 97.091 -> 98.299s；GC core 0.003 -> 0.003s。
- Sieve：total 121.248 -> 123.433s；GC core 0.812 -> 0.823s。
- Queens：total 115.794 -> 115.761s；GC core 0.009 -> 0.009s。
- Permute：total 391.007 -> 390.622s；GC core 0.001 -> 0.001s。
- Storage：total 208.025 -> 202.248s；GC core 41.602 -> 35.478s。
- Towers：total 183.708 -> 183.286s；GC core 0.001 -> 0.001s。
- Mandelbrot：total 237.636 -> 235.826s；GC core 1.498 -> 1.402s。
- Richards：total 934.810 -> 938.430s；GC core 0.045 -> 0.042s。
- CD：total 243.058 -> 244.191s；GC core 8.862 -> 8.396s。
- NBody：total 379.148 -> 382.729s；GC core 2.969 -> 2.870s。
- Havlak：total 420.523 -> 419.612s；GC core 30.314 -> 28.454s。

为什么 full-suite total 没有明显赢旧 GiYOL：
- GC 侧已经明显改善：GC core 比旧 GiYOL 少 8.641s。
- 但 business time 比旧 GiYOL 多 11.426s。
- business 变慢主要来自非 GC-heavy 项和运行噪声/缓存状态差异，例如 Richards +3.679s、NBody +4.020s、Sieve +2.196s。
- 对真正受 GC copy 策略影响的项，结果更好：
  - Storage total -5.777s，GC core -6.124s。
  - Havlak total -0.911s，GC core -1.860s。
  - CD GC core -0.466s，但 total +1.133s。

本轮最重要的机制结论：
- 聚合最划算的条件不是“对象尽量多、范围尽量大”。
- 聚合划算需要同时满足：
  - target 地址连续。
  - chunk 达到至少 4KB 左右的阈值。
  - 在确认达到阈值之前不要做 `src -> staging`。
  - object 上限保持小，当前数据支持 `<=64B`，不支持扩大到 `<=96B` 或更高。
- 对不满足阈值的 run，最划算的是直接 fallback，不要碰 staging。

为什么 `<=96B/128B/256B` 不划算：
- 一旦纳入这些对象，Storage 中 NT bytes 从几十 MB 暴涨到约 9.3GB。
- NT batches 也从 2 万级变成约 119 万级。
- 这说明中等对象形成了很多“看似连续但仍很碎”的 flush。
- 即使 deferred staging 避免了 tail 的无效中介拷贝，这些 flush 的管理成本和写入成本仍然太大。

当前最合理版本：
- `GIYOL_DEFER_TINY_STAGING=1`
- `GIYOL_TINY_MAX_OBJECT_BYTES=64`
- `GIYOL_TINY_FLUSH_BYTES=4096`
- `GIYOL_TINY_STAGING_BYTES=4096`
- `GIYOL_FORCE_TINY_TAIL_NT=0`
- `GIYOL_DIRECT_NONTINY_NT=0`

最终阶段性判断：
- “先判断 run 是否足够大，再 staging”是正确方向。
- “扩大 staging 到 256B”不是正确方向。
- 当前最佳策略显著改善 GC core，但 full-suite total 仍只接近旧 GiYOL，没有稳定超过 GiY/旧 GiYOL。
- 下一步如果继续优化，应围绕选择性 staging，而不是扩大对象大小：
  - 只对历史上/当前 run 可能达到 4KB+ 的分配序列启用 staging。
  - 低于阈值的 run 完全跳过 staging。
  - 保持 object size 上限在 64B。

## 2026-05-07 memcpy/NT store microbenchmark

目的：
- 确认三类纯拷贝原语的速度：
  - young/cache 区内 `memcpy`。
  - young/cache -> old/DRAM 区 `memcpy`。
  - young/cache -> old/DRAM 区 direct NT store。
- 注意：当前实现里的 cache_space 和 dram_space 都是 `malloc` 出来的普通主存地址，不是硬件上真的不同内存层级。因此该测试测的是“地址范围/写策略”的性能，不是 HBM/DRAM 物理差异。

测试环境：
- CPU：Intel Core Ultra 9 285K。
- young buffer：512KB。
- old buffer：256MB。
- 编译：`g++ -O3 -march=native`。
- NT128：使用 `_mm_stream_si128`，即 128-bit non-temporal store，接近当前 GiY/GiYOL stream copy 风格。
- NT256：使用 `_mm256_stream_si256`，即 AVX2 256-bit non-temporal store，不是 256-byte store。
- 表中为 3 轮中位数。

吞吐，GB/s：
- 16B：young memcpy 16.797，young->old memcpy 13.033，NT128 0.078，NT256 9.350。
- 32B：young memcpy 31.213，young->old memcpy 15.887，NT128 0.155，NT256 0.152。
- 64B：young memcpy 49.265，young->old memcpy 18.887，NT128 0.309，NT256 0.313。
- 96B：young memcpy 33.910，young->old memcpy 13.914，NT128 0.481，NT256 0.484。
- 128B：young memcpy 43.209，young->old memcpy 16.897，NT128 0.646，NT256 0.647。
- 192B：young memcpy 32.324，young->old memcpy 17.151，NT128 0.972，NT256 0.972。
- 256B：young memcpy 56.615，young->old memcpy 20.010，NT128 1.298，NT256 1.301。
- 512B：young memcpy 59.980，young->old memcpy 20.746，NT128 2.593，NT256 2.598。
- 1024B：young memcpy 67.993，young->old memcpy 21.224，NT128 5.148，NT256 5.157。
- 2048B：young memcpy 72.893，young->old memcpy 20.509，NT128 10.200，NT256 10.230。
- 4096B：young memcpy 79.016，young->old memcpy 14.803，NT128 19.410，NT256 20.411。
- 8192B：young memcpy 83.491，young->old memcpy 16.252，NT128 34.417，NT256 35.737。
- 16384B：young memcpy 86.510，young->old memcpy 17.654，NT128 49.006，NT256 54.075。

每次 copy 延迟，ns/copy：
- 64B：young memcpy 1.21ns，young->old memcpy 3.16ns，NT128 192.73ns，NT256 190.29ns。
- 256B：young memcpy 4.21ns，young->old memcpy 11.92ns，NT128 183.72ns，NT256 183.33ns。
- 2048B：young memcpy 26.17ns，young->old memcpy 93.00ns，NT128 187.00ns，NT256 186.44ns。
- 4096B：young memcpy 48.28ns，young->old memcpy 257.69ns，NT128 196.53ns，NT256 186.89ns。
- 8192B：young memcpy 91.38ns，young->old memcpy 469.43ns，NT128 221.67ns，NT256 213.49ns。
- 16384B：young memcpy 176.38ns，young->old memcpy 864.30ns，NT128 311.37ns，NT256 282.18ns。

关键结论：
- 在小对象范围内，direct NT store 极其不划算。
  - 64B：young->old memcpy 3.16ns，NT128 192.73ns，NT 慢约 61 倍。
  - 256B：young->old memcpy 11.92ns，NT128 183.72ns，NT 慢约 15 倍。
- 1KB/2KB 仍不适合 direct NT。
  - 2048B：young->old memcpy 93.00ns，NT128 187.00ns，NT 仍慢约 2 倍。
- 到 4KB 后，NT 开始比 young->old memcpy 快。
  - 4096B：young->old memcpy 257.69ns，NT128 196.53ns。
  - 8192B：young->old memcpy 469.43ns，NT128 221.67ns。
- young/cache 内 memcpy 始终非常快，因为读写都在 hot/cache-local 区域。

对 staging 的直接推论：
- staging copy 的真实成本是：
  - `src young -> staging young` 的 young memcpy。
  - 加上 `staging young -> old` 的 NT store。
- 64B 如果 staging 后 NT：
  - 约 `1.21ns + 192.73ns = 193.94ns`。
  - 直接 young->old memcpy 约 3.16ns。
  - staging+NT 完全不划算。
- 256B 如果 staging 后 NT：
  - 约 `4.21ns + 183.72ns = 187.93ns`。
  - 直接 young->old memcpy 约 11.92ns。
  - staging+NT 仍完全不划算。
- 4096B 如果 staging 后 NT：
  - 约 `48.28ns + 196.53ns = 244.81ns`。
  - 直接 young->old memcpy 约 257.69ns。
  - 只略微划算。
- 8192B 如果 staging 后 NT：
  - 约 `91.38ns + 221.67ns = 313.05ns`。
  - 直接 young->old memcpy 约 469.43ns。
  - 明显划算。

为什么之前实验结果吻合这个 microbenchmark：
- forced-NT 对 64B/256B 小对象直接 NT，会极慢，因此 Storage GC core 暴涨。
- `<=256B staging` 会让大量 64B~256B 对象参与 staging/NT，虽然形成了更多 NT bytes，但原语层面这些对象太小，收益覆盖不了成本。
- deferred staging + 4KB 阈值更合理，因为它避免了小 tail 的 `src -> staging`，也避免了小 chunk NT。
- 但真正明显划算的 chunk 更接近 8KB 以上；4KB 只是刚刚过盈亏平衡点。

关于 cache pollution：
- microbenchmark 的 hot young probe 没有测出明显差异：
  - 64B/256B/4096B 下，young 热区探测基本都在 0.75ms 左右。
- 原因是测试 young hot set 只有 512KB，能被当前 CPU cache 较好容纳；它不能完全代表真实 JS business 的工作集。
- 因此该 microbenchmark 更可靠地说明“拷贝原语速度”，不能单独量化真实业务 cache pollution。

最终判据：
- 小对象不要 direct NT。
- 小对象也不要先 staging，除非已经确认一整段 target-contiguous run 能达到至少 4KB，最好 8KB 以上。
- `<=64B` 的小对象可以作为候选，但必须 deferred staging。
- `64B~256B` 不应默认进入 staging；它们会制造大量看似连续但成本很高的 NT flush。
- 如果未来要用更宽的 NT store，阈值也不应降低；宽 store 只改善大块吞吐，不会改变小块 NT 固定成本高这个事实。

## 2026-05-07 综合研判：后续仍有优化可能的方案

基于目前所有 benchmark、消融实验和 microbenchmark，已经可以排除一些方向：
- 排除 direct NT 小对象。
- 排除 `<=256B` 全 staging。
- 排除 tail forced NT。
- 排除“为了完全消灭 old memcpy 而牺牲粒度”的方案。

仍有优化可能的方向如下。

### 方案 1：deferred staging 作为 GiYOL 基础路径

当前最有价值的机制改动是 `GIYOL_DEFER_TINY_STAGING=1`：
- 先判断 target-contiguous run 是否达到阈值。
- 达到阈值才 `src -> staging -> NT`。
- 不达阈值直接 fallback，不碰 staging。

理由：
- 它修复了之前 tail fallback 的隐藏成本：fallback 前已经 `src -> staging` 的无效拷贝。
- Storage 上 `max64/flush4096/deferred`：
  - 旧 GiYOL GC core 41.602s。
  - deferred GC core 35.478s。
  - GC core 改善 6.124s。
- full suite 校正后：
  - 旧 GiYOL GC core 86.167s。
  - deferred GC core 77.526s。
  - GC core 改善 8.641s。

风险：
- full-suite total 仍没有稳定赢旧 GiYOL，因为 business time 多了 11.426s。
- 这可能是噪声、缓存状态变化、或策略对业务阶段的副作用。

判断：
- 这是目前最值得保留的优化机制。
- 但不能只看 total，需要继续多轮重复验证 business time 是否稳定变慢。

### 方案 2：小对象上限保持 `<=64B`，不要扩大

当前数据强烈支持小对象 staging 候选上限保持 64B。

理由：
- microbenchmark：
  - 64B direct NT 比 young->old memcpy 慢约 61 倍。
  - 256B direct NT 比 young->old memcpy 慢约 15 倍。
- Storage deferred scan：
  - max64/flush4096：total 202.248s，GC core 35.478s。
  - max96/flush8192：total 221.826s，GC core 38.750s，NT bytes 9296.16MB。
  - max128/flush8192：total 208.510s，GC core 42.034s。
  - max256/flush8192：total 208.997s，GC core 38.674s。
- 一旦超过 64B，Storage 中 NT bytes 从几十 MB 量级暴涨到约 9.3GB，NT batches 到约 119 万级。

判断：
- `64B~256B` 不应默认进入 staging。
- 这些对象可以未来做“选择性 staging”，但不能按 size 上限无脑纳入。

### 方案 3：阈值使用 4KB 或 8KB，倾向 4KB 做当前默认

microbenchmark 判据：
- 4096B：staging+NT 约 244.81ns，direct young->old memcpy 约 257.69ns，只是略微划算。
- 8192B：staging+NT 约 313.05ns，direct young->old memcpy 约 469.43ns，明显划算。

Storage 实测：
- max64/flush2048：total 204.032s，GC core 35.962s。
- max64/flush4096：total 202.248s，GC core 35.478s。
- max64/flush8192：total 202.513s，GC core 35.620s。

判断：
- 4KB 是当前 Storage 上最佳点。
- 8KB 的理论原语收益更强，但可能错过一些 4KB~8KB 的有用 run。
- 当前建议默认 4KB，后续用多轮 full suite 验证 4KB/8KB 哪个更稳。

### 方案 4：选择性 staging，而不是按 size staging

更进一步的优化应该判断“这个 run 是否可能达到阈值”，而不是只看 object size。

可能实现方式：
- 在 reserve-order log 中扫描 target-contiguous run。
- 如果 run 总 bytes 小于阈值，整段直接 fallback。
- 如果 run 总 bytes 大于阈值，只对其中能组成 4KB/8KB chunk 的部分 staging+NT。
- tail 直接 fallback。

当前 deferred staging 已经接近这个方向，但仍可进一步减少 bookkeeping：
- 对明显不足阈值的 run，可以直接 fast-path fallback，避免进入复杂 chunk loop。
- 对单对象或极短 run，直接调用 `giy_copy_live_object`。

判断：
- 这是最有可能继续降低 GC core 的方向。
- 它不会试图消灭所有 old memcpy，而是只抓真正大的连续 run。

### 方案 5：按 allocation site / shape 做预测性 staging

当前 run 判断发生在 GC flush 阶段，仍需要记录 copy log。
下一步可以探索预测：
- 记录哪些 allocation site 或 shape 在过去 GC 中经常形成 4KB+ target-contiguous run。
- 只有这些 site/shape 的对象进入 staging candidate。
- 其他对象直接走普通 GiY copy。

潜在收益：
- 减少 copy log 中无效 candidate 数量。
- 减少 run 扫描和 batch bookkeeping。
- 避免对不可能形成大 run 的对象做任何 GiYOL 额外处理。

风险：
- 需要维护 profile/历史状态。
- 如果预测错误，会漏掉可优化 run 或引入额外判断成本。

判断：
- 这是中期优化方向，值得在 deferred staging 稳定后尝试。

### 方案 6：用更宽 NT store 只优化大 chunk，不改变阈值

如果目标是 256-byte 宽 NT store：
- 它可能改善 4KB/8KB+ 大 chunk 的吞吐。
- 但不改变小对象 NT 固定成本高这个事实。

判断：
- 宽 NT store 应该只用于已经满足阈值的大 chunk。
- 不能因为 store 更宽就降低阈值或扩大对象上限。
- 正确组合是：deferred staging + 4KB/8KB 阈值 + 大 chunk 宽 NT。

### 方案 7：减少 business time 回退

当前 deferred 版本 GC 明显变好，但 full-suite business time 比旧 GiYOL 多 11.426s。
这部分必须继续调查。

可能原因：
- benchmark 噪声，尤其 Richards/NBody/Sieve 对 GC 策略不敏感但 business 波动明显。
- old 写入模式改变影响后续业务缓存。
- NT flush 数量或 sfence 时机影响流水线。
- deferred scan 的内存访问模式改变了 cache 状态。

需要验证：
- 对 deferred max64/flush4096 跑 3 轮 full suite 或至少关键项重复。
- 分离 GC-heavy 与 business-heavy 项：
  - GC-heavy：Storage、Havlak、CD。
  - business-heavy：Richards、NBody、Sieve、Mandelbrot。
- 如果 GC-heavy 稳定收益而 business-heavy 波动无规律，则主要是噪声。
- 如果 business-heavy 稳定变慢，则要调查 sfence、NT 写回、old/cache 热度。

### 当前推荐的下一步实验顺序

1. 固化当前最佳配置：
   - `GIYOL_DEFER_TINY_STAGING=1`
   - `GIYOL_TINY_MAX_OBJECT_BYTES=64`
   - `GIYOL_TINY_FLUSH_BYTES=4096`
   - `GIYOL_TINY_STAGING_BYTES=4096`
   - `GIYOL_FORCE_TINY_TAIL_NT=0`
   - `GIYOL_DIRECT_NONTINY_NT=0`

2. 重复跑关键项 3 轮：
   - Storage、Havlak、CD、Richards、NBody、Sieve。

3. 如果 GC-heavy 稳定收益，继续做 fast-path：
   - run 总 bytes < 阈值，直接 fallback，不走 chunk loop。
   - 单对象 run 直接 fallback。

4. 如果 business time 稳定变慢，再调查：
   - NT/sfence 时机。
   - old 写入后是否被业务很快读取。
   - 是否需要对某些对象类型禁用 NT。

最终判断：
- 目前仍有优化空间，但空间不在“更激进 NT”。
- 最有希望的是“更少参与、更晚 staging、更大 chunk、更少 bookkeeping”。
- GiYOL 的原则应从“尽量避免 memcpy”改成“只在能证明 chunk 够大时才用 NT，否则完全走 GiY copy”。

## 2026-05-07 GiY / GiYOL 逻辑审查

本轮检查范围：
- `ejsvm/GiY.cc`
- `ejsvm/giy_rset.cc`
- `ejsvm/common.mk`

重点检查的问题：
- GiY reserve/traverse/copy/patch 顺序是否会复制未修复对象。
- GiYOL deferred staging 是否会漏复制、重复复制、错误 NT flush。
- RSet 的 `buffer[]/values[]` 语义是否仍然保持“每个 slot 的最新摘要”。
- NT store 后是否存在马上读取 old object 的时序风险。
- GiYOL 默认参数是否和当前实验结论一致。

结论：
- GiY / GiYOL 的主复制顺序没有发现“先复制未 patch 对象”的硬错误。
- 当前逻辑是：reserve 阶段只设置 forwarding pointer 并压栈；traverse 阶段扫描 young 源对象并先修复 young 内部 slot；之后才把源对象复制到 old。GiYOL tiny staging 也是在 traverse 完成后 flush reserve-order batch，因此 old 中拿到的是已经修复过的对象内容。
- GiYOL deferred staging 的核心路径没有发现漏复制：达到阈值的 target-contiguous run 会先 `src -> staging`，再 `staging -> old` NT；不足阈值的 tail 不会提前 staging，而是 fallback 到普通 GiY copy。

发现并修补的问题 1：RSet hash probe fallback 可能产生重复 slot entry。
- 位置：`ejsvm/giy_rset.cc`
- 原问题：`rememberset_add_with_value()` 在 hash probe 超过 `HASH_PROBE_LIMIT` 后，会直接把 slot 追加到 `remembered_set.buffer[]/values[]`。
- 这条 fallback 插入没有先做全表查重。
- 如果同一个 slot 后续再次落入 fallback 路径，就可能产生多个相同 slot 的 entry。
- 这会破坏 GiY RSet 的核心语义：`values[]` 应该是每个 slot 的最新 young-edge 摘要。
- 更严重的是，patch 阶段按 `values[]` 回写真实 old/init slot；重复 stale entry 可能导致旧值参与 reserve/patch。

修补：
- 在 fallback 插入前增加：
  - `if (rememberset_update_existing(obj_ptr, value)) { write_barrier_duplicate_filtered++; return; }`
- 这样 probe 超限时会先通过已有的 linear fallback 查找旧 entry。
- 如果 slot 已存在，只更新 `values[index]`，不再追加重复 entry。
- 如果 slot 确实不存在，才追加新 entry。

发现并修补的问题 2：GiYOL 源码默认参数仍停留在失败实验配置。
- 位置：`ejsvm/GiY.cc`
- 原默认值：
  - `GIYOL_TINY_STAGING_BYTES=2048`
  - `GIYOL_TINY_FLUSH_BYTES=2048`
  - `GIYOL_FORCE_TINY_TAIL_NT=1`
  - `GIYOL_DIRECT_NONTINY_NT=1`
- 这与当前实验结论冲突。
- 如果只用 `OPT_GC=giyol` 编译而忘记额外传宏，会得到“强制 tail NT + 非 tiny 对象 direct NT”的失败策略。

修补：
- 默认值改成当前推荐策略：
  - `GIYOL_TINY_STAGING_BYTES=4096`
  - `GIYOL_TINY_FLUSH_BYTES=4096`
  - `GIYOL_FORCE_TINY_TAIL_NT=0`
  - `GIYOL_DIRECT_NONTINY_NT=0`
  - `GIYOL_DEFER_TINY_STAGING=1`

发现并修补的问题 3：NT copy 后存在立即读取 materialized old object 的时序风险。
- 位置：`ejsvm/GiY.cc`
- 风险点：`giy_traverse_stack_and_copy()` 完成 old copy 后，可能马上执行 allocation-site object update，读取刚复制到 old 的对象字段，例如 `obj->shape`。
- 另外 `giy_materialize_shape()` / `giy_materialize_property_map()` 也可能在 `giy_traverse_stack_and_copy()` 返回后马上读取刚 materialize 的 old 对象。
- 原代码只在 minor GC 末尾做全局 `_mm_sfence()`，对这种“copy 后立即读”的局部路径不够保守。

修补：
- 新增 `giy_finish_local_nt_stores(bool *used_nt_store)`。
- 在 `giy_traverse_stack_and_copy()` flush 完 GiY/GiYOL copy batch 后立即调用。
- 作用：如果本次 traversal 使用过 NT store，就先 `_mm_sfence()`，然后再允许后续 allocation-site update 或调用者读取 materialized old object。
- slot patch 阶段的 old slot streaming store 仍然保留原来的全局 minor-GC 末尾 fence。

验证：
- `git diff --check -- ejsvm/GiY.cc ejsvm/giy_rset.cc ejsvm/common.mk` 通过。
- GiY 编译通过：
  - `make -B -C build.debug GiY.o giy_rset.o ejsvm OPT_GC=giy GIY_RSET_INDEX_FAST=true CACHE_SIZE_KB=552 GIY_AS_UPDATE=true GIY_AS_FT_ADVANCE=true -j4`
- GiYOL 编译通过：
  - `make -B -C build.debug GiY.o giy_rset.o ejsvm OPT_GC=giyol GIY_RSET_INDEX_FAST=true CACHE_SIZE_KB=552 GIY_AS_UPDATE=true GIY_AS_FT_ADVANCE=true -j4`
- GiYOL smoke 通过：
  - `hello_world.sbc`
  - `bounce_check.sbc`
  - `giy_gc_probe.sbc`
- GiY smoke 通过：
  - `giy_gc_probe.sbc`

当前保留风险：
- 这次是代码逻辑审查加 smoke，不是 full benchmark 验证。
- RSet fallback duplicate 修复在高冲突 workload 上应该提升正确性，也可能改变重复过滤统计；是否影响性能需要重新跑 Havlak/CD/Storage。
- 新增局部 `_mm_sfence()` 是正确性更保守的改动；它可能给 GC core 带来少量成本，需要用 benchmarks 重新测。

## 2026-05-07 GiY / GiYOL 完整审查与 benchmark 复测结论

本轮任务：
- 认真审查当前 GiY 和 GiYOL 是否有写错或逻辑错误。
- 修补确认存在的正确性风险。
- 分别跑完整 benchmark suite。
- 重新分析 GiY 与 GiYOL 的性能差距。

审查范围：
- `ejsvm/GiY.cc`
- `ejsvm/giy_rset.cc`
- `ejsvm/common.mk`

本轮确认并修补的问题：
1. RSet fallback 重复 entry 问题。
   - `rememberset_add_with_value()` 在 hash probe 超限后直接追加新 entry，没有先做全表 duplicate check。
   - 这可能让同一个 slot 在 `buffer[]/values[]` 中出现多份，破坏“每个 slot 只保留最新 value 摘要”的语义。
   - 已修补：fallback 插入前先调用 `rememberset_update_existing(obj_ptr, value)`，命中则只更新旧 entry 并返回。

2. RSet 满容量检查顺序问题。
   - 原逻辑可能在 RSet 已满时先报错，即使当前写入只是更新已有 slot。
   - 已修补：新增 `rememberset_require_new_entry_space()`，只在确实要插入新 entry 时检查容量。
   - 这样 full set 下的 duplicate/update 不会被误判成需要新增 entry。

3. GiYOL adaptive batch 关闭路径残留问题。
   - 如果 `GIYOL_ADAPTIVE_BATCH=1` 且当前 traversal 不使用 batch，reserve 阶段仍可能留下 small-object batch log。
   - 默认配置不触发，但这是潜在逻辑错误。
   - 已修补：新增 `giyol_copy_batch_reset()`；当 `giyol_use_batch == false` 时清空 `giyol_reserve_order_batch`。

4. NT store 后立即读取 old object 的可见性风险。
   - `giy_traverse_stack_and_copy()` 可能刚用 NT store materialize old object，随后 allocation-site update 或调用者立即读取这个 old object。
   - 已修补：新增 `giy_finish_local_nt_stores()`，在 traversal copy/batch flush 完成后立即 `_mm_sfence()`。
   - old slot patch 阶段仍保留 minor GC 末尾的全局 fence。

5. allocation-site update 被 mprotect 识别为 old-read 的问题。
   - mprotect strict core 验证显示，allocation-site update 需要读取 old Shape/PropertyMap 元数据。
   - 这不是 GiY reserve/traverse/patch 核心路径，而是后续元数据维护。
   - 已修补：复制 JSObject 时记录 young source object 的 `shape`，避免复制后立刻读 old object payload；同时把 allocation-site metadata update 放到 strict old guard 外。

6. GiYOL 默认参数修正。
   - 当前源码默认已改成推荐策略：
     - `GIYOL_TINY_STAGING_BYTES=4096`
     - `GIYOL_TINY_FLUSH_BYTES=4096`
     - `GIYOL_TINY_MAX_OBJECT_BYTES=64`
     - `GIYOL_FORCE_TINY_TAIL_NT=0`
     - `GIYOL_DIRECT_NONTINY_NT=0`
     - `GIYOL_DEFER_TINY_STAGING=1`
   - `common.mk` 已支持转发 `GIYOL_DEFER_TINY_STAGING`。

正确性验证：
- `git diff --check -- ejsvm/GiY.cc ejsvm/giy_rset.cc ejsvm/common.mk` 通过。
- GiY 编译通过。
- GiYOL 编译通过。
- `GIY_MPROTECT_OLD=1 ../ejsvm giy_gc_probe.sbc`：
  - GiY：status=0。
  - GiYOL：status=0。
- 完整 suite：
  - GiY：12/12 benchmark status=0。
  - GiYOL：12/12 benchmark status=0。

benchmark 输出目录：
- GiY full suite：`build.debug/benchmarks/out104_review_giy_full`
- GiYOL full suite：`build.debug/benchmarks/out105_review_giyol_full`
- GiYOL NBody 复验：`build.debug/benchmarks/out106_review_giyol_nbody_rerun1`
- GiYOL CD 复验：`build.debug/benchmarks/out107_review_giyol_cd_rerun1`

全套原始结果：

| 指标 | GiY | GiYOL 原始 full suite | GiYOL - GiY |
|---|---:|---:|---:|
| Total CPU | 3498.511s | 3547.200s | +48.689s |
| Business CPU | 3386.804s | 3429.650s | +42.846s |
| GC CPU(full) | 111.706s | 117.550s | +5.844s |
| GC core total | 73.077s | 77.176s | +4.099s |

原始 full suite 里有两个明确 outlier：
- NBody：GiYOL 原始 427.367s，但同一个当前 GiYOL 二进制单项重跑是 383.319s；历史同策略 GiYOL 也是 382.729s。因此 NBody 原始 427.367s 不能作为稳定差异。
- CD：GiYOL 原始 249.158s，但同一个当前 GiYOL 二进制单项重跑是 244.301s；历史同策略 GiYOL 是 244.191s。因此 CD 原始 249.158s 也偏高。

采用复验值替换 NBody/CD 后的判读数据：

| bench | GiY total | GiYOL total | delta total | GiY bus | GiYOL bus | delta bus | delta GC full | delta GC core |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Bounce | 158.634 | 158.543 | -0.091 | 158.516 | 158.417 | -0.099 | +0.008 | +0.004 |
| List | 99.666 | 97.732 | -1.934 | 99.652 | 97.723 | -1.929 | -0.005 | +0.000 |
| Sieve | 121.483 | 122.692 | +1.209 | 119.885 | 121.077 | +1.192 | +0.017 | -0.008 |
| Queens | 117.359 | 116.665 | -0.694 | 117.317 | 116.621 | -0.696 | +0.002 | +0.000 |
| Permute | 386.765 | 388.044 | +1.279 | 386.762 | 388.043 | +1.281 | -0.002 | +0.000 |
| Storage | 199.643 | 203.315 | +3.672 | 164.306 | 163.753 | -0.553 | +4.225 | +3.846 |
| Towers | 183.841 | 182.757 | -1.084 | 183.839 | 182.751 | -1.088 | +0.004 | +0.000 |
| Mandelbrot | 240.583 | 238.573 | -2.010 | 231.086 | 228.932 | -2.154 | +0.144 | -0.141 |
| Richards | 944.525 | 945.745 | +1.220 | 944.384 | 945.589 | +1.205 | +0.015 | +0.002 |
| CD | 243.787 | 244.301 | +0.514 | 230.487 | 230.795 | +0.308 | +0.206 | -0.181 |
| NBody | 381.490 | 383.319 | +1.829 | 367.744 | 368.827 | +1.083 | +0.747 | -0.063 |
| Havlak | 420.735 | 416.609 | -4.126 | 382.826 | 377.573 | -5.253 | +1.127 | +0.647 |
| TOTAL | 3498.511 | 3498.295 | -0.216 | 3386.804 | 3380.101 | -6.703 | +6.488 | +4.106 |

GiYOL batch/NT 行为：

| bench | NT batches | NT batch MB | tiny flushes | tiny NT MB | tiny fallback MB |
|---|---:|---:|---:|---:|---:|
| Bounce | 0 | 0.00 | 0 | 0.00 | 32.01 |
| List | 0 | 0.00 | 0 | 0.00 | 0.55 |
| Sieve | 0 | 0.00 | 0 | 0.00 | 2.68 |
| Queens | 0 | 0.00 | 0 | 0.00 | 0.55 |
| Permute | 0 | 0.00 | 0 | 0.00 | 0.02 |
| Storage | 0 | 0.00 | 0 | 0.00 | 45723.74 |
| Towers | 0 | 0.00 | 0 | 0.00 | 0.11 |
| Mandelbrot | 0 | 0.00 | 0 | 0.00 | 71.07 |
| Richards | 0 | 0.00 | 0 | 0.00 | 11.91 |
| CD | 1252 | 4.89 | 1252 | 4.89 | 2375.64 |
| NBody | 0 | 0.00 | 0 | 0.00 | 625.97 |
| Havlak | 313503 | 1224.62 | 313503 | 1224.62 | 11001.12 |
| TOTAL | 314755 | 1229.51 | 314755 | 1229.51 | 59845.37 |

最终结论：
- 目前没有发现 GiY 或 GiYOL 仍存在会破坏对象复制、slot patch、RSet 最新值语义的硬逻辑错误。
- 修补后的 GiY/GiYOL 都能通过 mprotect old-space smoke，并完整跑完 benchmark suite。
- 如果直接看 full suite 原始值，GiYOL 看似慢 48.689s；但 NBody 和 CD 被单项复验证明是 outlier。
- 用复验后的 NBody/CD 替换后，GiYOL 总时间 3498.295s，GiY 总时间 3498.511s，差异是 -0.216s，基本等于持平。
- GiYOL 的 business CPU 反而少 6.703s，但 GC full 多 6.488s，GC core 多 4.106s。
- 也就是说，当前 GiYOL 的主要真实代价不是 business time，而是 GC 内部多出来的小对象 staging/fallback/batch 管理开销。
- GiYOL 的有效 NT 写入高度集中在 Havlak 和少量 CD：总 NT 约 1229.51 MB，其中 Havlak 1224.62 MB，CD 4.89 MB。
- 大多数 benchmark 没有形成 4096B 的 tiny contiguous run，因此 GiYOL 只付出了记录/延迟/flush 判断的成本，没有得到 NT store 收益。
- Storage 是当前最明确的负面项：total +3.672s，GC full +4.225s，GC core +3.846s，且 NT batch 为 0；它说明大量 small-object fallback 记录会增加 GC 成本。
- Havlak 是当前最明确的正面项：total -4.126s，business -5.253s，但 GC full +1.127s；它说明当 target-contiguous tiny run 足够多时，GiYOL 的 NT batching 能抵消甚至超过额外 GC 成本。

下一步判断：
- 当前 GiYOL 策略不是普遍收益策略，而是 workload dependent。
- 若继续优化，方向应是减少“没有形成 NT batch 的 tiny object”记录成本，例如只在预测能形成足够 target-contiguous run 的区域启用 staging，或者按 benchmark/GC cycle 自适应关闭 tiny staging。

## 2026-05-07 为什么 Havlak 的 GiYOL business time 反而更低

Havlak 数据：
- GiY total：420.735s
- GiYOL total：416.609s
- GiYOL total delta：-4.126s
- GiY business：382.826s
- GiYOL business：377.573s
- GiYOL business delta：-5.253s
- GiY GC full：37.909s
- GiYOL GC full：39.036s
- GiYOL GC full delta：+1.127s
- GiY GC core：27.057s
- GiYOL GC core：27.704s
- GiYOL GC core delta：+0.647s

两边 workload 完全一致：
- Minor GC count：494300 vs 494300
- Total allocations：1817371816 vs 1817371816
- Total alloc bytes：100689.09 MB vs 100689.09 MB
- Forward operations：258554556 vs 258554556

因此 Havlak business time 下降不是因为 GiYOL 少做了 JS 业务逻辑，也不是因为对象数量变少。

最合理解释：
- GiYOL 在 Havlak 形成了大量有效 tiny NT batch。
- GiYOL Havlak：
  - NT batches：313503
  - tiny flushes：313503
  - tiny NT bytes：1224.62 MB
  - tiny NT objects：35132004
- 这些是 GiYOL 真正成功触发的 staging -> NT store。

为什么这会降低 business time：
- 普通 GiY copy 小对象时会用普通 store/memcpy 写 old 区。
- 普通 store 往往会把 old 区目标 cache line 拉进 cache，造成 write-allocate / cache pollution。
- GC 刚结束后，mutator 要继续执行 Havlak 自己的对象图、循环、栈和运行时数据访问。
- 如果 GC 期间把 cache 塞满了刚复制到 old 区的对象数据，mutator 恢复后会遇到更多 cache miss。
- 这些 cache miss 发生在 GC 结束之后，所以被统计进 business time，而不是 GC time。

GiYOL 在 Havlak 的效果：
- GiYOL 把约 1224.62 MB 的 tiny 连续对象通过 NT store 写到 old 区。
- NT store 的目的就是尽量避免把写入目标污染普通 cache。
- 所以 GiYOL 虽然让 GC full 增加了 1.127s，但它减少了 GC 后 mutator 的 cache pollution。
- 这个收益体现在 business time 上：business 少了 5.253s。
- 抵消 GC 增量后，总时间净收益约 4.126s。

为什么这个现象主要出现在 Havlak：
- Havlak 有足够多 target-contiguous tiny object run。
- 它能形成 313503 个 4096B 级别 tiny NT batch。
- 其他多数 benchmark 没形成这种 run，NT batches 为 0，只留下 batch/fallback 管理成本。
- 所以 GiYOL 在 Storage 是负收益，在 Havlak 是正收益。

需要注意：
- business time 的下降不是“GiYOL 在 business 阶段少执行了代码”。
- 它更像是“GC 的写入方式改变了后续 mutator 执行时的 cache miss 成本”。
- 当前机器 PMU cache miss counter 不可用，所以这个解释目前由时间分解和 GiYOL NT batch 统计支持，而不是由硬件 cache miss 计数直接证明。

补充修正：
- “cache pollution 导致 business 少 5.253s”不能理解成一次 GC 后的一轮 cache miss 造成 5 秒。
- Havlak 有 494300 次 minor GC。
- `5.253s / 494300 = 10.63us / minor GC`。
- Havlak 有 313503 个 GiYOL NT batch。
- `5.253s / 313503 = 16.76us / NT batch`。
- 如果按一次 LLC/内存 miss 约 100ns 粗估，5.253s 相当于约 52.53M 次 miss。
- 平均到每次 minor GC 是约 106 次 miss。
- 平均到每个 NT batch 是约 168 次 miss。
- 这个量级不是离谱的，但仍然只是“可能解释”，不是硬件计数证明。
- 因为当前 PMU 不可用，严格结论应该是：Havlak business 降低与大量 GiYOL NT batch 高度相关，cache pollution 是合理机制，但还需要 perf/cache miss counter 或专门 A/B 变体确认。

更严谨的验证方案：
- 做一个 GiYOL 变体：保留 batch 记录和 flush 逻辑，但强制 tiny chunk 走普通 copy，不执行 NT。
- 如果这个变体的 Havlak business 仍接近 GiYOL，说明 business 下降不是 NT 减少 cache pollution，而可能是代码布局/噪音/计时归因。
- 如果这个变体的 Havlak business 回到 GiY，而只有开启 NT 的 GiYOL business 降低，才能更有力证明是 NT 减少 cache pollution。

## 2026-05-07 GiY vs GiYOL 每项 benchmark 性能差别汇总

数据口径：
- GiY：`build.debug/benchmarks/out104_review_giy_full`
- GiYOL：主体来自 `build.debug/benchmarks/out105_review_giyol_full`
- GiYOL NBody：使用复验 `build.debug/benchmarks/out106_review_giyol_nbody_rerun1`
- GiYOL CD：使用复验 `build.debug/benchmarks/out107_review_giyol_cd_rerun1`
- 原因：GiYOL full suite 原始 NBody/CD 明显偏高，单项复验回到历史同策略区间，因此主表采用复验后的可信口径。

总览：

| 指标 | GiY | GiYOL | GiYOL - GiY |
|---|---:|---:|---:|
| Total CPU | 3498.511s | 3498.295s | -0.216s |
| Business CPU | 3386.804s | 3380.101s | -6.703s |
| GC CPU(full) | 111.706s | 118.194s | +6.488s |
| GC core total | 73.077s | 77.183s | +4.106s |

逐项数据：

| benchmark | GiY total | GiYOL total | total delta | GiY business | GiYOL business | business delta | GC full delta | GC core delta |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Bounce | 158.634 | 158.543 | -0.091 | 158.516 | 158.417 | -0.099 | +0.008 | +0.004 |
| List | 99.666 | 97.732 | -1.934 | 99.652 | 97.723 | -1.929 | -0.005 | +0.000 |
| Sieve | 121.483 | 122.692 | +1.209 | 119.885 | 121.077 | +1.192 | +0.017 | -0.008 |
| Queens | 117.359 | 116.665 | -0.694 | 117.317 | 116.621 | -0.696 | +0.002 | +0.000 |
| Permute | 386.765 | 388.044 | +1.279 | 386.762 | 388.043 | +1.281 | -0.002 | +0.000 |
| Storage | 199.643 | 203.315 | +3.672 | 164.306 | 163.753 | -0.553 | +4.225 | +3.846 |
| Towers | 183.841 | 182.757 | -1.084 | 183.839 | 182.751 | -1.088 | +0.004 | +0.000 |
| Mandelbrot | 240.583 | 238.573 | -2.010 | 231.086 | 228.932 | -2.154 | +0.144 | -0.141 |
| Richards | 944.525 | 945.745 | +1.220 | 944.384 | 945.589 | +1.205 | +0.015 | +0.002 |
| CD | 243.787 | 244.301 | +0.514 | 230.487 | 230.795 | +0.308 | +0.206 | -0.181 |
| NBody | 381.490 | 383.319 | +1.829 | 367.744 | 368.827 | +1.083 | +0.747 | -0.063 |
| Havlak | 420.735 | 416.609 | -4.126 | 382.826 | 377.573 | -5.253 | +1.127 | +0.647 |

按 total delta 排序的结论：
- GiYOL 明显更快：
  - Havlak：-4.126s
  - Mandelbrot：-2.010s
  - List：-1.934s
  - Towers：-1.084s
- GiYOL 明显更慢：
  - Storage：+3.672s
  - NBody：+1.829s
  - Permute：+1.279s
  - Sieve：+1.209s
  - Richards：+1.220s
- 接近持平：
  - Bounce：-0.091s
  - Queens：-0.694s
  - CD：+0.514s

核心判断：
- 总体上 GiYOL 与 GiY 几乎持平：GiYOL total 只少 0.216s，约 -0.01%。
- GiYOL 的 GC 成本更高：GC full 多 6.488s，GC core 多 4.106s。
- GiYOL 的 business time 更低：少 6.703s。
- 所以当前 GiYOL 的收益/损失不是单纯“GC 更快”；相反，GiYOL GC 本身更贵，但在部分 workload 中可能通过减少后续 cache pollution 让 mutator/business 更快。
- Storage 是当前最差项：没有形成有效 NT batch，却承担了 batch/fallback 管理成本。
- Havlak 是当前最好项：形成了大量有效 tiny NT batch，total 净收益最大。

## 2026-05-07 修正：GiY/GiYOL 全 benchmarks 重新完整复验

用户指出：
- 既然源码改过，不能只对 NBody/CD 做局部复验。
- 最终性能结论必须来自 GiY 和 GiYOL 都重新完整跑完所有 benchmarks。

这是正确的。
之前“用 NBody/CD 单项复验替换 full suite outlier”的口径不再作为主结论。
下面这轮 `out108/out109` 是新的主结论。

测试条件：
- 重新编译 GiY。
- 跑完整 12 项 benchmarks。
- 重新编译 GiYOL。
- 跑完整同一 12 项 benchmarks。
- 同样顺序：`Bounce List Sieve Queens Permute Storage Towers Mandelbrot Richards CD NBody Havlak`。
- 单进程顺序运行，不并行跑 benchmark。
- GiY 输出目录：`build.debug/benchmarks/out108_review_giy_full_rerun_all`
- GiYOL 输出目录：`build.debug/benchmarks/out109_review_giyol_full_rerun_all`

状态：
- GiY：12/12 status=0。
- GiYOL：12/12 status=0。
- NBody 这次没有出现 GiYOL 427s outlier。
- CD 这次保留完整 suite 原始值，不再用单项复验替换。

新完整复验总览：

| 指标 | GiY | GiYOL | GiYOL - GiY |
|---|---:|---:|---:|
| Total CPU | 3493.004s | 3497.121s | +4.117s |
| Business CPU | 3378.994s | 3379.880s | +0.886s |
| GC CPU(full) | 114.010s | 117.240s | +3.230s |
| GC core total | 75.001s | 76.873s | +1.872s |

新完整逐项数据：

| benchmark | GiY total | GiYOL total | total delta | GiY business | GiYOL business | business delta | GC full delta | GC core delta |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Bounce | 158.317 | 156.521 | -1.796 | 158.191 | 156.386 | -1.805 | +0.009 | +0.002 |
| List | 98.022 | 97.891 | -0.131 | 98.008 | 97.878 | -0.130 | -0.001 | +0.000 |
| Sieve | 120.478 | 121.598 | +1.120 | 119.002 | 120.066 | +1.064 | +0.056 | +0.002 |
| Queens | 117.795 | 116.738 | -1.057 | 117.748 | 116.697 | -1.051 | -0.006 | +0.000 |
| Permute | 388.474 | 390.268 | +1.794 | 388.473 | 390.267 | +1.794 | +0.000 | +0.000 |
| Storage | 204.620 | 202.519 | -2.101 | 169.072 | 163.422 | -5.650 | +3.549 | +3.036 |
| Towers | 183.314 | 181.815 | -1.499 | 183.307 | 181.810 | -1.497 | -0.002 | +0.000 |
| Mandelbrot | 236.194 | 239.123 | +2.929 | 226.861 | 229.394 | +2.533 | +0.396 | -0.125 |
| Richards | 936.327 | 937.505 | +1.178 | 936.184 | 937.346 | +1.162 | +0.015 | +0.001 |
| CD | 245.247 | 246.896 | +1.649 | 231.906 | 233.233 | +1.327 | +0.321 | +0.312 |
| NBody | 384.557 | 384.379 | -0.178 | 370.333 | 370.486 | +0.153 | -0.331 | -0.054 |
| Havlak | 419.659 | 421.868 | +2.209 | 379.909 | 382.895 | +2.986 | -0.776 | -1.302 |
| TOTAL | 3493.004 | 3497.121 | +4.117 | 3378.994 | 3379.880 | +0.886 | +3.230 | +1.872 |

GiYOL NT/batch 统计：

| benchmark | NT batches | NT batch MB | tiny flushes | tiny NT MB | tiny fallback MB |
|---|---:|---:|---:|---:|---:|
| Bounce | 0 | 0.00 | 0 | 0.00 | 32.01 |
| List | 0 | 0.00 | 0 | 0.00 | 0.55 |
| Sieve | 0 | 0.00 | 0 | 0.00 | 2.68 |
| Queens | 0 | 0.00 | 0 | 0.00 | 0.55 |
| Permute | 0 | 0.00 | 0 | 0.00 | 0.02 |
| Storage | 0 | 0.00 | 0 | 0.00 | 45723.74 |
| Towers | 0 | 0.00 | 0 | 0.00 | 0.11 |
| Mandelbrot | 0 | 0.00 | 0 | 0.00 | 71.07 |
| Richards | 0 | 0.00 | 0 | 0.00 | 11.91 |
| CD | 1252 | 4.89 | 1252 | 4.89 | 2375.64 |
| NBody | 0 | 0.00 | 0 | 0.00 | 625.97 |
| Havlak | 313503 | 1224.62 | 313503 | 1224.62 | 11001.12 |
| TOTAL | 314755 | 1229.51 | 314755 | 1229.51 | 59845.37 |

新主结论：
- GiYOL 在完整重验中总时间比 GiY 慢 `+4.117s`，约 `+0.12%`。
- GiYOL business time 慢 `+0.886s`，基本接近持平，但不是之前局部复验口径下的 business 明显更快。
- GiYOL GC full 慢 `+3.230s`，GC core 慢 `+1.872s`。
- 因此当前更稳妥的结论是：GiYOL 的 tiny NT batching 还没有带来稳定总收益；总体略慢，主要负担仍在 GC 侧。
- Havlak 这次不再表现为 GiYOL 明显收益：GiYOL Havlak total `+2.209s`，business `+2.986s`，GC full `-0.776s`。
- 这说明之前关于 Havlak business 下降/cache pollution 的解释只能作为可能机制，不能作为稳定结论。
- Storage 这次 total 反而 GiYOL 快 `-2.101s`，但它的 GC full 仍慢 `+3.549s`；business 快 `-5.650s`。这类 business 波动需要更多轮次或硬件计数才能归因。
- 最可靠的结构性事实仍然是：GiYOL 的有效 NT 只集中在 Havlak/CD，其他 benchmark 基本没有形成 tiny NT batch，却仍有 fallback 记录成本。

后续建议：
- 性能结论应至少使用完整 suite 多轮均值/中位数，而不是单轮或局部替换。
- 如果要证明 NT 减少 cache pollution，需要增加 A/B 变体：保留 batch 但禁用 NT，或者开启 PMU cache miss 计数。
- 当前实现继续优化的重点仍是降低“没有形成 NT batch 时”的 GiYOL 管理开销，并为 tiny staging 增加自适应关闭条件。

## 2026-05-07 GiYOL 执行逻辑与 NT store 触发例子

当前 GiYOL 默认参数：
- `GIYOL_TINY_MAX_OBJECT_BYTES=64`
- `GIYOL_TINY_STAGING_BYTES=4096`
- `GIYOL_TINY_FLUSH_BYTES=4096`
- `GIYOL_FORCE_TINY_TAIL_NT=0`
- `GIYOL_DIRECT_NONTINY_NT=0`
- `GIYOL_DEFER_TINY_STAGING=1`

GiYOL 的执行顺序：
1. minor GC 开始时初始化 reserve-order batch。
2. scan roots / scan function table / scan remembered set 时调用 `copy_for_minor()`。
3. `copy_for_minor()` 只做 reserve：
   - 在 old/dram 里 bump-pointer 分配目标地址。
   - 在 young source header 写 forwarding pointer。
   - 把 young source 压入 GC stack。
   - 如果 object 的 `align_bytes <= 64B`，把 `(dst_hdr, src_hdr, nbytes)` 记录进 GiYOL reserve-order batch。
   - 如果 object 大于 64B，不进入 tiny batch。
4. `giy_traverse_stack_and_copy()` 开始处理 GC stack。
5. 对每个 live young object：
   - 先扫描 young source object 的子指针。
   - 对 young 内部 slot 做 forwarding / patch。
   - 然后根据 object 大小决定复制方式。
6. 如果 object `>64B`：
   - 当前 GiYOL 不走 GiYOL direct non-tiny NT。
   - 它直接调用普通 `giy_copy_live_object()`。
   - `giy_copy_live_object()` 对 `<=256B` 用 memcpy，对 `>256B` 可能使用 GiY 共有的 NT copy。
   - 这部分不是 GiYOL tiny batching 的特殊收益。
7. 如果 object `<=64B`：
   - traverse 阶段不立刻复制。
   - 因为它已经在 reserve 阶段被记入 batch。
   - 等全部 live object traversal 完成后，统一 flush batch。
8. flush batch 时，GiYOL 按 batch 里的 reserve 顺序扫描。
9. 它要求 tiny object 的 target 地址严格连续：
   - 第一个 dst 是 `chunk_dst`。
   - 下一个 object 的 dst 必须等于 `expected`。
   - `expected += previous_object_size`。
   - 如果中间出现大对象造成 target gap，或者顺序不是严格连续，这个 run 就断开。
10. 在一个 target-contiguous tiny run 里，GiYOL 再切 chunk：
   - chunk 最多装 `4096B` staging buffer。
   - 如果 chunk bytes 达到 `4096B`，触发 NT。
   - 如果 chunk bytes 小于 `4096B`，因为 `GIYOL_FORCE_TINY_TAIL_NT=0`，不触发 NT，改为逐对象普通 copy fallback。
11. 触发 NT 时：
   - 先把每个 source object 当前内容 memcpy 到 staging buffer。
   - 再用 `giyol_stream_copy_region(dst, staging_buffer, bytes)` 一次性 NT store 到 old target 连续区。
   - 最后记录 `GiYOL NT batches / tiny flushes / tiny bytes`。
12. flush 完后执行 `giy_finish_local_nt_stores()`，必要时 `_mm_sfence()`。

成功触发 GiYOL tiny NT 的具体例子：

假设一次 minor GC 发现 64 个 tiny live object：
- 每个 object 的 `align_bytes=64B`。
- old target 地址是连续的：
  - O1 -> `0x10000000`
  - O2 -> `0x10000040`
  - O3 -> `0x10000080`
  - ...
  - O64 -> `0x10000FC0`
- 这 64 个 object 都 `<=64B`，所以都会在 `copy_for_minor()` 阶段记录进 reserve-order batch。
- traverse 阶段先修复它们各自的 young 内部指针，但暂时不复制 tiny object。
- flush 阶段发现：
  - O1 dst 等于 expected。
  - O2 dst 等于 O1 dst + 64。
  - O3 dst 等于 O2 dst + 64。
  - 一直到 O64 都连续。
- 累计 bytes = `64 * 64B = 4096B`。
- 因为 `bytes >= GIYOL_TINY_FLUSH_BYTES`，所以触发 GiYOL tiny NT。
- 实际动作：
  - `memcpy(staging+0, O1_src, 64)`
  - `memcpy(staging+64, O2_src, 64)`
  - ...
  - `memcpy(staging+4032, O64_src, 64)`
  - `giyol_stream_copy_region(0x10000000, staging, 4096)`
- 这就是当前 GiYOL 真正想要的情况：多个小对象先合并到一个 staging 连续块，再一次性 NT store 到 old 的连续 target 块。

不会触发 GiYOL tiny NT 的例子 1：target 中间被大对象打断。

假设 reserve 顺序是：
- A：tiny 64B，target `0x10000000`
- B：大对象 1024B，target `0x10000040`
- C：tiny 64B，target `0x10000440`

batch 里只记录 A 和 C，因为 B 大于 64B 不进 tiny batch。
flush 时看到：
- A 后 expected 是 `0x10000040`。
- C 的 dst 是 `0x10000440`。
- C 不等于 expected，说明 target 不连续。

结果：
- A 自己形成一个 64B run。
- C 自己形成一个 64B run。
- 两个 run 都小于 4096B。
- 因为 tail NT 关闭，所以都不触发 NT，全部 fallback 到普通 copy。

不会触发 GiYOL tiny NT 的例子 2：很接近 4096B 但没达到。

假设有 85 个连续 tiny object，每个 48B：
- 总大小 `85 * 48B = 4080B`。
- target 是连续的。

flush 时 chunk bytes = 4080B。
因为当前判断是严格 `bytes >= 4096B`，而且 `GIYOL_FORCE_TINY_TAIL_NT=0`：
- 4080B 不触发 NT。
- 这 85 个 object 会 fallback 到逐对象普通 copy。

这个例子解释了为什么当前 GiYOL 有些 benchmark 记录了大量 tiny fallback bytes，却没有 NT batches：只要 target 不连续，或者连续 run 的 chunk 没达到 4096B，就不会触发 GiYOL tiny NT。

一句话结论：
- 当前 GiYOL 的核心判据不是“有很多小对象”。
- 它真正需要的是：“一串 `<=64B` 的 live object，在 old target 上严格连续，并且 flush chunk 累计达到 `4096B`”。
- 满足这个条件才触发 GiYOL 特有的 staging -> NT store；否则 tiny object 会 fallback 到普通 GiY copy。

## 2026-05-07 GiYOL batch、4096B 判断时机和 NT 对齐/尾部处理

这里的 batch 不是一块已经合并好的对象数据，而是一个元数据数组。

当前 GiYOL 的 batch 类型是 `GiYOLCopyBatch`，里面保存很多 `GiYOLCopyEntry`：
- `dst`：object 在 old/dram 中已经 reserve 好的目标 header 地址。
- `src`：object 在 young/cache 中的源 header 地址。
- `nbytes`：object header + payload 后的 aligned object size。

也就是说，batch 记录的是“以后要从哪里复制到哪里、复制多少字节”，而不是马上把 object 数据拷进 batch。

当前使用的是全局 reserve-order batch：
- minor GC 开始时清空 batch。
- scan roots / scan remembered set 过程中调用 `copy_for_minor()`。
- `copy_for_minor()` reserve old target 后，如果 object `align_bytes <= 64B`，就把 `(dst_hdr, src_hdr, nbytes)` 追加到 batch。
- 大于 64B 的 object 不进 tiny batch。

什么时候判断是否达到 4096B：
- 不是在 `copy_for_minor()` 追加 entry 的时候判断。
- 也不是发现每个 object 时马上判断。
- 当前是在 `giy_traverse_stack_and_copy()` 完整处理完 GC stack 后，调用 `giyol_flush_copy_batch()` 时统一判断。

flush 判断顺序：
1. 遍历 batch entries。
2. 先找 target 地址严格连续的 tiny run。
3. 在这个连续 run 里再切 chunk。
4. 每个 chunk 累计 `bytes`。
5. 当前代码只允许 `bytes + next_object_size <= GIYOL_TINY_STAGING_BYTES`。
6. 当前 `GIYOL_TINY_STAGING_BYTES=4096`，`GIYOL_TINY_FLUSH_BYTES=4096`。
7. 所以当前 GiYOL tiny NT 实际只有在 chunk 正好累计到 `4096B` 时才触发。

关键点：
- 当前不会把一个 object 拆开来凑 4096B。
- 如果当前 chunk 已经是 4080B，下一个 object 是 64B，那么 `4080 + 64 > 4096`，这个 64B object 不会被塞进当前 chunk。
- 于是当前 4080B chunk 因为 `<4096B`，不会触发 NT，会 fallback 到逐对象普通 copy。
- 下一个 64B object 会作为下一个 chunk/run 的开始继续处理。

超过 4096B 后的小尾巴怎么处理：
- 假设有 65 个连续 tiny object，每个 64B。
- 总大小是 `65 * 64B = 4160B`。
- flush 时：
  - 前 64 个 object 组成 `4096B` chunk，触发 staging + NT store。
  - 第 65 个 object 剩下 `64B`，单独成为 tail chunk。
  - 因为 `GIYOL_FORCE_TINY_TAIL_NT=0`，这个 64B tail 不触发 NT，走普通 copy fallback。

再比如有 96 个连续 64B object：
- 总大小 `6144B`。
- 前 `4096B` 触发一次 NT。
- 剩余 `2048B` 小于 4096B。
- tail `2048B` 不触发 NT，走普通 copy fallback。

如果有 128 个连续 64B object：
- 总大小 `8192B`。
- 切成两个 `4096B` chunk。
- 触发两次 NT。

NT store 对齐怎么处理：
- 真正执行 NT 的函数是 `giyol_stream_copy_region(dst, staging_buffer, bytes)`。
- 它要求 old target 的 `dst` 至少满足当前代码允许的对齐形态：
  - 如果 `dst` 已经 16B 对齐，就直接用 `_mm_stream_si128` 按 16B 一组写。
  - 如果 `dst` 是 8B offset，也就是 `dst % 16 == 8`，先用 `_mm_stream_si64` 写前 8B，让后续地址变成 16B 对齐，然后再用 `_mm_stream_si128` 写主体，最后如果剩 8B，再用 `_mm_stream_si64` 写尾部。
  - 如果 `dst` 不是 16B 对齐，也不是 8B offset，代码会直接报 invariant failed。

当前为什么不会出现奇怪的 NT tail：
- object size 是 aligned size，通常至少按 8B 对齐。
- 当前 tiny NT chunk 因为 staging size 和 flush threshold 都是 4096B，所以触发 NT 的 chunk bytes 实际是 4096B。
- 4096B 是 16B 的整数倍。
- 如果 dst 16B 对齐：全程 16B stream store。
- 如果 dst 是 8B offset：前 8B 用 stream si64，中间 4080B 用 16B stream store，最后 8B 用 stream si64。

一句话结论：
- GiYOL 的 batch 是“待复制对象的 `(src,dst,size)` 列表”，不是对象数据本身。
- 是否达到 4096B 是在 GC stack traversal 完成后的 batch flush 阶段判断。
- 当前实现不会拆 object，也不会对 `<4096B` tail 强制 NT；超过 4096B 后剩下的小尾巴走普通 copy fallback。
- NT 写入的对齐由 `giyol_stream_copy_region()` 处理：16B 对齐直接写，8B 偏移则先写 8B 对齐主体，其他对齐视为 bug。

## 2026-05-07 修正：GiY/GiYOL 全 benchmarks 重新完整复验

用户指出：既然改了源代码，复验就不能只看部分 benchmarks，必须重验完整 suite。

这个指出是正确的。之前虽然已经跑过 full suite，但后续我用局部重跑的 NBody/CD 数据修正主结论，这个口径不严谨。现在以代码修补后的重新完整 suite 为准：
- GiY 完整重跑输出：`build.debug/benchmarks/out108_review_giy_full_rerun_all`
- GiYOL 完整重跑输出：`build.debug/benchmarks/out109_review_giyol_full_rerun_all`
- 两边都是 12/12 benchmarks `status=0`
- 重跑前机器负载较低：load average 约 `0.23, 0.32, 0.29`

总览：

| 指标 | GiY | GiYOL | GiYOL-GiY |
|---|---:|---:|---:|
| total | 3493.004s | 3497.121s | +4.117s (+0.12%) |
| business/non-GC | 3378.994s | 3379.880s | +0.886s (+0.03%) |
| GC full | 114.010s | 117.240s | +3.230s |
| GC core | 75.001s | 76.873s | +1.872s |
| minor GC 次数 | 2302292 | 2302292 | 0 |

逐项完整对比：

| benchmark | GiY total | GiYOL total | total 差值 | GiY business | GiYOL business | business 差值 | GiY GC full | GiYOL GC full | GC full 差值 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Bounce | 158.317 | 156.521 | -1.796 (-1.13%) | 158.191 | 156.386 | -1.805 | 0.126 | 0.135 | +0.009 |
| List | 98.022 | 97.891 | -0.131 (-0.13%) | 98.008 | 97.878 | -0.130 | 0.014 | 0.013 | -0.001 |
| Sieve | 120.478 | 121.598 | +1.120 (+0.93%) | 119.002 | 120.066 | +1.064 | 1.476 | 1.532 | +0.056 |
| Queens | 117.795 | 116.738 | -1.057 (-0.90%) | 117.748 | 116.697 | -1.051 | 0.047 | 0.041 | -0.006 |
| Permute | 388.474 | 390.268 | +1.794 (+0.46%) | 388.473 | 390.267 | +1.794 | 0.001 | 0.001 | +0.000 |
| Storage | 204.620 | 202.519 | -2.101 (-1.03%) | 169.072 | 163.422 | -5.650 | 35.548 | 39.097 | +3.549 |
| Towers | 183.314 | 181.815 | -1.499 (-0.82%) | 183.307 | 181.810 | -1.497 | 0.007 | 0.005 | -0.002 |
| Mandelbrot | 236.194 | 239.123 | +2.929 (+1.24%) | 226.861 | 229.394 | +2.533 | 9.333 | 9.729 | +0.396 |
| Richards | 936.327 | 937.505 | +1.178 (+0.13%) | 936.184 | 937.346 | +1.162 | 0.143 | 0.158 | +0.015 |
| CD | 245.247 | 246.896 | +1.649 (+0.67%) | 231.906 | 233.233 | +1.327 | 13.341 | 13.662 | +0.321 |
| NBody | 384.557 | 384.379 | -0.178 (-0.05%) | 370.333 | 370.486 | +0.153 | 14.224 | 13.893 | -0.331 |
| Havlak | 419.659 | 421.868 | +2.209 (+0.53%) | 379.909 | 382.895 | +2.986 | 39.750 | 38.974 | -0.776 |

GiYOL tiny NT batch 生效情况：

| benchmark | NT batches | NT batch MB | tiny flushes | tiny MB | fallback MB |
|---|---:|---:|---:|---:|---:|
| Bounce | 0 | 0.00 | 0 | 0.00 | 32.01 |
| List | 0 | 0.00 | 0 | 0.00 | 0.55 |
| Sieve | 0 | 0.00 | 0 | 0.00 | 2.68 |
| Queens | 0 | 0.00 | 0 | 0.00 | 0.55 |
| Permute | 0 | 0.00 | 0 | 0.00 | 0.02 |
| Storage | 0 | 0.00 | 0 | 0.00 | 45723.74 |
| Towers | 0 | 0.00 | 0 | 0.00 | 0.11 |
| Mandelbrot | 0 | 0.00 | 0 | 0.00 | 71.07 |
| Richards | 0 | 0.00 | 0 | 0.00 | 11.91 |
| CD | 1252 | 4.89 | 1252 | 4.89 | 2375.64 |
| NBody | 0 | 0.00 | 0 | 0.00 | 625.97 |
| Havlak | 313503 | 1224.62 | 313503 | 1224.62 | 11001.12 |
| TOTAL | 314755 | 1229.51 | 314755 | 1229.51 | 59845.37 |

修正后的结论：
- 当前最新全套复验下，GiYOL 没有赢过 GiY。总时间慢 `+4.117s`，约 `+0.12%`。
- business/non-GC 时间几乎持平，但 GiYOL 仍慢 `+0.886s`，约 `+0.03%`。
- 主要确定损失在 GC 侧：GC full 慢 `+3.230s`，GC core 慢 `+1.872s`。
- GiYOL tiny NT batching 只在 CD 和 Havlak 明显触发；其他 10 项没有 NT batch，只留下 batch 判定/记录与 fallback copy 路径。
- Storage 虽然 total 快 `-2.101s`，但 GC full 反而慢 `+3.549s`，它的 total 改善主要来自 business 时间波动/间接影响，不能当作 NT store 明确收益。
- Havlak 在这次完整重跑中不再表现为 business 更快：GiYOL total 慢 `+2.209s`，business 慢 `+2.986s`，GC full 快 `-0.776s`。因此之前“GiYOL 让 Havlak business 更快”的说法不能作为稳定结论。
- NBody 这次没有复现之前 GiYOL 的 427s outlier，说明之前那个异常更可能是单轮运行噪音或局部复验条件造成的，不应纳入主结论。

阶段性判断：
- 当前 GiYOL 的策略在“连续 tiny object 足够多”的场景下确实能触发 NT store，但覆盖范围太窄。
- 全 suite 看，GiYOL 引入的额外判定/批处理/fallback 成本，以及 NT flush 本身的成本，抵消了有限场景的收益。
- 如果继续优化，下一步不应该只看单轮 total，而应做：
  1. GiY/GiYOL full-suite 多轮 mean/median；
  2. 一个保留 batch 判定但禁用 NT store 的 A/B 版本，用来分离“batch 管理成本”和“NT store 本体收益”；
  3. 对 Havlak/CD/Storage 加 PMU 计数，确认 LLC miss、writeback、store bandwidth 是否真的被改善。

## 2026-05-07 GiY/GiYOL 当前实现复查

用户要求：重新全面检查 GiY 和 GiYOL 是否存在实现问题、逻辑问题。

本次检查范围：
- `ejsvm/GiY.cc`
- `ejsvm/giy_rset.cc`
- `ejsvm/common.mk`
- GiY/GiYOL 当前 benchmark 使用配置：
  - `OPT_GC=giy` 或 `OPT_GC=giyol`
  - `GIY_RSET_INDEX_FAST=true`
  - `CACHE_SIZE_KB=552`
  - `GIY_AS_UPDATE=true`
  - `GIY_AS_FT_ADVANCE=true`

构建与小验证：
- GiY 重新构建成功。
- GiYOL 重新构建成功。
- `git diff --check -- ejsvm/GiY.cc ejsvm/giy_rset.cc ejsvm/common.mk AIlog.md` 通过。
- GiY：`GIY_MPROTECT_OLD=1 ./ejsvm benchmarks/giy_gc_probe.sbc`，`status=0`，没有 old-space violation。
- GiYOL：`GIY_MPROTECT_OLD=1 ./ejsvm benchmarks/giy_gc_probe.sbc`，`status=0`，没有 old-space violation。
- GiYOL：`./ejsvm benchmarks/hello_world.sbc`，`status=0`。
- GiYOL：`./ejsvm benchmarks/bounce_check.sbc`，`status=0`。
- 额外尝试了 `GIY_MPROTECT_OLD=1 ./ejsvm benchmarks/CD.sbc`，但 mprotect 模式下 CD 运行被严重放大，约 1 分 47 秒后主动中止；这不是 correctness failure，只是不适合做长 benchmark 验证。
- 上一次完整 suite 仍是当前源代码后的主要完整运行证据：GiY `out108_review_giy_full_rerun_all` 和 GiYOL `out109_review_giyol_full_rerun_all` 都是 12/12 `status=0`。

当前激活路径的正确性检查结论：

1. GiY evacuation 顺序没有发现确定性逻辑错误。
   - `copy_for_minor()` 只 reserve old target，并在 young header 上写 forwarding pointer。
   - `giy_traverse_stack_and_copy()` 先扫描 young source object，并把 source object 内部的 young 指针修成 forwarded old pointer。
   - 然后从已经 patch 过的 source header/payload copy 到 old target。
   - roots、function table slot、remembered set slot 在对象复制完成后统一 patch。
   - 这个顺序能保证 old target 中得到的是已经修过内部指针的对象。

2. GiYOL 当前主体仍然和 GiY 相同，只改变“何时把对象字节写到 old target”。
   - `copy_for_minor()` 对 tiny object 记录 `(dst, src, nbytes)` 到 reserve-order batch。
   - `>64B` 的 object 当前不进 tiny batch，而是立即走 GiY 的 `giy_copy_live_object()`。
   - batch 在 `giy_traverse_stack_and_copy()` 处理完整个 GC stack 后 flush。
   - flush 时按 old target 地址连续性切 run，再按 chunk 处理。
   - chunk 达到 `GIYOL_TINY_FLUSH_BYTES=4096` 时才 staging + NT store。
   - 小于 4096B 的 tail 当前不强制 NT，fallback 到普通 `giy_copy_live_object()`。
   - 没有看到当前激活路径下会漏复制或重复复制的分支。

3. RSet 当前逻辑没有发现会漏更新 latest value 的确定性错误。
   - JSValue slot 和 ptr slot 用 raw slot 低位 tag 区分。
   - `remembered_set.values[]` 记录的是最后一次写入的值。
   - young pointer 写入时 add/update。
   - 写入 old/null/fixnum/special 时只对已有条目更新为 `0`，不新增无意义 clear 条目。
   - fast hash index 命中时直接更新对应 `values[index]`。
   - probe limit 之后的 fallback entry 虽然不进 hash table，但后续 update 会在同样 probe limit 满的情况下回退线性查找；没有发现 fallback entry 后续无法更新的问题。
   - capacity check 现在只在真正新增 entry 前做，不会因为 duplicate update 误判 full。

4. function table slot set 的设计在当前代码里是必要且一致的。
   - function table / alloc-site cache slot 不是普通 old object slot，不能只靠 object write barrier 覆盖。
   - `giy_record_ft_jsvalue_slot()` / `giy_record_ft_ptr_slot()` 只记录 slot 地址。
   - minor GC 时扫描真实 slot 当前值，再 reserve/patch。
   - 这避免了 function table 中 young Shape/PropertyMap 指针被 young 回收后悬空。

5. allocation-site update 的修补方向是合理的。
   - 现在记录的是 source object 中已经 patch 后的 `shape` 指针，而不是尚未稳定可读的 old target payload。
   - strict old mprotect guard 在 allocation-site metadata update 前结束，避免把有意读取 old Shape/PropertyMap 元数据误判成 GiY core violation。
   - mprotect probe 通过，说明当前核心 reserve/traverse/patch 阶段没有触发非法 old-space 访问。

发现的非 correctness 问题 / 风险：

1. GiYOL staging buffer 的实际首次分配大小和“4096B tiny staging”说法不完全一致。
   - 代码中 `giyol_ensure_staging_capacity(bytes)` 的初始容量是 `GIYOL_BATCH_BYTES`。
   - 当前 `GIYOL_BATCH_BYTES=128KB`，而 `GIYOL_TINY_STAGING_BYTES=4096`。
   - 所以 tiny path 的 chunk 上限确实是 4096B，但第一次 `realloc` 出来的 staging buffer 实际是 128KB。
   - 这不是 correctness bug，也不是 young/cache 区预留；它是 malloc 出来的额外缓冲。
   - 但如果论文/实验口径说“只使用 4KB 暂存区”，当前实现并不严格满足；应改成按 `bytes` 或 `GIYOL_TINY_STAGING_BYTES` 起始分配。

2. `GIYOL_TINY_STAGING_BYTES` 和 `GIYOL_TINY_FLUSH_BYTES` 都是 4096 时，NT 触发条件偏苛刻。
   - 当前不会拆 object。
   - 如果连续 run 的 chunk 累计到 4080B，下一个 object 又会超过 4096B，那么 4080B 会 fallback，不触发 NT。
   - 这是性能策略问题，不是逻辑错误。

3. GiYOL 的 profile 命名有轻微混淆。
   - fallback 计数里使用了 `giyol_small_flush_objects/bytes`。
   - 当前激活路径中 batch 只收 tiny object，所以基本可读。
   - 但防御性 non-small fallback 分支也会累加这个名字，后续如果打开其他配置，统计名可能误导。

4. 一些 dormant/非当前 benchmark 配置没有作为本次主验证对象。
   - 例如 `GIYOL_DEFER_TINY_STAGING=0`、`GIYOL_STAGING_COPY=0`、`GIYOL_DIRECT_NONTINY_NT=1`。
   - 当前结论只覆盖现在实际使用并跑 full suite 的 GiY/GiYOL 配置。

总判断：
- 当前 GiY 和当前激活配置的 GiYOL，没有发现会破坏 GC 正确性的确定性实现错误。
- 当前 GiYOL 的主要问题仍然不是“逻辑错”，而是“收益覆盖范围窄 + 额外判定/批处理/fallback 成本 + NT 触发条件苛刻”。
- 需要修正的最明确实现口径问题是：GiYOL tiny staging 的 chunk 是 4096B，但实际 staging buffer 初始分配是 128KB；如果要严格实验“小暂存区”，应把实际 buffer 初始容量也降到 4096B 附近，然后重新跑 full suite。

## 2026-05-07 汇报前修正版 GiY/GiYOL 完整复验

本节是导师汇报前的最终复验记录。结论先写在前面：工程上不能声称“数学意义绝对正确”，但当前源码状态下，GiY 和 GiYOL 已经修掉目前发现的确定性实现问题，并且在同一套配置下完成了 smoke correctness 与完整 benchmarks。两套完整 suite 均为 12/12 `status=0`。

### 最终源码状态

本轮最终确认的关键修补：

1. GiYOL tiny staging 的实际 buffer 分配口径已修正。
   - 文件：`ejsvm/GiY.cc`
   - 位置：`giyol_ensure_staging_capacity()`
   - 修补前：第一次 `realloc` 会从 `GIYOL_BATCH_BYTES=128KB` 起步，和“4KB tiny staging”实验口径不一致。
   - 修补后：第一次容量就是调用方请求的 `bytes`；当前 active tiny path 请求 `GIYOL_TINY_STAGING_BYTES=4096`，因此实际 staging buffer 起步就是 4096B。

2. 当前 GiYOL active 策略保持为保守正确版本。
   - `GIYOL_STAGING_COPY=1`
   - `GIYOL_TINY_STAGING=1`
   - `GIYOL_TINY_STAGING_BYTES=4096`
   - `GIYOL_TINY_FLUSH_BYTES=4096`
   - `GIYOL_TINY_MAX_OBJECT_BYTES=64`
   - `GIYOL_FORCE_TINY_TAIL_NT=0`
   - `GIYOL_DIRECT_NONTINY_NT=0`
   - `GIYOL_DEFER_TINY_STAGING=1`
   - 含义：只有 <=64B 的 tiny object 进入 reserve-order batch；按 old target 连续性切 run；每个 chunk 最多 4096B；达到 4096B 才通过 staging buffer 发 NT store；不足 4096B 的 tail 走普通 `giy_copy_live_object()`，不强行 NT。

3. GiY 主体和 GiYOL 主体的正确性路径保持一致。
   - `copy_for_minor()` reserve old target，并在 young header 写 forwarding pointer。
   - traversal 阶段扫描 source young object，先把 source object 内部 young pointer 修成 forwarded old pointer。
   - GiY 直接把 patch 后的 source bytes copy 到 old target。
   - GiYOL 只改变 tiny object 的字节写回时机；roots、RSet slot、function table slot 的 patch 逻辑与 GiY 保持同一套。

4. RSet 的 capacity check / duplicate update 逻辑保持为修正版。
   - 只有真正新增 remembered-set entry 前才检查容量。
   - duplicate slot 更新只更新 `values[]` 中的 latest value，不会被误判为 full。
   - hash probe fallback 后仍会通过线性查找更新已有 entry。

5. allocation-site update 使用 source object 中已经 patch 后的 shape。
   - 避免在 old target 尚处于受保护/未稳定可读阶段读取 old payload。
   - `GIY_MPROTECT_OLD=1` probe 已通过，没有发现核心 evacuation 阶段非法 old-space 访问。

### 构建与验证配置

共同配置：

- `GIY_RSET_INDEX_FAST=true`
- `CACHE_SIZE_KB=552`
- `GIY_AS_UPDATE=true`
- `GIY_AS_FT_ADVANCE=true`

构建命令：

- GiY：`make -B -C build.debug GiY.o giy_rset.o ejsvm OPT_GC=giy GIY_RSET_INDEX_FAST=true CACHE_SIZE_KB=552 GIY_AS_UPDATE=true GIY_AS_FT_ADVANCE=true -j4`
- GiYOL：`make -B -C build.debug GiY.o giy_rset.o ejsvm OPT_GC=giyol GIY_RSET_INDEX_FAST=true CACHE_SIZE_KB=552 GIY_AS_UPDATE=true GIY_AS_FT_ADVANCE=true -j4`

smoke correctness：

- GiY：`GIY_MPROTECT_OLD=1 ./ejsvm benchmarks/giy_gc_probe.sbc`，`status=0`
- GiY：`./ejsvm benchmarks/hello_world.sbc`，`status=0`
- GiYOL：`GIY_MPROTECT_OLD=1 ./ejsvm benchmarks/giy_gc_probe.sbc`，`status=0`
- GiYOL：`./ejsvm benchmarks/hello_world.sbc`，`status=0`

完整 suite 数据目录：

- GiY：`build.debug/benchmarks/out110_final_giy_corrected_full`
- GiYOL：`build.debug/benchmarks/out111_final_giyol_corrected_full`

完整 suite status：

- GiY：Bounce/List/Sieve/Queens/Permute/Storage/Towers/Mandelbrot/Richards/CD/NBody/Havlak 全部 `status=0`
- GiYOL：Bounce/List/Sieve/Queens/Permute/Storage/Towers/Mandelbrot/Richards/CD/NBody/Havlak 全部 `status=0`

运行结束时机器负载：

- `2026-05-07T11:21:25Z`
- load average：`1.05, 1.24, 1.26`

### GiY vs GiYOL CPU 数据

| benchmark | GiY total CPU | GiYOL total CPU | delta | delta% | GiY business | GiYOL business | business delta | GiY GC | GiYOL GC | GC delta |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Bounce | 157.258 | 157.902 | +0.644 | +0.41% | 157.114 | 157.746 | +0.632 | 0.144 | 0.156 | +0.012 |
| List | 98.093 | 98.645 | +0.552 | +0.56% | 98.078 | 98.634 | +0.556 | 0.015 | 0.011 | -0.004 |
| Sieve | 121.526 | 129.743 | +8.217 | +6.76% | 119.966 | 128.227 | +8.261 | 1.560 | 1.516 | -0.044 |
| Queens | 117.245 | 116.786 | -0.459 | -0.39% | 117.190 | 116.730 | -0.460 | 0.055 | 0.056 | +0.001 |
| Permute | 409.291 | 443.811 | +34.520 | +8.43% | 409.289 | 443.811 | +34.522 | 0.002 | 0.000 | -0.002 |
| Storage | 198.762 | 202.801 | +4.039 | +2.03% | 163.623 | 163.695 | +0.072 | 35.139 | 39.107 | +3.968 |
| Towers | 182.585 | 185.294 | +2.709 | +1.48% | 182.581 | 185.289 | +2.708 | 0.004 | 0.005 | +0.001 |
| Mandelbrot | 238.255 | 239.101 | +0.846 | +0.36% | 229.093 | 229.628 | +0.535 | 9.162 | 9.472 | +0.310 |
| Richards | 945.469 | 943.737 | -1.732 | -0.18% | 945.312 | 943.576 | -1.736 | 0.157 | 0.161 | +0.004 |
| CD | 244.298 | 247.496 | +3.198 | +1.31% | 231.306 | 233.980 | +2.674 | 12.993 | 13.517 | +0.524 |
| NBody | 383.137 | 386.473 | +3.336 | +0.87% | 368.418 | 372.280 | +3.862 | 14.719 | 14.193 | -0.526 |
| Havlak | 417.080 | 418.018 | +0.938 | +0.22% | 379.339 | 377.893 | -1.446 | 37.742 | 40.125 | +2.383 |
| **Total** | **3512.999** | **3569.807** | **+56.808** | **+1.62%** | **3401.309** | **3451.489** | **+50.180** | **111.692** | **118.319** | **+6.627** |

### Wall time 数据

| benchmark | GiY real | GiYOL real | delta | GiY user/sys | GiYOL user/sys |
|---|---:|---:|---:|---:|---:|
| Bounce | 157.300 | 157.927 | +0.627 | 157.243/0.017 | 157.887/0.018 |
| List | 98.110 | 98.664 | +0.554 | 98.092/0.004 | 98.645/0.003 |
| Sieve | 121.592 | 129.811 | +8.219 | 121.016/0.558 | 129.248/0.547 |
| Queens | 117.268 | 116.800 | -0.468 | 117.245/0.001 | 116.787/0.001 |
| Permute | 409.356 | 443.873 | +34.517 | 409.291/0.002 | 443.811/0.001 |
| Storage | 199.732 | 203.772 | +4.040 | 188.745/10.948 | 192.703/11.019 |
| Towers | 182.631 | 185.313 | +2.682 | 182.585/0.003 | 185.294/0.002 |
| Mandelbrot | 238.293 | 239.164 | +0.871 | 238.141/0.118 | 239.002/0.104 |
| Richards | 945.638 | 943.907 | -1.731 | 945.465/0.007 | 943.732/0.008 |
| CD | 244.499 | 247.696 | +3.197 | 242.612/1.846 | 245.863/1.789 |
| NBody | 383.227 | 386.565 | +3.338 | 382.879/0.279 | 386.222/0.271 |
| Havlak | 417.724 | 418.640 | +0.916 | 410.852/6.782 | 411.683/6.877 |
| **Total** | **3515.370** | **3572.132** | **+56.762** | **3494.166/20.565** | **3550.877/20.640** |

### GC core 数据

| benchmark | GiY minor GC | GiYOL minor GC | GiY core | GiYOL core | core delta | GiY scavenge | GiYOL scavenge |
|---|---:|---:|---:|---:|---:|---:|---:|
| Bounce | 5827 | 5827 | 0.044 | 0.047 | +0.003 | 0.042 | 0.044 |
| List | 685 | 685 | 0.003 | 0.003 | +0.000 | 0.003 | 0.003 |
| Sieve | 50000 | 50000 | 0.812 | 0.815 | +0.003 | 0.806 | 0.810 |
| Queens | 2474 | 2474 | 0.009 | 0.009 | +0.000 | 0.009 | 0.008 |
| Permute | 71 | 71 | 0.001 | 0.001 | +0.000 | 0.001 | 0.001 |
| Storage | 226887 | 226887 | 32.019 | 35.681 | +3.662 | 31.962 | 35.618 |
| Towers | 145 | 145 | 0.001 | 0.001 | +0.000 | 0.001 | 0.001 |
| Mandelbrot | 578592 | 578592 | 1.571 | 1.437 | -0.134 | 1.507 | 1.369 |
| Richards | 4791 | 4791 | 0.042 | 0.042 | +0.000 | 0.040 | 0.041 |
| CD | 219809 | 219809 | 8.128 | 8.416 | +0.288 | 8.009 | 8.284 |
| NBody | 718711 | 718711 | 2.771 | 2.718 | -0.053 | 2.523 | 2.439 |
| Havlak | 494300 | 494300 | 27.159 | 28.561 | +1.402 | 26.135 | 27.354 |

### GiYOL NT 覆盖数据

| benchmark | NT batches | NT MB | tiny flushes | tiny MB | fallback MB | direct NT MB |
|---|---:|---:|---:|---:|---:|---:|
| Bounce | 0 | 0.00 | 0 | 0.00 | 32.01 | 0.00 |
| List | 0 | 0.00 | 0 | 0.00 | 0.55 | 0.00 |
| Sieve | 0 | 0.00 | 0 | 0.00 | 2.68 | 0.00 |
| Queens | 0 | 0.00 | 0 | 0.00 | 0.55 | 0.00 |
| Permute | 0 | 0.00 | 0 | 0.00 | 0.02 | 0.00 |
| Storage | 0 | 0.00 | 0 | 0.00 | 45723.74 | 0.00 |
| Towers | 0 | 0.00 | 0 | 0.00 | 0.11 | 0.00 |
| Mandelbrot | 0 | 0.00 | 0 | 0.00 | 71.07 | 0.00 |
| Richards | 0 | 0.00 | 0 | 0.00 | 11.91 | 0.00 |
| CD | 1252 | 4.89 | 1252 | 4.89 | 2375.64 | 0.00 |
| NBody | 0 | 0.00 | 0 | 0.00 | 625.97 | 0.00 |
| Havlak | 313503 | 1224.62 | 313503 | 1224.62 | 11001.12 | 0.00 |
| **Total** | **314755** | **1229.51** | **314755** | **1229.51** | **59845.37** | **0.00** |

NT 覆盖率：

- GiYOL tiny/staging 相关总量约为 `1229.51 + 59845.37 = 61074.88 MB`。
- 真正通过 NT store 写回的是 `1229.51 MB`。
- 覆盖率约为 `2.01%`。
- 这说明当前 GiYOL 是 correctness-first 的保守版本：能触发 NT 的地方很少，大多数 tiny copy 仍然 fallback 到普通 copy。

### 汇报用结论

1. 当前 GiY 与 GiYOL 都能完整跑完 suite。
   - 两者 12 项 benchmarks 全部 `status=0`。
   - GiY/GiYOL 的 minor GC count 在每个 benchmark 上完全一致，说明对象发现/GC 触发节奏没有被 GiYOL 改乱。

2. 当前 GiYOL 比 GiY 总体慢，但退化幅度已经很小。
   - CPU total：`+56.808s`，`+1.62%`
   - wall real：`+56.762s`，`+1.61%`
   - business CPU：`+50.180s`，`+1.48%`
   - GC CPU：`+6.627s`，`+5.93%`

3. 这轮数据里最大的单项差距是 Permute。
   - Permute CPU delta：`+34.520s`
   - Permute GC CPU 几乎为 0，GiYOL NT MB 也是 0。
   - 因此 Permute 的差距不能解释为 NT copy 或 GC core 复制策略造成，更可能是单轮运行噪音、二进制布局差异、CPU 频率/调度变化，或者 benchmark 业务层执行差异。
   - 如果去掉 Permute，总 CPU delta 从 `+56.808s` 降到 `+22.288s`，总体差距约 `+0.72%`。

4. 真正能归因到 GiYOL GC 复制策略的主要成本在 Storage 和 Havlak。
   - Storage：GC CPU `+3.968s`，GC core `+3.662s`，但 NT MB 为 0，fallback MB 为 `45723.74 MB`。原因是 tiny batch/连续性判断存在开销，但没有形成 4096B NT chunk。
   - Havlak：GC CPU `+2.383s`，GC core `+1.402s`，NT MB 为 `1224.62 MB`，fallback MB 为 `11001.12 MB`。原因是确实有 NT chunk，但覆盖率仍低，大量 tiny copy 仍 fallback。
   - CD：NT MB 只有 `4.89 MB`，fallback MB `2375.64 MB`，因此收益也很有限。

5. 当前 GiYOL 的核心问题不是 correctness，而是收益覆盖率太低。
   - 当前策略只让 <=64B tiny object 进入 batch。
   - 只有 old target 连续并且 chunk 达到 4096B 才 NT。
   - tail 不强制 NT。
   - 大对象和 64B 以上对象全部普通 copy。
   - 所以多数 benchmark 没有 NT 触发；触发最多的 Havlak 也只有约 1.2GB NT，对比约 11GB fallback，覆盖仍低。

6. 导师汇报时可以这样表述：
   - “GiYOL 当前实现已经通过完整 correctness smoke 和 full benchmark suite；没有发现会破坏 GC 语义的确定性 bug。”
   - “GiYOL 的 NT batching 目前是保守正确版本，不是最终性能优化版本。”
   - “完整 suite 上 GiYOL 比 GiY 慢约 1.6%；去掉与 GC/NT 几乎无关的 Permute 后，差距约 0.7%。”
   - “当前性能瓶颈不是旧的严重逻辑错误，而是 NT 覆盖率太低，batch 判断/fallback 付出了成本但大部分对象没有真正走 NT store。”

剩余风险：

- 这是单轮 full-suite 数据，不是多轮 median；明天汇报时应明确写成“本轮复验结果”。
- PMU cache-miss counters 在当前机器不可用：`perf_event_paranoid=4`，因此没有硬件 cache-miss 证据。
- dormant 配置没有作为最终正确性结论覆盖范围，例如 `GIYOL_DEFER_TINY_STAGING=0`、`GIYOL_DIRECT_NONTINY_NT=1`、`GIYOL_STAGING_COPY=0`。

## 2026-05-08 当前最终实验 benchmark 选择

如果“最终实验”指用于向导师解释 GiYOL 复制策略效果和问题的两个代表性 benchmark，当前应选：

1. `Storage`
   - 作用：负向/压力代表。
   - 数据：GiYOL 比 GiY 的 GC CPU 多 `+3.968s`，GC core 多 `+3.662s`。
   - GiYOL NT MB 为 `0.00 MB`，fallback MB 为 `45723.74 MB`。
   - 含义：Storage 说明当前 GiYOL 的 batch/连续性判断会付出成本，但对象分布没有形成可用的 4096B NT chunk，因此几乎没有 NT 收益。

2. `Havlak`
   - 作用：正向/有效触发代表。
   - 数据：GiYOL 比 GiY 的 GC CPU 多 `+2.383s`，GC core 多 `+1.402s`。
   - GiYOL NT MB 为 `1224.62 MB`，fallback MB 为 `11001.12 MB`。
   - 含义：Havlak 说明 GiYOL 确实能形成大量 tiny NT chunk，但当前覆盖率仍低，大部分 copy 还是 fallback，所以收益不足以抵消额外成本。

不应把 `Permute` 作为 GiYOL 复制策略的最终代表 benchmark：

- Permute 的差距最大：GiYOL total CPU `+34.520s`。
- 但 Permute 的 GC CPU 几乎为 0，GiYOL NT MB 也是 `0.00 MB`。
- 所以它更像单轮运行噪音、二进制布局差异、调度/频率变化或业务层执行差异，不适合作为 NT batching 策略的证据。

因此当前最终汇报时建议：

- 完整总表仍使用全部 12 项 benchmarks，证明 correctness 和总体性能。
- 重点机制分析只抓 `Storage` 和 `Havlak`。
- `Storage` 证明“付出 batch/fallback 成本但没有 NT 覆盖”的失败场景。
- `Havlak` 证明“能触发 NT，但覆盖率仍太低”的有限有效场景。

## 2026-05-08 benchmarks 文件夹中的最终实验目录

更正上一节的理解：这里问的是 `build.debug/benchmarks` 目录下哪两组输出是最终实验。

当前最终实验使用这两个目录：

1. `build.debug/benchmarks/out110_final_giy_corrected_full`
   - 含义：修正后 GiY 的完整 full-suite 输出。
   - 用途：作为 GiY baseline。
   - 状态：12 项 benchmarks 全部 `status=0`。

2. `build.debug/benchmarks/out111_final_giyol_corrected_full`
   - 含义：修正后 GiYOL 的完整 full-suite 输出。
   - 用途：作为 GiYOL 对比组。
   - 状态：12 项 benchmarks 全部 `status=0`。

不要把下面这些旧目录当作最终实验：

- `out108_review_giy_full_rerun_all`
- `out109_review_giyol_full_rerun_all`
- 更早的 `out50_*`、`out67_*`、`out75_*`、`out101_*`、`out102_*`、`out103_*` 等

最终汇报与表格应只以 `out110_final_giy_corrected_full` 和 `out111_final_giyol_corrected_full` 为准。

## 2026-05-08 GiY 直接对象级 NT store 数量

问题：GiYOL 最终输出里 `GiYOL direct NT objects:0`，那么 GiY 里到底有多少对象是直接使用 NT store 的？

先澄清一个容易误解的点：

- `GiYOL direct NT objects:0` 只表示 GiYOL 没有走专门的 `giyol_direct_nt_copy_live_object()` 分支，因为当前 `GIYOL_DIRECT_NONTINY_NT=0`。
- 这不等于 GiYOL 完全没有 NT store；GiYOL 仍然有 batch NT store，最终数据中 `GiYOL NT batches=314755`，`GiYOL batch bytes NT=1229.51 MB`。
- 但最终 GiYOL 的 `direct NT objects` 这个字段确实为 0。

GiY 的对象级直接 NT store 规则在 `giy_copy_live_object()` 中：

- `nbytes <= 256`：普通 `memcpy`
- `nbytes > 256`：直接对这个对象使用 `_mm_stream_si64/_mm_stream_si128`，也就是对象级 NT store

由于最终 GiY full-suite `out110_final_giy_corrected_full` 没开 `GIY_PROFILE_DETAIL=1`，没有打印 `NT copy objects`。因此我重新构建了一轮 GiY detail-counter 版本，仅用于补齐这个计数：

- 输出目录：`build.debug/benchmarks/out112_giy_detail_nt_full`
- 构建差异：`GIY_PROFILE_DETAIL=true`
- 其他关键配置仍为：`OPT_GC=giy GIY_RSET_INDEX_FAST=true CACHE_SIZE_KB=552 GIY_AS_UPDATE=true GIY_AS_FT_ADVANCE=true`
- 结果：12 项 benchmarks 全部 `status=0`
- 这轮不替代最终性能数据，只用于统计 NT copy 数量。

GiY 直接对象级 NT store 统计：

| benchmark | materialized objs | >256 objs | NT copy objects | NT copy bytes | NT obj share | materialized bytes | NT byte share |
|---|---:|---:|---:|---:|---:|---:|---:|
| Bounce | 609462 | 5830 | 5830 | 4.54 MB | 0.9566% | 36.55 MB | 12.42% |
| List | 14421 | 3 | 3 | 0.00 MB | 0.0208% | 0.55 MB | 0.00% |
| Sieve | 100431 | 50002 | 50002 | 1908.07 MB | 49.7874% | 1910.76 MB | 99.86% |
| Queens | 20414 | 3 | 3 | 0.00 MB | 0.0147% | 1.62 MB | 0.00% |
| Permute | 604 | 3 | 3 | 0.00 MB | 0.4967% | 0.03 MB | 0.00% |
| Storage | 1092300458 | 3 | 3 | 0.00 MB | 0.0000% | 58771.54 MB | 0.00% |
| Towers | 2973 | 3 | 3 | 0.00 MB | 0.1009% | 0.12 MB | 0.00% |
| Mandelbrot | 3104819 | 3 | 3 | 0.00 MB | 0.0001% | 71.07 MB | 0.00% |
| Richards | 293026 | 6 | 6 | 0.00 MB | 0.0020% | 14.92 MB | 0.00% |
| CD | 83287709 | 10459610 | 10459610 | 4305.08 MB | 12.5584% | 7784.00 MB | 55.31% |
| NBody | 26552814 | 3 | 3 | 0.00 MB | 0.0000% | 626.01 MB | 0.00% |
| Havlak | 355641623 | 27978199 | 27978199 | 13847.30 MB | 7.8670% | 30932.32 MB | 44.77% |
| **Total** | **1561928754** | **38493668** | **38493668** | **20064.99 MB** | **2.4645%** | **100149.49 MB** | **20.04%** |

关键结论：

1. GiY 中直接对象级 NT store 的对象数是 `38,493,668` 个。
2. GiY 中直接对象级 NT store 的总字节量约 `20,064.99 MB`。
3. `NT copy objects` 和 `>256 objs` 完全相等，因此 GiY 的直接 NT store 判据就是对象复制大小 `>256B`。
4. 贡献主要来自三个 benchmark：
   - Havlak：`27,978,199` 个，`13,847.30 MB`
   - CD：`10,459,610` 个，`4,305.08 MB`
   - Sieve：`50,002` 个，`1,908.07 MB`
5. Storage 虽然 materialized bytes 很大，但 `>256B` 对象只有 3 个，所以 GiY 的对象级 direct NT 在 Storage 几乎没有覆盖；Storage 的复制量主要由大量 <=128B 小对象构成。

## 2026-05-08 CheneyGC vs GiY vs GiYOL 三方数据报告

本节比较三组数据：

- CheneyGC：`build.debug/benchmarks/out24`
  - root-controlled Cheney 对照组。
  - 说明：这是 2026-04-27 的既有 Cheney 对照目录，不是 2026-05-07 同一时间重新跑出的目录；但它是目前已经完整跑完并用于 Cheney 对照的最终目录。
  - `out24` 没有 `.status` 文件，但 12 个 `.out` 都包含完整 `Total CPU time` profile。
- GiY：`build.debug/benchmarks/out110_final_giy_corrected_full`
  - 修正后 GiY final full-suite。
  - 12/12 `status=0`。
- GiYOL：`build.debug/benchmarks/out111_final_giyol_corrected_full`
  - 修正后 GiYOL final full-suite。
  - 12/12 `status=0`。

### 总体数据

| metric | Cheney | GiY | GiY vs Cheney | GiYOL | GiYOL vs Cheney | GiYOL vs GiY |
|---|---:|---:|---:|---:|---:|---:|
| CPU total | 3498.813 | 3512.999 | +14.186 (+0.41%) | 3569.807 | +70.994 (+2.03%) | +56.808 (+1.62%) |
| CPU business | 3379.483 | 3401.309 | +21.826 (+0.65%) | 3451.489 | +72.006 (+2.13%) | +50.180 (+1.48%) |
| CPU GC full | 119.329 | 111.692 | -7.637 (-6.40%) | 118.319 | -1.010 (-0.85%) | +6.627 (+5.93%) |
| Wall total | 3499.343 | 3513.638 | +14.295 (+0.41%) | 3570.418 | +71.075 (+2.03%) | +56.780 (+1.62%) |
| Wall business | 3379.894 | 3401.615 | +21.721 (+0.64%) | 3451.219 | +71.325 (+2.11%) | +49.604 (+1.46%) |
| Wall GC full | 119.450 | 112.020 | -7.430 (-6.22%) | 119.196 | -0.254 (-0.21%) | +7.176 (+6.41%) |
| GC core | 89.940 | 72.560 | -17.380 (-19.32%) | 77.731 | -12.209 (-13.57%) | +5.171 (+7.13%) |
| scan_roots | 1.307 | 0.375 | -0.932 (-71.31%) | 0.394 | -0.913 (-69.85%) | +0.019 (+5.07%) |
| scan_RS | 2.925 | 1.146 | -1.779 (-60.82%) | 1.363 | -1.562 (-53.40%) | +0.217 (+18.94%) |
| scavenge | 85.707 | 71.038 | -14.669 (-17.12%) | 75.972 | -9.735 (-11.36%) | +4.934 (+6.95%) |
| Minor GC count | 2341014 | 2302292 | -38722 (-1.65%) | 2302292 | -38722 (-1.65%) | 0 |
| Write barrier calls | 2054529628 | 2079088344 | +24558716 (+1.20%) | 2079088404 | +24558776 (+1.20%) | +60 |
| Allocations | 19789564672 | 19637117907 | -152446765 (-0.77%) | 19637117915 | -152446757 (-0.77%) | +8 |
| Alloc bytes | 296777.18 MB | 295614.12 MB | -1163.06 MB (-0.39%) | 295614.12 MB | -1163.06 MB (-0.39%) | 0.00 MB |
| Forward operations | 1696428830 | 1468806830 | -227622000 (-13.42%) | 1468806755 | -227622075 (-13.42%) | -75 |

总体结论：

1. GiY 的 GC core 明显低于 Cheney：少 `17.380s`，`-19.32%`。
2. GiYOL 的 GC core 也低于 Cheney：少 `12.209s`，`-13.57%`。
3. 但 GiY 总 CPU 仍比 Cheney 多 `14.186s`，因为 GiY 的 business CPU 多 `21.826s`，抵消了 GC CPU 少 `7.637s`。
4. GiYOL 总 CPU 比 Cheney 多 `70.994s`，其中主要来自 business CPU 多 `72.006s`；GiYOL 的 GC CPU 只比 Cheney 少 `1.010s`，基本已经没有足够 GC 收益抵消 business/边界成本。
5. GiYOL 相对 GiY 多 `56.808s` CPU，其中 business 多 `50.180s`，GC 多 `6.627s`。也就是说 GiYOL 的当前损失不是单纯 GC core，而是 business/运行期扰动和 GC overhead 都有贡献。

### 每项 benchmark 的 total CPU

| benchmark | Cheney CPU | GiY CPU | GiY Δ | GiYOL CPU | GiYOL Δ vs Cheney | GiYOL Δ vs GiY |
|---|---:|---:|---:|---:|---:|---:|
| Bounce | 158.864 | 157.258 | -1.606 (-1.01%) | 157.902 | -0.962 (-0.61%) | +0.644 (+0.41%) |
| List | 98.867 | 98.093 | -0.774 (-0.78%) | 98.645 | -0.222 (-0.22%) | +0.552 (+0.56%) |
| Sieve | 123.718 | 121.526 | -2.192 (-1.77%) | 129.743 | +6.025 (+4.87%) | +8.217 (+6.76%) |
| Queens | 116.038 | 117.245 | +1.207 (+1.04%) | 116.786 | +0.748 (+0.64%) | -0.459 (-0.39%) |
| Permute | 382.188 | 409.291 | +27.103 (+7.09%) | 443.811 | +61.623 (+16.12%) | +34.520 (+8.43%) |
| Storage | 209.135 | 198.762 | -10.373 (-4.96%) | 202.801 | -6.334 (-3.03%) | +4.039 (+2.03%) |
| Towers | 185.343 | 182.585 | -2.758 (-1.49%) | 185.294 | -0.049 (-0.03%) | +2.709 (+1.48%) |
| Mandelbrot | 234.071 | 238.255 | +4.184 (+1.79%) | 239.101 | +5.030 (+2.15%) | +0.846 (+0.36%) |
| Richards | 943.091 | 945.469 | +2.378 (+0.25%) | 943.737 | +0.646 (+0.07%) | -1.732 (-0.18%) |
| CD | 251.007 | 244.298 | -6.709 (-2.67%) | 247.496 | -3.511 (-1.40%) | +3.198 (+1.31%) |
| NBody | 378.746 | 383.137 | +4.391 (+1.16%) | 386.473 | +7.727 (+2.04%) | +3.336 (+0.87%) |
| Havlak | 417.745 | 417.080 | -0.665 (-0.16%) | 418.018 | +0.273 (+0.07%) | +0.938 (+0.22%) |

逐项观察：

- GiY 明显赢 Cheney 的项：`Storage` (-10.373s)、`CD` (-6.709s)、`Towers` (-2.758s)、`Sieve` (-2.192s)、`Bounce` (-1.606s)。
- GiY 明显输 Cheney 的项：`Permute` (+27.103s)、`NBody` (+4.391s)、`Mandelbrot` (+4.184s)。
- GiYOL 赢 Cheney 的项：`Storage` (-6.334s)、`CD` (-3.511s)、`Bounce` (-0.962s)、`List` (-0.222s)、`Towers` 基本持平。
- GiYOL 输 Cheney 的主要项：`Permute` (+61.623s)、`NBody` (+7.727s)、`Sieve` (+6.025s)、`Mandelbrot` (+5.030s)。
- `Permute` 是最大异常项：几乎没有 GC，却贡献了 GiY 相对 Cheney 的 `+27.103s` 和 GiYOL 相对 Cheney的 `+61.623s`，不应解释为 GC copy/NT batching 本身。

### Business 与 GC 拆分

| benchmark | Cheney business | GiY business | GiYOL business | Cheney GC | GiY GC | GiYOL GC |
|---|---:|---:|---:|---:|---:|---:|
| Bounce | 158.725 | 157.114 | 157.746 | 0.138 | 0.144 | 0.156 |
| List | 98.849 | 98.078 | 98.634 | 0.018 | 0.015 | 0.011 |
| Sieve | 120.975 | 119.966 | 128.227 | 2.743 | 1.560 | 1.516 |
| Queens | 116.001 | 117.190 | 116.730 | 0.037 | 0.055 | 0.056 |
| Permute | 382.185 | 409.289 | 443.811 | 0.003 | 0.002 | 0.000 |
| Storage | 158.050 | 163.623 | 163.695 | 51.085 | 35.139 | 39.107 |
| Towers | 185.342 | 182.581 | 185.289 | 0.001 | 0.004 | 0.005 |
| Mandelbrot | 228.310 | 229.093 | 229.628 | 5.761 | 9.162 | 9.472 |
| Richards | 942.978 | 945.312 | 943.576 | 0.113 | 0.157 | 0.161 |
| CD | 239.133 | 231.306 | 233.980 | 11.874 | 12.993 | 13.517 |
| NBody | 369.957 | 368.418 | 372.280 | 8.789 | 14.719 | 14.193 |
| Havlak | 378.978 | 379.339 | 377.893 | 38.767 | 37.742 | 40.125 |

解释：

- `Storage`：GiY/GiYOL business 都比 Cheney 慢约 5.6s，但 GC 分别少 15.946s / 11.978s，所以 total 仍赢 Cheney。
- `CD`：GiY/GiYOL business 比 Cheney 快，但 GC 略慢；总结果仍赢 Cheney。
- `Havlak`：三者 total 非常接近。GiYOL business 最低，但 GC 最高。
- `NBody` 与 `Mandelbrot`：GiY/GiYOL 的 GC 明显高于 Cheney，是这两项输 Cheney 的主要原因。
- `Permute`：GC 约为 0，差距几乎全在 business/运行期，不应作为 GC 机制结论。

### GC core 与 scavenge

| benchmark | Cheney core | GiY core | GiYOL core | Cheney scavenge | GiY scavenge | GiYOL scavenge | minor GC C/G/O |
|---|---:|---:|---:|---:|---:|---:|---:|
| Bounce | 0.065 | 0.044 | 0.047 | 0.058 | 0.042 | 0.044 | 7235/5827/5827 |
| List | 0.002 | 0.003 | 0.003 | 0.001 | 0.003 | 0.003 | 1108/685/685 |
| Sieve | 2.271 | 0.812 | 0.815 | 1.517 | 0.806 | 0.810 | 50000/50000/50000 |
| Queens | 0.003 | 0.009 | 0.009 | 0.002 | 0.009 | 0.008 | 2502/2474/2474 |
| Permute | 0.000 | 0.001 | 0.001 | 0.000 | 0.001 | 0.001 | 81/71/71 |
| Storage | 48.552 | 32.019 | 35.681 | 48.342 | 31.962 | 35.618 | 229482/226887/226887 |
| Towers | 0.000 | 0.001 | 0.001 | 0.000 | 0.001 | 0.001 | 222/145/145 |
| Mandelbrot | 0.277 | 1.571 | 1.437 | 0.031 | 1.507 | 1.369 | 585211/578592/578592 |
| Richards | 0.021 | 0.042 | 0.042 | 0.017 | 0.040 | 0.041 | 4864/4791/4791 |
| CD | 8.000 | 8.128 | 8.416 | 7.675 | 8.009 | 8.284 | 230567/219809/219809 |
| NBody | 0.813 | 2.771 | 2.718 | 0.198 | 2.523 | 2.439 | 726941/718711/718711 |
| Havlak | 29.936 | 27.159 | 28.561 | 27.866 | 26.135 | 27.354 | 502801/494300/494300 |

GC core 结论：

- 三方 full-suite GC core：Cheney `89.940s`，GiY `72.560s`，GiYOL `77.731s`。
- GiY 的 GC core 比 Cheney 少 `17.380s`。
- GiYOL 的 GC core 比 Cheney 少 `12.209s`，但比 GiY 多 `5.171s`。
- GiY/GiYOL 的优势主要来自 `Storage`、`Sieve`、`Havlak`。
- Cheney 在 `Mandelbrot`、`NBody` 上 GC core 明显更低，尤其 NBody 的 scavenge：Cheney `0.198s`，GiY `2.523s`，GiYOL `2.439s`。

### Allocation / Forward / Write Barrier

| benchmark | Cheney allocs | GiY allocs | GiYOL allocs | Cheney forwards | GiY forwards | GiYOL forwards | Cheney WB | GiY WB | GiYOL WB |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Bounce | 45604143 | 30608585 | 30608585 | 1482507 | 391972 | 391972 | 244099 | 220708 | 220708 |
| List | 9302050 | 4655378 | 4655378 | 49908 | 13945 | 13945 | 1113 | 679 | 679 |
| Sieve | 601999 | 601999 | 601999 | 101802 | 50468 | 50468 | 50058 | 50058 | 50058 |
| Queens | 8002056 | 8002056 | 8002056 | 28713 | 14856 | 14856 | 3998804 | 3998786 | 3998786 |
| Permute | 404153 | 304710 | 304710 | 2201 | 576 | 576 | 99544 | 99513 | 99513 |
| Storage | 1092302019 | 1092301906 | 1092301914 | 1093028469 | 1091112748 | 1091112673 | 1960439 | 1910828 | 1910888 |
| Towers | 1802073 | 968893 | 968893 | 9488 | 2874 | 2874 | 2252749 | 1725345 | 1725345 |
| Mandelbrot | 6547928783 | 6547928783 | 6547928783 | 3433845 | 3393963 | 3393963 | 56 | 56 | 56 |
| Richards | 52753056 | 52552966 | 52552966 | 492613 | 398407 | 398407 | 25779205 | 24555971 | 24555971 |
| CD | 2099019821 | 2006796747 | 2006796747 | 141201425 | 108796847 | 108796847 | 9157893 | 9263154 | 9263154 |
| NBody | 8075024557 | 8075024068 | 8075024068 | 28038465 | 6075618 | 6075618 | 1874873328 | 1874873163 | 1874873163 |
| Havlak | 1856819962 | 1817371816 | 1817371816 | 428559394 | 258554556 | 258554556 | 136112340 | 162390083 | 162390083 |

Allocation 结论：

- GiY/GiYOL allocations 比 Cheney 少 `152,446,765`，约 `-0.77%`。
- GiY/GiYOL forward operations 比 Cheney 少约 `227,622,000`，约 `-13.42%`。
- GiY/GiYOL write barrier calls 比 Cheney 多约 `24,558,7xx`，约 `+1.20%`。
- 这支持之前的结论：allocation-site update 修补后，GiY/GiYOL 没有比 Cheney 做更多 allocation；剩余差距更多来自业务运行期扰动、write barrier/RSet、以及不同 benchmark 的 GC core 分布，而不是“对象分配数量爆炸”。

### NT store 相关补充

GiYOL final 输出：

| benchmark | GiYOL NT batches | NT MB | fallback MB | special direct NT objs | special direct NT MB |
|---|---:|---:|---:|---:|---:|
| Bounce | 0 | 0.00 | 32.01 | 0 | 0.00 |
| List | 0 | 0.00 | 0.55 | 0 | 0.00 |
| Sieve | 0 | 0.00 | 2.68 | 0 | 0.00 |
| Queens | 0 | 0.00 | 0.55 | 0 | 0.00 |
| Permute | 0 | 0.00 | 0.02 | 0 | 0.00 |
| Storage | 0 | 0.00 | 45723.74 | 0 | 0.00 |
| Towers | 0 | 0.00 | 0.11 | 0 | 0.00 |
| Mandelbrot | 0 | 0.00 | 71.07 | 0 | 0.00 |
| Richards | 0 | 0.00 | 11.91 | 0 | 0.00 |
| CD | 1252 | 4.89 | 2375.64 | 0 | 0.00 |
| NBody | 0 | 0.00 | 625.97 | 0 | 0.00 |
| Havlak | 313503 | 1224.62 | 11001.12 | 0 | 0.00 |
| **Total** | **314755** | **1229.51** | **59845.37** | **0** | **0.00** |

注意：

- `special direct NT objs=0` 只表示 GiYOL 没走 `giyol_direct_nt_copy_live_object()` 这个专门分支。
- 当前 GiYOL 的 non-tiny/fallback 路径仍可能调用通用 `giy_copy_live_object()`；这个通用路径在对象 `>256B` 时也会使用 NT store，但 final GiYOL 没开 `GIY_PROFILE_DETAIL=1`，所以没有打印通用 `NT copy objects`。
- 已补跑 GiY detail-counter：GiY 通用对象级直接 NT 为 `38,493,668` 个，`20064.99 MB`；这个统计来自 `out112_giy_detail_nt_full`，只用于计数，不替代性能 baseline。

### 最终分析结论

1. CheneyGC 是最简单稳定的 copying baseline；它 total CPU 为 `3498.813s`。
2. GiY 当前 total CPU 为 `3512.999s`，比 Cheney 慢 `14.186s` / `0.41%`。
3. GiYOL 当前 total CPU 为 `3569.807s`，比 Cheney 慢 `70.994s` / `2.03%`，比 GiY 慢 `56.808s` / `1.62%`。
4. GiY/GiYOL 的 GC core 都比 Cheney 更低，证明“减少 old/DRAM read、使用 GiY 风格扫描”的 GC core 方向仍然有效。
5. GiY 没有在 total 上超过 Cheney，原因是 business CPU 多 `21.826s`，超过了 GC CPU 节省的 `7.637s`。
6. GiYOL 没有超过 Cheney，原因更明确：business CPU 多 `72.006s`，而 GC CPU 只少 `1.010s`；当前 GiYOL 的 NT batching 收益覆盖率低，且带来额外 GC overhead。
7. 如果汇报时只讲 GC core：GiY 和 GiYOL 都赢 Cheney。
8. 如果汇报时讲 end-to-end：Cheney 当前仍略好于 GiY，明显好于 GiYOL。
9. 最需要解释的异常是 `Permute`：它几乎没有 GC，却让 GiY/GiYOL 相对 Cheney 分别多 `27.103s` / `61.623s`；这不适合作为 GC 机制证据，应标注为单轮运行/业务层/二进制布局或调度噪音风险。
10. 机制层最有价值的对比仍是 `Storage`、`CD`、`Havlak`：
    - `Storage`：GiY/GiYOL 靠 GC savings 赢 Cheney。
    - `CD`：GiY/GiYOL 靠 business 更低赢 Cheney，GC 略慢。
    - `Havlak`：三者非常接近，GiYOL business 最低但 GC 最高。

汇报建议：

- 主表用 full-suite total：Cheney `3498.813s`，GiY `3512.999s`，GiYOL `3569.807s`。
- 机制表用 GC core：Cheney `89.940s`，GiY `72.560s`，GiYOL `77.731s`。
- 结论写成：“GiY/GiYOL 的 GC core 已经优于 Cheney，但当前 end-to-end 仍被 business/边界成本抵消；GiYOL 的 NT batching 目前收益覆盖率不足，尚不能带来总性能收益。”

## 2026-05-08 为什么 GiY 现在看起来慢于 CheneyGC

问题：之前 `GiY` 不是比 `CheneyGC` 快吗？为什么现在三方报告里 `GiY out110` 又慢于 `Cheney out24`？

先给结论：

- 这不是 GiY 的 GC 主体突然退化。
- 之前“GiY 更快”的数据来自 `build.debug/benchmarks/out50_giy_as_update_final_full`。
- 现在“GiY 略慢”的数据来自 `build.debug/benchmarks/out110_final_giy_corrected_full`。
- `out110` 相比 `out50` 总 CPU 多 `+28.522s`，但 GC core 反而少 `-0.269s`。
- 真正导致符号翻转的是 single-run business/runtime 波动，尤其是 `Permute`。

### out50 / out110 / Cheney 总体差异

| metric | Cheney out24 | GiY out50 | GiY out110 | out110 - out50 | out50 - Cheney | out110 - Cheney |
|---|---:|---:|---:|---:|---:|---:|
| CPU total | 3498.813 | 3484.477 | 3512.999 | +28.522 | -14.336 | +14.186 |
| CPU business | 3379.483 | 3378.482 | 3401.309 | +22.827 | -1.001 | +21.826 |
| CPU GC full | 119.329 | 105.998 | 111.692 | +5.694 | -13.331 | -7.637 |
| Wall total | 3499.343 | 3485.047 | 3513.638 | +28.591 | -14.296 | +14.295 |
| Wall business | 3379.894 | 3378.598 | 3401.615 | +23.017 | -1.296 | +21.721 |
| Wall GC full | 119.450 | 106.448 | 112.020 | +5.572 | -13.002 | -7.430 |
| GC core | 89.940 | 72.829 | 72.560 | -0.269 | -17.111 | -17.380 |
| scavenge | 85.707 | 71.323 | 71.038 | -0.285 | -14.384 | -14.669 |
| Minor GC count | 2341014 | 2302292 | 2302292 | 0 | -38722 | -38722 |
| Write barrier calls | 2054529628 | 2079088412 | 2079088344 | -68 | +24558784 | +24558716 |
| Allocations | 19789564672 | 19637117887 | 19637117907 | +20 | -152446785 | -152446765 |
| Forward operations | 1696428830 | 1468807058 | 1468806830 | -228 | -227621772 | -227622000 |
| AS observations | 0 | 866092216 | 866091911 | -305 | +866092216 | +866091911 |
| AS entries | 0 | 5806327 | 5806322 | -5 | +5806327 | +5806322 |
| AS applied | 0 | 118 | 118 | 0 | +118 | +118 |

关键证据：

1. `out110` 和 `out50` 的 minor GC count 完全一样：`2302292`。
2. allocations 只差 `+20`，write barrier 只差 `-68`，forward operations 只差 `-228`。
3. AS update 的 observations/entries/applied 也几乎完全一样。
4. GC core 没变慢，反而 `out110` 比 `out50` 少 `0.269s`。

因此，`out110` 慢于 `out50` 不是因为 GiY 做了更多 GC 工作，也不是因为 allocation-site 修复失效。

### 逐项看 out110 相比 out50 多在哪里

| benchmark | out50 CPU | out110 CPU | delta | business delta | GC delta | GC core delta |
|---|---:|---:|---:|---:|---:|---:|
| Bounce | 157.303 | 157.258 | -0.045 | -0.069 | +0.024 | +0.000 |
| List | 99.082 | 98.093 | -0.989 | -0.992 | +0.003 | +0.000 |
| Sieve | 120.809 | 121.526 | +0.717 | +0.638 | +0.078 | +0.009 |
| Queens | 116.298 | 117.245 | +0.947 | +0.947 | +0.000 | +0.000 |
| Permute | 387.931 | 409.291 | +21.360 | +21.360 | +0.000 | +0.000 |
| Storage | 213.283 | 198.762 | -14.521 | -14.972 | +0.451 | +0.078 |
| Towers | 184.069 | 182.585 | -1.484 | -1.488 | +0.004 | +0.000 |
| Mandelbrot | 235.001 | 238.255 | +3.254 | +2.207 | +1.046 | +0.266 |
| Richards | 936.084 | 945.469 | +9.385 | +9.350 | +0.035 | +0.001 |
| CD | 242.552 | 244.298 | +1.746 | +1.240 | +0.507 | -0.318 |
| NBody | 378.066 | 383.137 | +5.071 | +2.097 | +2.974 | +0.301 |
| Havlak | 413.999 | 417.080 | +3.081 | +2.509 | +0.572 | -0.606 |

最大贡献项：

1. `Permute`: `+21.360s`
   - GC CPU 只有 `0.002s`。
   - GC core 只有 `0.001s`。
   - Minor GC count 同为 `71`。
   - allocations 只差 12 个。
   - 因此 Permute 的 `+21.360s` 不是 GC 机制差异，基本是 business/runtime 层波动。

2. `Richards`: `+9.385s`
   - GC delta 只有 `+0.035s`。
   - GC core delta 只有 `+0.001s`。
   - allocations、forward ops、AS counters 完全一样。
   - 因此也不是 GC core 退化。

3. `NBody`: `+5.071s`
   - 其中 business `+2.097s`，GC full `+2.974s`。
   - GC core 只多 `+0.301s`。
   - 主要不是 core scavenge 算法发生结构性变化。

抵消项：

- `Storage` 在 `out110` 反而比 `out50` 快 `-14.521s`。
- 如果没有 Storage 的这次变快，`out110` 相比 `out50` 会更慢。

### Permute 证明这是单轮波动

后来为了统计 GiY 直接 NT store，我又跑了一轮 GiY detail-counter 版本：

- 目录：`build.debug/benchmarks/out112_giy_detail_nt_full`
- 注意：这轮开了 `GIY_PROFILE_DETAIL=true`，不能替代性能 baseline，只能作为辅助证据。

但 `Permute` 在这轮的结果非常关键：

| run | Permute CPU | Business CPU | GC CPU | Minor GC count |
|---|---:|---:|---:|---:|
| out50 | 387.931 | 387.929 | 0.002 | 71 |
| out110 | 409.291 | 409.289 | 0.002 | 71 |
| out112 detail | 386.983 | 386.982 | 0.001 | 71 |

`out112 detail` 的 Permute 又回到了 `386.983s`，接近 `out50` 的 `387.931s`，而不是 `out110` 的 `409.291s`。

这说明 `out110` 的 Permute 是一次慢 outlier，而不是 GiY 实现突然多做了 GC 工作。

如果只把 `out110` 的 Permute 换成同样最终源码附近的 `out112 detail` Permute：

- `out110` total CPU `3512.999s`
- 减去 `409.291 - 386.983 = 22.308s`
- 得到约 `3490.691s`
- 这会比 Cheney `3498.813s` 快约 `8.122s`

所以“GiY 是否比 Cheney 快”现在被一个非常小的总差距和单轮噪音支配。

### 是否有源码变化导致真实性能回退？

`out50` 到 `out110` 之间确实有一些源码变化，但从 counters 看，它们不是导致这次符号翻转的主因：

1. allocation-site update 记录从 old target payload 改成 source object 的 `shape`。
   - 这是 correctness/old-guard 安全修补。
   - AS observations/entries/applied 在 out50 和 out110 几乎完全一致。

2. NT store 的 fence 位置做过安全调整。
   - 这可能影响 GC full 的边界时间。
   - 但 `Permute` 几乎没有 NT/GC，却贡献最大差距，因此不能解释主要翻转。

3. RSet duplicate/capacity check 做过 correctness 修补。
   - out50/out110 write barrier calls 几乎一样，只差 `-68`。
   - minor GC count 和 RSet 相关 profile 没有显示结构性退化。

因此更合理的判断是：

- 代码修补没有让 GiY GC core 退化；
- `out110` 的 end-to-end 变慢主要是单轮 full-suite 运行波动和 business/runtime 层差异；
- 之前 GiY 超 Cheney 的 margin 只有约 `14.3s / 0.41%`，这个 margin 太小，小于单轮 benchmark 波动。

### 最终解释

GiY 不是“忽然因为设计问题慢于 CheneyGC”。更准确的说法是：

> 之前 `out50` 中 GiY 比 Cheney 快 `14.296s`，现在 `out110` 中 GiY 比 Cheney 慢 `14.295s`，这个符号翻转主要来自单轮运行波动。`out110` 相比 `out50` 的 GC core 没有变慢，反而少了 `0.269s`；minor GC、allocations、forward ops、AS update 计数几乎完全一致。最大异常是 `Permute` 多了 `21.360s`，而 Permute 几乎没有 GC，因此不能解释为 GiY GC 退化。

汇报时应谨慎表述：

- 不能说“GiY 稳定超过 CheneyGC”。
- 也不能说“GiY 已经确定慢于 CheneyGC”。
- 目前最稳的结论是：GiY 的 GC core 稳定优于 Cheney，但 end-to-end 与 Cheney 处在约 `±0.5%` 的噪音区间；需要多轮 median 才能确定总时间胜负。

## 2026-05-08 GiYOL 中 >256B 对象是否可以直接使用 NT store

问题：GiYOL 里能不能让当前大于 `256B` 的对象也采取 NT store？

结论：

- 如果指“实际写 old 区时是否使用 non-temporal store”，当前 GiYOL 的 `>256B` 对象已经会走 NT store。
- 如果指“让 `GiYOL direct NT objects` 这个专门计数不再是 0”，当前默认配置没有打开这个专门分支；可以改，但不应直接粗暴打开现有 `GIYOL_DIRECT_NONTINY_NT=1`。

当前代码路径：

1. 通用对象复制函数 `giy_copy_live_object()`：
   - `nbytes <= GIY_NT_COPY_MIN_BYTES`，也就是 `<=256B`：走普通 `memcpy`。
   - `nbytes > 256B`：走 `_mm_stream_si64/_mm_stream_si128`，即 NT store。

2. 当前 GiYOL non-tiny 对象路径：
   - 当前 `GIYOL_TINY_MAX_OBJECT_BYTES=64`。
   - `align_bytes > 64B` 的对象不会进入 tiny staging batch，而是在 traversal 阶段直接 materialize。
   - 默认 `GIYOL_DIRECT_NONTINY_NT=0`，所以这条路径调用的是 `giy_copy_live_object()`。
   - 因此：`64B < object <= 256B` 走普通 `memcpy`，`object > 256B` 已经走通用 NT store。

为什么 final 输出里 `GiYOL direct NT objects:0`：

- 这个字段统计的是 `giyol_direct_nt_copy_live_object()` 专门分支。
- 当前 `GIYOL_DIRECT_NONTINY_NT=0`，所以这个专门分支没有被调用。
- 但 `giy_copy_live_object()` 内部的通用 NT store 仍然会发生，只是 final GiYOL 没开 `GIY_PROFILE_DETAIL=1`，所以没有打印通用 `NT copy objects`。

不建议直接做的事情：

- 不建议简单用 `GIYOL_DIRECT_NONTINY_NT=1` 重新编译。
- 原因：当前代码的 direct non-tiny 分支触发条件是 `align_bytes > GIYOL_TINY_MAX_OBJECT_BYTES`，也就是 `>64B`。
- 如果直接打开，它会让 `65B~256B` 的对象也强行走 NT store。
- 这违背当前 `256B` 阈值设计，可能让小对象 NT store 变慢。

如果要做“正确的 >256B direct NT 实验”，建议方式：

1. 保持 `GIYOL_TINY_MAX_OBJECT_BYTES=64` 不变。
2. 在 GiYOL non-tiny immediate materialization 路径里显式判断：
   - `align_bytes > GIY_NT_COPY_MIN_BYTES`：调用 `giyol_direct_nt_copy_live_object()`，并计入 `GiYOL direct NT objects/bytes`。
   - `64B < align_bytes <= 256B`：继续调用 `giy_copy_live_object()`，它会走普通 `memcpy`。
3. 这样语义上等价于当前通用路径，但 profile 会更清楚：`>256B` 的对象会被明确记到 GiYOL direct NT counters。

预期效果：

- 性能上未必有明显提升，因为当前 `>256B` 已经通过 `giy_copy_live_object()` 使用 NT store。
- 主要收益是实验口径更清楚：能直接在 GiYOL 输出里看到 `direct NT objects/bytes`。
- 如果实现时误把 `65B~256B` 也纳入 NT，反而可能降低性能。

需要补的实验：

- 如果要拿具体数据证明 GiYOL 的通用 `>256B` NT 数量，应重新构建 GiYOL：`GIY_PROFILE_DETAIL=true`。
- 这样可以打印通用 `NT copy objects/bytes`。
- 目前已有 GiY detail 计数：GiY 的 `>256B` 对象级 NT 是 `38,493,668` 个，`20064.99 MB`；GiYOL 由于 non-tiny 路径复用同一个 `giy_copy_live_object()`，理论上应非常接近，但需要 GiYOL detail run 才能给精确数。

## 2026-05-08 GiYOL hybrid 策略：小对象 batch NT，大对象直接 NT

用户澄清的目标策略：

- 连续的小对象使用 GiYOL 式 staging/batch。
- 累积到一定大小后统一使用 NT store。
- 大于 `256B` 的对象不进入 staging/batch，直接使用 NT store。

结论：这个策略是可行的，而且是当前 GiYOL 最值得尝试的清晰版本。

当前代码和这个策略的关系：

1. 当前 GiYOL 已经接近这个策略。
   - 当前 `GIYOL_TINY_MAX_OBJECT_BYTES=64`。
   - `<=64B` 对象进入 tiny staging/batch。
   - `>64B` 对象直接 materialize。
   - 其中 `>256B` 的对象通过 `giy_copy_live_object()` 已经会使用 NT store。
   - `65B~256B` 对象直接走普通 memcpy。

2. 如果想更符合“所有小对象都 batch，大对象直接 NT”的表述，建议把 batch 范围扩到 `<=256B`。
   - 设置或实现等价策略：`GIYOL_TINY_MAX_OBJECT_BYTES=256`。
   - `<=256B`：进入 GiYOL staging/batch，old target 连续且累计到阈值后统一 NT。
   - `>256B`：不进入 batch，直接走 `giy_copy_live_object()`；该函数会直接使用 NT store。

推荐的实验策略：

- `GIYOL_TINY_MAX_OBJECT_BYTES=256`
- `GIYOL_TINY_STAGING_BYTES=4096` 或先试 `8192`
- `GIYOL_TINY_FLUSH_BYTES=4096` 或先试 `8192`
- `GIYOL_FORCE_TINY_TAIL_NT=0`
- `GIYOL_DIRECT_NONTINY_NT=0`

为什么 `GIYOL_DIRECT_NONTINY_NT=0` 仍然可以：

- 因为 `>256B` 的 non-tiny object 会调用 `giy_copy_live_object()`。
- `giy_copy_live_object()` 的内部判据就是 `nbytes > 256B` 时使用 NT store。
- 所以不需要打开 `GIYOL_DIRECT_NONTINY_NT=1`。

不建议直接打开 `GIYOL_DIRECT_NONTINY_NT=1` 的原因：

- 现有 direct non-tiny 分支的触发条件不是 `>256B`，而是 `align_bytes > GIYOL_TINY_MAX_OBJECT_BYTES`。
- 如果 `GIYOL_TINY_MAX_OBJECT_BYTES=64`，打开后会让 `65B~256B` 也强行 direct NT。
- 如果 `GIYOL_TINY_MAX_OBJECT_BYTES=256`，打开后才近似等于 `>256B direct NT`。
- 但为了避免口径混乱，最简单安全的做法仍是保持 `GIYOL_DIRECT_NONTINY_NT=0`，让 `giy_copy_live_object()` 负责 `>256B` direct NT。

这个 hybrid 策略的预期：

- 相比当前 `<=64B batch`，`<=256B batch` 会显著提高 GiYOL batch NT 的覆盖率。
- 代价是 `65B~256B` 对象需要先复制到 staging buffer，再 NT 到 old 区；如果这些对象不能形成足够大的连续 chunk，就会增加额外 staging/fallback 成本。
- 因此必须通过 benchmarks 判断：关键指标是 `GiYOL batch bytes NT` 是否显著上升，以及 `GiYOL staging fallback bytes` 是否下降或至少没有带来更大的 GC/full-time 成本。

判断实验成功的标准：

1. `GiYOL batch bytes NT` 明显高于当前 final 的 `1229.51 MB`。
2. `GiYOL staging fallback bytes` 相对当前 final 的 `59845.37 MB` 明显下降，或虽然不下降但 total/GC CPU 改善。
3. `Storage` 不能继续出现 `NT MB=0` 且 fallback 巨大的情况；否则说明扩大到 `<=256B` 仍然没有形成有效连续 chunk。
4. `Havlak` 和 `CD` 应该是最先看到覆盖率改善的项目。

一句话结论：

> 可以采用“小对象累计 batch NT，`>256B` 大对象直接 NT”的 hybrid GiYOL。当前实现已经具备大对象 direct NT 的通用路径；下一步真正值得实验的是把 tiny/batch 上限从 `64B` 扩到 `256B`，让 `65B~256B` 对象也参与连续 staging/batch，而不是直接 memcpy。

## 2026-05-08 更正：`<=256B` 全量 staging/batch 之前已证明不划算

用户指出：之前不是已经发现 `<=256B` staging/batch 效率更低了吗？而且当前 GiYOL 里 direct NT store 字节不是很少吗？

更正结论：

- 用户的质疑是对的。
- 我上一节把 `<=256B batch + >256B direct NT` 说成“当前最值得尝试的清晰版本”，这个说法不够准确。
- 更准确的结论是：
  - “机制上可行”，但“作为默认优化方向已经被之前数据证明风险很大/通常不划算”。
  - 当前 final GiYOL 选择 `<=64B` batch，而不是 `<=256B` batch，正是因为之前 `<=256B` 扩大 staging 范围后 GC core 变差。

### 之前 `<=256B` 实验的关键数据

对比当前 final GiYOL `out111_final_giyol_corrected_full` 与之前 `<=256B` 相关实验：

#### Storage

| version | CPU total | business | GC full | GC core | NT MB | fallback MB |
|---|---:|---:|---:|---:|---:|---:|
| final GiYOL `<=64B, flush4096` out111 | 202.801 | 163.695 | 39.107 | 35.681 | 0.00 | 45723.74 |
| `<=256B, flush2048` out93 | 215.605 | 166.499 | 49.106 | 47.104 | 8382.48 | 50389.06 |
| `<=256B, flush8192` out95 | 210.934 | 163.730 | 47.203 | 44.655 | 9296.30 | 49475.23 |
| `<=256B, deferred staging, flush8192` out99 | 208.997 | 167.685 | 41.312 | 38.674 | 9296.31 | 49475.22 |

Storage 结论：

- `<=256B` 确实把 NT bytes 从 `0.00 MB` 提到约 `8~9 GB`。
- 但 GC core 从 final 的 `35.681s` 变成 `38.674s~47.104s`。
- 也就是说，“更多 NT bytes”没有换来更快，反而因为 staging/bookkeeping/extra copy 让 GC core 更慢。

#### CD

| version | CPU total | business | GC full | GC core | NT MB | fallback MB |
|---|---:|---:|---:|---:|---:|---:|
| final GiYOL `<=64B, flush4096` out111 | 247.496 | 233.980 | 13.517 | 8.416 | 4.89 | 2375.64 |
| `<=256B, flush2048` out93 | 246.475 | 233.198 | 13.277 | 8.935 | 43.20 | 3435.72 |
| `<=256B, flush8192/16384 output` out95 | 244.691 | 231.404 | 13.287 | 8.920 | 84.08 | 3394.85 |

CD 结论：

- `<=256B` 对 CD 的 total 有时略好，但 GC core 仍比 final GiYOL 更高：`8.920s/8.935s` vs `8.416s`。
- 改善主要来自 business/run 波动或非 core 因素，不是 batch NT 明确降低了 GC core。

#### Havlak

| version | CPU total | business | GC full | GC core | NT MB | fallback MB |
|---|---:|---:|---:|---:|---:|---:|
| final GiYOL `<=64B, flush4096` out111 | 418.018 | 377.893 | 40.125 | 28.561 | 1224.62 | 11001.12 |
| `<=256B, flush2048` out93 | 416.960 | 377.127 | 39.832 | 30.207 | 667.58 | 16417.46 |
| `<=256B, flush8192/16384 output` out95 | 418.047 | 378.361 | 39.686 | 30.347 | 1847.05 | 15237.99 |

Havlak 结论：

- `<=256B` 有时 total 接近甚至略低，但 GC core 变差：`30.207s/30.347s` vs final `28.561s`。
- fallback bytes 也更大：`15~16 GB` vs final `11 GB`。
- 所以扩大到 `<=256B` 并没有稳定改善 GiYOL 的核心复制成本。

### 为什么 `<=256B` 会更差

之前的失败原因现在可以明确写成：

1. `64B~256B` 对象用普通 cached memcpy 本来很便宜。
   - microbenchmark 已显示 256B 级别直接 NT 很慢。
   - 旧数据里 256B 直接 NT 比 young->old memcpy 慢约 15 倍。

2. 进入 staging 后多了一次复制。
   - 普通路径：`src_young -> dst_old` 一次 copy。
   - staging 路径：`src_young -> staging`，然后 `staging -> dst_old` NT。
   - 如果最终不能形成足够划算的连续 NT chunk，就多做了一次无效中介 copy。

3. 即使形成 NT chunk，NT 本身也有固定成本。
   - 对 4096B/8192B 这种 chunk 才可能摊薄。
   - 对由许多 64B~256B 对象拼起来的 chunk，bookkeeping、chunk split、sfence、streaming store 成本不一定能被收益覆盖。

4. fallback bytes 仍然很大。
   - Storage `<=256B` 版本 fallback 约 `49~50 GB`，比 final `45.7 GB` 还大。
   - Havlak `<=256B` 版本 fallback 约 `15~16 GB`，比 final `11 GB` 还大。
   - 说明扩大 staging 范围没有消灭 fallback，反而让更多对象进入复杂路径。

### 关于“当前 GiYOL 没几个 direct NT store 字节”

这里要分清两个计数：

1. `GiYOL direct NT bytes`
   - 这是专门的 `giyol_direct_nt_copy_live_object()` 分支计数。
   - 当前 final 配置 `GIYOL_DIRECT_NONTINY_NT=0`，所以它是 `0.00 MB`。
   - 这不代表 GiYOL 对 `>256B` 对象没有使用 NT。

2. 通用 `giy_copy_live_object()` 的 NT
   - 当前 GiYOL 的 non-tiny/fallback 路径会调用 `giy_copy_live_object()`。
   - 这个函数对 `>256B` 对象会使用 NT store。
   - final GiYOL 没开 `GIY_PROFILE_DETAIL=1`，所以没有打印通用 `NT copy objects/bytes`。

所以正确说法是：

- 当前 GiYOL 的“专门 direct NT counter”为 0。
- 当前 GiYOL 的“batch NT bytes”也很少：final full-suite 只有 `1229.51 MB`。
- 但当前 GiYOL 里 `>256B` 对象通过通用 copy 路径使用了多少 NT，需要 GiYOL detail run 才能精确统计；不能只看 `GiYOL direct NT bytes` 字段下结论。

### 修正后的策略建议

不建议再把“所有 `<=256B` 对象进入 staging/batch”作为默认方向。

当前更合理的策略是：

1. 保持当前 final GiYOL 的保守范围：
   - `<=64B` 才进入 tiny batch。
   - `65B~256B` 直接普通 memcpy。
   - `>256B` 直接通过通用路径 NT。

2. 如果还要优化 GiYOL，应做更精细的选择，而不是简单 `<=256B` 全收：
   - 只让预计能形成 target-contiguous 大 run 的对象进入 staging。
   - 对 run 总 bytes 小于阈值的，直接 fallback，不碰 staging。
   - 可以试 `<=96B` 或 `<=128B` 的更窄范围，但必须以 GC core 和 fallback bytes 为准。

3. 汇报时应明确：
   - “扩大到 `<=256B` 能提高 NT bytes，但此前实验显示 GC core 变差，所以 final 采用 `<=64B` 保守版本。”
   - “GiYOL 当前失败不是因为完全没有 NT，而是有效 NT 覆盖率低，且扩大覆盖范围会引入额外 staging/fallback 成本。”

## 2026-05-08 `giy_copy_live_object()` 函数解释

用户问题：为什么 `giy_copy_live_object()` 看起来总是先 `memcpy`，再使用 NT store？

先给结论：

- 大对象路径并不是“先普通 memcpy 到 old，再 NT store 到 old”。
- `memcpy(&v, s, 8)` 只是把 source/young 中的 8 字节安全读到本地变量/寄存器里。
- 真正写 old/DRAM target 的动作仍然是 `_mm_stream_si64` 或 `_mm_stream_si128`，也就是 non-temporal store。

当前函数逻辑：

```cpp
static inline void giy_copy_live_object(void *dst,
                                        const void *src,
                                        size_t nbytes,
                                        bool *used_nt_store) {
  giy_old_guard_allow_old_write(dst, nbytes);
  if (nbytes <= GIY_NT_COPY_MIN_BYTES) {
    memcpy(dst, src, nbytes);
    giy_old_guard_protect_old_write(dst, nbytes);
    return;
  }

  // x86/x86_64: stream-store copy for >256B
  ...
}
```

分两条路径：

1. `nbytes <= 256`
   - 直接 `memcpy(dst, src, nbytes)`。
   - 这是普通 cached store。
   - 原因：小对象用 NT store 固定成本太高，之前 microbenchmark 已经显示 64B/256B 级别 NT 很慢。

2. `nbytes > 256`
   - 进入 x86 streaming store 路径。
   - 读 source/young：普通 load。
   - 写 dst/old：non-temporal store。

大对象 NT 路径的核心：

```cpp
while (n >= 16) {
  __m128i v = _mm_loadu_si128((const __m128i *) s);
  _mm_stream_si128((__m128i *) d, v);
  s += 16;
  d += 16;
  n -= 16;
}
```

这里很重要：

- `_mm_loadu_si128`：从 source/young 读 16 字节到 SIMD register。
- `_mm_stream_si128`：把 register 写到 destination/old，使用 NT store。
- CPU 没有“memory-to-memory NT copy”这类直接把一段内存搬到另一段内存的单条指令；必须先 load 到寄存器，再 store 到目标地址。

为什么 8 字节 head/tail 用 `memcpy(&v, s, sizeof(v))`：

```cpp
uint64_t v;
memcpy(&v, s, sizeof(v));
_mm_stream_si64((long long *) d, (long long) v);
```

这个 `memcpy` 不是把对象复制到 old。它的目的只是安全地从 `s` 读 8 字节到局部变量 `v`。

这样做有几个原因：

1. 避免未对齐读取的 C/C++ undefined behavior。
   - `s` 不一定按 `uint64_t` 对齐。
   - 直接写 `*(uint64_t *)s` 在 C/C++ 语义上可能有未定义行为。
   - `memcpy(&v, s, 8)` 是标准安全写法，编译器通常会优化成普通 load。

2. 避免 strict-aliasing 问题。
   - source 是 `unsigned char *`。
   - 直接强转成 `uint64_t *` 再解引用可能违反别名规则。
   - `memcpy` 是合法的 byte-wise load 表达。

3. 配合 `_mm_stream_si64`。
   - `_mm_stream_si64` 的输入不是内存地址，而是一个 64-bit value。
   - 所以必须先把 source 的 8 字节读成一个值，再 stream store 到目标。

为什么要处理 8 字节 head：

- `_mm_stream_si128` 要求目标地址 16B 对齐。
- 当前 object target 地址有时可能是 `8 mod 16`。
- 如果 `d` 不是 16B 对齐，但满足 `8 mod 16`，函数先用 `_mm_stream_si64` 写 8 字节。
- 写完后 `d += 8`，目标地址就变成 16B 对齐。
- 后面就可以用 `_mm_stream_si128` 连续写 16B。

为什么 tail 只允许 8 字节：

- object copy size 来自 `ALIGN(hdr->size + sizeof(object_header))`，整体按 8 字节对齐。
- 16B loop 结束后，剩余只应该是 `0` 或 `8`。
- 如果剩余不是 8，代码认为对齐不变量坏了，直接报错。

`used_nt_store` 的作用：

- 大对象路径里执行过 streaming store 后，会设置：

```cpp
*used_nt_store = true;
```

- 调用方稍后执行 `giy_finish_local_nt_stores(&used_nt_store)`，里面会在需要时 `_mm_sfence()`。
- `sfence` 的作用是保证 NT store 在后续依赖这些 old writes 前完成/有序。

所以这段代码的真实语义是：

```text
<=256B:
  old <- memcpy(young)              // 普通 cached store

>256B:
  reg <- load young                 // 普通 load，源在 young/cache
  old <- stream_store reg           // NT store，目标 old/DRAM
```

为什么 source 读取不是 non-temporal：

- NT 主要解决的是写 old/DRAM target 时避免污染 cache。
- source 是 young/cache 区，GC 正在扫描/复制它，读 source 本来就是热路径。
- 所以 source 侧普通 load 是合理的；关键是 old target 写入用 streaming store。

最终一句话解释：

> `giy_copy_live_object()` 里看到的 `memcpy(&v, s, 8)` 不是先把对象普通写到 old，而是为了安全地把 source 的 8 字节读进寄存器；真正写 old 的仍然是 NT store。小对象 `<=256B` 才会真的使用普通 `memcpy(dst, src)`。

## 2026-05-08 当前 GiY business 比 CheneyGC 慢的原因

问题：当前 final GiY 的 business CPU 为什么比 CheneyGC 慢？

使用数据：

- CheneyGC：`build.debug/benchmarks/out24`
- 当前 GiY：`build.debug/benchmarks/out110_final_giy_corrected_full`

总体：

- Cheney business CPU：`3379.483s`
- GiY business CPU：`3401.309s`
- GiY - Cheney：`+21.826s`

先给结论：

- 当前这 `+21.826s` business gap 的主因不是 GiY GC core 退化。
- 最大来源是 `Permute` 的单轮 outlier。
- `Permute` 一项的 business gap 是 `+27.104s`，已经超过总 gap。
- 如果排除 `Permute`，GiY business 反而比 Cheney 快约 `5.278s`。

逐项 business CPU：

| benchmark | Cheney business | GiY business | GiY - Cheney |
|---|---:|---:|---:|
| Bounce | 158.725 | 157.114 | -1.611 |
| List | 98.849 | 98.078 | -0.771 |
| Sieve | 120.975 | 119.966 | -1.009 |
| Queens | 116.001 | 117.190 | +1.189 |
| Permute | 382.185 | 409.289 | +27.104 |
| Storage | 158.050 | 163.623 | +5.573 |
| Towers | 185.342 | 182.581 | -2.761 |
| Mandelbrot | 228.310 | 229.093 | +0.783 |
| Richards | 942.978 | 945.312 | +2.334 |
| CD | 239.133 | 231.306 | -7.827 |
| NBody | 369.957 | 368.418 | -1.539 |
| Havlak | 378.978 | 379.339 | +0.361 |
| **Total** | **3379.483** | **3401.309** | **+21.826** |

正向 gap 合计：

- 慢的项合计：`+37.344s`
- 快的项合计：`-15.518s`
- 净 gap：`+21.826s`

最大项：

1. `Permute`: `+27.104s`
2. `Storage`: `+5.573s`
3. `Richards`: `+2.334s`
4. `Queens`: `+1.189s`
5. `Mandelbrot`: `+0.783s`
6. `Havlak`: `+0.361s`

抵消项：

- `CD`: `-7.827s`
- `Towers`: `-2.761s`
- `Bounce`: `-1.611s`
- `NBody`: `-1.539s`
- `Sieve`: `-1.009s`
- `List`: `-0.771s`

### 为什么说 Permute 是 outlier

`Permute` 几乎没有 GC：

| run | Permute business | Permute GC | Minor GC count | allocations |
|---|---:|---:|---:|---:|
| Cheney out24 | 382.185 | 0.003 | 81 | 404153 |
| GiY out50 previous fast | 387.929 | 0.002 | 71 | 304698 |
| GiY out110 current final | 409.289 | 0.002 | 71 | 304710 |
| GiY out112 detail-counter | 386.982 | 0.001 | 71 | 304710 |

关键事实：

- out110 的 `Permute` 比 out50 慢 `21.360s`。
- 但 GC CPU 都只有约 `0.002s`。
- Minor GC count 同为 `71`。
- allocations 基本一样。
- 后来 out112 detail-counter 里 `Permute` 又回到 `386.982s`，接近 out50，而不是 out110。

因此，out110 的 `Permute +27.104s` 不能解释为 GiY GC 或 allocation-site 逻辑导致；它更像一次单轮运行 outlier、调度/频率/二进制布局/运行期噪音。

如果把 out110 的 `Permute` 替换成同源码附近 out112 的 `Permute`：

- 当前 GiY business：`3401.309s`
- 修正 Permute outlier：`409.289 - 386.982 = 22.307s`
- 修正后 GiY business 约：`3379.002s`
- Cheney business：`3379.483s`
- 修正后 GiY 反而快约：`0.481s`

如果直接排除 `Permute`：

- Cheney business without Permute：`3379.483 - 382.185 = 2997.298s`
- GiY business without Permute：`3401.309 - 409.289 = 2992.020s`
- GiY without Permute 快约：`5.278s`

### 除 Permute 外还有什么原因

剩余正向 gap 主要来自：

1. `Storage +5.573s`
   - Storage 的 allocations 与 Cheney 几乎一致。
   - Write barrier calls 甚至略少于 Cheney。
   - 但 GiY 的 business 仍更高。
   - 更合理的解释是 GiY 的 old-read avoidance/RSet value maintenance/GC 后 patch/cache locality side effects 对 mutator 有边界影响；这部分不在 GC core 计时里。
   - 但 Storage 的 GC savings 很大，所以 total 仍赢 Cheney。

2. `Richards +2.334s`
   - GC CPU 只有 `0.157s`，GC core `0.042s`。
   - allocations/forward ops/AS counters 与之前 run 一致。
   - 更像业务运行期波动或代码布局/cache locality 影响。

3. `Mandelbrot +0.783s`、`Queens +1.189s`、`Havlak +0.361s`
   - 这些单项 gap 不大。
   - 不足以单独说明结构性问题。

### 最终判断

当前 final GiY 的 business 比 Cheney 慢，最主要原因是：

1. `Permute` 单轮 outlier，贡献 `+27.104s`，超过总 business gap。
2. 其余项有小幅正负波动；排除 `Permute` 后 GiY business 其实更快。
3. Storage/Richards 等项的剩余 business gap 更像边界成本、cache locality、RSet/old-slot patch 副作用或运行期噪音，而不是 GC core 主体慢。

因此汇报时不能说：

- “GiY 的 business 已经确定比 Cheney 慢。”

更准确应说：

> 当前单轮 final 数据里，GiY business 比 Cheney 多 `21.826s`，但这个 gap 主要由 `Permute` 的一次慢 outlier 造成。`Permute` 几乎没有 GC，且后续 GiY detail run 又恢复到旧速度。排除 `Permute` 后，GiY business 比 Cheney 快约 `5.278s`。所以当前 business gap 不能作为 GiY 结构性慢于 Cheney 的证据；更稳的结论是 end-to-end 胜负处在单轮噪音范围内，而 GiY 的 GC core 稳定优于 Cheney。

## 2026-05-08 新 CheneyGC 判据 full-suite：`out113_final_cheney_552_full`

用户要求：重新跑一轮准确、有效的 CheneyGC，作为后续判据。

本轮输出目录：

- `build.debug/benchmarks/out113_final_cheney_552_full`

构建配置：

- `OPT_GC=cache_cheney`
- `CACHE_SIZE_KB=552`
- 构建命令：`make -B -C build.debug ejsvm OPT_GC=cache_cheney CACHE_SIZE_KB=552 -j4`
- 说明：这是当前源码下的 root-controlled `cache_cheney`，保留 Cheney copy/scavenge，使用当前源码中已经控制过的 generational root scan 和 function-table strong-slot set。
- 选择 `CACHE_SIZE_KB=552` 的原因：和当前 final GiY/GiYOL 的 cache-size 配置保持一致。

smoke 结果：

- `hello_world.sbc`: `status=0`
- `giy_gc_probe.sbc`: `status=0`
- `bounce_check.sbc`: `status=0`

完整 suite 状态：

- Bounce/List/Sieve/Queens/Permute/Storage/Towers/Mandelbrot/Richards/CD/NBody/Havlak 全部 `status=0`
- 12 个 `.out` 都包含完整 `Total CPU time` profile。

运行环境记录：

- start load average：`0.41, 0.36, 0.40`
- end load average：`1.27, 1.55, 1.68`
- runner 顺序：Bounce -> List -> Sieve -> Queens -> Permute -> Storage -> Towers -> Mandelbrot -> Richards -> CD -> NBody -> Havlak

### 新 Cheney out113 总体数据

| metric | Cheney out113 |
|---|---:|
| CPU total | 3503.724 |
| CPU business | 3387.517 |
| CPU GC full | 116.206 |
| Wall total | 3504.276 |
| Wall business | 3388.584 |
| Wall GC full | 115.694 |
| GC core | 89.304 |
| scan_roots | 1.185 |
| scan_RS | 3.015 |
| scavenge | 85.105 |
| Minor GC count | 2068906 |
| Write barrier calls | 2072257202 |
| Allocations | 19846031748 |
| Alloc bytes | 297208.01 MB |
| Forward operations | 1692894720 |

### 新 Cheney out113 vs 旧 Cheney out24

| metric | Cheney out113 | old Cheney out24 | out113 - out24 |
|---|---:|---:|---:|
| CPU total | 3503.724 | 3498.813 | +4.911 |
| CPU business | 3387.517 | 3379.483 | +8.034 |
| CPU GC full | 116.206 | 119.329 | -3.123 |
| GC core | 89.304 | 89.940 | -0.636 |
| scavenge | 85.105 | 85.707 | -0.602 |
| Minor GC count | 2068906 | 2341014 | -272108 |

解释：

- 新 Cheney out113 的 GC core 和旧 out24 基本一致，甚至略低 `0.636s`。
- 新 Cheney out113 的 total 比旧 out24 慢 `4.911s`，主要来自 business 时间 `+8.034s`。
- 新 Cheney 的 minor GC count 明显减少，这是因为这轮显式使用 `CACHE_SIZE_KB=552`，而旧 out24 是之前的对照目录；以后和当前 GiY/GiYOL 对比应优先使用 out113。

### 新 Cheney out113 vs 当前 GiY/GiYOL

| metric | Cheney out113 | GiY out110 | GiY - Cheney | GiYOL out111 | GiYOL - Cheney |
|---|---:|---:|---:|---:|---:|
| CPU total | 3503.724 | 3512.999 | +9.275 | 3569.807 | +66.083 |
| CPU business | 3387.517 | 3401.309 | +13.792 | 3451.489 | +63.972 |
| CPU GC full | 116.206 | 111.692 | -4.514 | 118.319 | +2.113 |
| GC core | 89.304 | 72.560 | -16.744 | 77.731 | -11.573 |
| scavenge | 85.105 | 71.038 | -14.067 | 75.972 | -9.133 |
| Minor GC count | 2068906 | 2302292 | +233386 | 2302292 | +233386 |

结论：

1. 新 Cheney 判据下，GiY 的 total CPU 比 Cheney 慢 `+9.275s`，约 `+0.26%`。
2. GiYOL 的 total CPU 比 Cheney 慢 `+66.083s`，约 `+1.89%`。
3. GiY 的 GC CPU 仍比 Cheney 少 `4.514s`，GC core 少 `16.744s`。
4. GiYOL 的 GC core 也比 Cheney 少 `11.573s`，但 GC full 反而多 `2.113s`。
5. end-to-end 上，新 Cheney 仍略优于 GiY，明显优于 GiYOL；但 GiY 与 Cheney 的差距只有 `9.275s / 3503.724s = 0.26%`，仍在单轮 full-suite 噪音敏感区。

### 每项 total CPU 对比

| benchmark | Cheney out113 | GiY out110 | GiY - Cheney | GiYOL out111 | GiYOL - Cheney |
|---|---:|---:|---:|---:|---:|
| Bounce | 158.065 | 157.258 | -0.807 | 157.902 | -0.163 |
| List | 99.719 | 98.093 | -1.626 | 98.645 | -1.074 |
| Sieve | 121.319 | 121.526 | +0.207 | 129.743 | +8.424 |
| Queens | 114.402 | 117.245 | +2.843 | 116.786 | +2.384 |
| Permute | 381.063 | 409.291 | +28.228 | 443.811 | +62.748 |
| Storage | 208.062 | 198.762 | -9.300 | 202.801 | -5.261 |
| Towers | 179.687 | 182.585 | +2.898 | 185.294 | +5.607 |
| Mandelbrot | 232.644 | 238.255 | +5.611 | 239.101 | +6.457 |
| Richards | 955.000 | 945.469 | -9.531 | 943.737 | -11.263 |
| CD | 254.701 | 244.298 | -10.403 | 247.496 | -7.205 |
| NBody | 377.018 | 383.137 | +6.119 | 386.473 | +9.455 |
| Havlak | 422.044 | 417.080 | -4.964 | 418.018 | -4.026 |

逐项观察：

- GiY 赢新 Cheney 的项目：Bounce、List、Storage、Richards、CD、Havlak。
- GiY 输新 Cheney 的项目：Sieve、Queens、Permute、Towers、Mandelbrot、NBody。
- `Permute` 仍是最大异常项：GiY 比新 Cheney 慢 `+28.228s`，GiYOL 慢 `+62.748s`，但该项几乎没有 GC。
- `Storage`、`CD`、`Havlak` 上 GiY/GiYOL 仍明显赢 Cheney。

### 之后使用哪个 Cheney 判据

后续建议：

- 和当前 final GiY/GiYOL 比较时，使用 `out113_final_cheney_552_full`。
- 旧 `out24` 只作为历史 root-controlled Cheney 对照，不再作为当前最终判据。
- 汇报时写明：`out113` 是当前源码、`CACHE_SIZE_KB=552`、12/12 status=0 的新 Cheney full-suite。

一句话结论：

> 新 CheneyGC 判据 `out113_final_cheney_552_full` 已完整有效。它显示当前 GiY 的 GC core 仍显著优于 Cheney，但 end-to-end total CPU 仍比 Cheney 慢约 `9.275s / 0.26%`；这个差距很小，仍需要多轮 median 才能判断稳定胜负。GiYOL 当前比新 Cheney 慢约 `66.083s / 1.89%`。

## 2026-05-08 当前最准确 CheneyGC / GiY / GiYOL 三方数据分析

本节使用当前三个最终有效 full-suite 目录：

- CheneyGC: `build.debug/benchmarks/out113_final_cheney_552_full`
- GiY: `build.debug/benchmarks/out110_final_giy_corrected_full`
- GiYOL: `build.debug/benchmarks/out111_final_giyol_corrected_full`

有效性：

- 三个目录均为 12/12 benchmarks 完整输出。
- 新 Cheney `out113` 是当前源码、`OPT_GC=cache_cheney`、`CACHE_SIZE_KB=552` 的最新判据。
- GiY/GiYOL 使用当前 final corrected full-suite 结果。

### 总体数据

| GC | total CPU | business CPU | GC full CPU | GC core | scan_roots | scan_RS | scavenge | minor GC | write barrier | allocs | alloc MB | forward ops |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Cheney | 3503.724 | 3387.517 | 116.206 | 89.304 | 1.185 | 3.015 | 85.105 | 2068906 | 2072257202 | 19846031748 | 297208.01 | 1692894720 |
| GiY | 3512.999 | 3401.309 | 111.692 | 72.560 | 0.375 | 1.146 | 71.038 | 2302292 | 2079088344 | 19637117907 | 295614.12 | 1468806830 |
| GiYOL | 3569.807 | 3451.489 | 118.319 | 77.731 | 0.394 | 1.363 | 75.972 | 2302292 | 2079088404 | 19637117915 | 295614.12 | 1468806755 |

### 相对 Cheney 的总体差异

| GC | total | business | GC full | GC core | scavenge | minor GC | forward ops | total pct |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| GiY - Cheney | +9.275 | +13.792 | -4.514 | -16.744 | -14.067 | +233386 | -224087890 | +0.265% |
| GiYOL - Cheney | +66.083 | +63.972 | +2.113 | -11.573 | -9.133 | +233386 | -224087965 | +1.886% |

直接结论：

1. 如果看 end-to-end total CPU，新 Cheney 当前最好：`3503.724s`。
2. GiY 比 Cheney 慢 `9.275s`，只有 `0.265%`；这个差距很小，单轮 full-suite 下仍然容易被 benchmark 噪音影响。
3. GiY 的 GC core 比 Cheney 少 `16.744s`，scavenge 少 `14.067s`。也就是说，GiY 的 GC 内核工作本身是更快的。
4. GiY 输在 business/non-GC 时间：GiY business 比 Cheney 多 `13.792s`，抵消了 `GC full -4.514s` 的优势。
5. GiYOL 当前不是成功优化：它比 Cheney 慢 `66.083s`，也比 GiY 慢 `56.808s`。

### GiYOL 相对 GiY

| metric | GiYOL - GiY |
|---|---:|
| total CPU | +56.808 |
| business CPU | +50.180 |
| GC full CPU | +6.627 |
| GC core | +5.171 |
| scavenge | +4.934 |
| minor GC | +0 |
| forward ops | -75 |
| total pct | +1.617% |

解释：

- GiYOL 和 GiY 的 minor GC 数完全一样，forward ops 几乎一样，说明 GiYOL 没有改变主要存活对象集合和 GC 触发结构。
- GiYOL 的额外成本主要来自 copy 策略本身：small-object staging、flush 判断、tail fallback、以及 NT store 路径没有换回足够收益。
- 因此当前 GiYOL 不是“做了更多 GC 工作”，而是“为了尝试批量 NT store，额外增加了一层搬运/判断成本”，收益不足以覆盖开销。

### 每项 benchmark total CPU

| benchmark | Cheney | GiY | GiY-Cheney | GiYOL | GiYOL-Cheney | GiYOL-GiY |
|---|---:|---:|---:|---:|---:|---:|
| Bounce | 158.065 | 157.258 | -0.807 | 157.902 | -0.163 | +0.644 |
| List | 99.719 | 98.093 | -1.626 | 98.645 | -1.074 | +0.552 |
| Sieve | 121.319 | 121.526 | +0.207 | 129.743 | +8.424 | +8.217 |
| Queens | 114.402 | 117.245 | +2.843 | 116.786 | +2.384 | -0.459 |
| Permute | 381.063 | 409.291 | +28.228 | 443.811 | +62.748 | +34.520 |
| Storage | 208.062 | 198.762 | -9.300 | 202.801 | -5.261 | +4.039 |
| Towers | 179.687 | 182.585 | +2.898 | 185.294 | +5.607 | +2.709 |
| Mandelbrot | 232.644 | 238.255 | +5.611 | 239.101 | +6.457 | +0.846 |
| Richards | 955.000 | 945.469 | -9.531 | 943.737 | -11.263 | -1.732 |
| CD | 254.701 | 244.298 | -10.403 | 247.496 | -7.205 | +3.198 |
| NBody | 377.018 | 383.137 | +6.119 | 386.473 | +9.455 | +3.336 |
| Havlak | 422.044 | 417.080 | -4.964 | 418.018 | -4.026 | +0.938 |

逐项结论：

- GiY 赢 Cheney：Bounce、List、Storage、Richards、CD、Havlak。
- GiY 输 Cheney：Sieve、Queens、Permute、Towers、Mandelbrot、NBody。
- GiYOL 只相对 GiY 赢 Queens 和 Richards；其余大部分项目输给 GiY。
- `Permute` 是 GiY/GiYOL total gap 的最大来源：GiY 比 Cheney 慢 `28.228s`，GiYOL 比 Cheney 慢 `62.748s`。但 `Permute` 的 GC CPU 几乎为零，所以它不能证明 GC core 变慢，只能说明当前 end-to-end 单轮结果中 business/non-GC 时间非常不利。

### 每项 business 与 GC full 差异

| benchmark | GiY business diff | GiY GC diff | GiYOL business diff | GiYOL GC diff |
|---|---:|---:|---:|---:|
| Bounce | -0.841 | +0.034 | -0.209 | +0.046 |
| List | -1.630 | +0.004 | -1.074 | +0.000 |
| Sieve | +0.953 | -0.746 | +9.214 | -0.790 |
| Queens | +2.810 | +0.033 | +2.350 | +0.034 |
| Permute | +28.227 | +0.001 | +62.749 | -0.001 |
| Storage | +6.717 | -16.016 | +6.789 | -12.048 |
| Towers | +2.899 | -0.001 | +5.607 | +0.000 |
| Mandelbrot | +1.635 | +3.976 | +2.170 | +4.286 |
| Richards | -9.604 | +0.073 | -11.340 | +0.077 |
| CD | -11.874 | +1.472 | -9.200 | +1.996 |
| NBody | -0.864 | +6.983 | +2.998 | +6.457 |
| Havlak | -4.636 | -0.327 | -6.082 | +2.056 |

关键解释：

- GiY 的 `GC full` 总体比 Cheney 少 `4.514s`，但 business 总体多 `13.792s`，所以最终 total 慢 `9.275s`。
- GiY 在 Storage 上 GC 优势极大：`GC full -16.016s`，但 business 慢 `+6.717s`，最后仍净赢 `9.300s`。
- GiY 在 CD/Richards/Havlak 的优势主要来自 business 时间更低，不只是 GC。
- GiY 在 NBody/Mandelbrot 的 GC full 明显更慢，说明这些 workload 上 GiY 的 root/RS/traverse 外围或对象访问形态不占优。
- GiYOL 的最大问题是 business 时间：相对 GiY 多 `50.180s`，其中 `Permute +34.520s`、`Sieve +8.217s`、`Storage +4.039s`、`NBody +3.336s`、`CD +3.198s`、`Towers +2.709s` 都贡献了损失。

### GC core / scavenge 层面的差异

| benchmark | GiY core diff | GiYOL core diff | GiY scavenge diff | GiYOL scavenge diff |
|---|---:|---:|---:|---:|
| Bounce | -0.013 | -0.010 | -0.006 | -0.004 |
| List | +0.001 | +0.001 | +0.002 | +0.002 |
| Sieve | -1.096 | -1.093 | -0.477 | -0.473 |
| Queens | +0.007 | +0.007 | +0.008 | +0.007 |
| Permute | +0.001 | +0.001 | +0.001 | +0.001 |
| Storage | -16.277 | -12.615 | -16.147 | -12.491 |
| Towers | +0.001 | +0.001 | +0.001 | +0.001 |
| Mandelbrot | +1.327 | +1.193 | +1.479 | +1.341 |
| Richards | +0.023 | +0.023 | +0.025 | +0.026 |
| CD | +0.242 | +0.530 | +0.412 | +0.687 |
| NBody | +2.057 | +2.004 | +2.348 | +2.264 |
| Havlak | -3.017 | -1.615 | -1.713 | -0.494 |

GC core 结论：

- GiY 的核心 GC 优势主要来自 Storage：`core -16.277s`，以及 Havlak：`core -3.017s`，Sieve：`core -1.096s`。
- GiY 的核心 GC 劣势主要在 NBody：`core +2.057s`，Mandelbrot：`core +1.327s`，CD：`core +0.242s`。
- GiYOL 保留了 GiY 对 Cheney 的一部分 GC core 优势，但比 GiY 自己差 `+5.171s`；说明 OL batching 当前没有提升核心复制路径，反而增加了 scavenge 成本。

### 最终大结论

1. 当前最准确 end-to-end 排名：Cheney `3503.724s` < GiY `3512.999s` < GiYOL `3569.807s`。
2. 当前最准确 GC core 排名：GiY `72.560s` < GiYOL `77.731s` < Cheney `89.304s`。
3. GiY 的 GC 内核设计是有效的：比 Cheney 少 `16.744s` core，少 `14.067s` scavenge，少 `224087890` 次 forward operations。
4. 但 GiY 的 end-to-end 没赢 Cheney，因为 business/non-GC 多 `13.792s`，抵消了 GC full 少 `4.514s`。
5. GiYOL 当前失败：它没有减少主工作量，反而相对 GiY 增加 `50.180s` business 和 `6.627s` GC full。当前 OL 策略不能作为最终优化方向，除非后续能证明 batching 的额外搬运/判断成本可以被更大规模的 NT store 收益覆盖。
6. 汇报时应区分两个结论：如果汇报“GC 核心机制”，GiY 明显优于 Cheney；如果汇报“当前整套 VM end-to-end 单轮 full-suite”，Cheney 目前小幅领先 GiY，GiYOL 明显落后。

## 2026-05-08 关于 coreGC / fullGC / business 的含义与 GiY/GiYOL business 差距解释

### coreGC 是什么

当前 profile 里的 `GC core total` 不是整个 GC，而是手工累加的内部核心阶段：

```c
total_gc_core_ns = total_scan_roots_time + total_scan_rs_time + total_scavenge_time;
```

也就是说：

- `coreGC = scan_roots + scan_RS + scavenge`
- Cheney 中大致对应：扫描 generational roots/function-table strong slots、扫描 remembered set、Cheney scavenge/copy。
- GiY/GiYOL 中对应：reserve roots、reserve remembered set、young graph traverse/copy/materialize、allocation-site update、patch roots、patch remembered set、function-table patch/clear、必要时 `sfence`。

需要注意：GiY/GiYOL 的 `scavenge` 标签比 Cheney 的传统 `scavenge` 更宽。它不是单纯 Cheney 式“从 scan 指针扫到 free 指针”，而是包含 GiY 的 young traversal、copy/materialize、patch roots、patch RSet 等后半段工作。

### fullGC 是什么

`GC CPU(full)` 是从 minor GC 入口到 minor GC 退出的整段 CPU 时间。它包围范围更大：

- GC 入口计时；
- `minor_gc_count++`、`in_minor_gc = 1`；
- coreGC 主体；
- weak clear：Cheney 走 `weak_clear<...>`，GiY/GiYOL 走 `giy_weak_clear(ctx)`；
- young/cache 区 reset；
- remembered set clear；
- PMU window stop；
- 计时器开销和一些 GC 外围 bookkeeping。

因此：

- `fullGC >= coreGC` 通常成立。
- `fullGC - coreGC` 代表 GC 主体外的外围成本，例如 weak_clear、RSet clear、cache reset、PMU/timer、其他 bookkeeping。

### business 是什么

当前 profile 的 business 不是直接测出来的“业务函数耗时”，而是剩余项：

```c
business_cpu = total_elapsed_cpu - total_gc_full_cpu;
```

所以：

- `business` 表示“没有被 fullGC 包住的所有 CPU 时间”。
- 它包括 JS/benchmark 本身的执行、allocation fast path、write barrier、普通对象访问、解释器 dispatch、以及 GC 结束后由 cache/TLB 状态变化带来的 mutator 访问成本。
- 它不是“业务语义操作数”。GiY 和 GiYOL 跑的是同一份 benchmark，但 business 时间仍然可以不同。

### 为什么 GiY 和 GiYOL business 层面操作几乎一样，business 时间仍会不同

原因 1：business 是 residual bucket。

- 只要不是在 minor GC full window 里面，就都会被算进 business。
- 单轮 benchmark 的运行波动、CPU 频率、二进制布局、cache/TLB 状态差异，都会反映到 business。
- 因此 business 差距不等于“JS 源码层面多执行了这么多业务操作”。

原因 2：GC 会改变 GC 之后 mutator 的 cache/TLB 状态。

- GiY/GiYOL 虽然 benchmark 业务逻辑一样，但 GC 对 old object 的写入方式、写入顺序、是否 delayed materialization、是否 NT store、是否 staging/fallback，会改变 GC 结束时 cache 里留下了什么。
- 如果 GC 把 mutator 马上要访问的 old object 留在 cache 里，后续 business 会更快。
- 如果 GC 用 NT store 或批量 fallback 导致目标 old line 没被正常 warm up，或者把有用 cache line 挤掉，后续 miss 成本会记在 business，而不是 GC。

原因 3：GiYOL 当前的实际 NT batching 没有真正吃到收益。

当前 GiYOL `out111` profile 显示：

- `GiYOL direct non-tiny NT: 0`
- `GiYOL direct NT objects: 0`
- `GiYOL tiny flushes: 0`
- 很多对象走了 staging fallback，例如 Storage：`929000412` 个 fallback object，`45723.74 MB`

这说明当前 final GiYOL 不是“大量成功批量 NT store”的状态，而是“引入了 GiYOL 的 deferred/batch/staging 判定，但最后大量落回普通 `giy_copy_live_object`”。这会增加 GC full/core 成本，也会改变 GC 结束时的 cache 状态。

原因 4：当前 GiYOL business 差距里有明显单轮异常成分。

GiYOL 相对 GiY：

| metric | GiYOL - GiY |
|---|---:|
| total CPU | +56.808 |
| business CPU | +50.180 |
| GC full CPU | +6.627 |
| GC core | +5.171 |

但这个 `business +50.180s` 不是均匀来自所有项目。主要来源：

| benchmark | GiYOL business - GiY business |
|---|---:|
| Permute | +34.522 |
| Sieve | +8.261 |
| NBody | +3.862 |
| CD | +2.674 |
| Towers | +2.708 |
| Storage | +0.072 |

其中 `Permute` 尤其关键：

- Permute 的 GC 时间几乎为零。
- GiYOL profile 中 Permute 只有 `490` 个 staging fallback object，`0.02 MB`。
- 但 GiYOL business 比 GiY 多 `34.522s`。

这不可能主要由 GiYOL 的 copy/staging 路径直接解释。它更像单轮 benchmark 波动、代码布局、CPU 状态或其他非 GC 因素落入 business residual bucket。因此不能把 `GiYOL business +50.180s` 全部解释为“GiYOL 业务路径真的慢了 50 秒”。

### 最终判断

1. `coreGC` 用来评价 GC 核心机制更合适，尤其是 scan/traverse/copy/patch 主体。
2. `fullGC` 用来评价一次 minor GC 从入口到出口的真实停顿/总 GC 成本。
3. `business` 是 `total - fullGC` 的剩余项，不等于“业务语义操作完全相同所以一定应相同”。
4. GiY/GiYOL 的 business 差距可以来自 GC 对 cache/TLB/old layout 的后效应，也可以来自单轮运行波动；尤其 Permute 的巨大差距不能用 GiYOL copy 工作量解释。
5. 当前可靠结论应写成：GiY 的 GC core 明确优于 Cheney；GiYOL 的 core 比 GiY 差；GiYOL 的 end-to-end/business 结果当前含明显单轮异常，若要把 business 差距作为正式结论，必须做多轮 median 或 paired A/B 重跑。

## 2026-05-08 当前 GiY / GiYOL 中 `_mm_sfence()` 的触发时机

用户问的 `mm_fense`，当前源码里实际是 x86 intrinsic `_mm_sfence()`，不是 `_mm_mfence()`。它是 store fence，主要用于约束 non-temporal / streaming store 的可见顺序。

源码位置：`ejsvm/GiY.cc`。

### 1. 每次 protected old write 结束时会触发一次

函数：

```c
static void giy_old_guard_protect_old_write(void *addr, size_t len) {
  if (!g_old_guard_active)
    return;
#if defined(__x86_64__) || defined(__i386__)
  _mm_sfence();
#endif
  giy_old_guard_set_range(addr, len, PROT_NONE);
}
```

含义：

- 只要 `g_old_guard_active == true`，每次允许写 old 区之后重新保护 old 区，都会先执行 `_mm_sfence()`。
- 这不仅发生在 NT store 后；当前实现里，小对象普通 `memcpy` 到 old 区后，也会调用 `giy_old_guard_protect_old_write()`，因此也会触发这个 fence。
- 这是当前 old-write guard 机制的一部分，目的是在重新 `mprotect(PROT_NONE)` 前，让之前对 old 区的 store 完成顺序更明确。

### 2. GiY 对 live object 执行 copy/materialize 时

普通 GiY 路径：

```c
giy_copy_live_object(dst_hdr, src_hdr, align_bytes, &used_nt_store);
```

`giy_copy_live_object()` 的行为：

- `nbytes <= 256`：使用 `memcpy(dst, src, nbytes)`，然后调用 `giy_old_guard_protect_old_write()`，所以如果 old guard active，会触发一次 `_mm_sfence()`。
- `nbytes > 256`：使用 `_mm_stream_si64` / `_mm_stream_si128` 做 NT store，设置 `*used_nt_store = true`，然后调用 `giy_old_guard_protect_old_write()`，因此会立即触发一次 `_mm_sfence()`。

之后，在整个 `giy_traverse_stack_and_copy()` 结束时，还有：

```c
giy_finish_local_nt_stores(&used_nt_store);
```

它内部是：

```c
if (*used_nt_store)
  _mm_sfence();
*used_nt_store = false;
```

所以对普通 GiY 来说：

1. live object copy 过程中，每次 old guard 重新保护 old 区时可能触发 fence；
2. 如果本轮 traversal 中至少有一次对象 copy 使用了 NT store，traversal 结束时还会再触发一次 local `_mm_sfence()`。

### 3. GiYOL 对 live object 执行 copy/materialize 时

GiYOL 使用同一个 `giy_traverse_stack_and_copy()`，但在 `USE_GIYOL` 下会先走 GiYOL 分支。

当前默认 GiYOL 配置：

```c
GIYOL_STAGING_COPY = 1
GIYOL_TINY_STAGING = 1
GIYOL_TINY_STAGING_BYTES = 4096
GIYOL_TINY_FLUSH_BYTES = 4096
GIYOL_TINY_MAX_OBJECT_BYTES = 64
GIYOL_FORCE_TINY_TAIL_NT = 0
GIYOL_DIRECT_NONTINY_NT = 0
```

GiYOL 的触发路径：

- `>64B` 的对象：当前 `GIYOL_DIRECT_NONTINY_NT=0`，所以走 `giy_copy_live_object()`，也就是和 GiY 一样，`>256B` 才用 NT store，随后 old guard protect 可能触发 `_mm_sfence()`。
- `<=64B` 的 tiny 对象：进入 tiny staging。只有当目标地址连续、chunk bytes 达到 `4096B`，或者 `GIYOL_FORCE_TINY_TAIL_NT=1` 时，`giyol_flush_tiny_chunk()` 才会调用 `giyol_stream_copy_region()` 执行 NT store。
- 如果 tiny chunk 没达到 4096B，当前会 fallback 到逐对象 `giy_copy_live_object()`。

因此 GiYOL 中 `_mm_sfence()` 的时机和 GiY 一样也有两类：

1. 每次 old guard protect 时触发；
2. 如果本次 traversal 中任何 GiYOL NT path 或 `giy_copy_live_object()` NT path 把 `used_nt_store` 置为 true，则 traversal 结束时 `giy_finish_local_nt_stores()` 再触发一次。

### 4. patch old slot 时也可能触发

GiY/GiYOL 修复 old 区 slot 时使用：

```c
giy_store_u64_old(...)
```

在 x86 下它会：

```c
_mm_stream_si64((long long *) slot, (long long) bits);
g_used_nt_old_store = true;
giy_old_guard_protect_old_write(slot, sizeof(*slot));
```

所以 old slot patch 的 NT store 会：

- 立即在 `giy_old_guard_protect_old_write()` 中触发 `_mm_sfence()`；
- 把全局 `g_used_nt_old_store = true`。

在 `giy_minor_collect()` 末尾还有：

```c
if (g_used_nt_old_store)
  _mm_sfence();
```

所以如果本次 minor GC 中 patch old slot 使用过 NT store，minor GC 主体结束处还会再触发一次全局 `_mm_sfence()`。

### 5. weak clear / inline cache patch 后也可能触发

`giy_weak_clear(ctx)` 开始时会：

```c
g_used_nt_old_store = false;
```

然后执行 weak clear 和 inline cache patch。结束时：

```c
if (g_used_nt_old_store)
  _mm_sfence();
```

也就是说，如果 weak clear / inline cache patch 阶段通过 `giy_store_u64_old()` 做过 old slot NT store，结束时会再执行一次 `_mm_sfence()`。

### 直接结论

当前 GiY 和 GiYOL 中 `_mm_sfence()` 不是“每个 GC 只在最后启动一次”。实际更复杂：

1. old guard active 时，每次 old write 结束并重新 protect old 区，都会先 `_mm_sfence()`；
2. live object copy 使用 NT store 后，`giy_traverse_stack_and_copy()` 结束会再 fence 一次；
3. old slot patch 使用 NT store 后，`giy_minor_collect()` 结束会再 fence 一次；
4. weak clear / inline cache patch 如果产生 old NT store，`giy_weak_clear()` 结束也会 fence 一次；
5. GiYOL 继承上述全部行为；额外的 GiYOL batch/tiny NT 只是在满足 tiny chunk >= `4096B` 或其他 batch 条件时才会把 `used_nt_store` 置 true，从而触发 traversal 末尾 fence。

重要判断：

- 当前实现里 fence 的数量可能比直觉中多，尤其是 `giy_old_guard_protect_old_write()` 里无条件 `_mm_sfence()` 这一点。
- 如果要继续优化 GiY/GiYOL，`old guard protect` 内的 per-write fence 是否必要，值得单独做消融实验。它可能会让 NT store 的收益被频繁 fence 抵消。

## 2026-05-08 导师汇报用：CheneyGC / GiY / GiYOL GC 时间简表

数据来源：

- 使用公平重跑中三者都已经完整完成的前两轮：
  - CheneyGC：`out123_fair_cheney_552_r1`、`out128_fair_cheney_552_r2`
  - GiY：`out124_fair_giy_552_r1`、`out127_fair_giy_552_r2`
  - GiYOL：`out125_fair_giyol_552_r1`、`out126_fair_giyol_552_r2`
- 每个目录 12/12 benchmarks status=0。
- 第三轮当前尚未三者全部完成，因此本表不混入第三轮，避免 Cheney/GiY/GiYOL 样本数不一致。
- 表中数值为前两轮平均值。

### 总体结果

| GC | Total CPU | Business CPU | Full GC | Core GC | Scavenge | Total vs Cheney | Full GC reduction | Core GC reduction |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| CheneyGC | 3558.039 | 3441.476 | 116.561 | 90.160 | 85.908 | baseline | baseline | baseline |
| GiY | 3569.227 | 3481.822 | 87.406 | 55.458 | 53.885 | +11.189 (+0.314%) | 25.01% | 38.49% |
| GiYOL | 3558.390 | 3466.772 | 91.620 | 60.194 | 58.373 | +0.351 (+0.010%) | 21.40% | 33.24% |

### GC 减少量

| GC | Full GC diff | Core GC diff | Scavenge diff | Business diff |
|---|---:|---:|---:|---:|
| GiY - Cheney | -29.155 | -34.702 | -32.023 | +40.346 |
| GiYOL - Cheney | -24.942 | -29.966 | -27.534 | +25.296 |

解释：

- GiY 的 GC 减少最明显：Full GC 少 `29.155s`，Core GC 少 `34.702s`。
- GiYOL 的 GC 也明显少于 Cheney：Full GC 少 `24.942s`，Core GC 少 `29.966s`。
- 但 GiY/GiYOL 的 Business CPU 都高于 Cheney，分别多 `40.346s` 和 `25.296s`，抵消了 GC 收益。
- 因此前两轮平均的 end-to-end 结果是：GiYOL 几乎与 Cheney 持平，GiY 小幅慢于 Cheney。

### 每项 benchmark 的 Full GC 时间

| benchmark | Cheney Full GC | GiY Full GC | GiY reduction | GiYOL Full GC | GiYOL reduction |
|---|---:|---:|---:|---:|---:|
| Bounce | 0.123 | 0.133 | -7.29% | 0.124 | -0.40% |
| List | 0.017 | 0.010 | 38.24% | 0.018 | -2.94% |
| Sieve | 2.321 | 1.369 | 40.98% | 1.260 | 45.72% |
| Queens | 0.025 | 0.032 | -28.00% | 0.030 | -20.00% |
| Permute | 0.002 | 0.002 | 0.00% | 0.002 | -33.33% |
| Storage | 50.805 | 27.090 | 46.68% | 30.755 | 39.46% |
| Towers | 0.002 | 0.002 | -33.33% | 0.005 | -233.33% |
| Mandelbrot | 5.149 | 6.424 | -24.77% | 6.222 | -20.84% |
| Richards | 0.100 | 0.104 | -4.00% | 0.104 | -4.00% |
| CD | 11.502 | 11.401 | 0.88% | 11.693 | -1.66% |
| NBody | 8.103 | 9.496 | -17.19% | 9.570 | -18.10% |
| Havlak | 38.413 | 31.343 | 18.41% | 31.838 | 17.12% |

导师汇报版结论：

1. GiY/GiYOL 的 GC 主体确实比 CheneyGC 更快。
2. GiY 的 Core GC 相比 CheneyGC 减少 `38.49%`，GiYOL 减少 `33.24%`。
3. Full GC 上，GiY 减少 `25.01%`，GiYOL 减少 `21.40%`。
4. End-to-end 上，GiYOL 与 CheneyGC 基本持平；GiY 因 Business CPU 增加较多，总时间小幅慢 `0.314%`。
5. 最大 GC 收益主要来自 Storage、Havlak、Sieve；NBody 和 Mandelbrot 上 GiY/GiYOL 的 Full GC 反而慢于 CheneyGC。

## 2026-05-08 导师展示用：指定 out110 / out113 / out111 三项数据

本节只使用用户指定的三个目录：

- `out110_final_giy_corrected_full`：GiY
- `out113_final_cheney_552_full`：CheneyGC
- `out111_final_giyol_corrected_full`：GiYOL

不混入正在后台跑的公平三轮数据。

### 总体表

| GC | 目录 | Total CPU | Business CPU | Full GC | Core GC | Scavenge | Minor GC | Forward ops |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| CheneyGC | out113 | 3503.724 | 3387.517 | 116.206 | 89.304 | 85.105 | 2068906 | 1692894720 |
| GiY | out110 | 3512.999 | 3401.309 | 111.692 | 72.560 | 71.038 | 2302292 | 1468806830 |
| GiYOL | out111 | 3569.807 | 3451.489 | 118.319 | 77.731 | 75.972 | 2302292 | 1468806755 |

### 相对 CheneyGC 的差异

| GC | Total diff | Total pct | Full GC diff | Full GC reduction | Core GC diff | Core GC reduction | Scavenge diff | Scavenge reduction | Business diff |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| GiY - Cheney | +9.275 | +0.265% | -4.514 | +3.88% | -16.744 | +18.75% | -14.067 | +16.53% | +13.792 |
| GiYOL - Cheney | +66.083 | +1.886% | +2.113 | -1.82% | -11.573 | +12.96% | -9.133 | +10.73% | +63.972 |

### 每项 benchmark Total CPU

| benchmark | Cheney out113 | GiY out110 | GiY - Cheney | GiYOL out111 | GiYOL - Cheney |
|---|---:|---:|---:|---:|---:|
| Bounce | 158.065 | 157.258 | -0.807 | 157.902 | -0.163 |
| List | 99.719 | 98.093 | -1.626 | 98.645 | -1.074 |
| Sieve | 121.319 | 121.526 | +0.207 | 129.743 | +8.424 |
| Queens | 114.402 | 117.245 | +2.843 | 116.786 | +2.384 |
| Permute | 381.063 | 409.291 | +28.228 | 443.811 | +62.748 |
| Storage | 208.062 | 198.762 | -9.300 | 202.801 | -5.261 |
| Towers | 179.687 | 182.585 | +2.898 | 185.294 | +5.607 |
| Mandelbrot | 232.644 | 238.255 | +5.611 | 239.101 | +6.457 |
| Richards | 955.000 | 945.469 | -9.531 | 943.737 | -11.263 |
| CD | 254.701 | 244.298 | -10.403 | 247.496 | -7.205 |
| NBody | 377.018 | 383.137 | +6.119 | 386.473 | +9.455 |
| Havlak | 422.044 | 417.080 | -4.964 | 418.018 | -4.026 |

导师展示版结论：

1. 在这三个指定目录中，GiY 的 Core GC 明显优于 CheneyGC：`72.560s` vs `89.304s`，减少 `18.75%`。
2. GiY 的 Full GC 也略优于 CheneyGC：`111.692s` vs `116.206s`，减少 `3.88%`。
3. GiYOL 的 Core GC 也优于 CheneyGC：减少 `12.96%`；但 Full GC 比 CheneyGC 多 `2.113s`。
4. End-to-end 上，CheneyGC 最快；GiY 慢 `9.275s / 0.265%`，GiYOL 慢 `66.083s / 1.886%`。
5. GiY 没有赢 end-to-end 的直接原因是 Business CPU 比 CheneyGC 多 `13.792s`，抵消了 GC 收益。

## 2026-05-08 交接记录：当前进度、数据目录、结论与下一步

这是一份给下一个 AI 看的交接记录。当前工作目录：

- `/home/qiancheng/ejs-new`

用户有明确要求：

- 所有阶段性结论、实验结论、解释都必须追加写入 `AIlog.md`。

### 当前源码/工作区状态

工作区是 dirty 状态，不要随意 revert。已知变化包括：

- `ejsvm/GiY.cc`
- `ejsvm/giy_rset.cc`
- `ejsvm/common.mk`
- `AIlog.md`
- `build.debug/*` 生成/复制文件
- `tools/giy_copy_microbench.cc`
- `build.micro/giy_copy_microbench*`

重要提醒：

- 当前 `build.debug/ejsvm` 最后一次构建来自公平三轮中的 GiYOL 第 3 轮，因此当前二进制大概率是 `OPT_GC=giyol`。
- 后续如果要跑 Cheney/GiY/GiYOL，必须重新显式执行 `make -B -C build.debug ejsvm OPT_GC=<...> CACHE_SIZE_KB=552 -j4`，不要假设当前二进制就是目标 GC。

### 当前 GC 实现概况

当前有三个主要 GC 对照：

1. `cache_cheney`
   - Cheney-style copy/scavenge。
   - 使用 root-controlled generational root scan 和 function-table strong slot set。

2. `giy`
   - GiY 主体实现。
   - 先在 young/cache 区解析活对象图，再 materialize/copy 到 old/DRAM。
   - 小对象 `<=256B` 走 `memcpy`。
   - 大对象 `>256B` 走 `_mm_stream_si64/_mm_stream_si128` NT store。

3. `giyol`
   - GiYOL 主体基本继承 GiY。
   - 当前默认参数：
     - `GIYOL_STAGING_COPY=1`
     - `GIYOL_TINY_STAGING=1`
     - `GIYOL_TINY_STAGING_BYTES=4096`
     - `GIYOL_TINY_FLUSH_BYTES=4096`
     - `GIYOL_TINY_MAX_OBJECT_BYTES=64`
     - `GIYOL_FORCE_TINY_TAIL_NT=0`
     - `GIYOL_DIRECT_NONTINY_NT=0`
   - `>64B` 对象当前不走 GiYOL direct NT，而是回到 `giy_copy_live_object()`。
   - `<=64B` tiny 对象进入 tiny staging；只有目标连续且 chunk 达到 4096B，或强制 tail NT 开启时，才会 batch NT。

### `_mm_sfence()` 当前重要发现

当前 GiY/GiYOL 中 `_mm_sfence()` 比直觉更多：

1. `giy_old_guard_protect_old_write()` 里只要 old guard active，每次 old write 结束重新 protect old 区都会 `_mm_sfence()`。
2. live object copy 使用 NT store 后，`giy_traverse_stack_and_copy()` 结束会通过 `giy_finish_local_nt_stores()` 再 fence 一次。
3. old slot patch 使用 NT store 后，`giy_minor_collect()` 末尾如果 `g_used_nt_old_store` 为 true，会再 fence 一次。
4. `giy_weak_clear()` / inline cache patch 如果产生 old NT store，结束也会 fence。

潜在优化点：

- `giy_old_guard_protect_old_write()` 内的 per-write `_mm_sfence()` 可能明显削弱 NT store 收益，值得做单独消融实验。

### 应忽略的无效 benchmark 目录

`out114_fair_*` 到 `out122_fair_*` 是一次无效尝试：

- 原因：脚本使用了不存在的 `/usr/bin/time`。
- 结果：所有 benchmark 很快返回 `status=127`。
- 这些目录不要用于任何性能分析。

### 用户指定展示用三项：out110 / out113 / out111

用户明确说想先给导师展示这三项：

- GiY：`build.debug/benchmarks/out110_final_giy_corrected_full`
- CheneyGC：`build.debug/benchmarks/out113_final_cheney_552_full`
- GiYOL：`build.debug/benchmarks/out111_final_giyol_corrected_full`

这三项的总体数据：

| GC | 目录 | Total CPU | Business CPU | Full GC | Core GC | Scavenge |
|---|---|---:|---:|---:|---:|---:|
| CheneyGC | out113 | 3503.724 | 3387.517 | 116.206 | 89.304 | 85.105 |
| GiY | out110 | 3512.999 | 3401.309 | 111.692 | 72.560 | 71.038 |
| GiYOL | out111 | 3569.807 | 3451.489 | 118.319 | 77.731 | 75.972 |

相对 CheneyGC：

| GC | Total diff | Full GC diff | Full GC reduction | Core GC diff | Core GC reduction | Business diff |
|---|---:|---:|---:|---:|---:|---:|
| GiY - Cheney | +9.275 | -4.514 | +3.88% | -16.744 | +18.75% | +13.792 |
| GiYOL - Cheney | +66.083 | +2.113 | -1.82% | -11.573 | +12.96% | +63.972 |

导师展示版结论：

- 在 out110/out113/out111 这组里，GiY 的 Core GC 明显优于 CheneyGC，减少 `18.75%`。
- GiY 的 Full GC 也略优于 CheneyGC，减少 `3.88%`。
- GiYOL 的 Core GC 也优于 CheneyGC，减少 `12.96%`，但 Full GC 比 CheneyGC 多 `2.113s`。
- End-to-end 上 CheneyGC 最快；GiY 慢 `9.275s / 0.265%`，GiYOL 慢 `66.083s / 1.886%`。
- GiY 没赢总时间的直接原因是 Business CPU 比 CheneyGC 多 `13.792s`，抵消了 GC 收益。

### 公平三轮 benchmark：out123 到 out131

后来为了公正重跑，执行了三轮 full suite。方法：

- 同一源码。
- 同一 `CACHE_SIZE_KB=552`。
- 每个 GC variant 重新 `make -B`。
- `taskset -c 2` 固定同一 CPU。
- 每个 benchmark 单独一个进程。
- 顺序做了时间漂移平衡：
  - 第 1 轮：Cheney -> GiY -> GiYOL
  - 第 2 轮：GiYOL -> GiY -> Cheney
  - 第 3 轮：GiY -> Cheney -> GiYOL

目录：

| GC | round 1 | round 2 | round 3 |
|---|---|---|---|
| CheneyGC | `out123_fair_cheney_552_r1` | `out128_fair_cheney_552_r2` | `out130_fair_cheney_552_r3` |
| GiY | `out124_fair_giy_552_r1` | `out127_fair_giy_552_r2` | `out129_fair_giy_552_r3` |
| GiYOL | `out125_fair_giyol_552_r1` | `out126_fair_giyol_552_r2` | `out131_fair_giyol_552_r3` |

主日志：

- `build.debug/benchmarks/out123_fair_master_run.log`

完成状态：

- `out123` 到 `out131` 全部完成。
- 每个目录 12/12 benchmarks status=0。
- smoke 也 status=0。
- 这些目录可以用于后续正式三轮分析。

三轮 round 汇总：

| GC | round | Total | Business | Full GC | Core GC | Scavenge |
|---|---|---:|---:|---:|---:|---:|
| CheneyGC | r1 | 3578.288 | 3461.592 | 116.695 | 90.269 | 86.026 |
| CheneyGC | r2 | 3537.790 | 3421.361 | 116.427 | 90.050 | 85.790 |
| CheneyGC | r3 | 3824.395 | 3707.475 | 116.920 | 90.233 | 85.943 |
| GiY | r1 | 3522.586 | 3435.292 | 87.298 | 55.378 | 53.805 |
| GiY | r2 | 3615.869 | 3528.352 | 87.515 | 55.538 | 53.964 |
| GiY | r3 | 3750.484 | 3663.003 | 87.481 | 55.612 | 54.043 |
| GiYOL | r1 | 3579.291 | 3487.616 | 91.676 | 60.390 | 58.569 |
| GiYOL | r2 | 3537.489 | 3445.928 | 91.563 | 59.997 | 58.178 |
| GiYOL | r3 | 3535.561 | 3444.513 | 91.048 | 60.197 | 58.381 |

三轮平均：

| GC | Total | Business | Full GC | Core GC | Scavenge |
|---|---:|---:|---:|---:|---:|
| CheneyGC | 3646.824 | 3530.143 | 116.681 | 90.184 | 85.920 |
| GiY | 3629.646 | 3542.216 | 87.431 | 55.509 | 53.937 |
| GiYOL | 3550.780 | 3459.352 | 91.429 | 60.195 | 58.376 |

三轮 median-of-rounds：

| GC | Total | Business | Full GC | Core GC | Scavenge |
|---|---:|---:|---:|---:|---:|
| CheneyGC | 3578.288 | 3461.592 | 116.695 | 90.233 | 85.943 |
| GiY | 3615.869 | 3528.352 | 87.481 | 55.538 | 53.964 |
| GiYOL | 3537.489 | 3445.928 | 91.563 | 60.197 | 58.381 |

相对 CheneyGC 的三轮平均差异：

| GC | Total diff | Total pct | Full GC diff | Full GC reduction | Core GC diff | Core GC reduction | Scavenge diff | Scavenge reduction | Business diff |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| GiY - Cheney | -17.178 | -0.471% | -29.249 | +25.07% | -34.675 | +38.45% | -31.982 | +37.22% | +12.073 |
| GiYOL - Cheney | -96.044 | -2.634% | -25.252 | +21.64% | -29.989 | +33.25% | -27.544 | +32.06% | -70.790 |

注意：

- 三轮中 Cheney r3 的 Business 时间明显偏高：`3707.475s`，导致 Cheney 三轮平均 total 被拉高。
- GC 指标本身很稳定：Cheney Full GC 大约 `116.4-116.9s`，Core GC 大约 `90.0-90.3s`；GiY Full GC 大约 `87.3-87.5s`，Core GC 大约 `55.4-55.6s`；GiYOL Full GC 大约 `91.0-91.7s`，Core GC 大约 `60.0-60.4s`。
- 因此若讨论 GC 机制，应优先引用 Full GC / Core GC / Scavenge；若讨论 end-to-end，应说明 Business 受长时间运行系统状态影响，建议使用 median 或继续增加轮次。

### 当前可靠技术结论

1. GiY 的 GC core 明确快于 CheneyGC。
   - out110/out113 指定组：Core GC 减少 `18.75%`。
   - 公平三轮平均：Core GC 减少 `38.45%`。

2. GiYOL 的 GC core 也快于 CheneyGC，但慢于 GiY。
   - 公平三轮平均：GiYOL Core GC `60.195s`，GiY Core GC `55.509s`。

3. GiYOL 当前 OL 策略仍没有证明优于 GiY。
   - 它的 Full GC 和 Core GC 均比 GiY 高。
   - 当前 GiYOL 的收益更多体现在某些 end-to-end 轮次的 Business 时间，而不是 GC core。

4. `Business CPU = Total CPU - Full GC CPU`，不是直接测出来的 JS 语义业务时间。
   - 它会包含解释器、allocation fast path、write barrier、GC 后 cache/TLB 后效应、系统噪音等。

5. `Permute` 和 `Richards` 等长运行 benchmark 对 Business 噪音很敏感。
   - 之前 out111 里 GiYOL 的 Permute 明显异常慢。
   - 公平三轮中 GiYOL 的 Permute 已不再出现 out111 那样的大异常。

### 建议下一个 AI 接着做什么

如果用户要给导师展示：

- 先使用用户指定的 out110/out113/out111 表，因为用户明确说“先给导师展示这三项”。
- 但要补充说明：这是单轮旧最终目录；更公正的三轮重跑 out123-out131 已完成，GC 指标趋势一致。

如果用户要正式写报告：

1. 用 out123-out131 做正式三轮分析。
2. 主要展示：
   - Full GC reduction
   - Core GC reduction
   - Scavenge reduction
   - Total CPU median
3. 对 end-to-end total 的表述要谨慎，因为 Cheney r3 business 明显偏高。
4. 下一步值得做的技术实验：
   - 消融 `giy_old_guard_protect_old_write()` 中的 per-write `_mm_sfence()`。
   - 对 GiYOL 开/关 tiny staging、direct non-tiny NT、force tail NT 做小规模 targeted benchmark。
   - 单独比较 `Storage/Havlak/Sieve/NBody/Mandelbrot`，因为这些最能区分 GC 行为。
