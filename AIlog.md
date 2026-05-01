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
