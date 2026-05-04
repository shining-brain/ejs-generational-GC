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
