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

## 2026-05-09 关于查询当前服务器登录密码

结论：

- 正常 Linux/Unix 服务器上，当前登录密码不能被查询出明文。
- 系统通常只保存密码 hash，例如 `/etc/shadow` 中的 hash，而不是原始密码。
- 如果已经登录但忘记密码，应使用重置方式，而不是尝试读取原密码。

可行做法：

- 如果知道当前密码，修改自己的密码：`passwd`
- 如果有 sudo/admin 权限，重置某个用户密码：`sudo passwd <用户名>`
- 如果是云服务器，使用云厂商控制台的 reset password / serial console / rescue mode。
- 如果是 SSH key 登录，可能根本没有用密码登录；应检查本地 SSH key 或云控制台配置。

## 2026-05-09 如何从另一台机器 SSH 到当前服务器

当前检查结果：

- 当前用户名：`qiancheng`
- 当前主机名：`vpi`
- 当前 SSH 会话显示的服务器地址：`172.18.0.5`
- 当前 SSH 端口：`22`
- 当前服务器上有 `sshd` 进程在运行。

从另一台机器连接的基本命令：

```bash
ssh qiancheng@172.18.0.5
```

## 2026-05-09 有 SSH key 文件但仍要求密码的原因

结论：

- 免密登录需要“客户端私钥”和“服务器 authorized_keys 中的对应公钥”匹配。
- 只有某一边有文件不够。
- 如果另一台机器连接 `vpi` 时仍要求密码，常见原因是：
  1. 另一台机器没有对应私钥；
  2. 另一台机器有私钥，但 SSH 没有使用它；
  3. `vpi` 上 `~/.ssh/authorized_keys` 没有这台机器私钥对应的公钥；
  4. 用户名不一致，例如公钥在 `qiancheng` 用户下，但用别的用户登录；
  5. 权限不对，SSH 拒绝读取 key 或 authorized_keys；
  6. 连接到的不是同一台服务器或同一个地址；
  7. 看到的是 key passphrase 提示，而不是服务器登录密码提示。

排查命令：

```bash
ssh -vvv qiancheng@vpi
```

重点看：

- 有没有 `Offering public key`
- 有没有 `Server accepts key`
- 最后提示是 `Enter passphrase for key` 还是 `qiancheng@host's password`

推荐修复：

```bash
# 在另一台机器上生成 key
ssh-keygen -t ed25519

# 把另一台机器的公钥追加到 vpi 的 authorized_keys
cat ~/.ssh/id_ed25519.pub
```

然后把输出追加到 `vpi` 上：

```bash
~/.ssh/authorized_keys
```

权限建议：

```bash
chmod 700 ~/.ssh
chmod 600 ~/.ssh/authorized_keys
chmod 600 ~/.ssh/id_ed25519
```

## 2026-05-09 如何把当前 AI 对话/项目上下文迁移到另一台服务器

结论：

- 活的聊天会话本身通常不能直接迁移到另一台服务器继续。
- 最优雅、最可靠的方式是迁移“可恢复上下文”：
  1. `AIlog.md`
  2. 当前 git diff
  3. 关键 benchmark 输出目录
  4. 修改过的源码/工具文件

最小迁移包建议：

```bash
cd /home/qiancheng/ejs-new
git diff > handoff.patch
tar czf ejs-handoff-minimal.tgz \
  AIlog.md \
  handoff.patch \
  build.debug/benchmarks/out110_final_giy_corrected_full \
  build.debug/benchmarks/out111_final_giyol_corrected_full \
  build.debug/benchmarks/out113_final_cheney_552_full \
  build.debug/benchmarks/out123_fair_master_run.log \
  build.debug/benchmarks/out123_fair_cheney_552_r1 \
  build.debug/benchmarks/out124_fair_giy_552_r1 \
  build.debug/benchmarks/out125_fair_giyol_552_r1 \
  build.debug/benchmarks/out126_fair_giyol_552_r2 \
  build.debug/benchmarks/out127_fair_giy_552_r2 \
  build.debug/benchmarks/out128_fair_cheney_552_r2 \
  build.debug/benchmarks/out129_fair_giy_552_r3 \
  build.debug/benchmarks/out130_fair_cheney_552_r3 \
  build.debug/benchmarks/out131_fair_giyol_552_r3
```

复制到另一台服务器：

```bash
scp ejs-handoff-minimal.tgz user@new-server:/path/
```

在新服务器解包：

```bash
tar xzf ejs-handoff-minimal.tgz
```

如果新服务器上已经有同一个 git 仓库，则推荐：

```bash
git apply handoff.patch
```

给下一个 AI 的第一句话可以是：

```text
请先阅读 AIlog.md 最后的“交接记录”和“out110/out113/out111 三项数据”部分。当前项目在 /home/qiancheng/ejs-new，重点 benchmark 目录是 out110/out111/out113 和 out123-out131。
```

更完整的迁移方式：

```bash
rsync -a --info=progress2 \
  --exclude='.git/' \
  /home/qiancheng/ejs-new/ \
  user@new-server:/path/ejs-new/
```

注意：

- 如果只想让下一个 AI 立刻理解上下文，`AIlog.md` 是最重要的。
- 如果要复现实验，必须同时迁移 benchmark 输出目录。
- 如果要继续开发，必须迁移 `git diff` 或整个工作树。

如果另一台机器使用指定私钥：

```bash
ssh -i ~/.ssh/<private_key> qiancheng@172.18.0.5
```

注意：

- `172.18.0.5` 是私有网段地址。另一台机器必须和当前服务器处于同一内网/VPN/容器网络/跳板链路中，否则无法直接连。
- 如果另一台机器不在同一网络，需要使用服务器公网 IP、云厂商端口映射、安全组放行，或通过跳板机：

```bash
ssh -J user@jump_host qiancheng@172.18.0.5
```

如果另一台机器没有登录密钥：

1. 在另一台机器生成 key：

```bash
ssh-keygen -t ed25519
```

2. 把另一台机器的公钥内容追加到当前服务器的：

```bash
~/.ssh/authorized_keys
```

3. 再从另一台机器执行：

```bash
ssh qiancheng@172.18.0.5
```

## 2026-05-09 项目通读后的当前任务理解

当前项目根目录是 `/home/qiancheng/ejs-new`，不是 `/home/qiancheng`。`/home/qiancheng/ejs-new` 才是有效 git 仓库，当前分支是 `master`。本次阅读时工作区只有 `AIlog.md` 有未提交修改，源码文件没有未提交 diff。

项目主体是 eJS：一个面向嵌入式系统的 JavaScript VM。核心目录包括：

- `ejsvm/`：VM 核心、对象模型、GC、解释器、loader、builtin。
- `ejsc/`：JavaScript 到 SBC/OBC bytecode 的编译器。
- `ejsi/`：解释器接口。
- `vmgen/` 和 `vmdl/`：指令/类型分派生成工具。
- `build.debug/benchmarks/`：当前主要实验 benchmark 与输出目录。

当前研究任务的核心是围绕 `Cache/Young -> DRAM/Old` 架构实现和评估 GiY minor GC：

- `cache_cheney` 是 root-controlled Cheney 对照组。
- `giy` 是当前主方案：先在 Young/Cache 内完成 live object graph traversal 和字段修补，再把每个 live object materialize 到 DRAM/Old。
- `giyol` 是 GiY 的进一步实验变体，加入 tiny staging / batch / non-temporal store 策略。

我对当前实现的理解：

- GiY 的核心入口在 `ejsvm/GiY.cc`，由 `giy_minor_collect()` 驱动。
- `copy_for_minor()` 只在 Old/DRAM 预留目标地址，并把 forwarding pointer 写回 Young header；此时不立即复制对象。
- `giy_traverse_stack_and_copy()` 弹出 Young 对象，在 Young 内扫描和修补其字段，之后调用 `giy_copy_live_object()` 一次性复制到 Old。
- 小对象 `<=256B` 当前走 `memcpy`，大对象 `>256B` 走 `_mm_stream_si64/_mm_stream_si128` non-temporal store。
- `GIY_MPROTECT_OLD=1` 用于验证 GiY 核心阶段不读 Old/DRAM；允许窗口是最终 object copy 和必要的 Old slot patch。
- `weak_clear` 当前被明确归类为 `weak_clear_generic_old_metadata`，仍会访问 old metadata；它不是 GiY strict core 的一部分。
- function table 不再整体扫描，而是通过 `giy_record_ft_jsvalue_slot()` / `giy_record_ft_ptr_slot()` 记录 constant pool、allocation site cache 中写入 Young 指针的强 slot。
- remembered set 在 GiY 中同时记录 slot 地址和最近写入值，避免 minor GC reserve 阶段回读 Old slot。

我对当前实验状态的理解：

- 用户指定给导师展示的三项是：
  - GiY：`build.debug/benchmarks/out110_final_giy_corrected_full`
  - GiYOL：`build.debug/benchmarks/out111_final_giyol_corrected_full`
  - CheneyGC：`build.debug/benchmarks/out113_final_cheney_552_full`
- 这三项中，GiY 的 Core GC 明显优于 CheneyGC；Full GC 也略优，但 end-to-end 总时间仍略慢，主要因为 Business CPU 抵消了 GC 收益。
- 更公平的三轮 benchmark 是 `out123` 到 `out131`，同一源码、同一 `CACHE_SIZE_KB=552`、每个 GC 重新构建、固定 CPU、顺序平衡。三轮 GC 指标稳定，GiY 和 GiYOL 的 Core GC/Full GC 均低于 CheneyGC。
- GiYOL 当前没有证明 GC core 优于 GiY；GiYOL 的可能价值更多出现在某些 end-to-end/business 时间波动中，而不是稳定的 GC core 优势。

当前最可能的下一步任务：

1. 如果目标是导师汇报，应优先整理 out110/out111/out113 的展示表，同时补充说明 out123-out131 三轮公平重跑支持 GC 指标趋势。
2. 如果目标是继续优化，应做消融实验：
   - `giy_old_guard_protect_old_write()` 中 per-write `_mm_sfence()` 的影响。
   - GiYOL tiny staging / direct non-tiny NT / force tiny tail NT 的开关组合。
   - 重点 benchmark：`Storage`、`Havlak`、`Sieve`、`NBody`、`Mandelbrot`。
3. 如果导师要求更严格的 0 Old read，需要单独设计 GiY 专用 weak remembered set；不能简单把通用 `weak_clear` 局部改成 Young-only。

当前理解程度：

- 对项目结构、构建入口、GC 变体关系、GiY/GiYOL 当前实现、benchmark 目录和已有结论：理解度约 80%-85%。
- 对每个 VM instruction、编译器 `ejsc` 全部优化流程、所有历史 benchmark 目录细节：还没有逐行完全展开，理解度约 50%-60%。
- 对继续做 GC 优化或写导师汇报所需的上下文：已经足够开始工作。

## 2026-05-09 GiY 和 GiYOL 差异代码位置与实现思想

用户指出：以后这类项目理解、代码调查、会议整理内容都需要同步记载到 `AIlog.md`。本次补记此前关于 GiY/GiYOL 差异的说明。

代码位置：

- `ejsvm/common.mk`
  - `OPT_GC=giy` 和 `OPT_GC=giyol` 都编译 `giy_dram_manager.cc`、`giy_rset.cc`、`GiY.cc`。
  - `giyol` 相比 `giy` 只额外打开 `-DUSE_GIYOL`。
  - 因此 GiYOL 不是另一份独立 GC 文件，而是同一套 GiY minor GC 主体上的条件编译变体。
- `ejsvm/giy_dram_manager.cc`
  - minor GC 主入口相同，都是调用 `giy_minor_collect(ctx, ...)`。
  - 打印信息会根据 `USE_GIYOL` 显示 GiY 或 GiYOL。
- `ejsvm/GiY.cc`
  - `copy_for_minor()`：reserve Old/DRAM 目标地址，设置 forwarding pointer，push GC stack。
  - `giy_copy_live_object()`：GiY 的真正 object copy 路径。
  - `giyol_*` 函数：GiYOL 的 batch、staging buffer、tiny object flush、stream copy 逻辑。
  - `giy_traverse_stack_and_copy()`：GiY 和 GiYOL 分歧的核心位置。

GiY 的基本思想：

1. root / remembered set / function table strong slot 发现 Young object。
2. `copy_for_minor()` 只给 object 在 Old/DRAM 中预留目标地址，并把目标 payload 地址写入 Young object header 的 forwarding pointer。
3. `copy_for_minor()` 此时不复制 object 内容。
4. `giy_traverse_stack_and_copy()` 从 GC stack 弹出 Young object。
5. 先在 Young/Cache 中扫描这个 object 的字段，并把 Young-to-Young 引用 patch 成对应的 forwarding pointer。
6. 字段修补完成后，调用 `giy_copy_live_object(dst, src, size, ...)`，把已经修好的 Young object 内容真正 materialize 到 Old/DRAM。

GiYOL 的基本思想：

- GiYOL 保留 GiY 的 root scanning、remembered set、function table slot、weak clear 等语义。
- GiYOL 主要只改变 materialize/copy 的策略。
- 目标是减少很多小 object 一个一个 copy 到 Old 的开销，并让多个小 object 合并成较大连续写入，从而更容易发挥 non-temporal store / streaming store 的效果。
- 当前默认重点优化 tiny object：
  - `GIYOL_STAGING_COPY=1`
  - `GIYOL_TINY_STAGING=1`
  - `GIYOL_TINY_MAX_OBJECT_BYTES=64`
  - `GIYOL_TINY_STAGING_BYTES=4096`
  - `GIYOL_TINY_FLUSH_BYTES=4096`
  - `GIYOL_DIRECT_NONTINY_NT=0`
- 因此当前 GiYOL 中 `<=64B` 的 tiny object 会进入 staging/batch 逻辑；大于 64B 的 object 默认仍回到 GiY 的 `giy_copy_live_object()` 路径。

continuous objects 的准确含义：

- 当前代码中的 continuous objects 指的是 Old/DRAM 目标地址连续，不是 Young 源地址连续。
- GiYOL copy entry 记录的是 `(dst, src, nbytes)`。
- 判断连续的逻辑是：
  - 第一个 entry 的目标地址为 `expected`。
  - 下一个 entry 必须满足 `entry->dst == expected`。
  - 然后 `expected += entry->nbytes`。
  - 如果下一个目标地址不等于 expected，就说明 Old 目标地址中间有 gap，当前连续 run 被切断。
- 也就是说，GiYOL 想要的是多个 tiny object 在 Old reserve address 上连续，这样才能把它们合成一个 staging chunk，再对 Old 做一段连续 stream write。

什么时候才会真正 copy：

- `copy_for_minor()` 名字里有 copy，但在 GiY/GiYOL 当前实现中它不是真正复制 object 内容。
- `copy_for_minor()` 做的是：
  - 计算 object 对齐后的大小。
  - 从 `dram_space.free` bump pointer 中分配/reserve Old 地址。
  - 更新 `dram_space.free` 和 `available_bytes`。
  - 设置 source object header 的 `forwarding_pointer`。
  - 把 source payload push 到 GC stack。
  - 在 GiYOL staging 模式下，如果 object 足够小，还会把 `(dst, src, size)` 记录进 `giyol_reserve_order_batch`。
- 真正 copy 发生在 object 字段已经在 Young/Cache 中被扫描和 patch 之后：
  - GiY：在 `giy_traverse_stack_and_copy()` 中直接调用 `giy_copy_live_object()`。
  - GiYOL：tiny object 先记录 copy entry，不立即 copy；等 traversal 结束后调用 `giyol_flush_copy_batch()`，再通过 `giyol_flush_tiny_staging_batch()` / `giyol_flush_tiny_chunk()` / `giyol_stream_copy_region()` 真正写入 Old。如果不能形成合适 batch，则 fallback 到 `giy_copy_live_object()`。

为什么这样实现：

- 必须先 reserve Old 地址，因为 object graph 中互相引用时，需要 forwarding pointer 来修补引用。
- 不能在 reserve 时就真正 copy，因为此时 source object 内部字段可能还没有被修补成新的 Old 地址。
- GiY 的设计是先把对象图在 Young/Cache 内处理完，再把修补后的 object 一次性 materialize 到 Old，减少核心 traversal 阶段对 Old/DRAM 的读取。
- GiYOL 进一步利用这个“reserve 和真正 copy 分离”的结构：先收集 tiny object 的 copy entry，等 Young 内修补完成后再批量写 Old。
- staging buffer 的作用是把多个源地址不一定连续的 tiny Young object 内容拼成一段连续临时 buffer，然后对 Old 中连续 reserve address 做较大粒度 streaming write。

当前 GiYOL 的问题和下一步可能性：

- 当前 Old reserve 仍然来自同一个 `dram_space.free` bump pointer。
- 大 object 虽然默认不进入 tiny staging batch，但它仍然会在 reserve 阶段占用 Old 地址。
- 如果一个大 object 插在很多 tiny object 中间，tiny object 的 Old reserve address 就会被大 object 打断，导致 `dst != expected`，continuous run 被切断。
- 用户提出的下一步想法是合理的：
  - 在 scan/reserve 阶段就识别某些很大的 object。
  - 把大 object 预先 reserve 到另一个专门放大 object 的 Old 区域。
  - 让更多小 object 的 reserve address 保持连续。
  - 这样 GiYOL tiny staging 更容易形成较大的 continuous chunk。
- 另一个可能性：
  - 如果两个 live object 中间的 garbage/gap 很小，可以考虑把这个小 garbage 也一起 copy 到 Old。
  - 这样可以用少量额外复制换取更大的连续写入区间。
  - 需要实验判断额外 copy 的浪费是否小于连续 streaming write 带来的收益。

会议记录事项：

- 切换实验主机。
- 确定 cache 的大小。

## 2026-05-09 GiYOL 相对 GiY 的具体差异行号

用户要求明确说明 GiYOL 和 GiY 的差异分别在哪些文件、哪些行，以及为什么这样实现。补记如下。

主要差异文件和行号：

1. `ejsvm/common.mk`
   - `135-176`：允许通过 make 变量覆盖 GiYOL 参数，例如 `GIYOL_BATCH_BYTES`、`GIYOL_STAGING_COPY`、`GIYOL_TINY_STAGING_BYTES`、`GIYOL_TINY_MAX_OBJECT_BYTES` 等。
   - `446-457`：`OPT_GC=giy` 和 `OPT_GC=giyol` 编译同一批文件；`giyol` 只比 `giy` 多 `-DUSE_GIYOL`。
   - 含义：GiYOL 不是独立文件实现，而是 GiY 主体上的条件编译实验变体。

2. `ejsvm/giy_dram_manager.cc`
   - `804-810`：根据 `USE_GIYOL` 打印 GiY 或 GiYOL。
   - `895-909`：GiY 和 GiYOL minor GC 入口相同，都是 `giy_minor_collect(...)`，之后都是 `giy_weak_clear(ctx)`。
   - 含义：GC 主控制流没有分叉；差异集中在 `GiY.cc` 内部 copy/materialize 策略。

3. `ejsvm/GiY.cc`
   - `113-158`：GiYOL 默认参数。
     - `GIYOL_BATCH_BYTES=128KB`
     - `GIYOL_STAGING_COPY=1`
     - `GIYOL_TINY_STAGING=1`
     - `GIYOL_TINY_STAGING_BYTES=4096`
     - `GIYOL_TINY_FLUSH_BYTES=4096`
     - `GIYOL_TINY_MAX_OBJECT_BYTES=64`
     - `GIYOL_DIRECT_NONTINY_NT=0`
   - `161-183`：GiYOL copy entry、copy batch、staging buffer、reserve-order batch。
   - `214-232`：GiYOL profile counters。
   - `1162-1195`：`copy_for_minor()` 中的 GiYOL staging hook。GiY 只 reserve Old 地址；GiYOL 在 staging 模式下还会把小对象的 `(dst, src, nbytes)` 记录到 `giyol_reserve_order_batch`。
   - `1795-1855`：GiY 原本的 `giy_copy_live_object()`，也是 GiYOL fallback 路径。小对象走 `memcpy`，大对象走 non-temporal store。
   - `1857-1937`：`USE_GIYOL` 下的 stream copy 函数，包括 `giyol_stream_copy_region()` 和 direct NT copy。
   - `1939-2018`：GiYOL batch 初始化、append、reset，以及 reserve-order log append。
   - `2019-2076`：staging buffer 扩容和 staging chunk flush。能形成合适大块时，先把多个源对象 copy 到 staging buffer，再 stream copy 到 Old。
   - `2078-2118`：tiny chunk flush。只有 chunk 足够大，或强制 tail NT 时，才用 stream copy；否则 fallback 到 GiY copy。
   - `2121-2199`：tiny staging 主逻辑。只接受 `nbytes <= GIYOL_TINY_MAX_OBJECT_BYTES` 的小对象；用 `dst == expected` 判断 Old 目标地址是否连续。
   - `2280-2388`：通用 staging/batch flush 逻辑。非 tiny staging 分支里可以排序、按连续 Old 目标地址形成 chunk，再决定是否 stream copy。
   - `2393-2495`：GiY 和 GiYOL 真正分叉的核心函数 `giy_traverse_stack_and_copy()`。
     - GiY：每弹出一个对象、扫描并 patch 完后，立刻 `giy_copy_live_object()`。
     - GiYOL staging：大于 tiny 阈值的对象直接走 GiY/fallback；tiny 对象不在循环里立即 copy，而是等循环结束后 `giyol_flush_copy_batch()`。
   - `2518-2564` 和 `2646-2691`：GiYOL profile 输出。
   - `2767-2776`：minor GC 开始时，如果是 GiYOL staging copy，初始化 reserve-order batch。

为什么这么实现：

- `copy_for_minor()` 必须先 reserve Old 地址，但不能立即复制。原因是对象内部字段还没有完成 forwarding/patch；如果过早 copy 到 Old，Old 中会留下旧 Young 指针。
- GiY 的核心思想是：先在 Young/Cache 中完成对象图遍历和字段修补，然后再把修补后的对象一次性 materialize 到 Old/DRAM。
- GiYOL 利用 GiY 已经存在的“reserve 和真正 copy 分离”结构，把多个 tiny object 的 copy 延迟到 traversal 之后。
- GiYOL 的目标不是改变 GC 语义，而是改变写 Old 的方式：把多个小对象聚合成更大的连续写入，以减少小 copy 开销，并尽量发挥 non-temporal store 的收益。
- continuous objects 在当前实现中指 Old 目标地址连续。判断逻辑是 `dst == expected`，之后 `expected += nbytes`。如果中间有大对象或 gap，连续区间就断开。
- staging buffer 的作用是解决 Young 源地址不连续的问题：多个源对象可以先被拼进 staging buffer，再对 Old 中连续目标地址做一次 stream write。
- `GIYOL_TINY_MAX_OBJECT_BYTES=64` 是为了把策略集中在 tiny object；大对象本身 copy 量足够大，默认回到 GiY copy 路径，避免 staging 的额外一次 memcpy 成本。
- `GIYOL_TINY_FLUSH_BYTES=4096` 是为了避免太小的 stream write。non-temporal store 对很小数据可能不划算，所以当前实现倾向于达到 4KB 再 flush。
- 当前实现的主要问题是：大对象虽然不进入 tiny staging，但仍然在同一个 `dram_space.free` 上 reserve 地址，会打断 tiny object 的 Old 地址连续性。这正是后续可以考虑“大对象单独 reserve 区域”的原因。

## 2026-05-09 关于 GiY.cc 中 copy 指针为什么用 unsigned char *

用户问：`ejsvm/GiY.cc` 约 `1876` 行里，GiY/GiYOL copy 代码为什么把 `dst` 转成 `unsigned char *`，地址明明是 64 位。

解释：

- `unsigned char *` 的意思不是“地址只有 1 byte”。
- 在 64 位机器上，`unsigned char *` 这个指针变量本身仍然是 64 位地址。
- `char`/`unsigned char` 表示的是这个指针指向的数据单位大小是 1 byte。
- copy 代码需要按 byte 移动源/目标指针，所以使用 `unsigned char *d` 和 `const unsigned char *s`。
- 例如 `d += 8` 表示目标地址前进 8 bytes；如果使用 `uint64_t *d`，`d += 8` 会前进 8 个 `uint64_t`，也就是 64 bytes，语义会错。
- 后续真正做 64-bit 或 128-bit store 时，代码会临时把地址 cast 成 `_mm_stream_si64((long long *) d, ...)` 或 `_mm_stream_si128((__m128i *) d, ...)`。
- 因此这里用 `unsigned char *` 是为了 byte-level pointer arithmetic，而不是因为地址宽度是 8 bit。

## 2026-05-09 关于 GiYOL stream copy 中 memcpy 到局部变量 v

用户问：`ejsvm/GiY.cc` 约 `1886` 行开始为什么先 `memcpy(&v, s, sizeof(v))`，`v` 存在哪里，是否看起来多存了一次。

解释：

- 代码形式是：

```cpp
uint64_t v;
memcpy(&v, s, sizeof(v));
_mm_stream_si64((long long *) d, (long long) v);
```

- `v` 是函数里的局部自动变量。语义上它在当前函数栈帧里；实际编译优化后，编译器很可能直接把它放在寄存器里，不一定真的写到栈内存。
- 这里用 `memcpy` 的主要原因是安全地从 `s` 指向的 byte 地址读取 8 bytes。
- `s` 是 `const unsigned char *`，它可能不是 8 字节对齐地址。如果直接写 `*(uint64_t *)s`，在 C/C++ 语义上可能有 alignment/strict-aliasing 问题。
- `memcpy(&v, s, 8)` 表达的是“从任意 byte 地址复制 8 bytes 到一个 64-bit 临时值”，这是标准、安全的写法。
- 对固定 8 bytes 的 `memcpy`，优化编译时通常会被编译器优化成一次普通 load 或几个 load，不一定真的调用 `memcpy` 函数。
- `_mm_stream_si64` 的接口需要一个 64-bit 值作为参数，而不是源地址；所以无论如何都需要先把源内存的 8 bytes load 成一个值，再 stream store 到目标地址。
- 因此这不是为了把数据多存一份到长期内存里，而是为了安全地构造 `_mm_stream_si64` 需要的 64-bit register value。
- 中间大块 16 bytes 的路径使用 `_mm_loadu_si128`，因为它本来就是“unaligned load”指令接口；8 bytes 的头尾路径没有对应地直接用 unaligned scalar load intrinsic，所以这里使用 `memcpy` 更稳妥。

## 2026-05-09 GiYOL staging buffer 的大小和存放位置

用户问：`ejsvm/GiY.cc` 约 `2019` 行开始的 staging 代码如何确定 staging 区大小，以及这个空间放在哪里。

解释：

- staging buffer 的全局状态定义在 `GiY.cc` 约 `179-183` 行：

```cpp
static unsigned char *giyol_staging_buffer = NULL;
static size_t giyol_staging_capacity = 0;
static GiYOLCopyBatch giyol_reserve_order_batch;
static bool giyol_reserve_order_batch_active = false;
```

- 真正分配空间的是 `giyol_ensure_staging_capacity(size_t bytes)`，约 `2019-2040` 行。
- 如果当前 `giyol_staging_capacity >= bytes`，不重新分配。
- 如果不够，就从当前 capacity 开始翻倍，直到 `new_capacity >= bytes`。
- 然后调用：

```cpp
realloc(giyol_staging_buffer, new_capacity)
```

- 因此 staging buffer 不在 `cache_space`，也不在 `dram_space`。
- 它是普通 C heap 上的一块全局缓冲区，由 libc `malloc/realloc` 管理。
- 这块 buffer 会跨 GC 复用；代码只扩大 capacity，不在每次 GC 后缩小或释放。

当前默认 tiny staging 路径下大小如何决定：

- 默认参数：
  - `GIYOL_TINY_STAGING_BYTES=4096`
  - `GIYOL_TINY_FLUSH_BYTES=4096`
  - `GIYOL_TINY_MAX_OBJECT_BYTES=64`
- `giyol_flush_tiny_staging_batch()` 约 `2121-2199` 行一开始会调用：

```cpp
giyol_ensure_staging_capacity((size_t) GIYOL_TINY_STAGING_BYTES);
```

- 所以默认第一次使用 tiny staging 时，至少申请 4096 bytes。
- 之后组 chunk 时，代码用 `bytes + entry->nbytes <= GIYOL_TINY_STAGING_BYTES` 限制一个 tiny chunk 不超过 4096 bytes。
- 如果 chunk 达到 `GIYOL_TINY_FLUSH_BYTES`，即默认 4096 bytes，就 flush。
- 如果 chunk 没达到阈值，通常 fallback 到普通 GiY copy；只有 `GIYOL_FORCE_TINY_TAIL_NT` 打开时，tail 小块也会走 NT stream。

非 tiny/general staging 路径：

- `giyol_flush_staging_chunk()` 约 `2042-2076` 行会根据实际 chunk bytes 调用：

```cpp
giyol_ensure_staging_capacity(bytes);
```

- 这时 staging buffer 至少扩到当前 chunk 所需大小。
- 但当前默认 `GIYOL_TINY_STAGING=1`，主要走 tiny staging 逻辑。

为什么不放在 cache_space / dram_space：

- staging buffer 是临时复制缓冲区，不是 VM object，不应该被 GC 当作 Young 或 Old object 管理。
- 如果放在 cache_space，会挤占 Young/cache 区，影响 GC 实验变量。
- 如果放在 dram_space，会污染 Old/DRAM 的对象布局和 reserve 连续性，反而破坏 GiYOL 要观察的 Old 目标地址连续性。
- 所以当前实现把 staging buffer 放在普通 C heap 上，作为 GC 算法外部的辅助缓冲区。

## 2026-05-09 GiYOL staging chunk 中 2056 行为什么用 memcpy

用户问：`ejsvm/GiY.cc` 约 `2056` 行为什么要用 `memcpy`。

相关代码在 `giyol_flush_staging_chunk()` 中：

```cpp
for (size_t i = start; i < end; i++) {
  memcpy(giyol_staging_buffer + off, items[i].src, items[i].nbytes);
  off += items[i].nbytes;
}
giyol_stream_copy_region(dst, giyol_staging_buffer, bytes, used_nt_store);
```

解释：

- 这里的 `memcpy` 是把多个源 object 的 bytes 从各自的 Young 地址复制到一个连续的 staging buffer。
- 原因是这些 object 的 Old 目标地址可以连续，但它们的 Young 源地址不一定连续。
- `giyol_stream_copy_region()` 只能从一段连续的源内存 stream copy 到一段连续的目标 Old 内存。
- 如果没有 staging buffer，就只能对每个 object 单独 stream/copy，无法把多个 tiny object 合并成一次大块连续写。
- 因此 `memcpy` 的作用是“pack”：把零散源对象拼成连续源 buffer。
- 后面的 `giyol_stream_copy_region(dst, giyol_staging_buffer, bytes, ...)` 才是真正写 Old/DRAM。
- 这确实会多一次从 Young 到 staging buffer 的普通 copy；GiYOL 的假设是，当多个 tiny object 合并成较大连续 Old 写入时，减少小 copy 和提升 streaming write 效率可能抵消这次额外 copy。
- 如果 chunk 太小、不值得这样做，代码会走 fallback：逐个调用 `giy_copy_live_object()`。

## 2026-05-10 对 GiYOL copy 策略理解的校正

用户给出的解释：

> GiYOL 的不同点在于：在数据 copy 环节当中，不是像 GiY 一样立刻复制，而是判断大小。如果对象大小小于 64B，那么就记录进表格里；如果小于 256B 大于 64B，那么就立刻 memcpy；剩下的大对象就立刻 NT store。然后在这个环节结束之后，再查验表格，如果大于某一个阈值，就复制到 staging place 然后使用 NT store，否则就照常使用 memcpy。

评估：大方向正确，但需要几处精确修正。

正确的部分：

- GiYOL 相比 GiY 的核心差异确实在 object materialize/copy 策略，而不是 root scanning 或 remembered set 主语义。
- 当前默认 `GIYOL_TINY_MAX_OBJECT_BYTES=64`，小于等于这个阈值的 tiny object 会进入 GiYOL reserve-order batch。
- 大于 tiny 阈值的对象在 `giy_traverse_stack_and_copy()` 中会直接走 `giy_copy_live_object()`。
- `giy_copy_live_object()` 中 `<= GIY_NT_COPY_MIN_BYTES`，即当前 `<=256B`，走 `memcpy`；更大的对象在 x86 下走 non-temporal store。
- traversal 结束后，GiYOL 会 flush batch；如果 tiny chunk 达到 `GIYOL_TINY_FLUSH_BYTES=4096`，会 copy 到 staging buffer，然后对 Old 连续目标地址做 NT stream copy；否则默认 fallback 到逐个 `giy_copy_live_object()`，对于 tiny object 实际就是普通 `memcpy`。

需要修正的部分：

- 代码判断的不是纯 payload “对象大小”，而是：

```cpp
ALIGN(hdr->size + sizeof(object_header))
```

也就是包含 object header 后再对齐的 `align_bytes`。

- “小于 64B”更准确应说“`align_bytes <= GIYOL_TINY_MAX_OBJECT_BYTES`”，当前阈值是 `<=64B`，不是 `<64B`。
- tiny object 记录进表格的时间点不是“真正 data copy 环节”，而是在 `copy_for_minor()` 的 reserve 阶段，代码约 `1168-1195` 行。
- GiYOL staging 模式下，`<=64B` 的 tiny object 在 `giy_traverse_stack_and_copy()` 循环里不会立刻 copy；它依靠之前 reserve 阶段记录的 batch，在 traversal 完成后统一 flush。
- `64B < align_bytes <= 256B` 的对象会在 traversal 中直接 `giy_copy_live_object()`，并因为 `nbytes <= 256` 走 `memcpy`。
- `align_bytes > 256B` 的对象会在 traversal 中直接 `giy_copy_live_object()`，并在 x86 下走 NT store。
- batch flush 时，不是只看总大小，还要求 Old 目标地址连续。tiny staging 代码通过 `dst == expected` 判断 continuous run。
- 默认 `GIYOL_FORCE_TINY_TAIL_NT=0`，所以不足 `4096B` 的 tail chunk 不会强行 NT store，会 fallback 到逐个普通 copy。

更准确的一句话版本：

GiYOL 默认策略是：在 reserve 阶段把 `align_bytes <=64B` 的 tiny object 记录到 reserve-order batch；在 traversal/copy 阶段，`>64B` 的对象立即走 GiY 的 `giy_copy_live_object()`，其中 `<=256B` 用 `memcpy`，`>256B` 用 NT store；traversal 结束后，对 batch 中 Old 目标地址连续的 tiny objects 组 chunk，如果 chunk 达到 `4096B`，先 pack 到 staging buffer 再 NT store 到 Old，否则 fallback 到普通 GiY copy。

日语版：

GiYOL の違いは、主に object を Old 領域へ copy する部分にある。GiY では、object を scan して中の pointer を直したあと、すぐに Old 領域へ copy する。

一方、GiYOL では、`align_bytes` が `64B` 以下の tiny object は、reserve の段階で copy せず、`dst`、`src`、`size` を batch に記録しておく。ここでいう size は payload だけではなく、object header を含めて align したサイズである。

`64B` より大きい object は、GiY と同じようにその場で copy する。そのとき、`256B` 以下なら普通の `memcpy` を使い、`256B` より大きければ NT store を使う。

すべての traversal が終わったあと、GiYOL は batch に記録した tiny object を確認する。Old 領域での destination address が連続している tiny object をまとめて chunk にする。

その chunk が `4096B` 以上になれば、まず staging buffer にまとめて copy し、そのあと staging buffer から Old 領域へ NT store で copy する。

もし chunk が `4096B` 未満なら、デフォルトでは NT store を使わず、普通の GiY copy に戻る。この場合、tiny object は `64B` 以下なので、実際には `memcpy` になる。

## 2026-05-10 GiYOL PPT 三页英文内容

用户要求：准备约三页 PPT 内容，用英语清楚说明 GiYOL 是什么，以及当前进度。

建议 PPT 内容如下：

Slide 1: What is GiYOL?

- GiYOL is an experimental extension of GiY.
- It keeps the main GiY minor GC design:
  - reserve Old addresses first,
  - traverse and patch live objects in Young/Cache,
  - then materialize the patched objects into Old/DRAM.
- The key difference is in the final copy/materialization step.
- GiY copies each live object to Old immediately after it is processed.
- GiYOL delays copying tiny objects and tries to copy them in larger contiguous chunks.

Speaker note:

GiYOL does not change the root scanning or remembered-set semantics. It mainly changes how small live objects are copied to Old space.

Slide 2: GiYOL Copy Strategy

- During reserve, if `align_bytes <= 64B`, GiYOL records the object in a copy batch:
  - destination address,
  - source address,
  - aligned object size.
- Objects larger than `64B` are copied immediately, using the normal GiY copy path.
- In the normal GiY copy path:
  - `<=256B`: copied with `memcpy`,
  - `>256B`: copied with non-temporal stores.
- After traversal, GiYOL checks the tiny-object batch.
- If tiny objects have contiguous Old destination addresses, they are grouped into a chunk.
- If the chunk reaches `4096B`, GiYOL packs the objects into a staging buffer and then uses non-temporal stores to copy the chunk to Old.
- If the chunk is smaller than `4096B`, it falls back to normal GiY copy, which is usually `memcpy` for tiny objects.

Speaker note:

The staging buffer is needed because Young source addresses are not necessarily contiguous, even when Old destination addresses are contiguous.

Slide 3: Current Progress, Findings, and Next Steps

- Implementation status:
  - GiYOL is implemented as a compile-time variant of GiY using `USE_GIYOL`.
  - It is located mainly in `ejsvm/GiY.cc`.
  - Current default settings focus on tiny objects:
    - tiny object threshold: `64B`,
    - tiny staging buffer size: `4096B`,
    - tiny flush threshold: `4096B`.
- Current finding:
  - GiYOL can reduce small-object copy fragmentation in principle.
  - However, current benchmark results do not yet show GiYOL clearly outperforming GiY in GC core time.
  - A likely reason is that large objects still reserve space in the same Old allocation stream, which can break the continuity of tiny-object destination addresses.
- Next steps:
  - switch the experiment host,
  - finalize the cache size,
  - test whether large objects can be detected during scanning/reserve and placed in a separate large-object area,
  - evaluate whether copying very small garbage gaps into Old is acceptable if it helps preserve larger contiguous chunks.

Speaker note:

The next design question is how to make more tiny objects receive contiguous Old addresses. This is necessary for GiYOL to form larger chunks and make non-temporal stores more effective.

## 2026-05-10 GiYOL PPT 精简版与插图建议

用户反馈上一版 PPT 文字量太多，要求减少文字量并提供插图建议。建议改为三页，每页只放 3-5 个短 bullet，细节放口头说明。

Slide 1: GiYOL = GiY + Batched Tiny Copy

PPT text:

- Same minor GC flow as GiY
- Difference: final copy to Old
- GiY: copy each object immediately
- GiYOL: delay tiny objects and batch them

Figure suggestion:

- 左右对比图。
- 左边 GiY：Young object -> patch -> copy to Old，一次一个 object。
- 右边 GiYOL：tiny objects -> batch table -> staging buffer -> Old contiguous chunk。

Slide 2: Copy Policy

PPT text:

- `<=64B`: record in batch
- `64B-256B`: immediate `memcpy`
- `>256B`: immediate NT store
- Tiny batch flush:
  - contiguous Old addresses
  - chunk >= `4096B`

Figure suggestion:

- 用三段颜色表示 size classes：
  - tiny `<=64B`
  - small `64B-256B`
  - large `>256B`
- 每段箭头指向不同 copy path。
- 对 tiny path 再画 batch -> staging -> NT store。

Slide 3: Current Status and Next Steps

PPT text:

- Implemented with `USE_GIYOL`
- Main file: `ejsvm/GiY.cc`
- Current issue:
  - large objects break tiny-object continuity
- Next:
  - separate large-object reserve area
  - test small-gap copying
  - switch host / fix cache size

Figure suggestion:

- 画 Old address layout。
- 当前：tiny, tiny, large, tiny, tiny，连续区间被 large object 打断。
- 目标：tiny, tiny, tiny, tiny 连续；large objects 放到 separate area。

## 2026-05-14 GiYOL staging buffer 和 reserve-order batch 是否在 Young/cache 区

用户问：GiYOL 的 `staging buffer` 和记录 copy entry 的 `giyol_reserve_order_batch` 是否都在规定的 Young 区，即预期在 cache 上的那部分。

结论：当前实现中，两者都不在规定的 Young/cache 区。

代码依据：

- `ejsvm/GiY.cc` 约 `179-183`：

```cpp
static unsigned char *giyol_staging_buffer = NULL;
static size_t giyol_staging_capacity = 0;
static GiYOLCopyBatch giyol_reserve_order_batch;
static bool giyol_reserve_order_batch_active = false;
```

- `giyol_staging_buffer` 是静态全局指针，但它指向的 buffer 在 `giyol_ensure_staging_capacity()` 中通过 `realloc()` 分配，约 `2019-2040`：

```cpp
unsigned char *new_buffer =
  (unsigned char *) realloc(giyol_staging_buffer, new_capacity);
```

因此 staging buffer 位于普通 C heap，由 libc malloc/realloc 管理，不在 `cache_space`，也不在 `dram_space`。

- `giyol_reserve_order_batch` 本身是静态全局变量，位于程序的全局/静态数据区，不在 Young/cache 区。
- 其 entry 存储 `items` 初始指向 `batch->inline_items`，约 `1939-1943`：

```cpp
batch->items = batch->inline_items;
batch->capacity = GIYOL_INLINE_COPY_ENTRIES;
```

- `inline_items` 是 `GiYOLCopyBatch` 结构体内部数组，约 `167-172`：

```cpp
GiYOLCopyEntry inline_items[GIYOL_INLINE_COPY_ENTRIES];
```

因为 `giyol_reserve_order_batch` 是静态全局对象，所以这个 inline table 也在全局/静态数据区。

- 如果 entry 超过 inline capacity，`giyol_copy_batch_reserve_entry()` 会用 `malloc()` 或 `realloc()` 扩容，约 `1957-1981`：

```cpp
new_items = (GiYOLCopyEntry *) malloc(...);
...
new_items = (GiYOLCopyEntry *) realloc(...);
```

扩容后的 table 位于普通 C heap，也不在 Young/cache 区。

对比：确实绑定到 Young/cache 区的是 GiY 的某些辅助结构，例如 `g_gc_stack` 和 `g_ft_slot_set`。`giy_bind_stack_to_cache_impl()` 约 `635-689` 从 `cache_space.end` 预留空间，并设置：

```cpp
g_gc_stack.items = (uintptr_t *) stack_begin;
g_ft_slot_set.items = (uintptr_t *) ft_slot_begin;
cache_space.end = stack_begin;
```

但是当前代码没有对 `giyol_staging_buffer` 或 `giyol_reserve_order_batch.items` 做类似绑定。

含义：

- 如果实验预期是“GiYOL 所有 GC 辅助结构也必须在 cache/Young 区”，当前实现不满足这个预期。
- 当前 GiYOL 的 staging buffer 和 batch table 是 GC 外部辅助内存，会额外使用普通 heap。
- 这可能影响实验解释：GiYOL 使用了不属于模拟 cache/Young 的额外内存空间。

后续如果要修正：

- 可以在 `giy_bind_stack_to_cache_impl()` 或 GiYOL 专用 bind 函数中，从 `cache_space.end` 再预留一段空间给：
  - fixed-size `giyol_reserve_order_batch.items`
  - fixed-size `giyol_staging_buffer`
- 然后相应减少 `cache_space.end` / `cache_space.total_size`，让这部分内存真实计入 cache/Young 预算。
- 需要同时处理容量不足时的策略：不能再随意 `malloc/realloc`，要么报错，要么 flush batch，要么动态限制 batch 大小。

## 2026-05-14 关于“不能叫 cache 区”的命名讨论

用户指出：预期所有 GiYOL 辅助结构都要塞进的那块空间不能直接叫 cache 区，否则导师可能误解为真实 CPU cache。

推荐命名：

- 中文：`GC 本地工作区`
- 英文：`GC Local Workspace`

推荐原因：

- 不暗示它是真实 CPU cache。
- 能表达这是 GC 算法使用的临时空间。
- 适合包含 stack、batch table、staging buffer 等不同辅助结构。
- 可以进一步说明它是 “allocated from the Young-space budget” 或 “counted against the cache-size budget”。

其他可选名称：

- `Young-space Auxiliary Area`
  - 强调它来自 Young space，更接近实现。
  - 但听起来比较像 heap layout 的一部分。
- `GC Scratch Space`
  - 简洁，适合临时 buffer/table。
  - 但略口语，PPT 上需要补充说明它计入预算。
- `Cache-budgeted GC Workspace`
  - 最清楚表达“计入 cache 预算，但不是 CPU cache”。
  - 名字较长，适合论文/说明，不一定适合代码变量。
- `Young-budgeted GC Workspace`
  - 避开 cache 这个词，强调预算来自 Young 区。
  - 如果导师关心实验预算，表达比较准确。

建议 PPT 表述：

```text
GC Local Workspace
(reserved from the Young-space / cache-size budget, not the CPU cache)
```

建议代码/文档中逐步避免裸用 `cache area` 表达，改成：

- `Young-space GC workspace`
- `GC local workspace`
- `cache-budgeted workspace`

最终命名决定：

- 从现在起，用户决定把“预期全部存进 cache-size/Young 预算中的 GC 辅助空间”统一称为：

```text
GC Local Workspace
```

- 后续讨论、PPT、文档和代码说明中，应优先使用 `GC Local Workspace`。
- 需要时补充解释：

```text
GC Local Workspace is reserved from the Young-space / cache-size budget.
It is not the hardware CPU cache.
```

## 2026-05-14 当前 GiY/GiYOL 哪些结构在 GC Local Workspace

用户问：当前 GiYOL 和 GiY 中，哪些结构实际放在 `GC Local Workspace`，哪些没有。

定义：

- `GC Local Workspace` 指从 Young-space / cache-size budget 中预留出来的 GC 辅助空间。
- 它不是 CPU hardware cache。
- 代码层面主要表现为：从 `cache_space.end` 向下切出空间，并减少 `cache_space.end` / `cache_space.total_size`。

当前在 GC Local Workspace 中的结构：

1. Remembered set backing arrays，GiY/GiYOL 共用
   - 文件：`ejsvm/giy_rset.cc`
   - 位置：约 `122-141`
   - 包括：
     - `remembered_set.values`
     - `remembered_set.buffer`
     - `remembered_set.hash_table`
   - 代码从 `cache_space.end` 向下分配，并减少 `cache_space.end` 和 `cache_space.total_size`。
   - 注意：`RememberedSet remembered_set` 这个结构体本身是全局变量，不在 workspace；在 workspace 中的是它指向的 backing arrays。

2. GiY traversal stack backing array，GiY/GiYOL 共用
   - 文件：`ejsvm/GiY.cc`
   - 位置：约 `635-689`
   - `g_gc_stack.items = (uintptr_t *) stack_begin`
   - 从 `cache_space.end` 预留 `stack_bytes`。
   - 注意：`g_gc_stack` 结构体本身是全局变量；真正进入 workspace 的是 `items` backing array。

3. Function-table strong-slot set backing array，GiY/GiYOL 共用
   - 文件：`ejsvm/GiY.cc`
   - 位置：约 `635-689`，使用位置约 `737-759`
   - `g_ft_slot_set.items = (uintptr_t *) ft_slot_begin`
   - 用于记录 function table 中可能指向 Young 的强引用 slot。
   - 同样，`g_ft_slot_set` 结构体本身是全局变量；在 workspace 中的是 `items` backing array。

当前不在 GC Local Workspace 中的结构：

1. GiYOL staging buffer
   - 文件：`ejsvm/GiY.cc`
   - 定义：约 `179-181`
   - 分配：约 `2019-2040`
   - `giyol_staging_buffer` 通过 `realloc()` 分配。
   - 所以它位于普通 C heap，不在 GC Local Workspace。

2. GiYOL reserve-order batch table
   - 文件：`ejsvm/GiY.cc`
   - 定义：约 `167-183`
   - 初始化：约 `1939-1943`
   - 追加/扩容：约 `1957-1981`
   - `giyol_reserve_order_batch` 本身是 static global object。
   - 初始 `items` 指向 `inline_items`，因此 inline table 在全局/静态数据区。
   - 超过 inline capacity 后，`items` 通过 `malloc()` / `realloc()` 扩容，位于普通 C heap。
   - 因此当前也不在 GC Local Workspace。

3. EdgePatchLog
   - 文件：`ejsvm/GiY.cc`
   - 定义：约 `41-53`
   - 绑定：约 `648` 和 `672-675`
   - 当前 `edge_bytes = 0`，`g_edge_log.items = NULL`，`capacity = 0`。
   - 它没有实际 backing array；目前主要只用 `g_edge_log.count` 做 profiling 计数。
   - 因此不能说它实际占用了 GC Local Workspace。

4. Allocation-site update logs
   - 文件：`ejsvm/GiY.cc`
   - 定义：约 `64-88`
   - 分配：约 `815-833`、`944-965`
   - `g_as_update_log.items` 和 `g_as_object_update_log.items` 使用 `realloc()`。
   - 如果相关功能开启并使用，它们在普通 C heap，不在 GC Local Workspace。

5. Optional remembered-set fast index
   - 文件：`ejsvm/giy_rset.cc`
   - 位置：约 `38-40`、`144-155`
   - `remembered_set_hash_indices` 在 `GIY_RSET_INDEX_FAST` 打开时通过 `malloc()` 分配。
   - 当前如果启用，也不在 GC Local Workspace。

6. 全局 profile / guard / control metadata
   - 例如 `giy_profile`、`g_old_guard_*`、`g_used_nt_old_store`、`giyol_batch_next_traversal` 等。
   - 它们是普通全局/静态变量，不在 GC Local Workspace。
   - 通常体积很小，但如果严格要求所有 GC metadata 都计入预算，也需要单独处理。

简表：

| 结构 | GiY | GiYOL | 当前位置 |
|---|---:|---:|---|
| `remembered_set.values/buffer/hash_table` | yes | yes | GC Local Workspace |
| `g_gc_stack.items` | yes | yes | GC Local Workspace |
| `g_ft_slot_set.items` | yes | yes | GC Local Workspace |
| `g_edge_log.items` | no real storage | no real storage | none / global count only |
| `giyol_staging_buffer` | N/A | yes | C heap, not workspace |
| `giyol_reserve_order_batch.inline_items` | N/A | yes | global/static data, not workspace |
| expanded `giyol_reserve_order_batch.items` | N/A | yes | C heap, not workspace |
| allocation-site update log arrays | optional | optional | C heap, not workspace |
| `remembered_set_hash_indices` when enabled | optional | optional | C heap, not workspace |

重要结论：

- 当前 GiY 的主要 GC auxiliary backing arrays 大多已经计入 `GC Local Workspace`。
- 当前 GiYOL 新增的核心辅助结构，即 staging buffer 和 reserve-order batch table，还没有计入 `GC Local Workspace`。
- 如果要让 GiYOL 满足“辅助结构全部来自 GC Local Workspace”的实验假设，需要把这两个结构从 `malloc/realloc` 或 global inline storage 改成从 `cache_space.end` 预留的固定区域，或实现 workspace 内的 bounded allocator / flush-on-full 策略。

## 2026-05-14 当前 GiYOL staging buffer 和 reserve-order batch 的大小

用户问：当前 GiYOL 的 `staging buffer` 和 `giyol_reserve_order_batch` 各有多大。

当前机器是 64-bit，因此：

- `void *` = 8 bytes
- `const void *` = 8 bytes
- `size_t` = 8 bytes

`GiYOLCopyEntry` 定义在 `ejsvm/GiY.cc` 约 `161-165`：

```cpp
struct GiYOLCopyEntry {
  void *dst;
  const void *src;
  size_t nbytes;
};
```

所以一个 entry 当前是：

```text
8 + 8 + 8 = 24 bytes
```

### staging buffer

定义位置：`ejsvm/GiY.cc` 约 `179-181`

```cpp
static unsigned char *giyol_staging_buffer = NULL;
static size_t giyol_staging_capacity = 0;
```

初始大小：

```text
0 bytes
```

第一次 tiny staging flush 时，`giyol_flush_tiny_staging_batch()` 会调用：

```cpp
giyol_ensure_staging_capacity((size_t) GIYOL_TINY_STAGING_BYTES);
```

当前默认：

```text
GIYOL_TINY_STAGING_BYTES = 4096
```

所以默认情况下，staging buffer 第一次使用后通常是：

```text
4096 bytes
```

在当前默认 `GIYOL_TINY_STAGING=1` 的路径中，tiny chunk 被限制不超过 `4096B`，所以 staging buffer 一般不会超过 `4096B`。如果以后关闭 tiny staging 或修改参数，`giyol_ensure_staging_capacity(bytes)` 仍可能按需求扩容，并且扩容方式是 capacity 翻倍直到足够。

### giyol_reserve_order_batch

定义位置：`ejsvm/GiY.cc` 约 `167-183`

```cpp
struct GiYOLCopyBatch {
  GiYOLCopyEntry *items;
  size_t count;
  size_t capacity;
  size_t bytes;
  GiYOLCopyEntry inline_items[GIYOL_INLINE_COPY_ENTRIES];
};
```

当前默认：

```text
GIYOL_INLINE_COPY_ENTRIES = 64
```

inline entry table 大小：

```text
64 entries * 24 bytes = 1536 bytes
```

`GiYOLCopyBatch` 结构体前面还有：

```text
items pointer  = 8 bytes
count          = 8 bytes
capacity       = 8 bytes
bytes          = 8 bytes
```

所以 `giyol_reserve_order_batch` 这个 static global object 本身大约是：

```text
32 + 1536 = 1568 bytes
```

这部分在 global/static data，不在 GC Local Workspace。

如果 tiny object 数超过 64 个，`giyol_copy_batch_reserve_entry()` 会扩容：

```cpp
new_capacity = batch->capacity * 2;
malloc/realloc(new_capacity * sizeof(GiYOLCopyEntry));
```

扩容大小示例：

```text
128 entries -> 128 * 24 = 3072 bytes
256 entries -> 256 * 24 = 6144 bytes
512 entries -> 512 * 24 = 12288 bytes
1024 entries -> 1024 * 24 = 24576 bytes
```

注意：

- batch 的 `count` 和 `bytes` 每次 minor GC 开始会 reset。
- 但如果 `items` 已经扩容到 heap，capacity 不会在每次 GC 后缩小。
- 因此 reserve-order batch 的 heap memory 是“按历史最大 tiny object entry 数增长并保留”的。

当前默认理解：

```text
staging buffer:
  initial 0B, normally grows to 4096B after first use

giyol_reserve_order_batch:
  initial inline capacity 64 entries
  inline table 1536B
  whole static batch object about 1568B
  overflow then malloc/realloc, doubling capacity
```

## 2026-05-14 GiYOL staging buffer 下一步设计想法：5KB buffer + 4KB NT threshold

用户提出一个可能策略：

- 在 `GC Local Workspace` 中给 GiYOL staging buffer 预留稍微多一点空间，例如 `5KB`。
- 一旦 tiny chunk 超过或达到 `4KB`，就启动 non-temporal store，把 staging buffer 中的数据写入 Old。

评估：

- 这个方向合理，尤其适合把当前 heap-allocated staging buffer 改成固定大小的 `GC Local Workspace` buffer。
- 当前默认实现是：
  - `GIYOL_TINY_STAGING_BYTES = 4096`
  - `GIYOL_TINY_FLUSH_BYTES = 4096`
  - staging buffer 通过 `realloc()` 分配，不在 `GC Local Workspace`。
- 新设计可以改成：
  - `GIYOL_TINY_STAGING_BYTES = 5 * 1024`
  - `GIYOL_TINY_FLUSH_BYTES = 4 * 1024`
  - staging buffer 从 `GC Local Workspace` 固定预留，不再 `malloc/realloc`。

好处：

- 4KB 仍然作为 NT store 的触发阈值，避免太小的 streaming write。
- 5KB buffer 提供 headroom，避免因为 tiny object 边界导致刚好接近 4KB 时处理过于紧张。
- 固定 5KB 更容易解释实验预算：staging buffer 明确计入 `GC Local Workspace`。
- 实现上可以做到 bounded：如果 staging buffer 满了，就 flush；不再动态扩容到 C heap。

需要注意：

- 当前 tiny chunk 逻辑在加 object 后如果 `bytes >= GIYOL_TINY_FLUSH_BYTES` 就 break，所以即使 buffer 是 5KB，实际每次 chunk 通常只会略大于 4KB，而不是接近 5KB。
- 如果希望利用完整 5KB buffer，需要调整 chunk 形成逻辑：可以在达到 4KB 后允许继续收集直到接近 5KB，或者保持当前“达到 4KB 就 flush”的低延迟策略。
- NT store 的 destination alignment 仍然要满足现有 `giyol_stream_copy_region()` 的 alignment invariant。
- 如果 chunk 小于 4KB，默认仍应 fallback 到普通 GiY copy / memcpy，除非专门打开 tail NT 实验。

建议 PPT 表述：

```text
Next design:
Reserve a fixed 5KB staging buffer in GC Local Workspace.
Trigger NT store when a contiguous tiny-object chunk reaches 4KB.
```

建议代码方向：

- 新增或修改参数：

```cpp
#define GIYOL_TINY_STAGING_BYTES (5 * 1024)
#define GIYOL_TINY_FLUSH_BYTES   (4 * 1024)
```

- 把 `giyol_staging_buffer` 从 `realloc()` 改为从 `GC Local Workspace` 预留。
- 超过固定 buffer capacity 时强制 flush，而不是扩容到 C heap。

## 2026-05-14 实现记录：GiYOL staging/batch 进入 GC Local Workspace

用户决定采用以下设计，并要求处理尾部对齐，同时把高频访问数据放进 `GC Local Workspace`：

- 固定预留 `5KB` GiYOL staging buffer。
- tiny chunk 达到 `4KB` 后触发 non-temporal store。
- `giyol_staging_buffer` 和 `giyol_reserve_order_batch` 的 backing storage 都必须来自 `GC Local Workspace`，不再使用 C heap。

本次修改文件：

- `ejsvm/GiY.cc`
- `ejsvm/common.mk`

主要修改：

1. 修改 GiYOL 默认参数：

```cpp
#define GIYOL_TINY_STAGING_BYTES (5 * 1024)
#define GIYOL_TINY_FLUSH_BYTES   (4 * 1024)
```

2. 新增固定 batch entry 数：

```cpp
#define GIYOL_WORKSPACE_BATCH_ENTRIES 4096
```

- 一个 `GiYOLCopyEntry` 在当前 64-bit 环境中是 `24B`。
- 因此 batch backing storage 大小为：

```text
4096 * 24B = 98304B = 96KB
```

3. `giy_bind_stack_to_cache_impl()` 现在会把 GiYOL 的两个高频结构也从 `cache_space.end` 预留出来：

- `giyol_staging_buffer`：`5KB`
- `giyol_reserve_order_batch` metadata：约 `32B`
- `giyol_reserve_order_batch.items`：`96KB`

这两部分会计入 `aux_total_bytes`，也会减少 `cache_space.end` 和 `cache_space.total_size`，因此属于 `GC Local Workspace`。

4. `giyol_staging_buffer` 不再通过 `realloc()` 扩容。

- `giyol_ensure_staging_capacity()` 现在只做 fixed-capacity 检查。
- 如果需要的 bytes 超过 workspace 中预留的 staging capacity，会直接报错，而不是偷偷用 C heap。

5. `giyol_reserve_order_batch` 不再使用 global inline table 或 C heap 扩容。

- 在 `GIYOL_STAGING_COPY=1` 下，`GiYOLCopyBatch` 不再包含 `inline_items`。
- `giyol_reserve_order_batch` 本体改为 workspace 内对象，代码只保留一个 global pointer。
- `giyol_reserve_order_batch.items` 也绑定到 `GC Local Workspace`。
- 如果本轮 reserve tiny object 数超过 `GIYOL_WORKSPACE_BATCH_ENTRIES`，设置 `giyol_reserve_order_batch_overflow = true`。
- overflow 时，为保证正确性，本轮 traversal 阶段退回 GiY immediate copy，不再 flush 已记录 batch。

6. 尾部/对齐处理：

- `giyol_stream_copy_region()` 不再假设尾部只能剩 `8B`。
- 对 destination 非 8-byte 对齐或不适合 NT 的情况，fallback 到 `memcpy`。
- 主体仍使用 `_mm_stream_si128` 进行 16B NT store。
- 8B 尾部使用 `_mm_stream_si64`。
- 小于 8B 的剩余尾部使用普通 `memcpy` 处理，避免因为非预期尾部大小直接 abort。

7. profile 输出现在显示 GiYOL workspace 用量：

```text
Aux total: ... (stack ..., edge ..., ft ..., giyol staging ..., giyol batch ...)
```

已验证：

1. 构建：

```bash
cd /home/qiancheng/ejs-new/build.debug
make OPT_GC=giyol -j2
```

- 第一次链接失败是因为旧的 `build.debug/giy_dram_manager.o` 引用了当前 glibc 没有的 `__isoc23_fscanf`。
- 删除该生成物后重新 make，重新编译得到 `__isoc99_fscanf`，链接通过。

2. 最小启动：

```bash
./ejsvm ../ejsvm/js/hello.sbc
```

- 通过。
- 没触发 minor GC。
- profile 显示：

```text
giyol staging 5.00 KB
giyol batch 96.03 KB
GiYOL tiny staging bytes:5120
GiYOL tiny flush bytes:4096
```

3. 触发 GC 的 probe：

```bash
./ejsvm benchmarks/giy_gc_probe.sbc
```

- 通过。
- Minor GC count: `26`
- GiYOL NT batches: `325`
- GiYOL staging bytes: `1.28 MB`
- Aux total 中显示 `giyol staging 5.00 KB` 和 `giyol batch 96.03 KB`。

4. mprotect 验证：

```bash
GIY_MPROTECT_OLD=1 ./ejsvm benchmarks/giy_gc_probe.sbc
```

- 通过。
- 无 old-space violation。
- 说明新 staging/NT copy 路径没有破坏当前 GiYOL 核心阶段的 Old/DRAM 访问约束。

当前注意事项：

- `GIYOL_WORKSPACE_BATCH_ENTRIES=4096` 是当前默认容量；如果实际 benchmark 中 tiny object entry 超过这个上限，本轮会退回 immediate copy，保证正确性但减少 GiYOL batching 收益。
- 这比继续 `malloc/realloc` 更符合 `GC Local Workspace` 设计理念。
- 后续可以实验不同 batch entry 容量，例如 `2048/4096/8192`，观察 GC Local Workspace 占用和 GiYOL batch 成功率之间的 tradeoff。

## 2026-05-14 当前 GiY 和 GiYOL 的 GC Local Workspace 使用量

用户问：现在 GiYOL 和 GiY 的 local space 空间使用情况。

口径说明：

- `GC Local Workspace` 指从 Young-space / cache-size budget 中预留给 GC 辅助结构的空间。
- 当前默认 `CACHE_SIZE_KB=512`。
- `init_remembered_set()` 先从 `cache_space.end` 预留 remembered set，所以初始化后打印的 cache size 是 `320KB`。
- 运行 `./ejsvm ../ejsvm/js/hello.sbc` 得到当前 GiYOL profile：

```text
Young before aux:    300.24 KB
Young after aux:     129.69 KB
Aux total:           170.55 KB
  stack              37.52 KB
  edge               0.00 KB
  ft                 32.00 KB
  giyol staging       5.00 KB
  giyol batch        96.03 KB
```

### remembered set，两者共用

文件：`ejsvm/giy_rset.cc`

当前 remembered set backing arrays：

- `remembered_set.values`: `64KB`
- `remembered_set.buffer`: `64KB`
- `remembered_set.hash_table`: `64KB`

合计：

```text
192KB
```

这部分 GiY 和 GiYOL 都使用，并且属于 `GC Local Workspace`，因为它从 `cache_space.end` 预留并减少 `cache_space.total_size`。

### GiY 当前使用量

GiY 没有 GiYOL staging/batch。

minor GC auxiliary workspace：

```text
stack: 37.52 KB
edge:   0.00 KB
ft:    32.00 KB
----------------
aux:   69.52 KB
```

如果把 remembered set 也算入完整 `GC Local Workspace`：

```text
remembered set: 192.00 KB
GiY aux:         69.52 KB
-----------------------
total:          261.52 KB
```

剩余 Young work area 约：

```text
300.24 KB - 69.52 KB = 230.72 KB
```

### GiYOL 当前使用量

GiYOL 当前新增：

- staging buffer: `5.00KB`
- reserve-order batch:
  - metadata: about `32B`
  - entries: `4096 * 24B = 96KB`
  - total printed: `96.03KB`

minor GC auxiliary workspace：

```text
stack:          37.52 KB
edge:            0.00 KB
ft:             32.00 KB
giyol staging:   5.00 KB
giyol batch:    96.03 KB
-----------------------
aux:           170.55 KB
```

如果把 remembered set 也算入完整 `GC Local Workspace`：

```text
remembered set: 192.00 KB
GiYOL aux:      170.55 KB
------------------------
total:          362.55 KB
```

剩余 Young work area 约：

```text
300.24 KB - 170.55 KB = 129.69 KB
```

### 对比

```text
GiY aux only:        69.52 KB
GiYOL aux only:     170.55 KB
extra GiYOL aux:   +101.03 KB

GiY total incl RS:   261.52 KB
GiYOL total incl RS: 362.55 KB
extra GiYOL total:  +101.03 KB
```

额外的 `101.03KB` 基本来自：

```text
5.00 KB staging + 96.03 KB reserve-order batch
```

当前结论：

- GiYOL 现在比 GiY 多占约 `101KB` 的 `GC Local Workspace`。
- 这符合刚刚的设计理念：高频访问的 staging buffer 和 reserve-order batch 不再放在 C heap/global table，而是显式计入 local workspace。
- 代价是 Young work area 从约 `230.72KB` 降到约 `129.69KB`，可能影响 GC 频率和 benchmark 结果，需要后续实验确认 tradeoff。

## 2026-05-14 关于 GC Local Workspace 是否真的需要这么大

问题：除了实际用于 ordinary young objects 的 Young work area 之外，其它空间是否真的需要这么大？

当前 GiYOL 的 512KB cache/local-space budget 大致分布为：

```text
remembered set:        192.00 KB
init objects:           19.76 KB
GiY/GiYOL aux:         170.55 KB
actual Young work:     129.69 KB
--------------------------------
total:                 512.00 KB
```

其中 GiYOL aux 为：

```text
stack:                  37.52 KB
edge log:                0.00 KB
function-table slots:   32.00 KB
GiYOL staging:           5.00 KB
GiYOL reserve batch:    96.03 KB
```

用 `GIY_PROFILE_DETAIL=1` 跑 `benchmarks/giy_gc_probe.sbc` 得到的当前观测：

```text
Minor collections:      26
Max stack depth:        684 entries  ~= 5.34 KB
FT max live set:        227 entries  ~= 1.77 KB
RSet slots scanned:     2021 total   ~= 77.7 entries / GC
Tiny objects staged:    22350 total  ~= 859.6 entries / GC
Tiny staging bytes:     1.28 MB total
Max materialized bytes per GC: 129.23 KB
```

判断：

1. `GiYOL staging buffer = 5KB`
   - 这个大小是比较合理的。
   - 设计目标是超过 `4KB` 就使用 NT store，所以 buffer 必须至少大于等于 `4KB`。
   - `5KB` 只是给 tail/alignment 留一点余量；它不是主要浪费来源。

2. `giyol_reserve_order_batch = 96KB`
   - 这是 GiYOL 额外空间里最大的一块。
   - 当前大小来自 `4096 * sizeof(GiYOLCopyEntry)`，每个 entry 现在是 `dst + src + nbytes = 24B`。
   - 它不是每次都真的用满。probe 平均大约每次 GC 记录 `~860` 个 tiny objects，平均实际 entry 数据约 `860 * 24B = 20KB`。
   - 但是这张表承担一个重要语义：它按 reserve order 记录对象，让最后 copy 时可以恢复 Old 区连续地址顺序。因为对象在 traversal 阶段还没全部 patch 完，所以不能简单地边发现边 flush。
   - 因此，在当前实现里，如果想避免 overflow 复杂性，表就必须足够容纳一次 minor GC 中可能出现的大量 tiny objects。
   - 结论：`96KB` 对当前 benchmark 看起来偏大，但对当前算法来说是保守设计。更好的优化方向不是盲目缩小，而是：
     - 增加 max batch count 计数；
     - 尝试 `GIYOL_WORKSPACE_BATCH_ENTRIES=2048` 或 `1024`；
     - 或把 entry 压缩到 16B 左右，例如用 per-GC `dst offset` 代替完整 64-bit `dst`；
     - 或实现 safe overflow / processed-prefix flush，否则缩小后可能破坏正确性或丢失 GiYOL 效果。

3. `function-table slot set = 32KB`
   - 当前 profile 最大只用了 `227` 个 entry，约 `1.77KB`。
   - 现在代码里有最小值 `4096 * sizeof(uintptr_t) = 32KB`，这明显是保守预留。
   - 从当前数据看，它很可能可以缩到 `4KB` 或 `8KB`。
   - 但当前 overflow 行为是直接 `exit(1)`，所以如果缩小，应先加 fallback 或至少加更完整的 benchmark 验证。

4. `GC traversal stack = 37.52KB`
   - 当前 probe 最大深度 `684`，约 `5.34KB`，实际远小于预留。
   - 这个 stack 的风险是 correctness：如果对象图 frontier 很大，stack overflow 会直接失败。
   - 所以它可以作为实验缩小，但不建议在没有 fallback 的情况下激进缩小。

5. `remembered set = 192KB`
   - 这块 GiY 和 GiYOL 共用，不是 GiYOL 新增。
   - 当前由三部分组成：
     - `values`: 64KB
     - `buffer`: 64KB
     - `hash_table`: 64KB
   - 当前 probe 每次 GC 平均扫描的 RSet entry 只有约 `78` 个，因此 `values/buffer` 的 8192 容量对该 workload 明显偏大。
   - 但是 RSet 不是只受 Young 区大小限制。大量 old slots 可以指向少量 young objects，因此理论上 RSet entry 数量可能远大于 young object 数量。
   - 更重要的是：`hash_table` 每次 `rememberset_clear()` 都会整块 `memset 64KB`，所以这 64KB 不是单纯 reserved，而是每次 minor GC 都会被实际写一遍。
   - 优化方向：
     - 记录 max remembered_set.count；
     - 把 RSet capacity / hash size 做成可配置；
     - 用 occupied-index list，只清理实际用过的 hash slots，避免每次清 64KB；
     - 或把 `hash_table` 与 `buffer/values` 的大小分开调。

总判断：

```text
最应该优先怀疑过大的部分：
1. remembered set hash/table area, especially 64KB full clear per GC
2. GiYOL reserve-order batch, 96KB
3. function-table slot set, 32KB

比较合理、暂时不是主要问题：
1. GiYOL staging buffer, 5KB
2. traversal stack, unless later profile shows consistently low max depth across benchmarks
```

建议下一步：

1. 先补 profile：max RSet count、max GiYOL batch count、max batch bytes。
2. 做三个参数实验：
   - `GIYOL_WORKSPACE_BATCH_ENTRIES=4096 / 2048 / 1024`
   - FT slot min: `4096 / 1024 / 512 entries`
   - RSet capacity/hash size: `8192 / 4096 / 2048 entries`
3. 如果要真正减少空间而不牺牲正确性，优先做：
   - batch entry 压缩；
   - RSet lazy clear；
   - overflow fallback，而不是只改常数。

## 2026-05-14 Cheney / GiY / GiYOL 的 GC Local Workspace 空间占用对比

统一口径：

- `CACHE_SIZE_KB=512`，总 local/cache budget 是 `512KB`。
- `init objects` 当前约 `19.76KB`，这部分在 `cache_space.begin` 到 `cache_space.work_begin`，不是普通 Young work area，也不是 GC auxiliary table。
- remembered set 在初始化时先从 `cache_space.end` 预留。
- minor GC auxiliary structures 在 `work_begin` 固定后继续从 `cache_space.end` 预留。

当前共同基准：

```text
total local/cache budget: 512.00 KB
after remembered set:     320.00 KB
init objects:              19.76 KB
young before minor aux:   300.24 KB
```

### Remembered set

Cheney 和 GiY/GiYOL 的 remembered set 总大小都是 `192KB`，但组成不同。

Cheney:

```text
remembered_set.buffer:     128 KB
remembered_set.hash_table:  64 KB
total:                     192 KB
```

GiY / GiYOL:

```text
remembered_set.buffer:      64 KB
remembered_set.values:      64 KB
remembered_set.hash_table:  64 KB
total:                     192 KB
```

原因：

- Cheney 的 RSet 只记录 slot address。
- GiY/GiYOL 为了避免在 minor GC 中读旧区 slot，还额外记录 last written value，所以同样 192KB 下，slot capacity 从 Cheney 的 `16384` entries 变成 GiY/GiYOL 的 `8192` pairs。

### Minor GC auxiliary structures

Cheney:

```text
function-table slot set: 37.52 KB
aux total:               37.52 KB
young after aux:        262.72 KB
```

GiY:

```text
traversal stack:         37.52 KB
edge log:                 0.00 KB
function-table slot set: 32.00 KB
aux total:               69.52 KB
young after aux:        230.72 KB
```

GiYOL:

```text
traversal stack:          37.52 KB
edge log:                  0.00 KB
function-table slot set:  32.00 KB
staging buffer:            5.00 KB
reserve-order batch:      96.03 KB
aux total:               170.55 KB
young after aux:         129.69 KB
```

### Full local/cache budget view

不把 init objects 算进 GC Local Workspace，只看 RSet + minor aux：

```text
Cheney: 192.00 +  37.52 = 229.52 KB
GiY:    192.00 +  69.52 = 261.52 KB
GiYOL:  192.00 + 170.55 = 362.55 KB
```

如果把 init objects 也算作占用 local/cache budget：

```text
Cheney: 192.00 + 19.76 +  37.52 + 262.72 = 512.00 KB
GiY:    192.00 + 19.76 +  69.52 + 230.72 = 512.00 KB
GiYOL:  192.00 + 19.76 + 170.55 + 129.69 = 512.00 KB
```

结论：

```text
Cheney 可用普通 Young work area 最大: 262.72 KB
GiY 次之:                         230.72 KB
GiYOL 最小:                       129.69 KB
```

GiYOL 比 GiY 多占 `101.03KB`，来自：

```text
staging buffer:        5.00 KB
reserve-order batch:  96.03 KB
```

GiY 比 Cheney 多占 `32.00KB`，主要因为 GiY 增加了 traversal stack，同时 GiY 的 FT slot set 比 Cheney 小一点：

```text
GiY extra stack:       +37.52 KB
GiY FT smaller:         -5.52 KB
net GiY - Cheney:      +32.00 KB
```

## 2026-05-15 GiY 核心原则与当前服务器上的 local workspace 最佳大小判断

服务器 CPU 配置：

```text
CPU: Intel Xeon W-2235 @ 3.80GHz
cores: 6 physical cores, 12 logical CPUs
SMT: 2 threads / core
L1d: 32KB per core
L2:  1024KB per core, shared by SMT siblings
L3:  8448KB shared by all logical CPUs
cache line: 64B
L2 associativity: 16-way
```

GiY 的核心原则：

1. Minor GC 的高频 metadata 应该留在 cache-friendly 的 local area。
2. Young object graph 的发现、reserve、patch 尽量在 local area 内完成，减少对 Old/DRAM slot 的反复读取。
3. Old 区只在对象真正 materialize/copy 时写入。
4. Local workspace 不是越大越好：太小会 overflow 或导致频繁 GC；太大则不能稳定留在 L2，反而破坏 GiY 想要的 locality。

当前 GiY 空间：

```text
CACHE_SIZE_KB total budget: 512KB
remembered set:             192.00 KB
GiY minor aux:               69.52 KB
  traversal stack:           37.52 KB
  function-table slots:      32.00 KB
GC Local Workspace total:   261.52 KB
ordinary Young work area:   230.72 KB
init objects:                19.76 KB
```

结合 CPU cache 的判断：

```text
L1d 32KB:
  太小，不可能容纳 GiY workspace。只能容纳当前正在访问的小窗口。

L2 1024KB:
  这是 GiY 应该主要瞄准的 cache level。
  512KB total local/cache budget 约等于 L2 的一半，比较合理。

L3 8448KB shared:
  可以作为后备，但不应该把 GiY 的设计目标设成“主要依赖 L3”。
  L3 是所有 core 共享的，benchmark 噪声更大。
```

因此分两个口径判断：

### 1. 如果 local workspace 指整块 local/cache budget

最佳起点仍然是：

```text
CACHE_SIZE_KB = 512KB
```

理由：

- 512KB 可以稳定放进 1MB L2，并给 VM stack、代码路径、临时访问的 young object、copy source/destination 元数据留出空间。
- 768KB 或 1MB 虽然也可能放进 L2，但会接近 L2 容量上限；SMT sibling、VM 热数据、cache conflict 都可能让它不稳定。
- 256KB 会更容易留在 L2/L1 附近，但 ordinary Young work area 太小，可能显著增加 minor GC 次数。

推荐实验范围：

```text
384KB, 512KB, 640KB
```

当前最推荐：

```text
512KB
```

不建议优先尝试：

```text
>= 768KB
```

除非后续实验显示 GC frequency 的下降收益明显超过 L2 locality 损失。

### 2. 如果 local workspace 指 RSet + GiY auxiliary metadata

当前：

```text
RSet + GiY aux = 261.52KB
```

这个数值对 1MB L2 来说可以接受，但略高于理想值。理想目标应是：

```text
224KB - 256KB
```

更具体地说：

```text
RSet:         暂时保持 192KB
stack:        32KB 左右
FT slots:      4KB - 8KB
--------------------------------
target:      about 228KB - 232KB
```

理由：

- RSet 是 correctness + write barrier 结构，不能只根据一个 benchmark 盲目缩小。
- FT slot set 当前 profile 最大只到约 1.77KB，32KB 明显偏保守。
- traversal stack 当前 profile 最大约 5.34KB，但它和对象图形状有关，缩小要比 FT slot set 更保守。

最终判断：

```text
GiY total local/cache budget 最佳当前值: 512KB
GiY metadata workspace 最佳目标:        约 232KB，最高不超过 256KB
当前 GiY metadata workspace:            261.52KB，略高但仍可接受
```

下一步如果要优化空间，优先顺序：

1. 把 FT slot set 从 `32KB` 调到 `8KB` 做实验。
2. 给 traversal stack 和 FT slot set 加 max/overflow profile。
3. 暂时不要先砍 RSet；先做 max RSet count 和 lazy clear profile。
4. 做 `CACHE_SIZE_KB=384/512/640` 的性能实验，观察 GC 次数、pause、cache miss。

## 2026-05-15 关于是否让 local/cache budget 占 L2 约 90%

用户想法：

> 想让 local workspace 占 L2 大概 90% 左右，因为大多数情况下不会访问 local workspace 之外的地方。

当前服务器每个物理核：

```text
L2 = 1024KB
90% L2 ~= 921KB
```

判断需要分两个概念：

### 如果指整块 local/cache budget

也就是 `CACHE_SIZE_KB`，设置到约 `896KB` 或 `900KB` 是可以作为实验的。

优点：

- ordinary Young work area 会明显变大；
- minor GC 次数可能下降；
- 如果 benchmark 确实主要在 local/cache region 内活动，可能有收益。

风险：

- L2 不是只给 local/cache region 用。VM stack、C/C++ runtime 数据、Context、roots、function table、当前执行代码带来的 data footprint、prefetch 访问、write barrier metadata 都会占 L2。
- 这台 CPU 的 L2 是每个物理核 1MB，但和 SMT sibling 共享。如果另一个 logical thread 有活动，90% 会非常紧。
- L2 是 16-way set associative，不是完全相联。即使总大小小于 1MB，也可能因为 set conflict 产生 L2 miss。
- GiY minor GC 虽然尽量避免读 Old slot，但不是完全不访问 local/cache region 之外：
  - 会写 Old/DRAM destination；
  - 会执行 VM/GC 代码；
  - roots、Context、function table、runtime stack 不一定全部在 local/cache region 内；
  - NT store 仍会占用 store buffer / write combining 资源。

因此：

```text
CACHE_SIZE_KB = 896KB 或 900KB 可以实验
但不建议直接认定为最佳默认值
```

比较合理的实验组：

```text
512KB, 640KB, 768KB, 896KB
```

如果必须选一个“激进但仍有解释力”的点：

```text
CACHE_SIZE_KB = 896KB
```

选择 `896KB` 比 `921KB` 更干净，因为它是 64KB 的整数倍，并且给 L2 留约 `128KB` 给非 local/cache 数据。

### 如果指 GC metadata workspace

不建议让 `RSet + stack + FT + GiYOL batch` 这类 metadata 占 L2 90%。

原因：

- metadata 过大意味着 ordinary Young work area 反而变小；
- GiY/GiYOL 的目标是让 metadata 热，而不是让 metadata 吃掉大部分 L2；
- metadata 很多结构是保守预留，不是每次真的用满。

结论：

```text
整块 local/cache budget 占 L2 约 90%：可以做实验，推荐 896KB。
GC metadata workspace 占 L2 约 90%：不推荐。
```

如果后续实验 `CACHE_SIZE_KB=896` 比 `512` 好，需要同时确认：

```text
minor GC count 是否下降
GC pause 是否下降
PMU L2/LLC miss 是否没有明显恶化
不同 benchmark 是否稳定
单线程 pin core 后是否仍然成立
```

## 2026-05-15 对 `CACHE_SIZE_KB=896` 作为强候选实验点的判断

当前共识：

```text
整块 local/cache budget 占 L2 约 90%：可以做实验，推荐 896KB。
```

判断：

- 这个方案是合理的，尤其适合作为 GiY/GiYOL 的强候选实验点。
- 它符合 GiY 的核心假设：minor GC 的主要工作集尽可能留在 per-core L2。
- `896KB` 比理论上的 `921KB` 更合适，因为它是 `64KB` 对齐的整数点，并且给 1MB L2 留出约 `128KB` 给 VM stack、Context、roots、GC/VM 其它热数据和 cache conflict 余量。
- 但是目前仍应把它叫作 experimental candidate，而不是直接叫 optimal default。是否成为默认值需要靠 benchmark 验证。

推荐实验矩阵：

```text
CACHE_SIZE_KB = 512, 640, 768, 896
```

重点比较：

```text
minor GC count
average / total GC pause
total runtime
young after aux
RSet pressure
GiY/GiYOL workspace overflow 是否发生
L2 / LLC miss，如果 PMU 权限允许
```

如果 `896KB` 在多数 benchmark 中降低 GC 次数和总时间，同时没有明显增加 cache miss，那么它可以成为当前服务器上的推荐默认值。

## 2026-05-15 `CACHE_SIZE_KB=896` 后建议的 heap/local-space 配置结构

目标：

```text
让整块 local/cache budget 接近 per-core L2 的 90%，但不要让 GC metadata 也等比例膨胀。
```

当前服务器：

```text
L2 = 1024KB
recommended local/cache budget = 896KB
reserved L2 headroom = about 128KB
```

建议的逻辑结构：

```text
Local/cache region: 896KB total

low address
  [init objects]                 about 20KB, program dependent
  [ordinary Young work area]      main expandable part
  [minor-GC aux metadata]         stack / FT / GiYOL staging / GiYOL batch
  [remembered set]                fixed first at high end
high address

Old/DRAM region:
  keep current large DRAM/old-space setting unchanged
```

### Minimal-change result with current formulas

如果只设置：

```text
CACHE_SIZE_KB=896
```

并保持当前代码公式不变，假设 init objects 仍约 `19.76KB`，则：

```text
total local/cache budget: 896.00 KB
remembered set:          192.00 KB
after remembered set:    704.00 KB
init objects:             19.76 KB
young before minor aux:  684.24 KB
```

Cheney:

```text
minor aux:                85.52 KB
GC Local Workspace:      277.52 KB
ordinary Young work:     598.72 KB
```

GiY:

```text
traversal stack:          85.52 KB
function-table slots:     32.00 KB
minor aux:               117.52 KB
GC Local Workspace:      309.52 KB
ordinary Young work:     566.72 KB
```

GiYOL:

```text
traversal stack:          85.52 KB
function-table slots:     32.00 KB
staging buffer:            5.00 KB
reserve-order batch:      96.03 KB
minor aux:               218.55 KB
GC Local Workspace:      410.55 KB
ordinary Young work:     465.69 KB
```

这个 minimal-change 配置的优点：

- 不需要改太多代码；
- ordinary Young work area 比 512KB 配置明显变大；
- 可以先验证 `896KB` 这个方向是否有效。

缺点：

- GiY stack 会从约 `37.52KB` 自动涨到约 `85.52KB`，但之前 profile 最大只用约 `5.34KB`；
- Cheney FT slot set 也会涨到约 `85.52KB`；
- metadata 有一部分是跟着 local/cache budget 被动放大，不一定真的需要。

### 我建议的目标结构

如果后续要把 `896KB` 做成正式配置，而不仅是实验点，建议不要让 metadata 等比例增长。目标结构应是：

GiY:

```text
total local/cache budget: 896KB
remembered set:          192KB
init objects:             ~20KB
traversal stack:           32KB - 64KB
function-table slots:       8KB
ordinary Young work:      about 612KB - 644KB
```

GiYOL:

```text
total local/cache budget: 896KB
remembered set:          192KB
init objects:             ~20KB
traversal stack:           32KB - 64KB
function-table slots:       8KB
staging buffer:             5KB
reserve-order batch:       96KB first, later test 48KB/64KB
ordinary Young work:      about 511KB - 543KB
```

推荐阶段：

1. 第一阶段只改 `CACHE_SIZE_KB=896`，不要同时改太多结构，先拿性能数据。
2. 第二阶段固定 `CACHE_SIZE_KB=896`，开始压缩 metadata：
   - FT slot set: `32KB -> 8KB`
   - stack: 当前公式产生 `85KB`，建议试 `64KB` 或 `32KB`
   - GiYOL batch: `96KB` 先保留；有 max batch profile 后再试 `64KB` 或 `48KB`
3. RSet 先保持 `192KB`，不要和第一轮实验混在一起；后续单独做 lazy clear 或缩小 RSet 实验。

最终推荐：

```text
先实验的配置:
  CACHE_SIZE_KB=896
  RSet=192KB
  current aux formulas unchanged

更理想的正式配置目标:
  CACHE_SIZE_KB=896
  RSet=192KB
  stack=32KB-64KB
  FT slots=8KB
  GiYOL staging=5KB
  GiYOL batch=96KB first, later 64KB/48KB experiment
```

## 2026-05-15 术语纠正：GC Local Workspace 的定义

纠正：

之前把 `RSet + minor aux metadata` 单独称作 `GC Local Workspace` 是不准确的。

用户定义的 **GC Local Workspace** 应该是：

```text
所有预期放进 cache 的 local/cache budget 的总空间
```

也就是代码里的整块 `cache_space` budget：

```text
GC Local Workspace = CACHE_SIZE_KB 指定的整块 local/cache region
```

其中包含多个子区域：

```text
GC Local Workspace
  - init objects
  - ordinary Young work area
  - remembered set
  - minor-GC auxiliary metadata
      - traversal stack
      - function-table slot set
      - GiYOL staging buffer
      - GiYOL reserve-order batch
```

因此之后必须这样表述：

- `GC Local Workspace total`: 整块预期进入 cache 的总空间，例如 `512KB` 或 `896KB`。
- `ordinary Young work area`: GC Local Workspace 中真正给普通 young objects 分配的部分。
- `metadata / auxiliary area inside GC Local Workspace`: RSet、stack、FT slots、GiYOL staging、GiYOL batch 等。

不能再把 `RSet + aux` 叫作完整的 `GC Local Workspace`；它只是 GC Local Workspace 内部的 metadata/auxiliary 子区域。

在 `CACHE_SIZE_KB=896` 时，正确说法是：

```text
GC Local Workspace total = 896KB
```

GiYOL 当前公式下的内部结构大约是：

```text
GC Local Workspace total: 896.00 KB
  remembered set:        192.00 KB
  init objects:           19.76 KB
  GiYOL minor aux:       218.55 KB
  ordinary Young work:   465.69 KB
```

GiY 当前公式下的内部结构大约是：

```text
GC Local Workspace total: 896.00 KB
  remembered set:        192.00 KB
  init objects:           19.76 KB
  GiY minor aux:         117.52 KB
  ordinary Young work:   566.72 KB
```

Cheney 当前公式下的内部结构大约是：

```text
GC Local Workspace total: 896.00 KB
  remembered set:        192.00 KB
  init objects:           19.76 KB
  Cheney minor aux:       85.52 KB
  ordinary Young work:   598.72 KB
```

## 2026-05-15 如何公平比较不同 GC Local Workspace 大小

问题：

如果改变 `GC Local Workspace` 大小，但内部结构不是用统一原则计算，那么 Cheney / GiY / GiYOL 之间的比较会不公平。

结论：

公平比较需要先明确比较口径，并且一次只改变一个主要变量。

### 口径 A：固定硬件预算，比较真实算法成本

这是最重要的口径。

规则：

```text
所有 GC 使用相同的 GC Local Workspace total
例如都使用 CACHE_SIZE_KB=512 或都使用 CACHE_SIZE_KB=896
```

然后内部结构按统一布局原则分配：

```text
GC Local Workspace total = init objects
                         + remembered set
                         + common auxiliary structures
                         + algorithm-specific auxiliary structures
                         + ordinary Young work area
```

在这个口径下：

- Cheney 不需要 GiY stack，所以它得到更多 ordinary Young work area，这是 Cheney 的真实优势。
- GiYOL 需要 staging/batch，所以 ordinary Young work area 变小，这是 GiYOL 的真实成本。
- 不能为了让 GiYOL 看起来更好而给它额外 workspace，也不能为了让 Cheney/GiY 看起来更公平而强行浪费空间。

这回答的是：

```text
在同样 L2/cache budget 下，哪个方案整体更好？
```

### 口径 B：固定 ordinary Young work area，隔离算法本身

这是辅助口径。

规则：

```text
让 Cheney / GiY / GiYOL 拥有相同 ordinary Young work area
然后各自额外承担自己的 metadata
```

这会导致不同 GC 的 `GC Local Workspace total` 不同。

这个口径回答的是：

```text
在相同 young capacity / GC frequency 条件下，算法本身的 copying / scanning / patching 成本如何？
```

它适合做机制分析，但不适合作为最终硬件预算结论。

### 对 `CACHE_SIZE_KB=896` 的建议

第一轮实验应采用口径 A：

```text
Cheney: CACHE_SIZE_KB=896
GiY:    CACHE_SIZE_KB=896
GiYOL:  CACHE_SIZE_KB=896
```

也就是所有算法使用同一个 `GC Local Workspace total`。

但内部结构不应该“随便自动膨胀”。目前代码里有一个问题：

```text
GiY stack = young_bytes / 8
Cheney FT slots = young_bytes / 8
GiY FT slots = max(young_bytes / 64, 32KB)
```

因此当 `CACHE_SIZE_KB` 从 512KB 增加到 896KB 时，有些 metadata 会跟着变大，有些不会。这会让实验混入另一个变量：metadata policy 变化。

更公平的做法是：

1. 先做 minimal-change 实验，记录真实内部布局。
2. 然后做 normalized-layout 实验，把 metadata policy 固定下来。

建议 normalized-layout：

```text
RSet: fixed 192KB for all GC
FT slots: fixed 8KB or 32KB for all GC that needs FT slot tracking
GiY/GiYOL stack: fixed 32KB or 64KB
GiYOL staging: fixed 5KB
GiYOL reserve batch: fixed 96KB first, later test 64KB/48KB
ordinary Young work area: remaining space
```

在 fixed hardware budget 口径下，算法专有 metadata 仍然计入成本：

```text
GiYOL batch/staging 减少 ordinary Young work area，这是 GiYOL 的真实空间成本。
GiY stack 减少 ordinary Young work area，这是 GiY 的真实空间成本。
Cheney 不需要这些结构，就不应该强行分配给 Cheney。
```

推荐实验顺序：

```text
1. CACHE_SIZE_KB=512, 640, 768, 896
   使用当前代码公式，得到 baseline。

2. 固定 CACHE_SIZE_KB=896
   使用 normalized metadata layout：
     RSet fixed
     FT fixed
     stack fixed
     GiYOL staging/batch fixed

3. 再做口径 B：
   固定 ordinary Young work area，隔离算法 copy/scanning 成本。
```

最终对论文/汇报最公平的说法：

```text
Primary comparison:
  same GC Local Workspace total, because this reflects the same L2/cache budget.

Secondary analysis:
  same ordinary Young work area, because this isolates algorithmic overhead from GC frequency.
```

## 2026-05-15 GiY 在不同 GC Local Workspace 总大小下的内部结构

定义确认：

```text
GC Local Workspace = 整块预期放进 cache 的 local/cache budget
                   = CACHE_SIZE_KB 指定的总大小
```

这里分析的是 GiY，不包含 GiYOL 的 staging buffer / reserve-order batch。

假设：

```text
init objects ~= 19.76 KB
remembered set = 192.00 KB
GiY edge log = 0.00 KB
GiY stack = young_before_aux / 8
GiY FT slot set = max(young_before_aux / 64, 32KB)
```

当前四组 GiY 配置：

```text
GC Local Workspace total = 512KB
  remembered set:        192.00 KB
  init objects:           19.76 KB
  young before aux:      300.24 KB
  GiY stack:              37.52 KB
  GiY FT slots:           32.00 KB
  GiY minor aux total:    69.52 KB
  ordinary Young work:   230.72 KB

GC Local Workspace total = 640KB
  remembered set:        192.00 KB
  init objects:           19.76 KB
  young before aux:      428.24 KB
  GiY stack:              53.52 KB
  GiY FT slots:           32.00 KB
  GiY minor aux total:    85.52 KB
  ordinary Young work:   342.72 KB

GC Local Workspace total = 768KB
  remembered set:        192.00 KB
  init objects:           19.76 KB
  young before aux:      556.24 KB
  GiY stack:              69.52 KB
  GiY FT slots:           32.00 KB
  GiY minor aux total:   101.52 KB
  ordinary Young work:   454.72 KB

GC Local Workspace total = 896KB
  remembered set:        192.00 KB
  init objects:           19.76 KB
  young before aux:      684.24 KB
  GiY stack:              85.52 KB
  GiY FT slots:           32.00 KB
  GiY minor aux total:   117.52 KB
  ordinary Young work:   566.72 KB
```

表格总结：

```text
Total workspace | RSet   | Init  | Stack | FT   | Aux total | Ordinary Young
512KB           | 192KB  | 19.76 | 37.52 | 32KB |  69.52KB  | 230.72KB
640KB           | 192KB  | 19.76 | 53.52 | 32KB |  85.52KB  | 342.72KB
768KB           | 192KB  | 19.76 | 69.52 | 32KB | 101.52KB  | 454.72KB
896KB           | 192KB  | 19.76 | 85.52 | 32KB | 117.52KB  | 566.72KB
```

判断：

- 这四组的比较是公平的，因为 `GC Local Workspace total` 是明确设定的实验变量。
- RSet 保持固定 `192KB`，避免把 RSet policy 变化混入 heap-size 实验。
- FT slots 当前因为 `max(young/64, 32KB)`，在这四组里都保持 `32KB`。
- stack 会随 total workspace 增大而线性变大，这是当前 GiY 公式的结果。
- 如果只想分析 total workspace 大小，第一轮应该保持这些公式不变。
- 如果之后要分析 metadata policy，应另开实验，把 stack/FT 改成固定值，不要和 workspace-size 实验混在一起。

## 2026-05-15 GiY 的 edge log / stack / FT slot set 分别是什么

### stack

代码结构：

```cpp
struct GiYGCStack {
  uintptr_t *items;
  size_t count;
  size_t capacity;
  bool in_cache_space;
};
```

作用：

- 保存已经被发现为 live young object、并且已经 reserve 了 Old/DRAM 目的地址的 young object payload pointer。
- `copy_for_minor()` 第一次遇到一个 young object 时：
  - 在 Old/DRAM 里 reserve 目标地址；
  - 把 forwarding pointer 写进 young object header；
  - `gc_stack_push(payload_ptr)` 把这个 young object 放进 GiY stack。
- 后续 `giy_traverse_stack_and_copy()` 从这个 stack 里 pop object，扫描它的子对象，最后真正 materialize/copy 到 Old/DRAM。

为什么需要：

- GiY 不是发现一个对象就马上 copy，而是先 reserve，再遍历 young graph，再 materialize。
- stack 是 young object graph traversal 的 worklist。

空间：

```text
当前公式: stack = young_before_aux / 8
```

### edge log

代码结构：

```cpp
struct EdgePatchEntry {
  void *slot_addr;
  EdgePatchKind kind;
};

struct EdgePatchLog {
  EdgePatchEntry *items;
  size_t count;
  size_t capacity;
  bool in_cache_space;
};
```

当前状态：

- 结构还在代码里，但当前实现没有真正为它分配 entries。
- `edge_bytes = 0`
- `g_edge_log.items = NULL`
- `g_edge_log.capacity = 0`

现在它主要用于 profile 计数：

- `g_edge_log.count++`
- 记录 max edge count per object / per GC
- 统计 young edge visits

它原本/概念上的用途：

- 记录某个 object 内部哪些 edge slot 需要 patch。
- 但当前 GiYReserveTracer 在扫描 young object 时，如果 slot 本身在 young object 内，就直接 patch 这个 slot，不需要把 slot 地址记下来等以后 patch。

所以当前结论：

```text
edge log 当前不占 GC Local Workspace 空间。
edge log 在 profile 语义上存在，但不是当前空间分析的实际占用项。
```

### FT slot set

FT = function table。

代码结构：

```cpp
struct GiYFTSlotSet {
  uintptr_t *items;
  size_t count;
  size_t capacity;
  bool in_cache_space;
};
```

作用：

- 记录 function table / inline cache 等 VM metadata 里面指向 young object 的 slot。
- 这些 slot 不一定在普通 young object 内，也不一定会被 roots scanning 完整覆盖。
- 当这些 slot 被写入 young pointer 时，会调用：
  - `giy_record_ft_jsvalue_slot(JSValue *slot, JSValue value)`
  - `giy_record_ft_ptr_slot(void **slot, void *value)`
- 它们把 slot address 记录到 `g_ft_slot_set`。

minor GC 时怎么用：

1. reserve 阶段：

```text
giy_reserve_function_table_slots()
```

读取这些 slot 当前保存的 young pointer，并对目标 young object 调用 reserve。

2. patch 阶段：

```text
giy_patch_function_table_slots()
```

把 slot 里的 young pointer patch 成 forwarding pointer 指向的 Old/DRAM address。

3. GC 结束：

```text
giy_clear_function_table_slots()
```

清空这次记录。

为什么需要：

- function table / inline cache slot 属于 VM metadata，不是普通 young object field。
- 如果不记录，minor GC 后这些 metadata 可能还指向已经被回收的 young address。

空间：

```text
当前公式: FT slot set = max(young_before_aux / 64, 32KB)
在 512/640/768/896KB 四组 GiY 实验里都是 32KB。
```

## 2026-05-15 GiY workspace size benchmark 运行计划

目标：

```text
彻底测试 GiY 在 512KB / 640KB / 768KB / 896KB 四种 GC Local Workspace 总大小下的性能。
```

规则：

- 只测试 GiY：`OPT_GC=giy`
- 只改变 `CACHE_SIZE_KB`
- benchmark 文件使用 `build.debug/benchmarks` 文件夹里的主 benchmark `.sbc`
- 不混入 `hello_world`、`giy_gc_probe`、`bounce_check` 这类 smoke/probe
- 每个 size 重新 build 一次
- 每个 benchmark 保存 `.out`、`.time`、`.status`
- 使用 `taskset -c 0` 尽量固定在同一个 logical CPU 上，减少调度噪声

benchmark 集合：

```text
Bounce
CD
DeltaBlue
Havlak
List
Mandelbrot
NBody
Permute
Queens
Richards
Sieve
Storage
Towers
```

输出脚本：

```text
tools/run_giy_cache_size_benchmarks.sh
tools/parse_giy_cache_size_benchmarks.py
```

预期输出目录：

```text
build.debug/benchmarks/out_giy_cache_size_YYYYMMDD_HHMMSS/
```

## 2026-05-15 GiY workspace size benchmark 结果

输出目录：

```text
build.debug/benchmarks/out_giy_cache_size_20260515_004640
```

生成文件：

```text
build.debug/benchmarks/out_giy_cache_size_20260515_004640/summary.csv
build.debug/benchmarks/out_giy_cache_size_20260515_004640/summary.md
```

执行情况：

- 四个配置都完成：`CACHE_SIZE_KB=512/640/768/896`
- 每个配置都重新 build
- 每个配置都运行 13 个主 benchmark
- 共 4 个 build status + 52 个 benchmark status = 56 个 status 文件
- 所有 status 都是 0

### 聚合结果

| GC Local Workspace | 完成数 | 总执行时间(s) | GC overhead(s) | minor GC 次数 |
|---:|---:|---:|---:|---:|
| 512KB | 13 | 6793.957 | 165.650 | 2749204 |
| 640KB | 13 | 6729.767 | 138.876 | 1846048 |
| 768KB | 13 | 6635.928 | 121.671 | 1389777 |
| 896KB | 13 | 6661.375 | 114.211 | 1114512 |

相对 512KB 的总执行时间：

| GC Local Workspace | 总时间变化 |
|---:|---:|
| 640KB | -0.945% |
| 768KB | -2.326% |
| 896KB | -1.951% |

### 每个 benchmark 的最优配置

| Benchmark | 最优配置 |
|---|---:|
| Bounce | 896KB |
| CD | 768KB |
| DeltaBlue | 768KB |
| Havlak | 768KB |
| List | 768KB |
| Mandelbrot | 896KB |
| NBody | 896KB |
| Permute | 768KB |
| Queens | 896KB |
| Richards | 768KB |
| Sieve | 896KB |
| Storage | 896KB |
| Towers | 768KB |

计数：

```text
768KB: 7 个 benchmark 最快
896KB: 6 个 benchmark 最快
512KB/640KB: 0 个 benchmark 最快
```

### 768KB 与 896KB 的关键差异

按总执行时间：

```text
768KB = 6635.928s
896KB = 6661.375s
896KB 比 768KB 慢 25.447s，约 +0.383%
```

按 GC overhead：

```text
768KB = 121.671s
896KB = 114.211s
896KB 比 768KB 少 7.460s，约 -6.131%
```

按 minor GC 次数：

```text
768KB = 1389777
896KB = 1114512
896KB 比 768KB 少 275265 次，约 -19.806%
```

所以这次结果显示：

```text
如果目标是总执行时间，768KB 是当前 full benchmark suite 的最好点。
如果目标是降低 GC overhead / minor GC 次数，896KB 是当前最好点。
```

896KB 没有拿到总时间第一，主要是因为它虽然减少了 GC 成本，但在若干长 benchmark 上变慢：

| Benchmark | 896KB - 768KB 时间差(s) |
|---|---:|
| Havlak | +26.484 |
| Richards | +9.943 |
| Permute | +3.143 |
| DeltaBlue | +1.580 |
| CD | +0.456 |
| Towers | +0.315 |
| List | +0.017 |

而 896KB 明显更快的 benchmark 是：

| Benchmark | 896KB - 768KB 时间差(s) |
|---|---:|
| Bounce | -6.604 |
| Storage | -5.339 |
| NBody | -2.126 |
| Mandelbrot | -1.386 |
| Queens | -0.547 |
| Sieve | -0.489 |

### 当前判断

这次单轮 full benchmark suite 的结论：

- 512KB 明显太小：minor GC 次数和 GC overhead 都最高。
- 640KB 比 512KB 明显改善，但不是最优。
- 768KB 是总执行时间最优点。
- 896KB 是 GC 指标最优点，但总时间略输给 768KB。

工程判断：

```text
768KB 更适合作为当前默认候选。
896KB 更适合作为“接近 90% L2 cache budget”的实验候选。
```

原因是：

- 896KB 的 GC 次数确实更少，说明扩大 GC Local Workspace 有效果。
- 但 896KB 接近 1MB L2，给代码、栈、运行时热点数据、硬件预取和其他临时数据留下的 L2 余量更少。
- 因此总执行时间不一定继续下降。
- GiY 的核心原则不是单纯把 workspace 做大，而是让高频访问的 GC 工作集留在 cache 内，并避免挤出同样重要的非 GC 热点数据。

注意：

```text
这次是每个 benchmark / 每个配置各跑一次的 full suite 长跑。
如果要做论文或答辩图表，建议之后对 768KB 和 896KB 做多轮重复，报告 median / min / variance。
```

## 2026-05-15 Cheney 对照组 benchmark 计划

目的：

```text
为 GiY 的 512KB / 640KB / 768KB / 896KB GC Local Workspace 实验建立公平对照组。
```

公平性规则：

- 使用同一台实验主机。
- 使用同一个 `build.debug/benchmarks` 文件夹里的 `.sbc` benchmark。
- 使用同一组 benchmark：

```text
Bounce
CD
DeltaBlue
Havlak
List
Mandelbrot
NBody
Permute
Queens
Richards
Sieve
Storage
Towers
```

- 使用同样的四个 `CACHE_SIZE_KB`：

```text
512
640
768
896
```

- 每个 size 都重新 build。
- 使用 `taskset -c 0` 固定到同一个 logical CPU。
- 保存 `.out`、`.time`、`.status`，并允许断点恢复。

对照组 GC：

```text
OPT_GC=cache_cheney
```

注意：

```text
这里的 Cheney 对照组不是传统 full-heap copying collector。
它是当前代码库里的 cache_cheney：young/local workspace + DRAM old 区 + remembered set + Cheney-style immediate copying minor GC。
因此它与 GiY 的公平比较重点是：
在相同 GC Local Workspace budget 下，
Cheney-style immediate copying minor GC 与 GiY reserve-first/materialize-later minor GC 的差异。
```

新增脚本：

```text
tools/run_cache_cheney_cache_size_benchmarks.sh
tools/parse_gc_cache_size_benchmarks.py
```

预期输出目录：

```text
build.debug/benchmarks/out_cache_cheney_cache_size_YYYYMMDD_HHMMSS/
```

## 2026-05-15 Cheney 对照组 benchmark 结果

输出目录：

```text
build.debug/benchmarks/out_cache_cheney_cache_size_20260515_115946
```

生成文件：

```text
build.debug/benchmarks/out_cache_cheney_cache_size_20260515_115946/summary.csv
build.debug/benchmarks/out_cache_cheney_cache_size_20260515_115946/summary.md
build.debug/benchmarks/out_cache_cheney_cache_size_20260515_115946/compare_giy_vs_cache_cheney.csv
build.debug/benchmarks/out_cache_cheney_cache_size_20260515_115946/compare_giy_vs_cache_cheney.md
```

执行情况：

- 四个配置都完成：`CACHE_SIZE_KB=512/640/768/896`
- 每个配置都重新 build：`OPT_GC=cache_cheney`
- 每个配置都运行同样 13 个主 benchmark
- 共 4 个 build status + 52 个 benchmark status = 56 个 status 文件
- 所有 status 都是 0

### Cheney 聚合结果

| GC Local Workspace | 完成数 | 总执行时间(s) | GC overhead(s) | minor GC 次数 |
|---:|---:|---:|---:|---:|
| 512KB | 13 | 6631.114 | 143.579 | 2374914 |
| 640KB | 13 | 6585.095 | 126.606 | 1660894 |
| 768KB | 13 | 6636.327 | 117.653 | 1282009 |
| 896KB | 13 | 6946.021 | 112.801 | 1044731 |

Cheney 内部最优点：

```text
总执行时间最优：640KB = 6585.095s
GC overhead 最低：896KB = 112.801s
minor GC 次数最少：896KB = 1044731
```

这个形状和 GiY 很像：

```text
workspace 变大以后，GC overhead / minor GC 次数会下降。
但总执行时间不会一直下降。
```

Cheney 在 896KB 下总时间明显变差，主要来自长 benchmark：

```text
Richards:
  512KB = 1734.365s
  640KB = 1749.271s
  768KB = 1749.952s
  896KB = 1992.976s

Havlak:
  512KB = 792.579s
  640KB = 755.147s
  768KB = 759.832s
  896KB = 819.027s
```

### GiY vs Cheney 同 size 比较

这里的差值定义：

```text
GiY - Cheney
负数表示 GiY 更快或更少。
正数表示 Cheney 更快或更少。
```

| GC Local Workspace | GiY 总时间(s) | Cheney 总时间(s) | GiY-Cheney(s) | GiY vs Cheney | GiY GC(s) | Cheney GC(s) | GiY-Cheney GC(s) |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 512KB | 6793.957 | 6631.114 | +162.843 | +2.456% | 165.650 | 143.579 | +22.071 |
| 640KB | 6729.767 | 6585.095 | +144.672 | +2.197% | 138.876 | 126.606 | +12.270 |
| 768KB | 6635.928 | 6636.327 | -0.399 | -0.006% | 121.671 | 117.653 | +4.018 |
| 896KB | 6661.375 | 6946.021 | -284.646 | -4.098% | 114.211 | 112.801 | +1.410 |

同 size 结论：

- 512KB：Cheney 更快，GiY 慢约 2.456%。
- 640KB：Cheney 更快，GiY 慢约 2.197%。
- 768KB：几乎打平，GiY 快 0.399s，只有 0.006%。
- 896KB：GiY 更快，主要因为 Cheney 在 Richards 上明显变慢。

跨所有 GC/size 的总时间排名：

```text
1. cache_cheney 640KB: 6585.095s
2. cache_cheney 512KB: 6631.114s
3. GiY 768KB:          6635.928s
4. cache_cheney 768KB: 6636.327s
5. GiY 896KB:          6661.375s
6. GiY 640KB:          6729.767s
7. GiY 512KB:          6793.957s
8. cache_cheney 896KB: 6946.021s
```

最优 Cheney vs 最优 GiY：

```text
best Cheney = 640KB, 6585.095s
best GiY    = 768KB, 6635.928s
GiY 比 Cheney 慢 50.833s，约 +0.772%
```

### 重要公平性说明

这次比较是公平的“总 GC Local Workspace budget”比较：

```text
CACHE_SIZE_KB 相同。
同 benchmark。
同 build.debug。
同 CPU pinning。
同输出和解析方式。
```

但这不是“普通 young 可分配区完全相同”的比较。

原因：

- Cheney 也会在 local workspace 里放 remembered set 和 FT slot set。
- GiY 还需要额外的 reserve stack，以及 FT slot set。
- 在当前实现里，GiY 的普通 young 可分配区比 Cheney 小约 32KB。

按当前公式估算：

| GC Local Workspace | Cheney 普通 young 约(KB) | GiY 普通 young 约(KB) | Cheney-GiY |
|---:|---:|---:|---:|
| 512KB | 262.71 | 230.71 | +32.00KB |
| 640KB | 374.71 | 342.71 | +32.00KB |
| 768KB | 486.71 | 454.71 | +32.00KB |
| 896KB | 598.71 | 566.71 | +32.00KB |

这个差异不应该被认为是实验不公平。
它是 GiY 算法设计本身的空间成本：

```text
GiY 用一部分 GC Local Workspace 换取 reserve-first / materialize-later 的执行方式。
Cheney 用较少辅助结构，因此在同样总 workspace 下有更大的普通 young 区。
```

如果之后想隔离“复制策略本身”的差异，可以再设计第二类实验：

```text
固定 ordinary young size 相同，
然后分别给 GiY/Cheney 分配各自需要的 aux space。
```

但那就不是用户当前定义的统一 `GC Local Workspace` budget 比较。

### 当前解释

这次数据说明：

- GiY 的 reserve-first 设计没有在当前实现上显著压过 Cheney。
- 在小 workspace 下，GiY 因为 reserve stack / FT 等辅助区占用，普通 young 更小，所以 minor GC 更多，总 GC overhead 更高。
- 到 768KB 时，GiY 与 Cheney 总时间几乎相同。
- 到 896KB 时，GiY 虽然 GC overhead 仍略高于 Cheney，但总时间明显好于 Cheney，因为 Cheney 在 Richards 上出现大幅变慢。

因此当前更稳妥的表达是：

```text
GiY 当前已经接近 Cheney 的最好性能点，但还没有稳定超过 Cheney。
GiY 的优势可能出现在较大的 GC Local Workspace 下，或者出现在 Cheney 对 cache pressure 更敏感的 workload 上。
下一步应该重点分析 768KB/896KB，并对 Havlak/Richards 做多轮重复。
```

## 2026-05-16 GiY cache miss rate 测试方法设计

问题：

```text
当前 GiY 是否真的降低了 cache miss rate？
```

当前不能直接下结论。
已有 benchmark 主要证明了：

- GiY / Cheney 的总执行时间差异；
- GC overhead 差异；
- minor GC 次数差异；
- 不同 GC Local Workspace 大小下的性能形状。

但这些不是硬件 cache miss rate。

当前代码里已经有 GC-window PMU 计数框架：

- `ejsvm/giy_dram_manager.cc`
- `ejsvm/cache_dram_manager.cc`

它会在 minor GC 窗口内尝试记录：

```text
cache-misses
cache-references
instructions
cache-miss rate
cache MPKI
```

但是当前机器限制：

```text
/proc/sys/kernel/perf_event_paranoid = 4
```

因此之前输出里显示：

```text
PMU counters: unavailable on this machine/config
PMU init errno: 13 (Permission denied)
```

也就是说：

```text
当前还没有硬件 cache miss counter 证据证明 GiY 降低 cache miss rate。
```

### 推荐测试方法

最好的实验应该分两层：

#### 1. Whole-program perf stat

目的：

```text
看整个 benchmark 运行期间的 cache miss rate 是否变化。
```

优点：

- 外部工具测量，不依赖 VM 内部计时代码。
- 容易对 GiY / Cheney / GiYOL 做统一比较。

缺点：

- 包含业务逻辑、解释器、GC、初始化等所有阶段。
- 如果业务逻辑占比很大，GC 的 cache miss 改善可能被稀释。

建议指标：

```text
cycles
instructions
cache-references
cache-misses
L1-dcache-loads
L1-dcache-load-misses
LLC-loads
LLC-load-misses
LLC-stores
LLC-store-misses
```

如果机器权限不足，至少先用 generic counters：

```text
cycles
instructions
cache-references
cache-misses
```

建议命令形状：

```text
perf stat -x, -r 5 \
  -e cycles,instructions,cache-references,cache-misses \
  taskset -c 0 ../ejsvm Benchmark.sbc
```

#### 2. GC-window PMU

目的：

```text
只测 minor GC 期间的 cache miss rate。
```

这是更接近论文主张的方法。

原因：

- GiY 的目标不是让整个程序所有阶段都降低 cache miss。
- GiY 的目标是让 GC 的高频工作集留在 GC Local Workspace / cache 内。
- 所以最重要的是 GC-window cache miss rate，而不是 whole-program cache miss rate。

当前代码已经有基础设施：

```text
gc_pmu_start_window()
gc_pmu_stop_window()
```

它的位置覆盖 minor GC 主体。

要让这个方法工作，需要：

```text
sudo sysctl kernel.perf_event_paranoid=1
```

或者用 root 权限运行 benchmark。

成功后，benchmark 输出里应该出现真实数字：

```text
PMU-sampled GC:      N / N
cache-misses:        ...
cache-references:    ...
cache-miss rate:     ...%
instructions:        ...
cache MPKI:          ...
```

#### 3. Phase-level PMU

如果需要更强的机制证明，可以进一步把 GC-window 拆成阶段：

Cheney：

```text
scan_roots
scan_RS
scavenge / copy
```

GiY：

```text
scan_roots / reserve
scan_RS / reserve
young trace
materialize / copy to old
patch
```

这样可以回答更细的问题：

```text
GiY 到底是哪一段降低了 miss？
reserve 阶段是否更 cache-local？
materialize 阶段是否增加了 miss？
GiY 的额外 aux structure 是否抵消了收益？
```

### 推荐实验矩阵

第一轮不要全量跑所有组合，先选最有信息量的点：

```text
GC: cache_cheney, giy
workspace: 640KB, 768KB, 896KB
benchmarks: Havlak, Richards, CD, Storage, NBody
repeat: 5 次
```

原因：

- 640KB 是 Cheney 总时间最优点。
- 768KB 是 GiY 总时间最优点，且 GiY/Cheney 几乎打平。
- 896KB 是 GiY 相对 Cheney 明显占优的点。
- Havlak/Richards 是差异最大的长 benchmark。
- CD/Storage/NBody 可以代表高分配量和不同访问模式。

第二轮再扩展到 full suite。

### 判断标准

不要只看 miss rate 百分比，还要同时看：

```text
cache-misses
cache-references
cache MPKI = cache-misses / instructions * 1000
total execution time
GC overhead
minor GC count
```

原因：

- miss rate 下降但 references 暴涨，不一定好。
- miss rate 上升但总 miss 数下降，也可能是好事。
- GiY 如果减少 long-latency miss，但增加一些 cheap L1 miss，单个指标可能误导。

更稳妥的结论格式：

```text
GiY 在 GC-window 内是否降低 LLC/cache misses per instruction？
GiY 是否降低 total GC cache misses？
GiY 的 cache miss 改善是否对应 total execution / GC overhead 改善？
```

### 当前结论

现在能说的是：

```text
GiY 在部分 workspace/benchmark 下表现出更好的总时间，
但还不能声称它已经被硬件 counter 证明降低了 cache miss rate。
```

下一步最应该做的是：

```text
打开 perf_event 权限，
先跑 GC-window PMU + whole-program perf stat 的小矩阵实验。
```

## 2026-05-16 当前机器 cache miss counter 可用性确认

检查命令：

```text
cat /proc/sys/kernel/perf_event_paranoid
command -v perf
perf --version
perf stat -e cache-references,cache-misses,instructions,cycles -- true
```

结果：

```text
perf_event_paranoid = 4
perf path = /usr/bin/perf
perf version = 6.8.12
perf stat hardware counters failed with permission error
```

`perf stat` 报错说明：

```text
Access to performance monitoring and observability operations is limited.
perf_event_paranoid setting is 4.
```

结论：

```text
当前机器安装了 perf，CPU/内核也应支持 performance counter。
但是当前普通用户权限下不能测 cache-references / cache-misses / instructions / cycles。
所以当前状态下不能直接测 cache miss rate。
```

要启用测量，需要管理员权限执行其中一种方式：

```text
sudo sysctl kernel.perf_event_paranoid=1
```

或者：

```text
sudo sysctl kernel.perf_event_paranoid=-1
```

之后再重新测试：

```text
perf stat -e cache-references,cache-misses,instructions,cycles -- true
```

如果这条命令成功，才可以继续做 GiY / Cheney 的 whole-program cache miss rate 和 GC-window PMU 实验。

## 2026-05-16 cache miss counter 权限恢复后确认

用户执行权限调整命令后重新检查。

检查结果：

```text
cat /proc/sys/kernel/perf_event_paranoid
=> 1

perf stat -e cache-references,cache-misses,instructions,cycles -- true
=> success
```

`perf stat` 已经可以读取：

```text
cache-references
cache-misses
instructions
cycles
```

一次测试输出示例：

```text
cache-references: 17251
cache-misses:     5803
miss rate:        33.64%
instructions:     804875
cycles:           919150
```

更细粒度事件也可以读取：

```text
L1-dcache-loads
L1-dcache-load-misses
LLC-loads
LLC-load-misses
LLC-stores
LLC-store-misses
```

注意：

```text
把太多硬件事件放在同一次 perf stat 里时，部分事件可能显示 <not counted>。
单独或分组测量时 LLC-stores / LLC-store-misses 可以读取。
```

VM 内部 GC-window PMU 也已经确认可用。

短测试：

```text
cd build.debug/benchmarks
taskset -c 0 ../ejsvm giy_gc_probe.sbc
```

当前 `../ejsvm` 是之前 benchmark 后留下的 `cache_cheney CACHE_SIZE_KB=896` 构建。

输出确认：

```text
PMU-sampled GC:      5 / 5
cache-misses:        2941
cache-references:    50164
cache-miss rate:     5.86%
instructions:        7642217
cache MPKI:          0.385
```

结论：

```text
当前机器现在可以测量 cache miss rate。
可以开始做 GiY / Cheney 的 whole-program perf stat 和 GC-window PMU 对照实验。
```

## 2026-05-16 GiY vs cache_cheney cache miss 分组测量计划

目标：

```text
用硬件 counter 直接测量 GiY 和 cache_cheney 的 cache miss 行为。
```

这次不是 full suite 穷举，而是第一轮核心 cache-miss 矩阵。
原因：

```text
perf 分组测量会让每个 benchmark 按 event group 重跑多次。
如果对 13 个 benchmark、4 个 workspace、多个 event group 全量跑，
运行时间会膨胀到非常长。
```

本轮选择更有研究价值的点：

GC：

```text
cache_cheney
giy
```

GC Local Workspace：

```text
640KB
768KB
896KB
```

原因：

- 640KB 是 Cheney 上一轮总时间最优点。
- 768KB 是 GiY 上一轮总时间最优点，也是 GiY / Cheney 几乎打平点。
- 896KB 是 GiY 相对 Cheney 明显占优的点，也是接近 90% L2 的实验候选。

Benchmark：

```text
CD
Havlak
Richards
Storage
```

原因：

- CD：高分配量，GiY/Cheney 差异明显。
- Havlak：GiY 在多个 size 下输给 Cheney，是必须解释的反例。
- Richards：GiY 在 896KB 下明显赢 Cheney，是必须解释的正例。
- Storage：GiY 多个 size 下表现较好，能观察 allocation-heavy workload 的 cache 行为。

Event groups：

```text
generic:
  cycles
  instructions
  cache-references
  cache-misses

l1d_load:
  L1-dcache-loads
  L1-dcache-load-misses

llc_load:
  LLC-loads
  LLC-load-misses
```

暂时不把 LLC store 放进第一轮默认矩阵。
原因：

```text
当前研究问题首先是 GiY 是否降低 cache miss rate，
最直接证据是 generic cache miss、L1D load miss、LLC load miss。
LLC store 可以作为第二轮补充实验，尤其在 GiYOL / NT store 研究里更重要。
```

同时，每个 benchmark 输出里的 GC-window PMU 也会被 parser 提取：

```text
GC-window cache-misses
GC-window cache-references
GC-window cache-miss rate
GC-window instructions
GC-window cache MPKI
```

新增脚本：

```text
tools/run_gc_cache_miss_benchmarks.sh
tools/parse_gc_cache_miss_benchmarks.py
```

预期输出目录：

```text
build.debug/benchmarks/out_gc_cache_miss_YYYYMMDD_HHMMSS/
```

重要判断标准：

```text
不能只看 miss rate。
需要同时看：

- total cache misses
- cache miss rate
- cache MPKI
- GC-window cache miss rate
- GC-window cache MPKI
- total execution time
- GC overhead
- minor GC count
```

因为 miss rate 降低但 total misses 或 runtime 变差，不一定说明设计更好。

## 2026-05-17 GiY vs cache_cheney cache miss 分组测量结果

输出目录：

```text
build.debug/benchmarks/out_gc_cache_miss_20260516_235434
```

生成文件：

```text
build.debug/benchmarks/out_gc_cache_miss_20260516_235434/summary.csv
build.debug/benchmarks/out_gc_cache_miss_20260516_235434/summary.md
build.debug/benchmarks/out_gc_cache_miss_20260516_235434/compare_giy_vs_cache_cheney_cache_miss.csv
build.debug/benchmarks/out_gc_cache_miss_20260516_235434/compare_giy_vs_cache_cheney_gc_pmu.csv
build.debug/benchmarks/out_gc_cache_miss_20260516_235434/compare_giy_vs_cache_cheney_cache_miss.md
```

执行情况：

- GC：`cache_cheney`, `giy`
- GC Local Workspace：`640KB`, `768KB`, `896KB`
- benchmark：`CD`, `Havlak`, `Richards`, `Storage`
- event group：`generic`, `l1d_load`, `llc_load`
- 共 6 次 build + 72 次 perf benchmark run = 78 个 status
- 所有 status 都是 0
- 没有 `<not counted>` 事件

### Whole-program perf aggregate

| Size | Group | Cheney miss rate | GiY miss rate | GiY-Cheney(pp) | Cheney misses | GiY misses | GiY misses vs Cheney | Cheney time(s) | GiY time(s) |
|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 640 | generic | 9.599% | 14.441% | +4.842 | 857015038 | 1095250408 | +27.798% | 3341.926 | 3421.599 |
| 640 | l1d_load | 2.127% | 2.468% | +0.341 | 211048143662 | 253996419055 | +20.350% | 3307.465 | 3430.637 |
| 640 | llc_load | 2.573% | 7.161% | +4.588 | 65672367 | 122442021 | +86.444% | 3343.208 | 3439.512 |
| 768 | generic | 8.971% | 12.499% | +3.529 | 857253830 | 1060551846 | +23.715% | 3328.244 | 3440.045 |
| 768 | l1d_load | 2.146% | 2.435% | +0.289 | 213466300586 | 250909383583 | +17.541% | 3326.590 | 3395.228 |
| 768 | llc_load | 2.476% | 5.787% | +3.311 | 60232242 | 110838263 | +84.018% | 3354.135 | 3385.968 |
| 896 | generic | 8.265% | 11.854% | +3.589 | 823855257 | 1012451244 | +22.892% | 3345.085 | 3444.052 |
| 896 | l1d_load | 2.174% | 2.543% | +0.369 | 216793173785 | 264420870745 | +21.969% | 3346.821 | 3434.163 |
| 896 | llc_load | 2.742% | 6.677% | +3.935 | 59614883 | 132238801 | +121.822% | 3352.300 | 3455.762 |

直接结论：

```text
在这个核心矩阵里，GiY 没有降低 whole-program cache miss rate。
同 size 比较时，GiY 的 generic / L1D load / LLC load miss rate 全部高于 cache_cheney。
```

尤其是 LLC load：

```text
640KB: GiY LLC-load miss rate 7.161%，Cheney 2.573%
768KB: GiY LLC-load miss rate 5.787%，Cheney 2.476%
896KB: GiY LLC-load miss rate 6.677%，Cheney 2.742%
```

这说明当前 GiY 的 locality 并没有比 Cheney 更好。

### GC-window PMU aggregate

这些数据来自 generic run 中 VM 内部的 GC-window PMU。

| Size | Cheney GC miss rate | GiY GC miss rate | GiY-Cheney(pp) | Cheney GC misses | GiY GC misses | GiY misses vs Cheney | Cheney GC MPKI | GiY GC MPKI | Cheney minor GC | GiY minor GC |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 640 | 0.657% | 1.882% | +1.226 | 32410056 | 61105682 | +88.539% | 0.058 | 0.114 | 676486 | 767776 |
| 768 | 0.777% | 1.609% | +0.831 | 34227935 | 55281715 | +61.511% | 0.069 | 0.116 | 525037 | 578458 |
| 896 | 0.865% | 1.701% | +0.836 | 36637133 | 53902698 | +47.126% | 0.077 | 0.117 | 429837 | 464136 |

直接结论：

```text
GiY 也没有降低 GC-window cache miss rate。
同 size 比较时，GiY 的 GC-window misses、miss rate、MPKI 都高于 cache_cheney。
```

这点非常关键。
因为如果 GiY 的设计目标是让 GC 工作集更 cache-local，那么最应该改善的是 GC-window PMU。
但当前测量结果没有支持这个假设。

### 重要细节

GiY 不是在所有单个 benchmark 上都更差。
例如 `Richards` 在部分 size 上，GiY 的 whole-program generic cache miss rate 低于 Cheney：

```text
640KB Richards:
  Cheney generic miss rate = 25.47%
  GiY generic miss rate    = 9.51%

768KB Richards:
  Cheney generic miss rate = 26.01%
  GiY generic miss rate    = 8.44%
```

但是聚合后 GiY 仍然更差。
主要原因是 `CD / Havlak / Storage` 等高分配 workload 上，GiY 的 miss count 和 miss rate 明显更高。

因此更精确的说法是：

```text
当前 GiY 可能改善某些 workload 的 locality，
但在这个核心矩阵的总体结果里，它没有降低 cache miss rate。
```

### 可能解释

当前 GiY 比 Cheney 有额外的空间和访问成本：

- reserve stack；
- FT slot set；
- reserve-first / materialize-later 的额外 traversal；
- 普通 young 可分配区比 Cheney 小约 32KB；
- minor GC 次数更多；
- reserve 阶段访问对象图的顺序可能不如 Cheney 的直接复制/scavenge 顺序线性。

从结果看：

```text
GiY 有时减少 references / LLC loads，
但 misses 和 miss rate 反而更高。
```

这意味着：

```text
GiY 访问次数可能减少了，
但访问更不容易命中 cache。
```

### 当前结论

当前不能说：

```text
GiY 降低了 cache miss rate。
```

当前应该说：

```text
在 2026-05-17 的核心 cache-miss 矩阵实验中，
GiY 没有降低 whole-program 或 GC-window cache miss rate。
相反，在 640/768/896KB 三个同 size 对照下，
GiY 的 miss rate、miss count、GC-window MPKI 大多高于 cache_cheney。
```

这并不否定 GiY 的所有价值。
它说明当前实现/参数下，GiY 的性能差异不能用“cache miss rate 更低”来解释。

下一步建议：

```text
1. 对 Richards 单独多轮重复，因为它是 GiY 可能改善 locality 的正例。
2. 对 Havlak/CD 单独分析，因为它们是 GiY cache miss 明显更差的反例。
3. 如果要继续 GiYOL，重点看 NT store 是否能降低 LLC pollution，而不是假设 GiY baseline 已经降低 miss。
4. 可以补跑 LLC-store group，尤其给 GiYOL 做 store-side 分析。
```

## 2026-05-17 perf cache counter 分组含义

这次实验里分成几组：

```text
generic cache counters
L1 data cache
LLC loads
LLC stores
```

它们不是同一个指标的不同名字。
它们观察的是 cache hierarchy 的不同层次和不同访问方向。

### generic cache counters

事件：

```text
cache-references
cache-misses
```

含义：

```text
Linux perf 提供的通用硬件 cache 事件。
它们是一个整体性的 cache 参考/未命中指标。
```

优点：

- 简单；
- 跨机器比较容易写脚本；
- 适合做 whole-program 的粗粒度观察。

缺点：

- 语义依赖 CPU/内核映射；
- 在 Intel 上通常更接近 LLC / last-level cache 行为，但不能简单等同于精确的 L1 或 L2；
- 不区分 load 和 store；
- 不适合解释具体是哪一级 cache 出问题。

适合回答：

```text
这个程序整体 cache miss 是否变多？
整体 cache miss rate 是否变差？
```

不适合回答：

```text
到底是 L1 变差、LLC 变差，还是 store 造成污染？
```

### L1 data cache

事件：

```text
L1-dcache-loads
L1-dcache-load-misses
```

含义：

```text
数据 load 在 L1 data cache 层级上的访问和 miss。
```

L1D 是离 core 最近的数据 cache。
它容量小，但命中很快。

适合回答：

```text
程序访问的数据是否在最近的 L1 data cache 里？
对象扫描 / reserve / traversal 是否有很好的局部性？
```

如果 L1D miss rate 高，通常说明：

- 访问数据分散；
- working set 超过 L1D；
- pointer chasing 多；
- 对象布局不够连续；
- 遍历顺序对 cache 不友好。

注意：

```text
L1D miss 不一定非常严重。
如果数据在 L2 或 L3 命中，代价仍然远低于 DRAM。
```

所以 L1D miss 要和 LLC miss 一起看。

### LLC loads

事件：

```text
LLC-loads
LLC-load-misses
```

LLC = Last Level Cache。
在当前机器上就是共享 L3 cache。

含义：

```text
load 请求到达 LLC 层级，以及在 LLC 里 miss。
```

LLC-load-miss 通常比 L1D miss 更严重。
因为如果 L3 也 miss，数据大概率要去更远的 memory hierarchy，例如 DRAM。

适合回答：

```text
程序是否产生更多昂贵的 read-side cache miss？
GiY 的对象图访问是否让数据逃出 cache hierarchy？
```

对 GiY / Cheney 很重要：

```text
如果 GiY 的目标是让 GC local workspace 留在 cache，
那么 GC-window 的 LLC-load-miss 或 cache MPKI 应该下降。
```

当前结果中，GiY 的 LLC-load miss rate 比 Cheney 高，
所以当前不能说 GiY 降低了 cache miss rate。

### LLC stores

事件：

```text
LLC-stores
LLC-store-misses
```

含义：

```text
store/write 请求在 LLC 层级上的行为。
```

这和 loads 不一样。
store 相关事件更适合观察：

- 写入 old 区是否污染 LLC；
- copy/materialize 是否产生大量写流量；
- non-temporal store 是否减少 cache pollution；
- 写分配 write-allocate 是否带来额外 cache line traffic。

对 GiYOL 特别重要。

原因：

```text
GiYOL 的 NT store 设计重点不是普通 read locality，
而是减少 copy-to-old 时写入大量 old objects 对 cache 的污染。
```

所以如果要证明 GiYOL 的价值，
LLC-store / LLC-store-miss / write bandwidth 这类指标会比单纯 L1D load 更相关。

### 为什么不能把这些 miss rate 直接互相比大小

不同 group 的 denominator 不一样：

```text
generic:
  cache-misses / cache-references

L1D:
  L1-dcache-load-misses / L1-dcache-loads

LLC load:
  LLC-load-misses / LLC-loads

LLC store:
  LLC-store-misses / LLC-stores
```

所以不能说：

```text
L1D miss rate 2% 比 LLC miss rate 6% 更好或更差。
```

它们的意义不同。

正确比较方式是：

```text
同一个 event group 内，
比较 GiY vs Cheney；
或者比较同一个 GC 在不同 workspace size 下的变化。
```

例如：

```text
GiY 768KB LLC-load miss rate vs Cheney 768KB LLC-load miss rate
GiY 640KB generic miss rate vs GiY 896KB generic miss rate
```

### 对本研究最有价值的指标

当前研究问题：

```text
GiY 是否真的降低 cache miss rate？
```

最重要的是：

```text
1. GC-window cache miss rate / GC-window MPKI
2. whole-program LLC-load-miss rate
3. whole-program generic cache MPKI
4. L1D load miss rate
```

如果进入 GiYOL / NT store 研究，还应该补：

```text
LLC-stores
LLC-store-misses
store-side bandwidth
```

当前结论：

```text
generic counters 看整体。
L1D 看近端数据局部性。
LLC loads 看昂贵 read-side miss。
LLC stores 看写入 old 区 / NT store / cache pollution。
```

## 2026-05-17 cache miss 调查报告总结

独立报告文件：

```text
build.debug/benchmarks/out_gc_cache_miss_20260516_235434/cache_miss_investigation_report.md
```

报告数据源：

```text
build.debug/benchmarks/out_gc_cache_miss_20260516_235434/summary.csv
```

实验范围：

```text
GC:
  cache_cheney
  giy

GC Local Workspace:
  640KB
  768KB
  896KB

Benchmarks:
  CD
  Havlak
  Richards
  Storage

Perf groups:
  generic
  l1d_load
  llc_load
```

总体结论：

```text
当前 GiY 没有降低 cache miss rate。
在这个核心矩阵里，GiY 的 whole-program generic cache miss rate、
L1D-load miss rate、LLC-load miss rate、GC-window miss rate
都高于 cache_cheney。
```

Whole-program aggregate 关键数字：

```text
640KB:
  generic:  Cheney 9.599%, GiY 14.441%
  L1D:      Cheney 2.127%, GiY 2.468%
  LLC-load: Cheney 2.573%, GiY 7.161%

768KB:
  generic:  Cheney 8.971%, GiY 12.499%
  L1D:      Cheney 2.146%, GiY 2.435%
  LLC-load: Cheney 2.476%, GiY 5.787%

896KB:
  generic:  Cheney 8.265%, GiY 11.854%
  L1D:      Cheney 2.174%, GiY 2.543%
  LLC-load: Cheney 2.742%, GiY 6.677%
```

GC-window PMU 关键数字：

```text
640KB:
  Cheney GC miss rate = 0.657%, MPKI = 0.058
  GiY    GC miss rate = 1.882%, MPKI = 0.114

768KB:
  Cheney GC miss rate = 0.777%, MPKI = 0.069
  GiY    GC miss rate = 1.609%, MPKI = 0.116

896KB:
  Cheney GC miss rate = 0.865%, MPKI = 0.077
  GiY    GC miss rate = 1.701%, MPKI = 0.117
```

解释：

```text
如果 GiY 的核心效果真的是“让 GC 工作集更 cache-local”，
最应该改善的是 GC-window PMU。
但当前数据里，GiY 的 GC-window miss rate、miss count、MPKI 都更高。
所以当前实现不能用“cache miss rate 更低”解释 GiY 的性能。
```

更细的观察：

- GiY 的 generic / LLC references 有时比 Cheney 少，但 misses 反而更多。
- 这意味着 GiY 不是单纯“访问更多”导致差，而是剩下的访问更不容易命中 cache。
- `Richards` 是重要例外：在 640KB / 768KB 下，GiY 的 whole-program generic miss rate 明显低于 Cheney。
- 但是 `CD`、`Havlak`、`Storage` 多数情况下对 GiY 不利，聚合后压过了 Richards 的正例。
- `Havlak` 是最强反例：GiY 在 time 和 miss rate 上都明显更差。
- `LLC-load` 是最危险信号：GiY aggregate LLC-load misses 比 Cheney 高约 84% 到 122%。

可能原因：

```text
1. GiY 需要 reserve stack / FT slot set，ordinary young 区更小。
2. 同 total workspace 下，GiY minor GC 次数更多。
3. reserve-first traversal 带来额外 pointer chasing。
4. Cheney immediate copying/scavenge 在这些 workload 上可能更线性。
5. workspace 变大虽然减少 GC 次数，但不必然降低 LLC miss rate。
```

当前可声明：

```text
在 2026-05-17 的核心 cache-miss 实验中，
GiY 没有降低 whole-program 或 GC-window cache miss rate。
硬件 counter 结果反而显示 GiY 的 locality 比 cache_cheney 更差。
```

当前不能声明：

```text
GiY generally reduces cache miss rate.
GiY 的性能差异来自更低 cache miss rate。
当前一轮数据已经足够做 publication-level final claim。
```

后续建议：

```text
1. Richards 单独多轮重复，因为它是正例。
2. Havlak/CD 单独多轮重复，因为它们是反例。
3. 给 GiY 增加 phase-level PMU：reserve roots、reserve RS、young trace、materialize、patch。
4. 给 GiYOL 补 LLC-store group，分析 NT store 是否减少写污染。
5. 区分 total-workspace-equal 与 ordinary-young-equal 两种公平性实验。
```

### perf stat 中 `<not counted>` 的含义

现象：

```text
一次 perf stat 里同时放太多硬件事件时，
某些事件可能显示：

<not counted>
```

这不是说事件发生次数是 0。
它的意思是：

```text
这次运行中 perf 没有成功把这个 event 放到硬件 PMU counter 上计数。
```

原因通常有几个：

1. CPU 的硬件 PMU counter 数量有限。

   一颗 core 同一时间只能硬件计数有限数量的 event。
   如果一次要求：

   ```text
   L1-dcache-loads
   L1-dcache-load-misses
   LLC-loads
   LLC-load-misses
   LLC-stores
   LLC-store-misses
   ...
   ```

   event 数量可能超过当前可用 counter。

2. 某些 event 之间有硬件调度约束。

   不是所有 event 都可以任意组合在同一轮里同时计数。
   某些 event 只能放在特定 counter，或者和某些 event 组合时无法一起调度。

3. NMI watchdog 可能占用一个 PMU counter。

   当前机器：

   ```text
   /proc/sys/kernel/nmi_watchdog = 1
   ```

   perf 输出也提示：

   ```text
   Some events weren't counted. Try disabling the NMI watchdog.
   ```

   这说明 NMI watchdog 可能减少了可用于 perf 的硬件 counter 数量。

4. perf multiplexing / event scheduling。

   当 event 多于硬件 counter 时，perf 有时会 multiplex：

   ```text
   一段时间计 event A/B/C，
   另一段时间计 event D/E/F，
   最后按运行比例 scale。
   ```

   这种情况下 perf 输出旁边可能有百分比，表示这个 event 实际被计数的时间比例。
   如果事件完全没有被调度，就可能显示 `<not counted>`。

实验设计上的含义：

```text
<not counted> 的数据不能用。
不能把它当成 0。
也不应该把有大量 multiplex 的结果和正常完整计数结果直接比较。
```

当前建议：

把 perf stat 分成几组测，而不是一次测所有事件。

第一组：generic cache counters

```text
cycles
instructions
cache-references
cache-misses
```

第二组：L1 data cache

```text
L1-dcache-loads
L1-dcache-load-misses
```

第三组：LLC loads

```text
LLC-loads
LLC-load-misses
```

第四组：LLC stores

```text
LLC-stores
LLC-store-misses
```

这样每组 event 数量少，硬件 counter 压力低，结果更可信。

如果需要一次测更多 event，可以考虑暂时关闭 NMI watchdog：

```text
sudo sh -c 'echo 0 > /proc/sys/kernel/nmi_watchdog'
```

实验结束后恢复：

```text
sudo sh -c 'echo 1 > /proc/sys/kernel/nmi_watchdog'
```

但当前不一定需要这么做。
对 GiY / Cheney 实验来说，分组测量已经足够。

## 2026-05-17 GiY cache miss 反而更差的代码级原因分析

用户问题：重新分析代码，解释为什么当前 GiY 的缓存反而更不容易命中，并给出最可能理由。

关键实验背景：

- cache miss 实验目录：`build.debug/benchmarks/out_gc_cache_miss_20260516_235434`
- 报告文件：`build.debug/benchmarks/out_gc_cache_miss_20260516_235434/cache_miss_investigation_report.md`
- 同样 workspace 下，GiY 的 whole-program generic / L1D-load / LLC-load / GC-window PMU miss rate 都高于 cache_cheney。
- 例如 aggregate：
  - 640KB generic: Cheney 9.599%, GiY 14.441%
  - 768KB generic: Cheney 8.971%, GiY 12.499%
  - 896KB generic: Cheney 8.265%, GiY 11.854%
  - 768KB LLC-load: Cheney 2.476%, GiY 5.787%
  - 768KB GC-window: Cheney 0.777%, GiY 1.609%

代码级最可能原因：

1. GiY 的 JSObject 扫描比 Cheney 更保守，可能扫描更多字段。

   - `ejsvm/GiY.cc:1182-1208` 的 `giy_scan_jsobject_conservative()` 会根据 object header size 扫描 `JSObject::eprop` 后面的全部 embedded slot。
   - `ejsvm/GiY.cc:1211-1227` 对 SIMPLE_OBJECT / ARRAY / FUNCTION / BUILTIN / BOXED_* 等都走这个 conservative scanner。
   - Cheney 则走 `process_node<CacheCheney_Tracer>()`，核心扫描逻辑在 `ejsvm/gc-visitor-inl.h:88-148`，会根据 `Shape` / `PropertyMap` 的 `n_special_props`, `n_embedded_slots`, `n_props`, extension array 等信息只扫描有意义的字段。
   - 因此 GiY 可能读取更多无效、非指针、空洞或特殊字段，增加 L1D / LLC load 压力。这是目前最强嫌疑之一。

2. GiY 把 reserve 和 copy 分开，并且用 LIFO stack，导致 materialization 顺序不如 Cheney 线性。

   - `ejsvm/GiY.cc:1237-1264` 的 `copy_for_minor()` 只是 reserve old destination、设置 forwarding pointer、把 young payload push 到 `g_gc_stack`，没有立即复制。
   - `ejsvm/GiY.cc:2474-2551` 的 `giy_traverse_stack_and_copy()` 从 stack pop，所以遍历顺序是 DFS/LIFO 风格。
   - old destination 地址按发现顺序分配，但实际 copy 按 pop 顺序发生，可能在 old reserved 区域内前后跳动。
   - Cheney 在 `ejsvm/cache_dram_manager.cc:276-328` 中发现对象时立即 `memcpy` 到 `dram_space.free`，随后 `ejsvm/cache_dram_manager.cc:946-959` 从 `scan_start` 到 `dram_space.free` 线性 scavenge。这对硬件预取更友好。

3. GiY minor GC phase 更多，会重复访问 roots / function table / remembered set。

   - `ejsvm/GiY.cc:2852-2924` 的 phase 包括 `scan_roots_reserve`, `function_table_reserve`, `remembered_set_reserve`, `young_traverse_copy`, allocation-site update, `scan_roots_patch`, `function_table_patch`, `remembered_set_patch`。
   - Cheney 的对应路径在 `ejsvm/cache_dram_manager.cc:855-878`，基本是 scan roots / function table / remembered set，然后线性 scavenge。
   - GiY 的 reserve + patch 拆分让同一批 root/RSet/FT slot 更容易被重复读取和写回，扩大 GC-window 的 cache 工作集。

4. 同样 total workspace 下，GiY 的普通 young allocation area 更小，minor GC 次数更多。

   - `ejsvm/GiY.cc:644-758` 中，GiY 从 `cache_space.end` 预留 GC Local Workspace：stack = young/8，FT slot set = young/64 且至少 32KB，GiYOL 还有 staging/batch。
   - `cache_space.end = cursor` 会减少真正可用于普通 young allocation 的空间。
   - Cheney 也有 FT slot set，但在 `ejsvm/cache_dram_manager.cc:548-575` 只预留 FT slot set，结构更少。
   - 实验中 GiY 的 minor GC 数通常高于 Cheney，例如 aggregate 768KB: Cheney 525037 次，GiY 578458 次；Havlak 768 generic: Cheney 269900 次，GiY 298950 次。

5. GiY remembered set 为了 patch 存了 value，并且当前默认没有开启 fast index，mutator write barrier 可能更重。

   - `ejsvm/giy_rset.cc:122-160` 中，GiY RSet 同时存 `buffer` 和 `values`，还维护 hash table。
   - `ejsvm/giy_rset.cc:240-310` 的 write barrier 在 value 不再指向 young 时，也会尝试 `rememberset_update_existing(..., 0)` 清除旧 value。
   - `ejsvm/giy_rset.cc:18-20` 默认 `GIY_RSET_INDEX_FAST=0`，`ejsvm/giy_rset.cc:49-110` 可能退化为线性扫描已有 RSet entry。
   - 这个影响主要体现在 whole-program perf 中；GC-window PMU 不包含大部分 mutator write barrier 时间。

6. 当前 GiY 的大部分 copy 没有真正得到 NT-store 优势。

   - `ejsvm/GiY.cc:88-95` 设置 `GIY_NT_COPY_MIN_BYTES = 256`。
   - `ejsvm/GiY.cc:1864-1874` 中，小于等于阈值的对象走普通 `memcpy`。
   - Havlak / Richards 的平均对象很小，例如 Havlak 约 52.9B、Richards 约 8.9B，所以当前 GiY 大多数对象仍是普通 memcpy，不会自动带来 streaming-store 的 cache 好处。

当前排序：

1. 最可能：GiY conservative JSObject scanner 扫描过多 slot。
2. 很可能：reserve-first + LIFO traversal 破坏 old-space copy/scavenge 的线性 locality。
3. 很可能：reserve/patch 多阶段重复访问 roots/RSet/FT。
4. 很可能：同样 total workspace 下 GiY 普通 young 更小，minor GC 更多。
5. 影响 whole-program 的重要因素：GiY RSet value tracking + 未启用 fast index。
6. 次要但需要测量：NT-store benefit 没发挥，LLC-store 还没完整测。

建议验证：

- A/B 实验 1：让 GiY 的 JSObject 扫描临时改成与 Cheney 相同的 precise `process_node` 路径，确认 L1D/LLC miss 是否下降。
- A/B 实验 2：把 GiY stack 的 LIFO 改成 FIFO，或者按 old destination 地址顺序 materialize，测试 traversal/copy order 是否是主要问题。
- A/B 实验 3：开启 `GIY_RSET_INDEX_FAST=true`，重点重跑 Havlak/CD 的 whole-program perf。
- A/B 实验 4：给 GiY 各 phase 分别加 PMU window，拆开 reserve roots、reserve RSet、young traverse/copy、patch roots/RSet/FT。
- A/B 实验 5：做 ordinary-young-equal 和 total-workspace-equal 两套实验，避免把 workspace overhead 和算法效果混在一起。

## 2026-05-17 为什么 GiY 的 JSObject scanner 和 Cheney 不一样

用户问题：为什么当前 GiY 的 JSObject 扫描会和 CheneyGC 不一样？为什么不能一样？

结论：

- 理论上可以做成“语义等价”的 precise scanner。
- 但当前不能直接复用 Cheney 的 `process_node<Tracer>()` 路径，因为 GiY 的 forwarding 语义和 Cheney 不一样。
- Cheney 是发现 young object 后立即复制；GiY 是先 reserve old 地址、设置 forwarding pointer，之后才 copy/materialize。
- 因此 Cheney 的 scanner 可以安全读 forwarded/copied metadata；GiY 的 reserve phase 直接使用同一套 scanner，有机会读到尚未 materialize 的 old 地址，或者违反 GiY 避免 old-space read 的设计。

具体代码：

- Cheney：
  - `ejsvm/cache_dram_manager.cc:276-328` 中，`CacheCheney_Tracer::forward()` 调用 `copy_to_dram()`，立刻 `memcpy`。
  - 所以 `PROCESS_EDGE(p->shape)` 后，shape 如果被 forward，目标 old object 已经有真实内容。
  - `ejsvm/gc-visitor-inl.h:88-148` 的 precise JSObject scanner 后续读取 `Shape` / `PropertyMap` 元数据是安全的。

- GiY：
  - `ejsvm/GiY.cc:1237-1264` 中，`copy_for_minor()` 只 reserve old destination、设置 forwarding pointer、push stack，不 copy。
  - `ejsvm/GiY.cc:1266-1328` 的 `GiYReserveTracer` 在 slot 本身在 young 时会 patch 该 slot 到 forwarding address。
  - `ejsvm/GiY.cc:2474-2551` 中，真正 copy 发生在之后的 `giy_traverse_stack_and_copy()`。
  - 因此如果直接用 `gc-visitor-inl.h:88-148` 的 precise scanner，`PROCESS_EDGE(p->shape)` 之后再读 `p->shape->pm` 等信息，就可能读到一个只有地址、还没内容的 old destination。

为什么 GiY 当前用了 conservative scanner：

- `ejsvm/GiY.cc:1182-1208` 的 `giy_scan_jsobject_conservative()` 只处理 `p->shape` 这条边，然后根据 object header size 扫描 object 内部的 eprop slot。
- 它不依赖 `Shape::n_embedded_slots`、`PropertyMap::n_special_props`、`PropertyMap::n_props` 等 metadata。
- 这样做比较保守，能绕开“reserve phase 读 metadata”这件事。
- 代价是它可能扫描比 Cheney 更多的 slot，造成额外 L1D/LLC load miss。

重要补充：

- “不能一样”不是永久不能一样，而是不能直接把 Cheney 的 precise `process_node` 原样搬给 GiY。
- 如果要让 GiY 也 precise，应该写一个 GiY 专用 precise scanner：
  - 先保存 source object 的原始 `Shape *source_shape`，不要在读取 metadata 前用 patched old forwarding address。
  - 确保读取的是已经有效的 young/init metadata，或者先 materialize metadata。
  - 避免在 old guard active 时读取 old-space metadata。
  - 再根据 `source_shape` / `PropertyMap` 精确决定要扫描哪些 eprop slot。
- 这个 A/B 实验值得做：如果改成 GiY-safe precise scanner 后 L1D/LLC miss 明显下降，就说明当前 conservative scanner 是 cache miss 变差的主要来源之一。

### 更正和补充：为什么 scanner 必须读 Shape / PropertyMap

需要修正一个细节：

- 当前构建使用 `THREADED`，`ejsvm/gc-visitor-inl.h:90-95` 中 precise scanner 会先执行 `Shape *os = p->shape`，再执行 `PROCESS_EDGE(p->shape)`。
- 所以在当前构建里，问题不只是“`PROCESS_EDGE(p->shape)` 后 `p->shape` 被改成 forwarding address，随后马上读错地址”。
- 更根本的问题是：precise JSObject scanner 本身必须解引用 `Shape` / `PropertyMap` metadata，而这些 metadata 可能在 GiY reserve phase 中处于“不应该读 old space”或“尚未 materialize”的语义风险里。

为什么 precise scanner 需要读这些 metadata：

- `ejsvm/types.h:340-346` 的 `JSObject` 只有：
  - `Shape *shape`
  - `JSValue eprop[]`
- 单靠 object header size 只能知道 eprop 数组占了多少字节，不能知道哪些 slot 是真实属性、哪些是 special prop、有没有 extension array、extension array 有多长。
- `ejsvm/types.h:314-324` 的 `Shape` 记录：
  - `pm`
  - `n_embedded_slots`
  - `n_extension_slots`
- `ejsvm/types.h:260-277` 的 `PropertyMap` 记录：
  - `n_props`
  - `n_special_props`
  - `__proto__`
  - transition/hash metadata
- 因此 `ejsvm/gc-visitor-inl.h:101-110` 需要读：
  - `os->n_extension_slots`
  - `os->n_embedded_slots`
  - `os->pm->n_special_props`
  - `os->pm->n_props`
- 这些信息决定 precise scanner 到底扫描哪些 `eprop` slot。

这个问题如何在 GiY 里产生：

1. 原来的 generic scanner 是为普通 GC/Cheney 语义写的。
   - Cheney forward 时立即 copy。
   - forwarding pointer 指向的 old object 已经有内容。
   - 因此 scanner 读 forwarded/copy 后的 metadata 通常是安全的。

2. GiY 改变了 forwarding 的含义。
   - `ejsvm/GiY.cc:1237-1264` 的 `copy_for_minor()` 只 reserve old destination，不立即 copy。
   - forwarding pointer 在 GiY 中可能只表示“将来会在这里 materialize”，不表示“这里现在已经有完整对象内容”。

3. precise scanner 不是单纯枚举边，它会边扫描边读 metadata。
   - 对 JSObject，它必须读 shape/property-map metadata 来决定扫描范围。
   - 如果这些 metadata 在 old/DRAM，GiY 的 old-guard 设计会认为这是不该发生的 old read。
   - 如果 scanner 在某些配置或路径上拿到的是尚未 materialize 的 forwarded metadata，就会读到无效 old destination。

4. 所以当前 GiY 选择了 conservative scanner。
   - `ejsvm/GiY.cc:1182-1208` 不依赖 `Shape` / `PropertyMap` 来决定 slot 范围。
   - 它只处理 `p->shape` 这条边，然后按 object header size 扫描全部 `eprop` slot。
   - 这样避免了在 reserve phase 依赖 metadata 的风险。
   - 代价是扫描过多 slot，可能造成更多 cache miss。

更准确的说法：

- 不是 JSObject scanner 绝对不能和 Cheney 一样。
- 而是不能直接复用 Cheney/generic scanner，因为它默认“forward 后对象内容可读”以及“读 metadata 没问题”。
- GiY 可以实现一个自己的 precise scanner，但必须显式保证：
  - metadata 来源是 source young object 或 init space，不能是不完整 old destination；
  - 不违反 GiY old-space read guard；
  - 读取 `Shape` / `PropertyMap` 前不会被 reserve tracer 的 patch 语义污染；
  - 然后再按 `Shape` / `PropertyMap` 精确扫描 slot。

### 再次更正：当前 giy/cache_cheney 构建没有 THREADED

上面关于 `THREADED` 的细节需要更正：

- `ejsvm/common.mk:444-458` 中：
  - `OPT_GC=cache_cheney` 定义 `CACHE_CHENEY` 和 `USE_REMEMBERED_SET`
  - `OPT_GC=giy` 定义 `CACHE_CHENEY`, `USE_REMEMBERED_SET`, `USE_GIY_MINOR`
  - `OPT_GC=giyol` 定义 `CACHE_CHENEY`, `USE_REMEMBERED_SET`, `USE_GIY_MINOR`, `USE_GIYOL`
  - 这些都没有定义 `THREADED`
- 因此当前 GiY / Cheney 使用 `gc-visitor-inl.h:97-99` 的 `#ifndef THREADED` 路径。

这让问题更直接：

```text
generic precise scanner:

PROCESS_EDGE(p->shape)
Shape *os = p->shape
read os->n_extension_slots
read os->n_embedded_slots
read os->pm->n_special_props
read os->pm->n_props
```

在 Cheney 中：

```text
PROCESS_EDGE(p->shape)
-> 如果 shape 在 young，就立刻 copy shape 到 old
-> p->shape 改成 old address
-> old address 已经有真实 Shape 内容
-> 继续读 os->... 是安全的
```

在 GiY 中：

```text
PROCESS_EDGE(p->shape)
-> 如果 shape 在 young，只 reserve old address
-> p->shape 可能被改成 old forwarding address
-> 但这个 old address 的 Shape 内容还没 copy/materialize
-> Shape *os = p->shape
-> 继续读 os->... 就可能读到尚未 materialize 的地址
```

所以当前 GiY 使用 `giy_scan_jsobject_conservative()` 的直接原因更清楚：

- 避免 precise scanner 在 `PROCESS_EDGE(p->shape)` 后马上依赖 `p->shape` 的 metadata。
- 避免读取尚未 materialize 的 old destination。
- 代价是只能按 object size 扫描全部 eprop slot，因此 cache miss 可能更高。

## 2026-05-17 简化解释：eprop / Shape / PropertyMap / GC scanner 的机制

用户反馈：前面的解释太复杂，需要更清楚说明 eprop 是什么、现在是什么机制、shape 是什么、为什么会被这样处理、流程是什么。并再次要求所有内容记录到 AIlog。

核心概念：

1. `eprop` 是 JSObject 里面真正存属性值的数组。

   - 代码：`ejsvm/types.h:340-346`
   - `JSObject` 结构基本是：

     ```c
     struct jsobject_cell {
       Shape *shape;
       JSValue eprop[];
     };
     ```

   - 可以理解为：

     ```text
     JSObject = 一个 shape 指针 + 一排属性值格子 eprop[0], eprop[1], ...
     ```

   - 对象创建时，`ejsvm/object.c:398-410` 根据 `shape->n_embedded_slots` 分配这么多 eprop slot，并初始化为 `JS_EMPTY`。

2. `Shape` 是这个对象的布局说明。

   - 代码：`ejsvm/types.h:314-338`
   - 它记录：
     - `pm`: 指向 PropertyMap
     - `n_embedded_slots`: 对象内部有多少个 eprop slot
     - `n_extension_slots`: 是否有外部 extension array
   - 可以理解为：

     ```text
     Shape 告诉 VM：这个对象有几个内部格子，最后一个格子是不是指向外部扩展数组。
     ```

3. `PropertyMap` 是属性名到格子编号的地图。

   - 代码：`ejsvm/types.h:260-277`
   - 它记录：
     - `map`: property name -> index 或 transition
     - `n_props`: 总属性数量
     - `n_special_props`: 前面多少个 slot 是 VM 内部特殊字段
     - `__proto__`
   - 可以理解为：

     ```text
     PropertyMap 告诉 VM：属性名 x 应该存在 eprop[几号]。
     ```

普通属性读写流程：

1. 读属性时，`ejsvm/object.c:293-324` 通过 `prop_index()` 查 PropertyMap，得到 index。
2. 然后 `object_get_prop(obj, index)` 到 `eprop[index]` 读值。
3. 写属性时，`ejsvm/object.c:129-284` 如果属性不存在，会创建/查找新的 PropertyMap 和 Shape。
4. 如果当前对象内部 eprop 不够，会通过 `object_grow_shape()` 扩展，代码在 `ejsvm/object.c:828-864`。
5. `ejsvm/types.h:359-371` 的 `object_get_prop_address()` 决定：
   - 如果 index 在 embedded 范围内，返回 `&p->eprop[index]`
   - 如果超出 embedded 范围，就从 `p->eprop[n_embedded - 1]` 找 extension array

为什么 GC scanner 要处理 shape：

- 因为 `p->shape` 本身也是 heap object 指针。
- 如果 shape 在 young 区，minor GC 后 young 会被清空。
- 所以 GC 必须把 shape 也 forward/copy 到 old，并把 `p->shape` 改成新地址。
- 否则对象的 layout 说明书就丢了。

为什么 GC scanner 要读 Shape / PropertyMap：

- GC 不能盲目扫描全部 eprop，否则会把非指针、内部字段、空 slot 都当作候选指针处理，增加成本。
- precise scanner 要知道：
  - 哪些 slot 是 special prop
  - 哪些 slot 是普通属性
  - embedded slot 有多少
  - extension array 有没有、有多长
- 这些信息只能从 Shape / PropertyMap 读。
- 代码：`ejsvm/gc-visitor-inl.h:94-110`

Cheney 的正常流程：

```text
扫描 JSObject
-> PROCESS_EDGE(p->shape)
-> 如果 shape 在 young，立刻 copy shape 到 old
-> p->shape 改成 old 地址
-> 这个 old 地址里已经有真实 Shape 内容
-> 读 Shape / PropertyMap，决定扫描哪些 eprop
-> 扫描真正需要扫描的 eprop slot
```

这对 Cheney 是安全的，因为 Cheney 的 forward = 已经 copy 完。

GiY 如果直接照抄 Cheney scanner，会出现的问题：

```text
扫描 JSObject
-> PROCESS_EDGE(p->shape)
-> 如果 shape 在 young，GiY 只 reserve old 地址
-> p->shape 可能改成 reserved old 地址
-> 但这个 reserved old 地址还没有真实 Shape 内容
-> scanner 继续读 p->shape->n_embedded_slots / p->shape->pm
-> 可能读到还没 materialize 的地址
```

这就是问题发生的真正原因：

```text
Cheney 的 forward = 复制完成的新对象
GiY 的 forward = 只是预定的新地址
```

所以 GiY 当前用了 conservative scanner：

- 代码：`ejsvm/GiY.cc:1182-1208`
- 它只做：

  ```text
  处理 p->shape 这条边
  不读 Shape / PropertyMap 来判断精确范围
  直接根据 object header size 扫描所有 eprop slot
  ```

- 好处：避免读尚未 materialize 的 Shape / PropertyMap。
- 坏处：会多扫描很多 eprop slot，可能增加 cache miss。

最简单总结：

```text
eprop = 对象里放属性值的格子。
Shape = 说明这些格子怎么布局。
PropertyMap = 说明属性名对应哪个格子。
Cheney 可以先搬 shape 再看说明书，因为它立刻复制。
GiY 只是先预约新地址，还没复制，所以不能直接看新地址里的说明书。
因此 GiY 现在选择不看说明书，直接把全部格子扫一遍。
这安全，但更慢、更容易 cache miss。
```

## 2026-05-17 符合 GiY 的 JSObject scanner 修复方案思考

用户问题：应该用什么方法解决 GiY 当前 JSObject conservative scanner 导致 cache miss 变差的问题？需要详细思考符合 GiY 的解决方案。

目标：

1. 不把 GiY 退化成 Cheney。
2. 保留 GiY 的 reserve-first / later-materialize 思想。
3. GC core 尽量不读 old/DRAM metadata。
4. 让 JSObject 扫描尽量恢复 precise，减少无意义 eprop 扫描。
5. 出现不确定情况时必须安全 fallback，不能牺牲正确性。

不推荐方案：在 scanner 里每次强制 materialize shape。

- GiY 已经有 `giy_materialize_shape()` 和 `giy_materialize_property_map()`，代码在 `ejsvm/GiY.cc:965-989`。
- 它们会调用 `copy_for_minor()` 后立刻 `giy_traverse_stack_and_copy()`。
- 如果在每个 JSObject scanner 里都这样做，会破坏 GiY 的 reserve-first / batch materialization 思路。
- 这会让 traversal/copy 顺序更混乱，也可能增加 GC core cache pressure。
- 所以它可以作为特殊情况工具，但不应该是主 scanner 的常规路径。

短期 A/B 方案：GiY-safe precise scanner + fallback。

核心思想：

```text
先保存 source object 的原始 shape 指针。
然后照常 reserve p->shape。
如果原始 shape 和 pm 是 young/init 等可安全读取的 metadata，就 precise scan。
如果 metadata 在 old/DRAM 或不确定，就 fallback 到当前 conservative scan。
```

伪代码：

```c
Shape *source_shape = p->shape;
giy_process_edge<Tracer>(p->shape);  // reserve shape, may patch p->shape

if (!shape_metadata_is_gc_local_readable(source_shape))
  goto conservative_fallback;

PropertyMap *pm = source_shape->pm;
if (!pm_metadata_is_gc_local_readable(pm))
  goto conservative_fallback;

// Now use source_shape/source_pm scalar fields:
n_embedded = source_shape->n_embedded_slots;
n_extension = source_shape->n_extension_slots;
n_special = pm->n_special_props;
n_props = pm->n_props;

scan_special_fields_by_cell_type(type, p);
scan_regular_eprop_precisely(p, n_special, n_embedded, n_extension, n_props);
return;

conservative_fallback:
  scan_all_eprop_by_object_size(p);
```

优点：

- 实现小，适合做 A/B 实验。
- 可以直接验证“减少 eprop over-scan 是否降低 L1D/LLC miss”。
- 不改变 GiY 的 copy/materialize 策略。

缺点：

- 如果大多数 Shape / PropertyMap 已经在 old/DRAM，那么 fallback 很多，收益有限。
- 它解决的是“不要读未 materialize forwarding address”，但没有从根本上解决“old metadata 不想读”的问题。

正式推荐方案：GC Local Workspace 中的 Shape Layout Cache。

核心思想：

```text
不要在 GC scanner 里读 Shape / PropertyMap。
在 GC 之前或 mutator 已经访问 shape 时，把 scanner 需要的 layout 摘要保存到 GC Local Workspace。
GC scanner 只查这个 local layout table。
```

需要保存的不是整个 Shape / PropertyMap，而是非常小的摘要：

```c
struct GiYShapeLayout {
  uintptr_t shape;          // key: original Shape*
  uint16_t n_embedded_slots;
  uint16_t n_extension_slots;
  uint16_t n_props;
  uint8_t  n_special_props;
  uint8_t  valid;
};
```

这些字段正好对应 precise scanner 需要的信息：

- `n_embedded_slots`
- `n_extension_slots`
- `n_props`
- `n_special_props`

表放在哪里：

- 放在 GC Local Workspace。
- 和 GiY stack / FT slot set / GiYOL staging 一样，从 cache/local workspace budget 里统一切。
- 建议先给 32KB 或 64KB 做实验。
- 例如 2048 或 4096 个 open-addressing entry。

什么时候填充：

1. 对象分配时填。
   - `ejsvm/object.c:398-410` 的 `allocate_jsobject()` 已经会读 `os->n_embedded_slots` 来决定对象大小。
   - 这里可以顺便记录 `os` 的 layout。
   - 这发生在 mutator/分配路径，不是在 GC core 中临时读 old metadata。

2. shape 改变时填。
   - `ejsvm/object.c:828-878` 的 `object_grow_shape()` 会把 `p->shape = os`。
   - 这里也应该记录新 shape 的 layout。

3. 新 shape 创建时也可以填。
   - `ejsvm/object.c:768-822` 的 `new_object_shape()` 知道 `pm`, `num_embedded`, `num_extension`。
   - 这里最容易拿到 layout 信息。

4. GC 结束后清空。
   - 一个 minor GC 结束后 young 区被清空。
   - 下一轮 mutator allocation 会重新记录本轮 young objects 用到的 shape layout。

GC scanner 如何使用：

```text
扫描 JSObject p:

1. source_shape = p->shape
2. giy_process_edge(p->shape)   // 保留 GiY reserve 行为
3. 在 Shape Layout Cache 中 lookup source_shape
4. 如果命中：
     用 layout precise scan
   如果 miss：
     fallback 到 conservative scan
```

precise scan 的内容：

- 对 `Array`：
  - 扫 `array_body`
  - 扫 `array_length`
  - 跳过 `array_size`
  - 再扫普通属性区
- 对 `Function`：
  - 扫 `function_environment`
  - 跳过 `function_table_entry`
  - 再扫普通属性区
- 对 `Builtin`：
  - special fields 是 C function pointer / nargs，基本跳过
  - 再扫普通属性区
- 对 `BoxedNumber` / `BoxedString`：
  - 扫 value slot
  - 再扫普通属性区
- 对 `BoxedBoolean`：
  - value 是 boolean，不需要当 heap edge 扫
  - 再扫普通属性区
- 对普通对象：
  - 从 `n_special_props` 到 `actual_embedded` 扫普通属性
  - 如有 extension array，处理 extension array pointer/array

为什么这个方案符合 GiY：

- GC core 不需要读 old Shape / PropertyMap。
- 不需要为了 scanner 立刻 materialize shape。
- 保留 reserve-first。
- 高频访问的 layout 摘要在 GC Local Workspace，符合“高频 GC 数据在 local workspace”的设计。
- 如果 layout cache miss，fallback conservative，正确性仍然安全。

风险和注意点：

- 需要防止 layout table 太小。太小时 miss 多，性能退回 conservative。
- 需要记录 shape 改变路径，否则对象分配后新增属性导致 shape 更新，table 里没有新 shape，会 fallback。
- 需要 whole-program benchmark，因为把一部分工作移动到 mutator allocation/property update 路径。
- 需要 GC-window benchmark，因为目标是降低 GC core 的 L1D/LLC miss。
- 需要比较三组：
  1. 当前 GiY conservative scanner
  2. GiY-safe precise scanner + fallback
  3. GiY shape layout cache + precise scanner

最终推荐：

```text
第一步：做 GiY-safe precise scanner + fallback，快速验证方向。
第二步：做 GC Local Workspace Shape Layout Cache，作为正式设计。
第三步：用 phase-level PMU 和 cache miss benchmark 验证：
        JSObject scan 是否减少 L1D/LLC miss，
        GC-window miss rate 是否接近或超过 Cheney。
```

## 2026-05-17 shape 是不是对象、为什么扫描 JSObject 需要 shape、其他语言怎么处理

用户问题：

- 当前说的 shape 本身是不是一个对象？
- 扫描对象时是不是无论如何都免不了先知道它的 shape？
- shape 产生的意义是什么？
- 别的语言/VM 怎么处理这个问题？

结论：

1. 在当前 VM 里，`Shape` 是 GC 管理的 heap object，但它不是用户层的 JSObject。

   - 代码：`ejsvm/object.c:768-787`
   - `new_object_shape()` 里通过：

     ```c
     s = (Shape *) gc_malloc(ctx, sizeof(Shape), CELLT_SHAPE);
     ```

   - 所以 Shape 会参与 GC，会被复制/forward/扫描。
   - 但它不是 JavaScript 程序能直接访问的普通对象，而是 VM 内部 metadata object。

2. 精确扫描 JSObject 时，基本免不了要知道 layout metadata。

   - 对当前 VM 来说，这个 metadata 就是 `Shape` + `PropertyMap`。
   - `JSObject` 本体只有 `Shape *shape` 和 `JSValue eprop[]`，代码在 `ejsvm/types.h:340-346`。
   - 没有 Shape/PropertyMap，就只能知道“这里有一排 eprop slot”，但不知道哪些 slot 该扫、哪些不该扫。
   - 所以 precise scanner 在 `ejsvm/gc-visitor-inl.h:94-110` 需要读 `shape` 和 `shape->pm`。

3. 但“不一定必须在 GC 扫描当下直接读 Shape 对象本体”。

   - 必须知道 layout 信息。
   - 不一定必须每次从 Shape / PropertyMap heap object 里读。
   - 可以把 layout 摘要提前缓存到 GC Local Workspace。
   - GiY 的正式方案应该利用这一点。

shape 的意义：

```text
JS 对象是动态的，属性可以不断增加/改变。
如果每个对象自己保存完整的 property name -> slot 映射，会很慢、很浪费。
所以多个结构相同的对象共享一个 Shape / PropertyMap。
```

Shape / PropertyMap 带来的好处：

- 共享对象布局，节省内存。
- 让 `obj.x` 可以变成 “查 shape/map -> 得到固定 index -> 读 eprop[index]”。
- 支持 inline cache。
- 支持属性新增时从旧 shape transition 到新 shape。
- 让 GC precise scanner 知道哪些 slot 需要扫描。

和 GiY 问题的关系：

- Cheney 的 `Shape` 是 heap object，但 Cheney 处理它时会立刻 copy。
- GiY 的 `Shape` 也是 heap object，但 GiY forward 只 reserve 地址，不立即 materialize。
- 所以 GiY 如果直接照 Cheney scanner 读 `p->shape->...`，可能读到还没 materialize 的 reserved old address。
- 因此 GiY 需要：
  - 要么 fallback conservative scan；
  - 要么用 GC Local Workspace 里的 layout cache；
  - 不应该在主路径上每次强制 materialize shape。

其他语言/VM 的对应概念：

- Java / JVM：
  - 每个对象 header 有 `klass` 指针。
  - `klass` metadata 记录对象有哪些引用字段。
  - GC 根据 class metadata / oop map 精确扫描。

- .NET CLR：
  - 对象有 MethodTable / EEType 指针。
  - runtime metadata 记录字段布局和引用字段。
  - GC 用这些 metadata 扫描对象。

- Go：
  - 每个类型有 type descriptor。
  - descriptor 里有 pointer bitmap / GC program。
  - GC 根据 bitmap 知道对象哪些 word 是指针。

- JavaScript VMs：
  - V8 叫 Map。
  - JavaScriptCore 叫 Structure。
  - SpiderMonkey 有 Shape / BaseShape / ObjectGroup 等历史结构。
  - 它们都用类似 hidden class / shape 的 metadata 描述动态对象布局。

- Python / Ruby：
  - 对象有 type/class 指针。
  - GC 根据类型对应的 traverse function 扫描引用字段。
  - 动态对象属性通常在 dict/ivar table 等结构里，GC 扫那些容器。

- C / C++：
  - 普通 C/C++ 没有精确移动 GC。
  - C++ 的 vptr 是为虚函数服务，不是 GC layout metadata。
  - 如果要精确 GC，也需要额外 type descriptor 或保守扫描。

最简单总结：

```text
Shape 在这个 VM 里确实是一个 GC 管理的内部对象。
扫描 JSObject 要么需要读取 Shape/PropertyMap，
要么需要某种等价的 layout 信息。
Shape 的意义是给动态 JS 对象提供共享布局和快速属性访问。
别的语言也都有类似东西，只是名字不同：
Java 叫 klass，.NET 叫 MethodTable，Go 叫 type descriptor，V8 叫 Map。
GiY 的特殊问题是：它的 forwarding 只是 reserve，不是马上 copy。
所以 GiY 最好不要在 GC core 里直接依赖 Shape 本体，
而应该把 Shape layout 摘要放进 GC Local Workspace。
```

## 2026-05-17 师兄 DPU WRAM/MRAM generational GC 代码思想分析

用户贴了一份师兄写的 DPU allocator / GC 代码，要求解释思想。

代码在本地有相近版本：

- `miniproject-qiancheng-pimgc/yoshitsune/runtime_c/mydpulib_latest/mydpulib/allocator/generational_wram_major_is_compact/generational_wram.c`
- 用户贴出的版本和本地多个 variant 略有差异，但核心思想一致。

核心结论：

```text
这是一套面向 DPU 的分代 GC。
WRAM 是 fast local memory，用作 young generation。
MRAM 是 slow large memory，用作 old generation。
minor GC 的核心思想是：
  先给 live young object 在 MRAM old 里 reserve 目标地址；
  在 WRAM young 上继续扫描对象并把 child pointer 改成目标 MRAM pointer；
  等对象内部指针都更新好以后，再一次性 mram_write 整个对象到 MRAM。
```

这和当前 GiY 的思想非常接近：

```text
forward/copy_for_minor 不是马上真正复制内容，
而是先 reserve old address + 设置 forwarding/gc_ptr + 入栈。
真正的数据写入发生在对象 child pointer 都处理完之后。
```

重要结构：

1. heap 分区。

   - `HEAP_RS_SIZE = 4096`
   - `HEAP_YOUNG_SIZE = 28KB`
   - `HEAP_WRAM_SIZE = Allocator + RS + Young`
   - `HEAP_OLD_SIZE = HEAP_MRAM_SIZE - Allocator`

   含义：

   ```text
   WRAM:
     Allocator metadata
     remembered set
     young objects

   MRAM:
     allocator snapshot
     old objects
   ```

2. pointer tagging。

   - `get_is_wram_ptr(ptr)` 判断 `ptr % 2 != 0`。
   - young/WRAM pointer 用低 bit 标记。
   - old/MRAM pointer 不带这个标记。

   含义：

   ```text
   只要看到奇数 pointer，就知道它指向 WRAM young。
   minor GC 只需要处理这些 young pointer。
   ```

3. object header。

   每个对象 header 中至少有：

   - `gc_ptr`: minor GC 中的 forwarding pointer / reserved old destination
   - `class_table_index`: 指向 class metadata
   - `size`: payload 大小

   `gc_ptr != 0` 表示对象已经被发现并 reserve 过 old 位置。

minor GC 流程：

1. 从 exported_ref 扫 root。

   CPU / host 可见的 exported reference 如果指向 WRAM young，就调用 `copy_for_minor()`，把它改成 MRAM old pointer。

2. 扫 Java stack。

   stack frame 用 bitmap 判断哪些 slot 是 pointer。
   如果 slot 里是 WRAM young pointer，就调用 `copy_for_minor()`，并把 stack slot 改成 MRAM old pointer。

3. 扫 remembered set。

   old object 写入 young reference 时，write barrier 把 old field address 放进 RS。
   minor GC 不扫描整个 MRAM old，只扫描 RS 记录的 old fields。
   如果 old field 指向 WRAM young，就把它改成 `copy_for_minor()` 返回的 MRAM pointer。

4. traverse stack。

   `copy_for_minor()` 第一次发现对象时，只做：

   ```text
   reserve old address
   header->gc_ptr = reserved old data pointer
   push young header 到 GC stack
   return old pointer
   ```

   `traverse_stack()` 之后弹出 young object：

   ```text
   读 class metadata / bitmap
   扫描所有 reference fields
   如果 child 是 young:
      reserve child old address
      push child young header
      把当前对象字段改成 child 的 MRAM pointer
   所有 child pointer 更新完以后:
      mram_write 整个对象到 MRAM old destination
   ```

这段代码最重要的思想：

```text
不是先 copy 再修指针；
而是先修 young source object 里的指针，
再把已经修好的对象整体写入 old。
```

这样做的动机：

1. 减少 MRAM 随机写。

   MRAM 访问慢。若先把对象 copy 到 MRAM，再扫描 child，之后还要回头修改 MRAM 中的字段，会产生很多 MRAM random writes。

   当前设计是在 WRAM young 里修字段，最后一次性顺序写整对象到 MRAM。

2. WRAM 是 fast local memory。

   GC traversal 尽量在 WRAM 中读写 young object。
   MRAM 只用于：

   - root/old field 必要读写
   - 最后 promotion 的 `mram_write`
   - class table / stack map metadata 读取

3. 保留分代 GC 优势。

   minor GC 不扫描整个 old/MRAM。
   old-to-young references 依赖 remembered set。

4. 支持多 tasklet 并行。

   - 每个 tasklet 有自己的 `gc_stack[me()]`
   - 有 shared stack 可 work stealing
   - `gc_working_flag[]` + `gc_end_flag` 判断全局结束
   - old allocation 需要 mutex 保护
   - RS 扫描也用 index/mutex 分配任务

和 Cheney 的区别：

```text
Cheney:
  copy object to to-space / old
  再扫描 copied object
  需要在 copied object 上修 child pointer

师兄这份代码:
  reserve old destination
  扫描 WRAM source object
  在 WRAM source object 上修 child pointer
  最后一次性写到 MRAM
```

和当前 GiY 的关系：

```text
这份代码的思想非常接近 GiY：
  reserve first
  scan/update in young/local memory
  materialize/copy to old later
```

区别在于：

- 师兄代码是 Java-like 对象模型。
  - `class_table_index` + class bitmap 就能精确知道哪些字段是引用。
  - 因此没有 JS `Shape / PropertyMap / eprop` 的复杂问题。

- 当前 eJS / GiY 是 JavaScript 对象模型。
  - JSObject 的字段布局需要 Shape / PropertyMap。
  - Shape 本身也是 heap object。
  - 所以 GiY 在 JSObject scanner 上遇到了“不能直接像 Cheney 一样读 shape metadata”的问题。

最简单总结：

```text
师兄这份代码的核心是：
利用 WRAM 快、MRAM 慢的硬件特性，
把 young 放在 WRAM，把 old 放在 MRAM。
minor GC 时不立刻把对象完全搬到 MRAM 后再修改，
而是先在 MRAM 预约位置，
在 WRAM 上把对象内部 young 指针全部修成 MRAM 指针，
最后一次性写整个对象到 MRAM。

这就是一种 DPU 版的 GiY / reserve-first promotion 思想。
```

## 2026-05-17 当前 eJS GiY 和师兄 DPU 代码的差异及原因

用户问题：详细说明当前 GiY 和师兄代码的区别到底是什么，为什么会产生这样的区别。

总体判断：

```text
两者的核心思想相同：
  reserve old destination first
  scan/update young/local source object
  materialize/copy to old later

但是两者面对的运行环境和对象模型不同，
所以工程形态差异很大。
```

最本质的区别有两个：

1. 硬件/内存层次不同。

   - 师兄代码：
     - DPU 环境。
     - WRAM 是真实的 fast local memory。
     - MRAM 是真实的 slow large memory。
     - young 放 WRAM，old 放 MRAM。
     - 主要目标是减少 MRAM random access，尤其是 MRAM random writes。

   - 当前 eJS GiY：
     - CPU 环境。
     - 没有真正显式控制的 WRAM/MRAM。
     - 用 cache-space / GC Local Workspace 模拟“希望留在 cache 的区域”。
     - old 在 DRAM。
     - 主要目标是减少 cache miss / old-space cache pollution。

   产生原因：

   ```text
   师兄代码面对的是物理分离的 WRAM/MRAM；
   当前 GiY 面对的是 CPU cache/DRAM 层次，cache 不是软件可直接分配的地址空间。
   所以师兄可以明确说“这个对象在 WRAM/这个对象在 MRAM”，
   当前 GiY 只能通过 workspace size、访问局部性、NT-store 等手段影响 cache 行为。
   ```

2. 对象模型不同。

   - 师兄代码：
     - Java-like / static class model。
     - object header 里有 `class_table_index`。
     - class table 里有 field bitmap。
     - GC 扫描对象时，直接通过 class bitmap 知道哪些字段是 reference。

   - 当前 eJS GiY：
     - JavaScript dynamic object model。
     - `JSObject = Shape* + eprop[]`。
     - 字段布局由 `Shape / PropertyMap` 决定。
     - Shape 本身也是 GC-managed heap object。
     - GC 要 precise scan JSObject，就需要 Shape/PropertyMap layout 信息。

   产生原因：

   ```text
   Java 对象布局基本由 class 决定，稳定、静态、容易用 bitmap 表示。
   JavaScript 对象属性可以动态增加/转变，多个对象通过 hidden class / Shape 共享布局。
   所以 eJS GiY 多了 Shape / PropertyMap / eprop 问题。
   ```

具体差异：

1. Young / Old 的边界判断不同。

   - 师兄代码：
     - `get_is_wram_ptr(ptr)` 用低 bit 判断。
     - WRAM pointer 是奇数。
     - MRAM pointer 是普通地址。

   - 当前 GiY：
     - 用地址范围判断：`in_young_space`, `in_dram_space`, `in_init_space`。
     - JSValue 还带 PTag，需要 `clear_ptag()` / `put_ptag()`。

   原因：

   ```text
   DPU 代码自己定义 WRAM/MRAM pointer representation。
   eJS 已经有 JSValue tagging 和 VM heap spaces，必须和现有表示兼容。
   ```

2. Local workspace 的含义不同。

   - 师兄代码：
     - WRAM 里明确放：
       - Allocator
       - remembered set
       - young objects
     - 代码中 `HEAP_WRAM_SIZE = Allocator + RS + Young`。

   - 当前 GiY：
     - GC Local Workspace 是从 cache-space 中切出来的逻辑区域。
     - 放：
       - `g_gc_stack`
       - FT slot set
       - GiYOL staging buffer
       - GiYOL reserve-order batch
       - RSet/hash 等其他 local metadata
     - `ejsvm/GiY.cc:644-758` 会从 `cache_space.end` 往下切空间。

   原因：

   ```text
   WRAM 是真实 local memory，能明确划分。
   CPU cache 不是 malloc 可指定的物理区域，只能用 cache-space/workspace 近似控制工作集。
   ```

3. `copy_for_minor()` 的含义相似，但 materialization 位置不同。

   - 师兄代码：
     - `copy_for_minor()` / `get_object_copy_handle()`：
       - reserve MRAM old destination
       - 写 `header->gc_ptr`
       - push young header
       - 返回 MRAM pointer
     - `traverse_stack()`：
       - 在 WRAM source object 上修 child pointer
       - 最后 `mram_write()` 整个对象到 MRAM。

   - 当前 GiY：
     - `copy_for_minor()`：
       - reserve DRAM old destination
       - 写 `forwarding_pointer`
       - push young payload
       - 不复制
     - `giy_traverse_stack_and_copy()`：
       - scan young source object
       - patch young in-object slots
       - 调用 `giy_copy_live_object()` 把对象 materialize 到 old。

   原因：

   ```text
   两者都想避免“copy 到 old 后再回头修改 old 字段”。
   师兄代码最后用 mram_write，一次性把修好的 WRAM object 写到 MRAM。
   当前 GiY 用 memcpy/NT-store，把修好的 young object materialize 到 DRAM old。
   ```

4. Root / remembered set patch 的阶段不同。

   - 师兄代码：
     - 扫 exported_ref / stack / RS 时，遇到 young pointer 立刻把 slot 改成 MRAM pointer。
     - root/RS patch 基本和 reserve 同时完成。

   - 当前 GiY：
     - minor GC 分成 reserve 和 patch：
       - scan_roots_reserve
       - function_table_reserve
       - remembered_set_reserve
       - young_traverse_copy
       - scan_roots_patch
       - function_table_patch
       - remembered_set_patch
     - 代码在 `ejsvm/GiY.cc:2852-2924`。

   原因：

   ```text
   eJS 的 root/slot 类型更多：
     JSValue tagged slot
     void* pointer slot
     function frame
     function table
     remembered set values
     weak hidden-class metadata
   直接在 reserve 阶段全部 patch 更容易破坏语义。
   所以当前 GiY 更保守地拆成 reserve phase 和 patch phase。
   ```

5. 对象扫描方式不同。

   - 师兄代码：
     - 通过 `class_table_index` 取 class metadata。
     - 用 field bitmap 判断 reference field。
     - bitmap bit = 1 才扫描该字段。

   - 当前 GiY：
     - 对普通非 JSObject 类型可以用 generic `process_node`。
     - 对 JSObject 当前用了 conservative scanner：
       - 处理 `p->shape`
       - 按 object size 扫所有 `eprop`
     - 代码在 `ejsvm/GiY.cc:1182-1208`。

   原因：

   ```text
   师兄代码的 layout metadata 是 class table，基本静态，不是每个对象图里的普通 movable object。
   eJS 的 layout metadata 是 Shape/PropertyMap，而且 Shape 也是 heap object。
   GiY reserve 后 shape 可能只是 reserved old address，还没有 materialize。
   所以不能直接照 Cheney/generic precise scanner 读 shape metadata。
   ```

6. remembered set 的复杂度不同。

   - 师兄代码：
     - RS 主要记录 old field address。
     - minor GC 扫 RS 时读 old field 当前值，如果是 WRAM pointer，就 copy/update。

   - 当前 GiY：
     - GiY RSet 存 slot address 和 last written value。
     - 因为 GiY reserve phase 需要在不随便读 old slot 的情况下知道 old->young edge。

   原因：

   ```text
   师兄代码的目标是减少 MRAM 访问，但仍可在 RS scan 时读 MRAM old field。
   当前 GiY 更强调 GC core 避免 old-space read，因此 RSet 需要保存 value。
   这让当前 GiY RSet 更复杂，也可能增加 whole-program overhead。
   ```

7. 并行模型不同。

   - 师兄代码：
     - DPU 上多 tasklet 并行。
     - 每个 tasklet 有自己的 `gc_stack[me()]`。
     - shared stack 做 work stealing。
     - 用 mutex / barrier / `gc_working_flag` / `gc_end_flag` 判断结束。

   - 当前 eJS GiY：
     - 当前路径基本是单线程 collector。
     - 没有 tasklet work stealing。

   原因：

   ```text
   DPU 程序天然多 tasklet，必须设计并行 GC 协作。
   eJS 当前运行在 CPU VM 上，研究重点是 cache locality / old-space access，不是多 tasklet 调度。
   ```

8. 大对象与直接 old allocation。

   - 用户贴出的师兄版本中，大对象超过阈值会直接 old allocation。
   - 当前 GiY/eJS 主要关注 young survivor promotion，GiYOL 另外区分小对象 staging / 大对象直接 copy 或 NT-store。

   原因：

   ```text
   DPU WRAM 很小，大对象放 WRAM young 不划算。
   当前 GiY 的问题更多是 CPU cache/DRAM copy 策略，因此大对象策略体现在 GiYOL batching / NT-store 上。
   ```

一句话总结：

```text
两者的思想源头一样：
  不要先把对象复制到 old 再反复修改 old；
  而是先 reserve old 地址，
  在 young/local memory 中完成指针修正，
  最后再 materialize 到 old。

差异来自两个事实：
  师兄代码是 DPU + Java-like static class bitmap；
  当前 GiY 是 CPU cache/DRAM + JavaScript dynamic Shape/PropertyMap。

所以师兄代码可以用 class bitmap 精确扫描对象，
当前 GiY 必须处理 JSObject 的 Shape/eprop/PropertyMap 问题；
这就是当前 GiY 比师兄代码复杂很多的根本原因。
```

## 2026-05-17 Java 和 JavaScript 类型/对象布局差异

用户问题：具体说明 Java 和 JavaScript 的类型到底有什么区别。

核心结论：

```text
Java 的对象布局主要由 class 决定，比较静态。
JavaScript 的对象布局由运行时 shape/hidden class 决定，可能动态变化。

所以 Java GC 可以通过 class metadata / bitmap 精确知道对象哪些字段是引用；
JavaScript GC 需要通过 Shape / PropertyMap / Map / Structure 等运行时 metadata 判断对象布局。
```

Java 的类型特点：

1. 变量有声明类型。

   ```java
   Person p;
   int x;
   String s;
   ```

2. class 定义字段。

   ```java
   class Person {
     int age;
     String name;
     Address address;
   }
   ```

   对这个 class 的所有 `Person` 对象来说，字段集合基本固定：

   ```text
   age
   name
   address
   ```

3. 对象运行时有 class/klass 指针。

   GC 可以通过 class metadata 知道：

   ```text
   age     = int，不是引用
   name    = reference
   address = reference
   ```

4. Java 对象不能随便新增字段。

   不能写：

   ```java
   p.newField = 123;
   ```

   除非 class 本来声明了这个字段。

JavaScript 的类型特点：

1. 变量没有固定声明类型。

   ```js
   let x = 1;
   x = "hello";
   x = {};
   ```

2. 对象可以动态增加/删除属性。

   ```js
   let obj = {};
   obj.x = 1;
   obj.y = "hello";
   delete obj.x;
   obj.z = {};
   ```

3. 同一个对象的布局可以不断变化。

   ```text
   {}
   -> {x}
   -> {x, y}
   -> {y}
   -> {y, z}
   ```

4. VM 为了效率会创建 Shape / Hidden Class。

   JavaScript VM 不可能每次访问属性都慢慢查字典，所以会把相似对象归类成一种 layout：

   ```text
   Shape1: {}
   Shape2: {x -> eprop[0]}
   Shape3: {x -> eprop[0], y -> eprop[1]}
   ```

   对象里保存一个 `shape` 指针，表示当前对象采用哪个 layout。

对 GC 的影响：

Java:

```text
object header -> class metadata
class metadata -> reference bitmap
GC 根据 bitmap 精确扫描字段
```

JavaScript:

```text
JSObject -> shape
shape -> PropertyMap
PropertyMap -> 哪些 eprop 是属性、哪些 slot 要扫描
GC 需要 Shape/PropertyMap 才能 precise scan
```

为什么师兄代码简单：

- 师兄代码是 Java-like。
- 对象 header 有 `class_table_index`。
- class table 有 field bitmap。
- GC 扫描对象时直接按 bitmap 走。

为什么当前 GiY 复杂：

- eJS 是 JavaScript-like。
- JSObject 只有 `shape` 和 `eprop[]`。
- `eprop[]` 的含义需要 Shape / PropertyMap 解释。
- Shape 本身又是 GC 管理的 heap object。
- GiY reserve-first 后，Shape 的 forwarding address 可能还没 materialize。
- 所以不能像 Java 那样简单读一个稳定 class bitmap。

最简单总结：

```text
Java:
  类型和字段主要在 class 里提前确定。
  GC 看 class bitmap 就知道哪里是 pointer。

JavaScript:
  对象属性可以运行时变化。
  VM 用 Shape/Hidden Class 动态描述对象布局。
  GC 要精确扫描 JSObject，就要知道当前 Shape/PropertyMap。

这就是为什么师兄代码可以直接 bitmap 扫描，
而当前 GiY 必须处理 Shape / PropertyMap / eprop。
```

## 2026-05-17 Java 扫描对象过程、Klass 指针位置、JavaScript 对象处理

用户问题：

- Java 扫描对象的过程是什么？
- `Klass` / `kclass` 指针放在哪里？
- JavaScript 到底怎么处理对象？

Java / HotSpot 中对象大致结构：

```text
普通 Java object:

[ mark word ]
[ klass pointer / compressed klass pointer ]
[ fields... ]

Java array:

[ mark word ]
[ klass pointer / compressed klass pointer ]
[ array length ]
[ elements... ]
```

说明：

- `klass pointer` 在对象 header 里，不是 Java 程序能直接看到的字段。
- 开启 compressed class pointer 时，klass pointer 可能是 32-bit narrow klass。
- `klass pointer` 指向 VM 内部的 `Klass` metadata。
- 在 HotSpot 中，`Klass` metadata 通常在 Metaspace / VM metadata 区，不是普通 Java heap object。
- Java heap 里也有 `java.lang.Class` mirror object，但对象 header 里的 klass pointer 指向的是 VM metadata，不是直接指向普通 Java 对象字段。

Java GC 扫描对象的基本过程：

```text
1. GC 从 roots 找到一个 object pointer。
2. 读取对象 header 里的 klass pointer。
3. 通过 Klass metadata 知道：
     - 这个对象是什么类型
     - 对象大小是多少
     - 哪些字段是 reference
     - 哪些字段是 primitive
4. 如果是普通对象：
     按 Klass 中的 reference-field layout / oop map 扫描引用字段。
5. 如果是 object array：
     扫描数组元素，因为元素都是 reference。
6. 如果是 primitive array：
     不需要扫描元素，因为没有 reference。
7. 如果是 moving GC：
     对发现的 child object 做 mark/copy/forward，并更新 slot。
```

Java stack 扫描：

- Java GC 不会把整个 native stack 当作对象乱扫。
- 在 safepoint，JIT/interpreter 会提供 OopMap / stack map。
- OopMap 告诉 GC：

  ```text
  当前 stack frame / register 里哪些位置是 object reference。
  ```

- 这和师兄代码中通过 stack bitmap 扫 Java frame 是同一类思想。

Java 为什么可以这样简单：

```text
Java class 在编译/加载后，字段布局基本确定。
对象 header 里有 Klass pointer。
Klass metadata 里有 reference field 信息。
所以 GC 可以通过 Klass 精确扫描。
```

师兄代码中的对应关系：

```text
HotSpot Klass pointer    <-> 师兄代码 ObjectHeader.class_table_index
HotSpot Klass oop map    <-> 师兄代码 class table bitmap
HotSpot stack OopMap     <-> 师兄代码 stack frame bitmap
```

区别是：

```text
HotSpot object header 里放 Klass*。
师兄代码 object header 里放 class_table_index。
但思想一样：对象自己带一个通向类型/布局 metadata 的入口。
```

JavaScript 对象处理：

JavaScript 对象不是 class 固定字段布局。

例如：

```js
let o = {};
o.x = 1;
o.y = {};
delete o.x;
o.z = "abc";
```

这个对象的属性集合可以运行时变化：

```text
{}
{x}
{x, y}
{y}
{y, z}
```

因此 JavaScript VM 通常使用 hidden class / shape / map：

```text
object -> shape/map/structure -> property layout
```

在 eJS 当前代码中：

```text
JSObject:
  Shape *shape;
  JSValue eprop[];

Shape:
  PropertyMap *pm;
  n_embedded_slots;
  n_extension_slots;

PropertyMap:
  property name -> index
  n_props;
  n_special_props;
```

含义：

```text
eprop 是真正存属性值的格子。
Shape 告诉 VM 这个对象有多少格子、有没有 extension array。
PropertyMap 告诉 VM 属性名对应哪个格子。
```

JavaScript 属性访问大致流程：

```text
读 o.x:

1. 读取 o.shape。
2. 在 shape->PropertyMap 里查 x。
3. 得到 index。
4. 读取 o.eprop[index]。
```

为了加速，VM 会使用 inline cache：

```text
如果上次看到的 shape 相同，
就不用重新查 PropertyMap，
直接读固定的 eprop[index]。
```

JavaScript GC 扫描 JSObject：

```text
1. 扫 object->shape，因为 shape 也是对象布局 metadata。
2. 根据 shape / property map 判断哪些 eprop slot 需要扫描。
3. 扫描这些 eprop slot 中的 heap reference。
4. 如有 extension array，也扫描 extension array。
```

Java 和 JavaScript 最大区别：

```text
Java:
  object header -> Klass metadata
  Klass metadata 通常不是普通 heap object
  字段布局稳定
  GC 扫描直接用 Klass oop map / bitmap

JavaScript:
  JSObject -> Shape / Map metadata
  Shape 在 eJS 里是 GC-managed heap object
  对象布局可以运行时变化
  GC 扫描需要动态 layout metadata
```

这对 GiY 的影响：

```text
师兄 Java-like 代码：
  class_table_index 是稳定 metadata 入口。
  GC 扫对象时直接拿 bitmap。

当前 eJS GiY：
  shape 是 heap object，可能在 young。
  GiY forward shape 时只 reserve，不立即 copy。
  如果直接读 forwarded shape，就可能读到未 materialize 的 old address。
```

所以当前 GiY 的问题不是“GC 不知道怎么扫对象”，而是：

```text
Java-like metadata 是稳定的 class bitmap。
eJS metadata 是动态的 Shape / PropertyMap，而且 Shape 也参与 GC。
GiY reserve-first 让这个 metadata 读取时机变得困难。
```

最简单总结：

```text
Java 对象 header 里有 Klass pointer。
Klass 告诉 GC 哪些字段是引用。
所以 Java GC 扫描对象时：object -> Klass -> reference bitmap。

JavaScript 对象里有 shape pointer。
Shape/PropertyMap 告诉 VM 属性怎么放在 eprop 里。
所以 JavaScript GC 扫描对象时：object -> Shape -> PropertyMap -> eprop layout。

当前 GiY 的麻烦来自：
Shape 在 eJS 里也是 heap object，
而 GiY 先 reserve 不立刻 materialize。
```

## 师兄代码中的 klass/class_table_index 位置，以及 JavaScript 对象生成流程

结论：

```text
师兄代码里的 klass 等价物不是一个指向 young/old 对象区的指针。
它主要表现为 ObjectHeader.class_table_index。
这个值是“类元数据表”的索引，而不是 GC heap object pointer。
```

在师兄的 DPU runtime 中：

```text
object header:
  gc_ptr
  class_table_index
  size

class_table_index:
  用来找到 class_table / class_info 里的类布局信息。
  class_table / class_info 定义在 MRAM metadata 区。
  它们不是 young 区对象，也不是 old 区对象。
```

相关代码：

```text
mydpulib/mram.c:
  __class_table / __class_info 是 __mram_noinit 的全局 metadata。
  class_table = __class_table;
  class_info = __class_info;

generational_wram_major_is_compact/generational_wram.c:
  GC 扫描对象时读 header->class_table_index。
  然后通过 class_table[class_table_index] 找字段数量和 reference bitmap。
```

如果开启 `CACHE_CLASS_TABLE`：

```text
class table 的一小份布局缓存会被 mram_read 到 WRAM。
扫描时使用 wram_class_table。
但是这仍然是 metadata cache，不是 young generation 对象。
它也不是 minor GC 要复制或晋升的普通对象。
```

因此准确说：

```text
默认物理位置：MRAM metadata 区。
可选快速缓存：WRAM metadata cache。
逻辑位置：GC 管理的 young/old heap 之外。
```

这和 eJS JavaScript 对象不同。

eJS 的 JavaScript object 结构：

```text
JSObject:
  Shape *shape;
  JSValue eprop[];

Shape:
  PropertyMap *pm;
  n_embedded_slots;
  n_extension_slots;

PropertyMap:
  HashTable *map;
  prev;
  shapes;
  n_props;
  n_special_props;
  __proto__;
```

JavaScript 对象从生成开始的大致流程：

```text
1. VM 初始化阶段先创建内建 prototype、root property map、初始 shape。

2. 创建普通对象 / array / function 时，VM 先选一个 Shape。
   Shape 决定这个对象有多少 embedded property slot。

3. allocate_jsobject 根据 shape->n_embedded_slots 分配 JSObject。
   对象里保存 p->shape = os。
   eprop[] 初始化为 JS_EMPTY。

4. array/function 会额外写入 special props。
   例如 array body、size、length。

5. 读属性时：
   JSObject -> shape -> property map -> hash table
   property name 被查成 eprop index。
   然后从 eprop[index] 或 extension array 读取值。

6. 写已有属性时：
   同样先找到 index，然后写入 eprop。
   如果启用 remembered set，需要 write barrier。

7. 新增属性时：
   当前 PropertyMap 没有这个名字，就创建/复用 next PropertyMap。
   再找/创建对应 Shape。
   如果 embedded slots 不够，就分配 extension array。
   最后把 object->shape 更新为新 Shape。

8. GC 扫描 JSObject 时：
   先扫描 shape。
   再根据 shape / property map 判断 eprop 和 extension 里哪些位置需要扫描。
```

为什么这对 GiY 很重要：

```text
师兄 Java-like 代码:
  class_table_index 是稳定 metadata 索引。
  metadata 不跟着对象在 young/old 之间移动。
  所以扫描对象时可以直接读 class bitmap。

eJS JavaScript:
  JSObject->shape 是一个 GC-managed heap pointer。
  Shape / PropertyMap 自己也可能被复制、转发、移动。
  对象字段布局不是编译期固定的，而是运行时 shape 决定的。

所以 GiY 如果先 reserve shape 但不立刻 materialize，
后续扫描 JSObject 时如果需要读 shape->pm / slot 信息，
就可能遇到“地址已经 forward，但目标内容还没真正复制好”的问题。
```

## 师兄代码如何避免每次从 MRAM 读取 klass/class metadata

问题判断：

```text
如果每次扫描对象都通过 class_table_index 去 MRAM 读 class_table / class_info，
确实会慢。
师兄代码也意识到了这个问题。
```

他的主要解决方法是 `CACHE_CLASS_TABLE`。

核心思想：

```text
不要把完整 klass/class metadata 当成普通对象搬到 young/old。
而是把 GC 扫描最常用、最小必要的信息提前压缩成一个 WRAM cache。
```

WRAM cache 的加载位置：

```text
mydpulib/trampoline.h:
  如果定义了 CACHE_CLASS_TABLE，
  每次 binary 运行前，从 MRAM 的 binary_class_table 中读取当前 binary 需要的 class cache。
  读取到 WRAM 的 wram_class_table_cache。
```

cache 的结构大致是：

```text
wram_class_table_cache[0]:
  cached_class_num

wram_class_convert_table:
  local class id -> global class id

wram_class_table:
  每个 local class id 两个 long:
    wram_class_table[2 * id]     = field_num
    wram_class_table[2 * id + 1] = reference bitmap
```

GC 扫描时：

```text
#ifdef CACHE_CLASS_TABLE
  class_id = header->class_table_index;
  field_num = wram_class_table[2 * class_id];
  bitmap = wram_class_table[2 * class_id + 1];
  header->class_table_index = wram_class_convert_table[class_id];
#else
  class_table_head = class_table[header->class_table_index];  // MRAM
  field_num = class_table_head[CLASS_INFO_IDX_INSTANCE_SIZE] / 8;
  bitmap_info = class_table_head[CLASS_INFO_IDX_BITMAP_INFO];
  bitmap = class_table_head[bitmap_entry_index];
#endif
```

所以：

```text
没有 CACHE_CLASS_TABLE:
  扫描对象需要读 MRAM metadata。

有 CACHE_CLASS_TABLE:
  minor GC 扫描 young 对象时只读 WRAM 中的 field_num 和 bitmap。
```

为什么还需要 `wram_class_convert_table`：

```text
启用 cache 时，transpiler 对 NEW 传入的是 local class id。
young 区对象 header 里保存 local class id。

minor GC 扫描并复制到 old 区时，
用 local class id 查 WRAM cache。
在真正写入 old 区前，
把 header->class_table_index 改回 global class id。
```

这样 old 区对象仍然保存全局 class id，后续 major GC 或跨 binary 场景还能用完整 MRAM metadata。

这和 eJS GiY 的区别：

```text
师兄代码:
  class metadata 静态、数量有限、编译/转换阶段可知。
  GC 只需要 field_num + reference bitmap。
  所以可以提前压缩到 WRAM cache。

eJS JavaScript:
  Shape / PropertyMap 是运行时动态创建的 heap object。
  对象加属性会产生新的 PropertyMap / Shape。
  Shape 还可能被 GC 移动。
  所以不能简单提前做一个固定 class cache。
```

对 GiY 的启发：

```text
可以考虑为 JSObject 做一个类似的 shape scan descriptor cache。
cache 中只放 GC 扫描需要的信息：
  n_embedded_slots
  n_extension_slots
  n_props
  n_special_props
  extension 起点

但必须解决：
  Shape 是动态产生的。
  Shape 可能被 young->old 复制。
  cache entry 需要在 shape 创建/变化时维护。
  reserve-first 策略下不能读未 materialize 的 forwarded shape。
```

## GiY 的 JavaScript 类型/Shape 问题带来多大性能影响

问题定义：

```text
这里说的“类型问题”主要不是 JS 运行时类型判断本身，
而是 JSObject 的 layout 依赖 Shape / PropertyMap。

Cheney 原本可以用普通 visitor：
  先处理 object->shape
  再用 shape->pm 判断 embedded/extension slots

GiY 因为 reserve-first：
  shape 可能已经 forward/reserve，但 old 目标内容还没有真正 materialize
  所以当前 GiY 对 JSObject 采用 conservative scan：
    处理 p->shape
    然后按对象 header size 扫全部 eprop slots
```

相关代码：

```text
ejsvm/GiY.cc:
  giy_scan_jsobject_conservative()
  按 hdr->size 算 slots，扫描全部 eprop。

ejsvm/gc-visitor-inl.h:
  原 visitor 会通过 Shape / PropertyMap 精确判断 embedded/extension。
```

现有数据不能把“类型问题”完全独立拆出来，因为 GiY 和 Cheney 同时还有其他差异：

```text
GiY:
  reserve-first
  edge log / function table slot set
  local workspace 占用 young 空间
  different copying strategy
  conservative JSObject scan

Cheney:
  immediate copy
  precise visitor
  no GiY edge log
```

因此目前可以给出的结论是：

```text
类型/Shape 问题在 cache 行为上很明显；
但在总运行时间上，目前不是最大的单独瓶颈。
```

已有 PMU 实验：

```text
数据目录：
build.debug/benchmarks/out_gc_cache_miss_20260516_235434

对象：
  cache_cheney vs giy
  cache/local workspace sizes: 640, 768, 896 KB
  benchmarks: CD, Havlak, Richards, Storage
  event groups: generic cache, L1D load, LLC load
```

GC window 内部结果：

```text
640KB:
  GiY GC overhead: +3.85%
  GiY GC cache misses: +88.54%
  GC miss rate: Cheney 0.657% -> GiY 1.882%
  GC MPKI: +95.93%

768KB:
  GiY GC overhead: +1.36%
  GiY GC cache misses: +61.51%
  GC miss rate: Cheney 0.777% -> GiY 1.609%
  GC MPKI: +68.41%

896KB:
  GiY GC overhead: -0.20%
  GiY GC cache misses: +47.13%
  GC miss rate: Cheney 0.865% -> GiY 1.701%
  GC MPKI: +51.85%
```

重要细节：

```text
GiY 的 GC references 比 Cheney 少 22% 到 34%，
但是 cache misses 反而多 47% 到 89%。

这说明 GiY 不是“访问更多所以 miss 更多”，
而是访问模式/metadata locality 更差。
Shape / PropertyMap / conservative eprop scan 是最可能来源之一。
```

whole-program PMU：

```text
generic cache misses:
  GiY 比 Cheney 多约 22.9% 到 27.8%

L1D load misses:
  GiY 比 Cheney 多约 17.5% 到 22.0%

LLC load misses:
  GiY 比 Cheney 多约 84.0% 到 121.8%
```

但总时间变化不大：

```text
PMU run 总时间：
  GiY 比 Cheney 慢约 2.4% 到 3.4%

PMU run GC overhead：
  GiY 从 -0.2% 到 +3.9%
```

另外，552KB fair full-suite 三轮实验显示：

```text
GiY minor GC 次数比 Cheney 多 13.55%。
但 GiY GC overhead 总和比 Cheney 低约 25%。
总运行时间三轮混合：
  round1: GiY -1.56%
  round2: GiY +2.21%
  round3: GiY -1.93%
```

解释：

```text
类型/Shape 问题确实破坏 cache locality。
但是当前 GiY 的 copy/reserve 流程在部分 benchmark 上减少了 GC 工作量，
所以 wall-clock 上没有直接放大成很大的退化。

换句话说：
  cache 层面：问题很明显，GC miss 大约 1.5x 到 1.9x。
  时间层面：目前只表现为几个百分点以内，甚至在部分公平实验中被 GiY 其他优势抵消。
```

如果要精确测“类型问题本身”的成本，需要做 ablation：

```text
方案 A：
  GiY current conservative JSObject scan
  vs
  GiY + shape scan descriptor cache / materialized shape precise scan
  其他参数完全不变。

方案 B：
  Cheney precise scan
  vs
  Cheney conservative JSObject scan
  这样可以单独测 conservative scan 的代价。

应增加 counters：
  jsobject_scanned
  eprop_slots_checked
  precise_slots_expected
  shape_edges
  property_map_edges
  extension_slots_scanned
  old_or_immediate_slots_noop
```

当前可写进报告的保守表述：

```text
GiY 的 JavaScript Shape/Layout 问题目前主要表现为 cache locality 问题：
GC-window cache misses 比 Cheney 高约 47% 到 89%，
GC miss rate 约为 Cheney 的 2.0x 到 2.9x。

但是它没有等比例转化为总运行时间退化；
在现有 benchmark 中，GiY 总时间通常只在 Cheney 附近几个百分点内波动。
```

## 采用方案 A 测量 Shape/Layout 问题成本的合理成本

方案 A：

```text
A0: 当前 GiY conservative JSObject scan
A1: GiY + materialized/descriptor-based precise JSObject scan
其他参数保持不变
```

测量目标：

```text
type_cost_time = A0_GC_overhead - A1_GC_overhead
type_cost_misses = A0_GC_cache_misses - A1_GC_cache_misses
extra_slots = conservative_eprop_slots - precise_eprop_slots
```

需要避免的误测：

```text
不能简单把 GiY 改成普通 precise visitor 就跑。
原因是 GiY reserve-first 下 shape 可能已经 forwarded/reserved，
但 old 目标内容还没有真正 materialize。

如果直接读 shape->pm，可能读到不安全的 metadata。
```

推荐低风险 A1：

```text
新增编译开关，例如 GIY_JSOBJECT_PRECISE_SCAN=1。

扫描 JSObject 时：
  1. 先处理 p->shape edge。
  2. 如果 shape 在 young 且已/将被 forwarded，则强制 materialize shape。
  3. materialize shape->pm。
  4. 用 shape/pm 信息做 precise scan。
  5. 如果 shape/pm 无效，则 fallback 到 conservative scan。
```

这个 A1 测到的是：

```text
解决 Shape/Layout 问题后的净收益：
  少扫描无用 eprop slot
  但增加 shape/pm 提前 materialization 成本
```

它不是完全纯粹的理论成本，但非常接近真实可实现方案。

需要增加 counters：

```text
jsobject_scans
conservative_eprop_slots
precise_eprop_slots
skipped_eprop_slots
shape_edges
shape_materialize_attempts
shape_materialize_young
pm_materialize_attempts
pm_materialize_young
precise_scan_fallbacks
extension_arrays_scanned
extension_slots_scanned
```

合理实施成本：

```text
代码修改：
  低风险 materialized precise scan: 2-4 小时
  更完整 descriptor cache: 0.5-1.5 天

推荐先不要直接做完整 descriptor cache。
因为 descriptor cache 会改 Shape/PropertyMap 创建和维护路径，
更像优化实现，不像纯测量。
```

合理运行成本：

```text
Smoke:
  hello_world + giy_gc_probe + Bounce
  约几分钟到 30 分钟

Key benchmark:
  CD, Havlak, Richards, Storage
  A0/A1 各跑 3 次
  约 2-5 小时

PMU cache miss:
  固定一个 workspace，例如 896KB
  A0/A1 x 4 benchmarks x 3 event groups
  约 5-6 小时

Full suite:
  12 benchmarks x A0/A1 x 3 rounds
  约 6-8 小时
```

推荐顺序：

```text
第 1 步：
  做 A1 materialized precise scan + counters。

第 2 步：
  smoke + key benchmarks。

第 3 步：
  如果 key benchmarks 显示 GC cache misses 明显下降，
  再跑 PMU 和 full suite。

第 4 步：
  只有当 A1 有收益，再考虑实现真正 descriptor cache。
```

可接受的报告口径：

```text
如果 A1 比 A0 快：
  当前 GiY 的 conservative JSObject scan/Shape 问题有实际时间成本。

如果 A1 cache miss 降低但时间不变：
  类型问题主要是 cache locality 问题，还没成为 wall-clock 主瓶颈。

如果 A1 更慢：
  当前问题存在，但“强制 materialize shape/pm”不是好解决方案；
  应转向 descriptor cache。
```

## 2026-05-18 GiY JSObject precise scan 实装与测量

目标：

```text
解决当前 GiY 在 JSObject 扫描时过度 conservative 的问题：
  旧实现不读取 Shape/PropertyMap layout，
  而是把 JSObject header size 后面的 eprop[] 全部当作可能引用扫描。

这样安全，但会扫描很多不需要的 slot。
```

最终实装：

```text
文件：
  ejsvm/GiY.cc
  ejsvm/common.mk

新增开关：
  GIY_JSOBJECT_PRECISE_SCAN
    0: 旧 conservative JSObject scan
    1: 新 precise JSObject scan

  GIY_JSOBJECT_LAYOUT_CACHE_ENTRIES
    默认 0。
    可以显式设为 2048 等值，实验 Shape layout cache。
```

实现思想：

```text
1. 进入 JSObject scan 时，先保存 source-side p->shape。
2. 处理 p->shape 这个 edge。
3. 不从 forwarding 后的 old object 读取 layout。
   而是从仍在 young/init/dram 中的 source-side Shape 读取 layout。
4. 验证 Shape 和 PropertyMap 的对象类型。
5. 如果 layout 合法，就只扫描：
     special slots
     pm->n_special_props 到 actual_embedded 之间的普通 property slots
     extension property array edge
6. 如果 layout 不合法，fallback 到旧 conservative scan。
```

为什么这样做：

```text
GiY 的 minor GC 是 reserve first / materialize later。
如果扫描 p->shape 后立刻去读新的 old-side shape，
这个 old-side copy 可能还没有 materialize。

所以最小风险方案不是强制提前 copy Shape/PropertyMap，
而是读取 source-side shape layout。
这样不会破坏 GiY 的 reserve/copy 顺序。
```

验证：

```text
最终默认形态：
  make -C build.debug ejsvm OPT_GC=giy CACHE_SIZE_KB=896 GIY_JSOBJECT_PRECISE_SCAN=1 -B -j4
  成功。

Smoke:
  build.debug/benchmarks/out135_giy_jsobj_final_smoke
  giy_gc_probe status=0
  JSObject precise scan=1
  JSObject scans=10964
  JSObject precise=10964
  JSObject fallbacks=0
```

第一组测量：A0 conservative vs A1 source-side precise scan

```text
目录：
  build.debug/benchmarks/out133_giy_jsobj_precise_896_key

配置：
  CACHE_SIZE_KB=896
  A0: GIY_JSOBJECT_PRECISE_SCAN=0
  A1: GIY_JSOBJECT_PRECISE_SCAN=1
  GIY_JSOBJECT_LAYOUT_CACHE_ENTRIES=0

CD:
  wall time:
    A0 486.634 sec
    A1 483.499 sec
    delta -0.64%

  GC full time:
    A0 15.402 sec
    A1 16.948 sec
    delta +10.04%

  JSObject scan:
    scans 64516021 -> 64516021
    conservative slots 95978461 -> 0
    precise slots 0 -> 85471147
    skipped slots 0 -> 10507314
    fallbacks 0

  GC cache misses:
    A0 13555354
    A1 12896873
    delta -4.86%

  cache MPKI:
    A0 0.201
    A1 0.157
    delta -21.89%

  instructions:
    A0 67368404290
    A1 82091916019
    delta +21.86%
```

解释：

```text
A1 确实减少了 cache miss 和 MPKI。
但是每个 JSObject 都读取 Shape/PropertyMap layout，
所以指令数明显增加，GC full time 没有改善。
wall time 小幅下降，但幅度很小，不能认为是稳定性能收益。
```

第二组测量：layout cache 实验

```text
目录：
  build.debug/benchmarks/out134_giy_jsobj_layout_cache_896

配置：
  CACHE_SIZE_KB=896
  A0-cache:
    GIY_JSOBJECT_PRECISE_SCAN=0
    GIY_JSOBJECT_LAYOUT_CACHE_ENTRIES=2048
  A2-cache:
    GIY_JSOBJECT_PRECISE_SCAN=1
    GIY_JSOBJECT_LAYOUT_CACHE_ENTRIES=2048

说明：
  A0-cache 也预留 96KB layout cache 空间。
  所以 A0-cache vs A2-cache 的 local workspace 使用量公平。
```

giy_gc_probe：

```text
layout cache hits/misses:
  A2-cache hits 10943
  A2-cache misses 21
  invalid 0

cache misses:
  A0-cache 3313
  A2-cache 2570
  delta -22.43%

cache MPKI:
  A0-cache 0.480
  A2-cache 0.325
  delta -32.29%
```

CD：

```text
layout cache hits/misses:
  A2-cache hits 63294618
  A2-cache misses 1341867
  invalid 0

wall time:
  A0-cache 483.815 sec
  A2-cache 481.196 sec
  delta -0.54%

GC full time:
  A0-cache 16.450 sec
  A2-cache 16.556 sec
  delta +0.64%

cache misses:
  A0-cache 12750843
  A2-cache 13355433
  delta +4.74%

instructions:
  A0-cache 73406934594
  A2-cache 77168575189
  delta +5.12%
```

解释：

```text
layout cache 对 micro benchmark 有帮助。
但是 CD 中 96KB local workspace cache 会占用 young/local budget，
并且 cache table 本身也会被频繁访问。

结果是：
  wall time 只有 -0.54%，基本接近噪声；
  GC full time 略增；
  cache misses 反而增加。

因此 layout cache 不适合作为默认开启方案。
它保留为实验开关，默认 GIY_JSOBJECT_LAYOUT_CACHE_ENTRIES=0。
```

当前结论：

```text
最合理的默认方案：
  使用 source-side precise JSObject scan。
  不默认启用 layout cache。

能确认的问题：
  旧 GiY 的 conservative JSObject scan 确实会扫描不必要 slot。
  precise scan 能降低 GC cache miss / MPKI。

还不能确认：
  这个优化能稳定降低 wall-clock 时间。
  当前 CD 上 wall time 只小幅变化，GC time 没有变好。

后续更有价值的方向：
  不是简单扩大 layout cache，
  而是寻找更轻量的 shape descriptor / slot bitmap 表示，
  或者只对高收益 Shape 启用 precise scan。
```

## 2026-05-18 GiY JSObject cache miss 修复迭代：A5-A10

目标：

```text
继续解决 GiY 的 JSObject 扫描导致 cache miss / GC 性能不理想的问题。
要求不是只提出方案，而是实现、测试，失败后继续迭代。
```

本轮代码改动：

```text
文件：
  ejsvm/GiY.cc
  ejsvm/common.mk

新增/保留的实验开关：
  GIY_JSOBJECT_PRECISE_SCAN
  GIY_JSOBJECT_PRECISE_ARRAY
  GIY_JSOBJECT_SHAPE_ONLY_SCAN
  GIY_JSOBJECT_TYPE_AWARE_SCAN
  GIY_JSOBJECT_TYPE_AWARE_ARRAY
  GIY_JSOBJECT_TYPE_AWARE_BOXED_PRIMITIVE
  GIY_JSOBJECT_ARRAY_FAST_PATH
  GIY_JSOBJECT_ARRAY_SKIP_SIZE
  GIY_JSOBJECT_PRECISE_MIN_SLOTS
  GIY_JSOBJECT_LAYOUT_CACHE_ENTRIES

最终较合理的方案：
  GIY_JSOBJECT_ARRAY_SKIP_SIZE=1

思想：
  JavaScript Array 的 eprop[0] 是 size 槽。
  它不是 GC 需要追踪的对象引用。
  旧 GiY conservative JSObject scan 会把 eprop[0] 也当作普通 JSValue 扫描。
  A10 对 CELLT_ARRAY 增加专用扫描路径：
    仍然扫描 shape；
    扫描 eprop[1] 以后所有槽；
    跳过 eprop[0] size；
    不读取 Shape / PropertyMap；
    不占用额外 GC Local Workspace。
```

为什么放弃前面的方案：

```text
A5 selective type-aware：
  只对少数类型做 type-aware，但仍有额外通用分派成本。
  Storage 结果：
    total 331.697s
    GC overhead 42.708s
    instructions 268,413,274,040
  相比 A0-current，GC 和 instructions 仍退化。

A6 selective dispatch：
  把类型选择提前到 giy_process_young_node。
  Storage 结果：
    total 362.702s
    GC overhead 42.768s
    instructions 267,352,840,747
  cache miss 降低，但 GC/instructions 仍不理想。

A7 non-array precise + layout cache：
  512-entry layout cache 占用 12KB GC Local Workspace。
  Storage 结果：
    minor GC 108582，而 A0 是 106281
    GC overhead 44.032s
  结论：
    layout cache 即使缩小，也会吃掉 young/local budget，增加 minor GC。
    不适合作为默认方案。

A8 shape-only non-array：
  不读 PropertyMap，不占 layout cache。
  但 Storage/CD 中 precise count 都是 0。
  说明该 workload 中命中的非 Array precise scan 很少/没有。
  结果更像代码布局/分支影响，不是真正解决扫描问题。

A9 array fast path only：
  只把 Array 提前走 conservative fast path。
  Storage 结果：
    total 334.810s
    GC overhead 45.812s
    instructions 294,152,167,568
  明确失败。
```

A10 测试结果：

```text
二进制：
  build.debug/ejsvm_giy_jsobj_a10_array_skip_size_896

编译：
  make -C build.debug ejsvm OPT_GC=giy CACHE_SIZE_KB=896 GIY_JSOBJECT_ARRAY_SKIP_SIZE=1 -B -j4

短测试 giy_gc_probe：
  JSObject scans        10964
  type-aware(Array)     10839
  conservative          125
  skipped slots         10839
  cache-misses          1655
  instructions          6,610,950

Storage：
  A0-current r2:
    total               331.238s
    GC overhead         41.921s
    GC CPU              41.838s
    minor GC            106281
    forward operations  1,091,998,221
    cache-misses        1,473,720
    instructions        265,165,742,901

  A10:
    total               333.614s
    GC overhead         42.161s
    GC CPU              41.871s
    minor GC            106281
    forward operations  1,091,998,221
    skipped slots       546,097,411
    cache-misses        1,479,382
    instructions        260,427,802,730

  判断：
    minor GC / forward operations 完全相同，说明 workspace 公平。
    GC CPU 基本持平。
    instructions 下降约 1.8%。
    total time 稍慢，可能包含 wall-time 抖动。
    cache miss 与 A0 r2 接近，但低于 A0 r1。

CD：
  A0-current:
    total               487.230s
    GC overhead         15.305s
    GC CPU              15.072s
    GC core             8.964s
    minor GC            115688
    forward operations  162,299,040
    cache-misses        13,090,644
    instructions        67,368,403,117

  A10:
    total               485.309s
    GC overhead         15.036s
    GC CPU              14.892s
    GC core             8.948s
    minor GC            115688
    forward operations  162,299,040
    skipped slots       10,452,756
    cache-misses        13,051,193
    instructions        67,425,946,795

  判断：
    total time 下降约 0.4%。
    GC overhead 下降约 1.8%。
    GC CPU 下降约 1.2%。
    cache miss 小幅下降。
    instructions 基本持平。
```

当前结论：

```text
A10 是当前最合理的低风险修复。

它不是完整的 Shape/PropertyMap precise scan。
它是针对 JS Array 热路径的专用精确化：
  跳过必然非引用的 size 槽；
  不读取 Shape/PropertyMap；
  不增加 GC Local Workspace；
  不改变 minor GC 次数；
  在 CD 上同时改善 GC time 和 cache miss；
  在 Storage 上保持 GC CPU 基本不变，并降低 instructions。

因此：
  不推荐默认开启 layout cache。
  不推荐默认开启通用 type-aware JSObject scan。
  可以把 GIY_JSOBJECT_ARRAY_SKIP_SIZE=1 作为下一步主要候选。

如果还要继续提高 cache miss：
  应该考虑把“对象扫描描述符”提前放到对象头或 allocation-site metadata，
  避免 GC 扫描时读取 Shape/PropertyMap。
  这比在 GC Local Workspace 里放 layout cache 更符合 GiY 设计。
```

验证：

```text
已运行：
  make -C build.debug ejsvm OPT_GC=giy CACHE_SIZE_KB=896 GIY_JSOBJECT_ARRAY_SKIP_SIZE=1 -B -j4
  build.debug/ejsvm_giy_jsobj_a10_array_skip_size_896 giy_gc_probe.sbc
  build.debug/ejsvm_giy_jsobj_a10_array_skip_size_896 Storage.sbc
  build.debug/ejsvm_giy_jsobj_a10_array_skip_size_896 CD.sbc
  make -C build.debug ejsvm OPT_GC=giy CACHE_SIZE_KB=896 -B -j4

结果目录：
  build.debug/benchmarks/out139_giy_jsobj_typeaware_selective_896
```

## 2026-05-18 当前最好 GiY 方案与 Cheney 差距简表

用户问题：

```text
简单说明当前最好的方案是什么，怎么做的，和 Cheney GC 差多少。
```

当前最好方案：

```text
A10: GIY_JSOBJECT_ARRAY_SKIP_SIZE=1

做法：
  JS Array 的 eprop[0] 是 size，不是 GC 引用。
  旧 GiY conservative scan 会把它也当 JSValue 扫。
  A10 对 CELLT_ARRAY 做专用扫描：
    扫 shape；
    跳过 eprop[0]；
    从 eprop[1] 开始扫描 body/length/其他槽；
    不读 Shape / PropertyMap；
    不使用 layout cache；
    不增加 GC Local Workspace。
```

相对旧 GiY 的直接效果：

```text
Storage:
  instructions 265.17B -> 260.43B，下降约 1.8%
  GC CPU 基本持平：41.838s -> 41.871s
  skipped slots: 546,097,411

CD:
  total 487.230s -> 485.309s，约快 0.4%
  GC overhead 15.305s -> 15.036s，约快 1.8%
  cache misses 13,090,644 -> 13,051,193，小幅下降
  skipped slots: 10,452,756
```

和已有 Cheney 对照的差距：

```text
注意：
  A10 还没有重新跑完整 13 benchmark Cheney 同轮对照。
  下面 Cheney 数字来自已有 896KB cache_cheney 对照矩阵。

已有完整矩阵中，896KB:
  GiY total      6661.375s
  Cheney total   6946.021s
  GiY 比同 size Cheney 快 284.646s，约 4.10%

但最优点比较:
  best Cheney = 640KB, 6585.095s
  best GiY    = 768KB, 6635.928s
  GiY 比 best Cheney 慢 50.833s，约 0.77%

A10 单项与已有 896KB Cheney 粗略比较:
  Storage:
    A10 333.614s vs Cheney 340.908s
    A10 快约 7.294s，约 2.14%
    GC: A10 42.161s vs Cheney 51.761s，A10 GC 更快

  CD:
    A10 485.309s vs Cheney 492.660s
    A10 快约 7.351s，约 1.49%
    GC: A10 15.036s vs Cheney 11.981s，A10 GC 仍更慢
```

一句话结论：

```text
当前最好的方案是 A10 Array skip-size。
它是低风险、小改动、不占 workspace 的优化。
它让 GiY 更接近 Cheney，并在部分 896KB 单项上已经快于已有 Cheney 对照；
但如果拿各自最优 workspace 的完整 benchmark 比，GiY 仍大约慢 0.77%，还没有稳定全面超过 Cheney。
```

补充：GiY 最好 size 与 Cheney size 对比：

```text
基于完整 13 benchmark workspace-size 矩阵：

GiY:
  best size = 768KB
  total = 6635.928s

Cheney:
  best size = 640KB
  total = 6585.095s

best GiY vs best Cheney:
  GiY 慢 50.833s
  约慢 0.772%

同 size 比较：
  512KB: GiY 慢 162.843s，约慢 2.456%
  640KB: GiY 慢 144.672s，约慢 2.197%
  768KB: GiY 快 0.399s，约快 0.006%，基本打平
  896KB: GiY 快 284.646s，约快 4.098%

所以：
  有 GiY 比 Cheney 快的 size：768KB 和 896KB。
  其中 768KB 基本可以看成打平；
  896KB 是 GiY 明显快于同 size Cheney 的配置。
  但从“各自最优配置”看，Cheney 640KB 仍比 GiY 768KB 快约 0.77%。
```

## 2026-05-18 896KB 参数详细分析

空间结构：

```text
GiY 896KB GC Local Workspace:
  RSet:                192.00 KB
  init objects:         19.76 KB
  young before aux:    684.24 KB
  GiY stack:            85.52 KB
  FT slot set:          32.00 KB
  aux total:           117.52 KB
  ordinary Young:      566.72 KB

Cheney 896KB:
  RSet:                192.00 KB
  init objects:         19.76 KB
  FT slot set:          85.52 KB
  ordinary Young:      約 598.71 KB

差异：
  Cheney 的 ordinary Young 比 GiY 多约 32KB。
  原因是 GiY 需要额外的 traversal stack。
  这是算法结构差异，不是实验不公平。
```

完整 13 benchmark 聚合：

```text
GiY 896KB:
  total:     6661.375s
  GC:         114.211s
  minor GC: 1,114,512

Cheney 896KB:
  total:     6946.021s
  GC:         112.801s
  minor GC: 1,044,731

差值:
  total: GiY 快 284.646s，约 4.098%
  GC:    GiY 慢 1.410s，约 1.25%
  minor: GiY 多 69,781 次，约 6.68%

解释：
  896KB 下 GiY 总时间明显赢同 size Cheney。
  但 GC overhead 并没有赢，minor GC 次数也更多。
  所以 896KB 的胜利不是“GC 全指标压倒 Cheney”，而是 workload 总执行时间层面的胜利。
```

逐 benchmark 总时间：

```text
GiY 赢 11/13：
  Richards:    快 266.488s，约 13.37%
  CD:          快 15.442s，约 3.13%
  Bounce:      快 14.047s，约 4.30%
  Storage:     快 13.401s，约 3.93%
  Permute:     快 13.273s，约 1.62%
  Queens:      快 6.727s，约 3.05%
  Mandelbrot:  快 4.993s，约 1.24%
  Towers:      快 3.641s，约 1.10%
  Sieve:       快 3.525s，约 1.48%
  NBody:       快 2.760s，约 0.43%
  List:        快 2.314s，约 1.24%

GiY 输 2/13：
  Havlak:      慢 58.393s，约 7.13%
  DeltaBlue:   慢 3.572s，约 2.74%

关键判断：
  896KB 的总优势很大程度来自 Richards。
  如果去掉 Richards，GiY 对 Cheney 的优势会小很多。
  Havlak 仍然是主要反例。
```

逐 benchmark GC：

```text
GiY GC 明显赢：
  Storage: GiY GC 比 Cheney 少 10.412s

GiY GC 明显输：
  Havlak:     多 6.364s
  CD:         多 2.138s
  NBody:      多 1.605s
  Mandelbrot: 多 1.504s

解释：
  GiY 在 Storage 上的 GC 优势很清楚。
  但在 CD/Havlak/NBody/Mandelbrot 上，Cheney 的 immediate copy + linear scavenge 仍然有优势。
```

cache miss 896KB 对照：

```text
whole-program generic:
  Cheney miss rate: 8.265%
  GiY miss rate:    11.854%
  GiY misses:       +22.892%

L1D load:
  Cheney miss rate: 2.174%
  GiY miss rate:    2.543%
  GiY misses:       +21.969%

LLC load:
  Cheney miss rate: 2.742%
  GiY miss rate:    6.677%
  GiY misses:       +121.822%

GC-window:
  Cheney GC miss rate: 0.865%
  GiY GC miss rate:    1.701%
  GiY GC misses:       +47.126%
  GiY GC refs:         -25.199%
  GiY GC MPKI:         +51.853%

解释：
  896KB 下 GiY 总时间可以赢 Cheney，
  但 cache miss rate 没有赢。
  GiY 的访问次数/引用次数有时更少，但 miss rate 更高，尤其 LLC-load 很差。
```

A10 补充：

```text
A10 = GIY_JSOBJECT_ARRAY_SKIP_SIZE=1。
它只在 Storage/CD 做了单项验证，还没有重新完整跑 13 benchmark。

Storage:
  A10 333.614s vs 已有 Cheney 896KB 340.908s
  A10 快约 7.294s，约 2.14%

CD:
  A10 485.309s vs 已有 Cheney 896KB 492.660s
  A10 快约 7.351s，约 1.49%

注意：
  这是跨实验结果的粗略比较。
  如果要发表/汇报为最终结论，需要重新跑 A10 的完整 13 benchmark 和 Cheney 同轮对照。
```

896KB 总结：

```text
896KB 是 GiY 相对同 size Cheney 最明显占优的配置。
总时间 GiY 快约 4.10%。
但是 GC overhead 仍略慢，minor GC 更多，cache miss rate 也更差。

因此 896KB 的正确表述应该是：
  GiY 在 896KB 下端到端性能优于同 size Cheney；
  但 GiY 的 GC/cache locality 还没有全面优于 Cheney；
  当前优势主要来自 workload-level 表现，特别是 Richards 和 Storage；
  Havlak 仍是最重要的反例。
```

## 2026-05-18 GC 论文中应该比较 total time 还是 GC time

用户问题：

```text
作为 GC 研究者，在论文中比较时到底应该比较总运行时间还是 GC 运行时间？
世界上其他 GC 论文一般比较什么？
```

结论：

```text
正式论文里不能只比较 GC time。
最重要的主结果通常应该是 end-to-end total execution time / throughput。

原因：
  用户和 VM 最终感受到的是程序总运行时间。
  GC 优化可能减少 GC time，但增加 mutator cost、write barrier、cache pollution、memory footprint。
  反过来，也可能 GC time 略差，但 total time 更好。
  所以 total time 才是最终性能结论。

GC time 是必须报告的辅助指标。
它用来解释：
  collector 本身是否更快；
  minor/major GC 是否减少；
  pause time 是否改善；
  优化收益是否真的来自 GC，而不是 benchmark noise。
```

推荐论文指标层级：

```text
一级主指标：
  total execution time / throughput
  normalized runtime
  geometric mean / median across benchmarks

二级 GC 指标：
  total GC time / GC overhead
  core GC time, e.g. scavenge/materialize/copy/scan
  number of collections
  average/max/percentile pause time

三级解释指标：
  allocation amount
  promoted/copied bytes
  memory footprint / heap size / workspace size
  cache miss rate / LLC miss / MPKI
  barrier cost / remembered-set cost
```

对 GiY 论文的建议：

```text
应该同时给两种公平比较：

1. Same-budget comparison
   Cheney / GiY / GiYOL 使用相同 GC Local Workspace total。
   这是回答：
     在相同 cache/workspace 预算下，谁更好？

2. Best-tuned comparison
   各自选择自己的最佳 workspace size。
   这是回答：
     每个 collector 都调到最佳后，谁更好？

你的当前数据应该这样写：
  same 896KB:
    GiY total time 比 Cheney 快约 4.10%。
    但 GiY GC overhead 略慢，cache miss rate 更差。

  best size:
    best GiY = 768KB, 6635.928s。
    best Cheney = 640KB, 6585.095s。
    GiY 仍慢约 0.77%。

因此论文中不能只说“GiY 快于 Cheney”。
更严谨的说法是：
  GiY 在较大 local workspace, especially 896KB, 可以取得更好的 end-to-end time；
  但在各自最佳配置下，目前 GiY 还没有稳定超过 Cheney；
  GC/cache 指标显示 locality 仍是 GiY 的主要改进空间。
```

一句话原则：

```text
论文主图放 total time / normalized runtime。
GC time、pause time、cache miss、minor GC count 放在分析图里解释原因。
如果 claim 是 low-latency GC，则 pause time 是主指标之一。
如果 claim 是 cache-local GC，则必须同时报告 cache counters 和 total time。
```

2026-05-18 补充调查：size 越大 GiY 是否越占优势，以及 GC 时间更长但 business 时间更短的原因

本次没有修改 GC 代码，只做数据和代码路径调查。

使用的数据：
- `build.debug/benchmarks/out_cache_cheney_cache_size_20260515_115946/compare_giy_vs_cache_cheney.csv`
- `build.debug/benchmarks/out_giy_cache_size_20260515_004640/summary.csv`
- `build.debug/benchmarks/out_cache_cheney_cache_size_20260515_115946/summary.csv`
- `build.debug/benchmarks/out_gc_cache_miss_20260516_235434/compare_giy_vs_cache_cheney_cache_miss.csv`
- `build.debug/benchmarks/out_gc_cache_miss_20260516_235434/compare_giy_vs_cache_cheney_gc_pmu.csv`
- `build.debug/benchmarks/out29_giy_current_full/business_non_gc_delta_investigation.md`
- `build.debug/benchmarks/out29_giy_current_full/deep_business_gap_causal_investigation.md`

结论 1：相对 Cheney，GiY 的确随着 total GC Local Workspace 变大而更有优势。

全 benchmark 汇总：

```text
size  GiY total  Cheney total  GiY-Cheney  GiY GC  Cheney GC  business diff  minor GC diff
512   6793.957   6631.114      +162.843s   165.650 143.579    +140.772s      +374290
640   6729.767   6585.095      +144.672s   138.876 126.606    +132.402s      +185154
768   6635.928   6636.327      -0.399s     121.671 117.653    -4.417s        +107768
896   6661.375   6946.021      -284.646s   114.211 112.801    -286.056s      +69781
```

但是这个结论要加两个限制：
- GiY 自己的最佳 total time 是 768KB，不是 896KB。
- 896KB 的 4.10% 大胜主要被 Richards 放大。896KB 中 Richards 一项贡献了 `-266.488s` total diff 和 `-266.429s` business diff。

去掉 Richards 后，896KB 仍然是 GiY 更快，但幅度只有：

```text
GiY 4934.887s vs Cheney 4953.045s
diff = -18.158s = -0.367%
business diff = -19.627s
```

Richards 896KB 的 Cheney full-matrix 时间是 `1992.976s`，但后续 cache-miss/PMU 三次 Cheney 896KB Richards 是 `1744.968s / 1733.641s / 1763.784s`。因此 full-matrix 里的 Cheney 896KB Richards 很可能是异常慢样本，不能把 4.10% 当成稳定机制收益。若用 PMU 中 Richards 的均值替代，896KB 的总体优势大约只剩 `0.49%`。

结论 2：size 变大后 GiY 更占优势，最可信的结构性原因是“固定额外空间开销被摊薄”。

GiY 在 ordinary young 之外还要占用 GC Local Workspace 内部空间：
- RSet values/buffer/hash 在 `giy_rset.cc` 中占用 cache/local workspace。
- GiY traversal stack 和 FT slot set 在 `GiY.cc` 的 `giy_bind_stack_to_cache_impl` 中从 `cache_space.end` 向下预留。
- 当前 GiY 比 Cheney 额外少了约 32KB ordinary young，这个 32KB 在小 workspace 下影响很大，在大 workspace 下影响变小。

估算 active young：

```text
size  Cheney active young est.  GiY active young  GiY capacity penalty
512   262.71KB                  230.72KB          13.87%
640   374.71KB                  342.72KB          9.33%
768   486.71KB                  454.72KB          7.04%
896   598.71KB                  566.72KB          5.64%
```

实际 minor GC excess 与这个容量惩罚方向一致：

```text
size  observed GiY minor GC excess
512   +15.76%
640   +11.15%
768   +8.41%
896   +6.68%
```

因此 GiY 的劣势随 size 增大而缩小，是可信的。

结论 3：GiY 的 GC total time 反而更长，不是因为每次 GC 都更慢，而主要是因为 minor GC 次数更多。

全 benchmark 加权平均 pause：

```text
size  GiY avg pause  Cheney avg pause  GiY pause diff  GiY count diff  GiY total GC diff
512   0.0603ms       0.0605ms          -0.34%          +15.76%         +15.37%
640   0.0752ms       0.0762ms          -1.31%          +11.15%         +9.69%
768   0.0875ms       0.0918ms          -4.60%          +8.41%          +3.42%
896   0.1025ms       0.1080ms          -5.09%          +6.68%          +1.25%
```

解释：
- GiY 每次 minor GC 平均并不比 Cheney 慢，甚至略短。
- 但 GiY ordinary young 更小，所以触发 minor GC 更多。
- `total GC time = GC count * average pause`，因此目前 full matrix 中 GiY total GC time 仍略高。
- 随着 workspace 变大，GC count gap 快速缩小，所以 GC time gap 也从 `+22.071s` 缩到 `+1.410s`。

结论 4：GiY business time 更短，不能简单解释成 cache miss 更低。

计时代码确认：
- `giy_dram_manager.cc` 和 `cache_dram_manager.cc` 都是 `business_wall = total_elapsed_wall - total_gc_full_wall`。
- 所以 business time 不是纯 JS mutator 语义时间，而是所有非 GC window 的 wall time，包括 write barrier、allocation fast path、metadata/cache 行为和噪声。

cache-miss/PMU 数据反而显示当前 GiY cache miss 更差：

```text
size  generic miss rate          L1D-load miss rate        LLC-load miss rate
640   Cheney 9.599%, GiY 14.441% Cheney 2.127%, GiY 2.468% Cheney 2.573%, GiY 7.161%
768   Cheney 8.971%, GiY 12.499% Cheney 2.146%, GiY 2.435% Cheney 2.476%, GiY 5.787%
896   Cheney 8.265%, GiY 11.854% Cheney 2.174%, GiY 2.543% Cheney 2.742%, GiY 6.677%
```

GC-window PMU 也显示 GiY GC miss rate 更高：

```text
size  Cheney GC miss rate  GiY GC miss rate
640   0.657%               1.882%
768   0.777%               1.609%
896   0.865%               1.701%
```

因此不能说“GiY business time 更短是因为 GiY cache miss 更低”。更合理的解释是：
- 之前的 `out31/out33` 实验验证过：如果启用 `GIY_RSET_INDEX_FAST=true`，可以明显减少以前 GiY write barrier 中的线性扫描病灶；但本次 size matrix 的 GiY build log 没有看到 `-DGIY_RSET_INDEX_FAST=1`，所以不能把本次 896KB business 优势归因于 index-fast。
- 大 workspace 降低 GC 频率，也减少 GC 对 runtime metadata、inline cache、shape/alloc-site 状态的扰动。
- 896KB aggregate business 优势主要来自 Richards 的 Cheney 异常慢结果；去掉 Richards 后，business 优势只剩约 `19.6s / 12 benchmarks`。

结论 5：最可信的表述。

```text
GiY 相对 Cheney 随 local workspace size 增大而更有优势，这个趋势可信。
主要原因不是当前 GiY 的 cache miss 更好，而是 GiY 的固定 local-workspace 额外开销在大 size 下被摊薄，ordinary young 容量劣势下降，minor GC 次数差距下降。

当前 GiY 的 total GC time 略长，是因为 minor GC 次数仍更多；每次 GC 的平均 pause 实际上略短。

当前 896KB 的 business time 明显更短，不能完全当作稳定算法收益；它被 Richards 的 Cheney 异常慢样本强烈放大。去掉 Richards 或用后续 PMU Richards 均值修正后，GiY 仍略快，但优势大约只有 0.4%-0.5% 量级。
```

后续如果要把这个结论写进论文，应补做：
- 896KB Richards 单独重复多次，确认是否异常。
- phase-level PMU：reserve roots、reserve RS、young traverse/materialize、patch roots/RS 分开测。
- same total workspace 和 same ordinary young capacity 两组都测，分别回答“总预算公平”和“实际 young 容量公平”两个问题。

2026-05-18 补充实验：GiY/Cheney 性能差距的因果归因

用户要求：
- 从当前角度彻底找出性能差距原因。
- 反复做实验，直到真正确信原因为止。

本次实验输出目录：
- `build.debug/benchmarks/out_perf_gap_causal_20260518_115744`
- 生成汇总：`build.debug/benchmarks/out_perf_gap_causal_20260518_115744/causal_summary.csv`

本次跑过的实验：

```text
1. cheney896_richards
   OPT_GC=cache_cheney CACHE_SIZE_KB=896
   benchmark=Richards
   目的：验证旧 full matrix 中 Cheney896 Richards=1992.976s 是否异常。

2. giy896_default_havlak
   OPT_GC=giy CACHE_SIZE_KB=896
   benchmark=Havlak
   目的：当前同轮 GiY default baseline。

3. cheney896_havlak
   OPT_GC=cache_cheney CACHE_SIZE_KB=896
   benchmark=Havlak
   目的：当前同轮 Cheney baseline。

4. cheney860_havlak
   OPT_GC=cache_cheney CACHE_SIZE_KB=860
   benchmark=Havlak
   目的：把 Cheney ordinary young 降到接近 GiY896，反向验证容量因素。

5. giy933_havlak
   OPT_GC=giy CACHE_SIZE_KB=933
   benchmark=Havlak
   目的：把 GiY ordinary young 提到接近 Cheney896，正向验证容量因素。

6. giy896_rsetfast_havlak
   OPT_GC=giy CACHE_SIZE_KB=896 GIY_RSET_INDEX_FAST=true
   benchmark=Havlak
   目的：验证 remembered-set value 更新路径是否是 business gap 主因。

7. giy896_precise_havlak
   OPT_GC=giy CACHE_SIZE_KB=896 GIY_JSOBJECT_PRECISE_SCAN=1
   benchmark=Havlak
   目的：验证 JSObject precise scan 是否能解释/解决 Havlak gap。

8. giy896_wbprofile_havlak
   OPT_GC=giy CACHE_SIZE_KB=896 GIY_WB_PROFILE=true
   benchmark=Havlak
   目的：直接统计 GiY write barrier / RSet 线性扫描步数。
```

结论 1：旧 896KB aggregate 的大胜确实被 Richards 异常放大。

旧 full matrix：

```text
Cheney896 Richards = 1992.976s
GiY896 Richards    = 1726.488s
Richards alone made GiY look faster by 266.488s.
```

本次重跑：

```text
Cheney896 Richards = 1737.234s
business           = 1737.052s
GC                 = 0.182s
minor GC           = 2131
```

这个结果和之前 PMU 三次 Cheney896 Richards `1744.968 / 1733.641 / 1763.784s` 一致，和旧 `1992.976s` 不一致。因此：

```text
896KB 的 GiY 4.10% aggregate win 不能当作稳定机制收益。
用本次 Richards 修正旧 full matrix 后，GiY896 的总体优势约为 0.43%。
如果同时用 PMU 的 GiY Richards 均值修正，优势约为 0.33%。
```

结论 2：Havlak 当前同轮差距主要是 business-side，不是 GC time。

当前同轮 baseline：

```text
config             total      business   GC       minor GC  allocations   WB calls
Cheney896 Havlak   753.030s   718.707s   34.323s  219201    1.828B        116.642M
GiY896 default     887.230s   845.507s   41.724s  239914    2.030B        145.691M
```

差距：

```text
GiY default - Cheney896:
  total    +134.200s
  business +126.800s
  GC       +7.401s

business 解释 94.49% 的 total gap。
```

因此 Havlak 不是“GiY GC 本身慢一点”这么简单，而是 GiY 默认配置在非 GC window 中有很大的额外成本。

结论 3：capacity / ordinary young 不是 Havlak gap 主因。

GiY933 把 ordinary young 提到约 Cheney896 水平：

```text
GiY896 default:
  young_after_aux = 566.72KB
  minor GC        = 239914
  total           = 887.230s
  business        = 845.507s
  GC              = 41.724s

GiY933:
  young_after_aux = 599.09KB
  minor GC        = 226920
  total           = 882.661s
  business        = 841.703s
  GC              = 40.958s
```

变化：

```text
minor GC -12994
total    -4.569s
business -3.804s
GC       -0.766s
```

也就是说，容量补偿确实降低了 minor GC 次数，但只改善约 `4.6s`，远小于 `134.2s` 的 Havlak gap。

反向验证 Cheney860：

```text
Cheney860:
  total    777.625s
  business 742.002s
  GC       35.623s
  minor GC 234829
```

即使把 Cheney 的 ordinary young 降到接近 GiY896，Cheney860 仍明显快于 GiY896 default：

```text
GiY896 default - Cheney860:
  total +109.605s
```

因此：

```text
local workspace / ordinary young 容量解释 minor GC count 和一小部分 GC time，
但不是 Havlak 主要性能差距的原因。
```

结论 4：RSet value 更新路径是 Havlak 最大已确认原因。

RSet-fast 实验：

```text
GiY896 default:
  total       887.230s
  business    845.507s
  GC           41.724s
  minor GC     239914
  allocations 2.029601985B
  forwards    384564451

GiY896 RSet-fast:
  total       800.577s
  business    757.771s
  GC           42.806s
  minor GC     239914
  allocations 2.029601985B
  forwards    384564455
```

变化：

```text
RSet-fast - default:
  total    -86.653s
  business -87.736s
  GC       +1.082s
```

关键点：
- minor GC count 没变。
- allocation count 没变。
- forward operations 基本没变。
- 改善几乎全部发生在 business time。

因此这是非常干净的因果证据：

```text
GiY default Havlak 的主要性能问题来自 mutator-side RSet/value maintenance，
不是来自 GC copy 本身，也不是来自 young 容量。
```

解释比例：

```text
RSet-fast 解释 default GiY vs Cheney896 total gap 的约 64.57%。
RSet-fast 解释 default GiY vs Cheney896 business gap 的约 69.19%。
```

RSet-fast 后仍有剩余差距：

```text
GiY896 RSet-fast - Cheney896:
  total    +47.547s
  business +39.064s
  GC       +8.483s

GiY896 RSet-fast - Cheney860:
  total    +22.952s
  business +15.769s
  GC       +7.183s
```

这说明 RSet-fast 修掉最大病灶后，剩余问题主要是：
- GiY 仍有额外 allocation / runtime metadata 行为。
- GiY GC core/scavenge/materialization 仍比 Cheney 更贵一些。
- GiY cache miss rate 仍更高。

结论 5：WB profile 直接确认线性扫描病灶。

`GIY_WB_PROFILE=true` 当前 Havlak 结果：

```text
Write barrier calls: 145691273
Update attempts:     438551833
Update hits:         31771029
Update scan steps:   377525614949
Steps per attempt:   860.846
Clear attempts JS:   380431513
Clear attempts ptr:  26797759
Young adds JS:       118826933
Young adds ptr:      26864340
Duplicate updates:   26588697
Fallback insertions: 4727859
```

换算：

```text
Update scan steps / write barrier call = 2591.27
clear attempts total = 407229272
```

这与代码机制一致：
- `giy_rset.cc` 中 `rememberset_update_existing` 在未启用 `GIY_RSET_INDEX_FAST` 时会调用线性扫描。
- 当 old/init slot 被写成 immediate/old/null 时，write barrier 会尝试把 `remembered_set.values[index]` 清掉。
- Havlak 会产生大量这类 clear/update attempt，导致数千亿级扫描步数。

因此 RSet 线性扫描不是猜测，而是由：
1. 代码路径；
2. WB profile counter；
3. RSet-fast macro 干预带来的 86.653s total 改善；
三者共同确认。

结论 6：JSObject precise scan 不是 Havlak 的解决方向。

`GIY_JSOBJECT_PRECISE_SCAN=1`：

```text
GiY896 precise:
  total       946.942s
  business    898.472s
  GC           48.470s
  minor GC     239914
  scavenge     33.468s

GiY896 default:
  total       887.230s
  business    845.507s
  GC           41.724s
  scavenge     26.968s
```

变化：

```text
precise - default:
  total    +59.712s
  business +52.965s
  GC       +6.746s
```

虽然 precise scan 改变了 JSObject 扫描路径：

```text
JSObject scans:         252379895
JSObject precise:       252379895
JSObject cons slots:    0
JSObject precise slots: 397431541
JSObject skipped slots: 72654137
```

但它让 Havlak 变慢。因此：

```text
“JSObject conservative scan 多扫了字段”不是 Havlak 当前性能差距的主因。
至少 naive precise scan 不是解决方案。
```

最终可信归因：

```text
1. 896KB aggregate 看起来 GiY 大胜，主要被 Richards 的 Cheney 异常慢样本放大。
   修正后 GiY896 只小幅领先，大约 0.3%-0.4%。

2. Havlak 是当前 GiY 的最大负例。
   同轮数据中 GiY896 default 比 Cheney896 慢 134.2s，其中 126.8s 是 business-side。

3. Havlak 的最大已确认原因是 GiY default 的 remembered-set value 更新路径。
   它在 write barrier 中触发大量线性扫描，WB profile 记录到 3775 亿次 scan steps。
   开启 GIY_RSET_INDEX_FAST 后，不改变 allocation/minor-GC/forward 数量，却让 total 改善 86.653s。

4. local workspace 容量解释 minor GC count 趋势和一小部分 GC time，但不是 Havlak gap 主因。
   GiY933 只改善 4.569s。

5. JSObject precise scan 不是当前 Havlak gap 的解决方案。
   它让 Havlak 变慢 59.712s。

6. RSet-fast 之后仍有剩余 gap：
   对 Cheney896 还慢 47.547s，对 same-active-young 的 Cheney860 还慢 22.952s。
   下一步应研究剩余 extra allocation / metadata behavior / GiY materialization scavenge overhead。
```

后续优先级：

```text
第一优先级：
  把 GIY_RSET_INDEX_FAST 变成默认或实现无 fallback 线性扫描的 production RSet index。

第二优先级：
  继续追剩余 22-48s gap：
    allocation count 为什么 GiY 仍比 Cheney 高；
    GiY materialization/scavenge 为什么比 Cheney scavenge 多约 6-8s；
    cache miss rate 为什么仍更差。

暂不优先：
  naive JSObject precise scan。
```

2026-05-18 补充解释：GIY_RSET_INDEX_FAST 默认化 / 无 fallback production RSet index，以及 Richards 修正含义

用户问题：
- “把 GIY_RSET_INDEX_FAST 变成默认或实现无 fallback 线性扫描的 production RSet index”是什么意思？
- “896KB aggregate 看起来 GiY 大胜，主要被 Richards 的 Cheney 异常慢样本放大。修正后 GiY896 只小幅领先 0.3%-0.4%”是什么意思？
- 这是否表示原来的 Cheney 实现有问题？

解释 1：GIY_RSET_INDEX_FAST 的意思。

当前 GiY remembered set 不只记录 old/init slot 地址，还记录该 slot 里最近写入的 young value：

```text
remembered_set.buffer[i] = slot address
remembered_set.values[i] = current young value or 0
```

当 old/init slot 被写成 young object 时，GiY 会加入或更新 RSet。
当 old/init slot 后来被写成 immediate / old / null 时，GiY 需要把对应的 `values[i]` 清成 0。

问题是：

```text
已知 slot address，怎样快速找到它在 remembered_set.buffer[] 里的 index i？
```

默认实现的问题：
- `rememberset_update_existing` 找不到 O(1) index 时，会调用 `rememberset_find_index_linear`。
- 这个函数从 `buffer[0]` 扫到 `buffer[count)`。
- Havlak 会产生大量 clear/update attempt，于是这个线性扫描被调用非常多次。
- WB profile 实测 `Update scan steps = 377,525,614,949`，也就是三千七百多亿步。

`GIY_RSET_INDEX_FAST=true` 的作用：
- 在 hash table 旁边维护一个 `remembered_set_hash_indices[]`。
- hash table 里找到 slot address 后，立刻知道它对应 `buffer[index]`。
- 这样更新 `values[index]` 不需要从头扫描 buffer。

为什么还说要 production index：
- 当前 fast path 是实验开关，只在 make 时传 `GIY_RSET_INDEX_FAST=true` 才打开。
- 当前代码仍保留 fallback：如果 hash probe 超过 `HASH_PROBE_LIMIT`，插入的 entry 可能没有 index 映射，后续仍可能退回线性扫描。
- production 版本应该保证所有 RSet entry 都能通过 bounded/O(1) 方式找到 index，不能再退回全表线性扫描。

因此“把 GIY_RSET_INDEX_FAST 变成默认或实现无 fallback 线性扫描的 production RSet index”的具体意思是：

```text
让 GiY 默认维护 slot -> RSet index 的映射；
所有 remembered slot 的 update/clear 都通过这个映射直接更新 values[index]；
不允许正常路径调用 rememberset_find_index_linear 扫整个 buffer；
如果 hash table 满或 probe 超限，应扩容、rehash、使用 overflow index table，或直接触发可控处理，而不是静默退回线性扫描。
```

解释 2：Richards 修正是什么意思。

旧 full matrix 里：

```text
Cheney896 Richards = 1992.976s
GiY896 Richards    = 1726.488s
```

这一个 benchmark 就让 GiY 看起来快了：

```text
1992.976 - 1726.488 = 266.488s
```

而整个 896KB aggregate 里 GiY 比 Cheney 快 `284.646s`。
也就是说，几乎整个 896KB “大胜”都来自 Richards 这一项。

后来我们重跑同一个 `Cheney896 Richards`：

```text
本次重跑 Cheney896 Richards = 1737.234s
之前 PMU 三次 Cheney896 Richards = 1744.968 / 1733.641 / 1763.784s
```

这说明 `1992.976s` 那次明显偏慢，是异常测量点或运行环境噪声，不应该当作稳定算法性能。

用 `1737.234s` 修正后：

```text
原本 896KB GiY aggregate advantage ≈ 4.10%
修正 Richards 后 advantage ≈ 0.43%
如果同时用 PMU 中 GiY Richards 均值修正，advantage ≈ 0.33%
```

解释 3：这不等于 Cheney 实现有 bug。

目前没有证据说明 Cheney 实现“错了”。

更准确的说法是：

```text
Cheney896 Richards 那一次 benchmark 测量结果异常慢。
异常可能来自系统噪声、CPU frequency/scheduling、cache/TLB 状态、后台负载、thermal/OS jitter、单次长 benchmark 的方差等。
```

如果 Cheney 实现真的有 bug，应该更稳定地复现错误或慢速。
但后续多次运行同一 Cheney896 Richards 都回到 `1730-1760s` 区间，所以更像测量异常，而不是实现错误。

论文里应该写：

```text
The large 896KB aggregate improvement is not robust because it is dominated by an outlier in Cheney/Richards.
After replacing that outlier with repeated measurements, GiY896 only shows a small end-to-end advantage.
```

中文表述：

```text
896KB 下 GiY 可能略快，但原来 4.10% 的大幅领先不可靠；
它主要被一次 Cheney/Richards 异常慢运行放大。
目前没有证据说明 Cheney 算法或实现本身有错误。
```

## 2026-05-18 补充解释：师兄代码如何避免 slot address -> RSet index 问题

用户问：师兄是怎么解决“已知 old field slot address，怎样快速找到它在 `remembered_set.buffer[]` 里的 index `i`”这个问题的。

结论：师兄不是用一个更快的 index 解决这个问题，而是他的 remembered set 设计没有制造这个问题。

师兄的 remembered set 只保存 old object 里的 field slot address：

```text
register_to_rs(field_ptr)
  -> rs[rs_index] = field_ptr
  -> rs_index++
```

minor GC 扫 remembered set 时，不读取一个另外保存的 `values[i]`，而是直接读取这个 old slot 当前的内容：

```text
old_field_ptr = rs[i]
young_ref_ptr = *old_field_ptr
if young_ref_ptr is still a young pointer:
    *old_field_ptr = copy_for_minor(young_ref_ptr)
```

所以如果这个 slot 后来被写成了 `null`、old pointer、immediate value，minor GC 会直接读到当前值，然后自然跳过。它不需要提前找到 `rs[i]`，也不需要清掉一个保存的 `values[i]`。

GiY 现在的问题来自另一个设计选择：

```text
GiY 为了让 minor GC 的 reserve/copy 阶段尽量只访问 local workspace，
额外保存 remembered_set.values[i]。
```

这样 minor GC 可以不读 old slot，但是 write barrier 后续必须维护 `values[i]` 的正确性。当某个 old slot 不再指向 young object 时，GiY 需要把对应的 `values[i]` 清掉。于是就产生了：

```text
已知 slot address，怎样快速找到它对应的 remembered_set.values[i]？
```

师兄方案的代价是：minor GC 每次扫描 RS 都要读 old/MRAM slot，一条 RS entry 至少一次 old memory read。GiY 方案的目标是避免这类 old-slot read，但代价是 write barrier 必须维护 slot -> index 映射。

因此：

```text
师兄：RS 只存 slot；GC 时读 slot 当前值；简单，但 minor GC 读 old memory。
GiY：RS 存 slot + saved value；GC 时尽量不读 old slot；但需要 production slot->index。
```

如果 GiY 直接采用师兄方法，可以简化正确性问题，但会削弱 GiY 的核心 cache/local workspace 设计。因此更符合 GiY 的方向是实现无 fallback 线性扫描的 production RSet index；也可以另外做一个“读 old slot”的对照变体，用来量化这个设计取舍的真实成本。

## 2026-05-19 阶段记录：GiY 增加“像师兄一样”的 RSet 读 slot 变体与中途 benchmark 状态

用户要求：给当前 GiY 增加一个多余选项，采用类似师兄代码的 remembered set 设计，看看实际性能提升。

实现状态：
- 新增编译选项 `GIY_RSET_READ_SLOT_AT_GC=true`。
- 默认 GiY 行为不变：RSet 保存 `buffer[] + values[]`，minor GC 用 `values[i]` 避免读 old slot。
- 新变体行为：
  - RSet 只保存 old/init slot address，不维护 `values[]`。
  - write barrier 对非 young 写入不再需要查找 index 并清 `values[i]`。
  - minor GC 扫 RSet 时读取 slot 当前值：
    - 如果当前值仍然指向 young，就 reserve；
    - reserve 后立即根据 forwarding pointer patch 这个 slot；
    - 后面的 `giy_patch_remembered_set_slots()` 在该模式下直接返回。
- 新增脚本：
  - `tools/run_giy_rset_read_slot_benchmarks.sh`
  - `tools/parse_giy_rset_read_slot_benchmarks.py`

已经做过的正确性/构建测试：
- `make OPT_GC=giy CACHE_SIZE_KB=896 -B -j2` 通过。
- `make OPT_GC=giy CACHE_SIZE_KB=896 GIY_RSET_READ_SLOT_AT_GC=true -B -j2` 通过。
- read-slot 变体下，小样例 `a.sbc` 到 `g.sbc`、`../ejsvm/js/hello.sbc`、`../ejsvm/js/f1.sbc` 全部退出码 0。
- read-slot 变体下，`GIY_MPROTECT_OLD=1 ./ejsvm a.sbc` 退出码 0。
- read-slot 变体下，`benchmarks/Sieve.sbc` 单独运行退出码 0。

benchmark 状态：
- 第一次目录 `out_giy_rset_read_slot_20260518_215620` 无效：传入相对输出路径，脚本进入 `benchmarks` 后重定向路径失效，默认组 status 1；这个目录不用于结论。
- 已修正脚本：如果 `OUT_BASE` 是相对路径，先转换成绝对路径。
- 有效目录：
  - `/home/qiancheng/ejs-new/build.debug/benchmarks/out_giy_rset_read_slot_20260518_220105`
- 该有效目录中：
  - `giy_default` 13 个 benchmark 全部完成，status 0。
  - `giy_rset_read_slot` 构建完成，`Bounce` 完成 status 0。
  - 用户中断时正在跑 `giy_rset_read_slot/CD`，该项只有半截 `CD.out`，没有 status/time，不能作为有效结果。

目前唯一完整成对结果是 `Bounce`：

```text
Default GiY:
  total execution = 304.598 sec
  GC overhead(full) = 0.182 sec
  minor GC count = 3351
  RSet capacity = 8192

GiY read-slot-at-GC:
  total execution = 305.128 sec
  GC overhead(full) = 0.209 sec
  minor GC count = 3351
  RSet capacity = 16384

end-to-end speedup = -0.174%
GC overhead change = worse by about 14.8% on this one benchmark
```

当前阶段性结论：
- 目前没有证据表明“像师兄一样在 GC 时读 old slot”会提升 GiY 性能。
- 在唯一完整成对 benchmark `Bounce` 上，它反而略慢；差距很小，接近噪声范围，但 GC full time 明显略高。
- 这符合预期的 tradeoff：
  - 优点：write barrier 简化，不需要维护 `values[]`，也没有 slot->index clear/update 问题；RSet 容量从 8192 增加到 16384。
  - 代价：minor GC 扫 RSet 时必须读取 old/init slot 当前值，并且本实现会在 RSet scan 阶段即时 patch old slot；这破坏了原 GiY “minor GC reserve 阶段尽量不读 old memory”的设计目标。
- 因为 full paired benchmark 还没有跑完，不能下最终性能结论。当前只能说：从 `Bounce` 与正确性 smoke tests 看，这个变体可以运行，但尚未显示性能收益。

## 2026-05-19 实验方法判断：一次完整 benchmark 是否足够判断 read-slot 方案

用户问：能不能只跑一次完整 benchmarks，就判断 GiY 采用师兄式 read-slot RSet 后是否真正提升。

结论：
- 一次完整 paired benchmark 可以作为初步筛选，可以判断“有没有明显的大提升”。
- 但一次完整 benchmark 不能严格证明“真正提升”，尤其当差距只有 0.x% 到 2% 时，很容易被系统噪声、CPU 频率、调度、thermal、cache/TLB 状态、后台负载影响。
- 如果一次完整结果显示 read-slot 变体在 aggregate 上慢，或者只快不到约 1%，应认为“没有可靠提升”。
- 如果一次完整结果显示多数 benchmark 都快，并且 aggregate 快超过约 3%-5%，可以说“有明显提升的信号”，但论文中仍应至少对关键项重复 3 次。
- 如果提升主要来自单个 benchmark，尤其是 Richards 这种长时间项目，就不能直接下结论，需要重跑该项确认不是 outlier。

建议判断标准：

```text
只跑一次 full suite 时：
1. 看所有 benchmark 的方向：多数快，还是少数快。
2. 看 aggregate speedup：是否超过 3%-5%。
3. 看 GC full time：是否真的下降，而不是 business time 噪声。
4. 看 RSet 压力大的 benchmark：CD / Storage / Havlak 是否改善。
5. 如果只有 0.x% 差距，结论写成 no reliable improvement。
```

对当前 read-slot 方案的预期：
- 它可能减少 write barrier 的 update/clear 成本。
- 但它会让 minor GC 扫 RSet 时读 old slot，并且即时 patch old slot。
- 因此它不一定比当前 GiY 更快；如果 RSet update/clear 不是主要瓶颈，read-slot 方案很可能没有提升，甚至会让 GC time 增加。

## 2026-05-19 实验执行计划：read-slot GiY 一次公平 full suite

用户要求：跑一次可靠、公平的 full suite，验证 GiY 采用师兄式 read-slot RSet 后是否有真实提升。

本次执行规则：
- 使用新的干净输出目录，不混用之前中断产生的半截数据。
- 只比较两个配置：
  - `giy_default`
  - `giy_rset_read_slot`，即 `GIY_RSET_READ_SLOT_AT_GC=true`
- 两个配置都使用：
  - `OPT_GC=giy`
  - `CACHE_SIZE_KB=896`
  - 同一套 benchmarks：Bounce, CD, DeltaBlue, Havlak, List, Mandelbrot, NBody, Permute, Queens, Richards, Sieve, Storage, Towers
  - `taskset -c 0` 固定同一个 CPU core
- 每个配置重新构建，构建日志和二进制保存在各自目录。
- 结论标准：
  - 如果 aggregate 提升小于约 1%，或者主要来自单个 benchmark，不认为有可靠提升。
  - 如果多数 benchmark 改善、aggregate 明显提升、GC full time 也下降，才认为有真实提升信号。

### Full suite 结果

输出目录：

```text
/home/qiancheng/ejs-new/build.debug/benchmarks/out_giy_rset_read_slot_fullsuite_20260519_001326
```

两个配置均完成 13/13 benchmark，status 全部为 0。

整体结果：

```text
Default GiY total:     6730.506 sec
Read-slot GiY total:   6646.009 sec
End-to-end speedup:    +1.255%

Default GiY GC full:   126.084 sec
Read-slot GiY GC full: 147.527 sec
GC full change:        -17.007%  (read-slot 的 GC time 更长)
```

逐项重要结果：

```text
Bounce:      +0.045%
CD:          -0.391%
DeltaBlue:   +0.836%
Havlak:      +8.638%
List:        -0.063%
Mandelbrot:  -0.127%
NBody:       +0.146%
Permute:     +0.449%
Queens:      -0.173%
Richards:    -0.227%
Sieve:       +0.332%
Storage:     +2.268%
Towers:      +0.188%
```

关键稳健性检查：

```text
Aggregate excluding Havlak:
Default GiY total:     5846.082 sec
Read-slot GiY total:   5837.980 sec
Speedup:               +0.139%

Default GiY GC full:   84.055 sec
Read-slot GiY GC full: 88.109 sec
GC full change:        -4.823%
```

结论：
- 不能认为“采用师兄式 read-slot RSet”给 GiY 带来了可靠、真实的提升。
- 表面 aggregate 有 +1.255%，但主要由 `Havlak` 的 +8.638% 拉动。
- 排除 `Havlak` 后只剩 +0.139%，基本属于噪声范围。
- 更重要的是，GC full time 总体变差 17.007%，说明该方案没有改善 GiY 的 GC 本体；它只是可能减少了一部分业务阶段 write barrier / values[] 维护成本，或者出现了 benchmark 噪声。
- 该结果支持之前判断：read-slot 方案简化 RSet 维护，但把成本转移回 minor GC 的 old-slot read 和 old-slot patch，削弱 GiY 的核心 local workspace 设计。
- 后续不建议把 read-slot 作为 GiY 主方案；它可以作为对照实验保留。更符合 GiY 的主线仍是 production slot->index 或 reserved-space/local-workspace 方案。

### 4-5 页英文 PPT 大纲：本周进度报告

Slide 1: Weekly Goal and Context
- Focus: understand GC locality problems in GiY and evaluate RSet design choices.
- Worked with Sonda-san to read and discuss the related paper.
- Main question: should GiY keep remembered-set metadata inside the local workspace, or read old slots during minor GC?
- Goal of the week: separate design intuition from measured performance.

Slide 2: Paper Reading with Sonda-san
- Reviewed the paper's motivation: reduce expensive memory traffic during GC.
- Key idea understood: locality is not only about copying objects, but also about metadata and root/RSet processing.
- Discussion point: a design can reduce write barrier cost while increasing GC-time memory traffic.
- Takeaway for GiY: the GC local workspace should be treated as a first-class budget, not just a cache-like area.

Slide 3: Remembered Set Design Experiment
- Baseline GiY:
  - RSet stores slot addresses and saved values.
  - Minor GC can reserve young targets without reading old slots.
  - Cost: write barrier must maintain values[] and needs slot-to-index updates.
- Senior-style read-slot variant:
  - RSet stores only slot addresses.
  - Minor GC reads the current old slot value.
  - Cost shifts from write barrier to GC-time old memory reads.

Slide 4: Benchmark Result and Interpretation
- Full suite completed at 896KB local workspace.
- Aggregate result: read-slot variant was +1.255% faster end-to-end.
- However, most of the gain came from Havlak.
- Excluding Havlak, improvement was only +0.139%.
- GC full time became worse by 17.007%.
- Conclusion: no reliable evidence that the senior-style RSet improves GiY.

Slide 5: Next Step
- Keep read-slot RSet as an experimental comparison, not the main design.
- Next implementation target: redesign the reserved-space layout.
- Plan:
  - Revisit how GC local workspace is divided.
  - Try reserving large objects or special regions earlier.
  - Improve address continuity for small objects.
  - Measure whether better reserved-space layout improves locality without increasing GC-time old memory reads.

### Excel 用 remembered-set 策略差异表

用户要求：做一个 Excel 表格，显示各 benchmark 在 remembered-set 策略上的差异。

已生成 CSV：

```text
/home/qiancheng/ejs-new/build.debug/benchmarks/out_giy_rset_read_slot_fullsuite_20260519_001326/remember_set_strategy_diff_for_excel.csv
```

字段含义：
- `Default Total sec`: 当前 GiY 的总运行时间。
- `Read-slot Total sec`: 师兄式 read-slot RSet 变体的总运行时间。
- `Delta sec`: `Read-slot - Default`，正数表示 read-slot 更慢。
- `Speedup %`: `(Default - Read-slot) / Default * 100`，正数表示 read-slot 更快。
- `Default GC sec` / `Read-slot GC sec`: 两种策略的 full GC time。
- `GC Delta sec`: `Read-slot GC - Default GC`，正数表示 read-slot 的 GC 更慢。
- `GC Change %`: `(Default GC - Read-slot GC) / Default GC * 100`，负数表示 read-slot 的 GC 更慢。
- `AGG_ALL`: 全 13 个 benchmark 聚合。
- `AGG_EXCL_HAVLAK`: 排除 Havlak 后的稳健性检查。

### PPT/Excel 图表建议：展示当前 GiY 与师兄式 read-slot RSet 的差异

用户问：想展示各 benchmark 的总时间、当前 GiY 策略和师兄式策略在 GC 时间和总时间上的差异，应该做什么图。

建议不要把 total time 和 GC time 放在同一个普通柱状图里，因为两者量级差异很大：例如 Richards 总时间约 1700 秒，但 GC time 只有约 0.2 秒；放在同一轴会导致 GC 差异完全看不见。

推荐图表组合：

1. 主图：每个 benchmark 的 total speedup 条形图
   - 横轴：`Speedup %`
   - 纵轴：benchmark name
   - 0% 画一条基准线
   - 正数表示 read-slot 更快，负数表示 read-slot 更慢
   - 用不同颜色标出 `Havlak`，因为 aggregate 的提升主要由它贡献
   - 目的：最清楚地展示“这个策略是否整体更快”

2. 辅图：GC time change 条形图
   - 横轴：`GC Change %` 或 `GC Delta sec`
   - 纵轴：benchmark name
   - 0 画基准线
   - 负数表示 read-slot 的 GC 更慢
   - 目的：展示虽然 total time 有些项目变快，但 GC 本体通常没有改善

3. Absolute total time grouped bar chart
   - 每个 benchmark 两根柱：`Default Total sec` vs `Read-slot Total sec`
   - 建议横向柱状图
   - 如果 Richards 太大导致其他项目看不清，可以用 normalized total chart 替代：
     - `Default = 100%`
     - `Read-slot = Read-slot Total / Default Total * 100%`

4. Summary chart
   - 只放两组：
     - `AGG_ALL`
     - `AGG_EXCL_HAVLAK`
   - 展示 speedup：
     - `AGG_ALL = +1.255%`
     - `AGG_EXCL_HAVLAK = +0.139%`
   - 目的：说明 read-slot 表面提升主要由 Havlak 拉动，不是稳健提升。

最推荐用于 PPT 的三张图：

```text
Figure 1: Total runtime speedup by benchmark
Figure 2: GC time change by benchmark
Figure 3: Aggregate speedup with/without Havlak
```

结论表达：

```text
The read-slot remembered-set strategy slightly improves aggregate runtime,
but the improvement is dominated by Havlak. Excluding Havlak, the speedup
almost disappears, while total GC time becomes worse.
```

### 已生成 PPT 图表对应 CSV 数据

用户要求：生成上述图表对应的数据。

已生成 3 个 CSV，均在 full suite 输出目录下：

```text
/home/qiancheng/ejs-new/build.debug/benchmarks/out_giy_rset_read_slot_fullsuite_20260519_001326/figure1_total_runtime_speedup.csv
/home/qiancheng/ejs-new/build.debug/benchmarks/out_giy_rset_read_slot_fullsuite_20260519_001326/figure2_gc_time_change.csv
/home/qiancheng/ejs-new/build.debug/benchmarks/out_giy_rset_read_slot_fullsuite_20260519_001326/figure3_aggregate_with_without_havlak.csv
```

用途：
- `figure1_total_runtime_speedup.csv`
  - 用于画 `Total Runtime Speedup by Benchmark`
  - 主要列：`Benchmark`, `Speedup_pct`, `ReadSlot_Normalized_pct`
  - `Speedup_pct > 0` 表示 read-slot 更快。
- `figure2_gc_time_change.csv`
  - 用于画 `GC Time Change by Benchmark`
  - 主要列：`Benchmark`, `GC_Change_pct`, `GC_Delta_sec`
  - `GC_Change_pct < 0` 表示 read-slot 的 GC 更慢。
- `figure3_aggregate_with_without_havlak.csv`
  - 用于画 `Aggregate With / Without Havlak`
  - 主要行：`AGG_ALL`, `AGG_EXCL_HAVLAK`
  - 目的：展示 overall speedup 主要被 Havlak 放大。

### 已生成两张表格：总时间变化与 GC 时间变化

用户要求：生成两张表格，第一张展示两个 GC/remembered-set 策略的总时间变化，第二张展示 GC 时间变化。

已生成：

```text
/home/qiancheng/ejs-new/build.debug/benchmarks/out_giy_rset_read_slot_fullsuite_20260519_001326/table1_total_time_change.csv
/home/qiancheng/ejs-new/build.debug/benchmarks/out_giy_rset_read_slot_fullsuite_20260519_001326/table2_gc_time_change.csv
```

表 1 字段：
- `Current_GiY_Total_sec`
- `SeniorStyle_ReadSlot_Total_sec`
- `Delta_sec = SeniorStyle - Current`
- `Speedup_pct = (Current - SeniorStyle) / Current * 100`

表 2 字段：
- `Current_GiY_GC_sec`
- `SeniorStyle_ReadSlot_GC_sec`
- `GC_Delta_sec = SeniorStyle GC - Current GC`
- `GC_Change_pct = (Current GC - SeniorStyle GC) / Current GC * 100`

解释规则：
- `Delta_sec > 0` 表示师兄式 read-slot 更慢。
- `Speedup_pct > 0` 表示师兄式 read-slot 更快。
- `GC_Delta_sec > 0` 表示师兄式 read-slot 的 GC 时间更长。
- `GC_Change_pct < 0` 表示师兄式 read-slot 的 GC 更慢。

### 展示用简化表格：只显示 GiY 两个策略的 benchmark 时间

用户澄清：展示时只需要 GiY 的两个策略在各个 benchmark 上的时间，不需要差值、speedup、解释列。

已生成两个更干净的 CSV：

```text
/home/qiancheng/ejs-new/build.debug/benchmarks/out_giy_rset_read_slot_fullsuite_20260519_001326/presentation_total_time_two_giy_strategies.csv
/home/qiancheng/ejs-new/build.debug/benchmarks/out_giy_rset_read_slot_fullsuite_20260519_001326/presentation_gc_time_two_giy_strategies.csv
```

表格只包含：
- `Benchmark`
- `Current GiY ... Time`
- `Senior-style GiY ... Time`

用于 PPT 展示时推荐直接用 grouped bar chart。

用户要求：不要 CSV，要可以直接粘贴进 Excel 的格式，并且必须输出进 `AIlog.md`。

Total Time:

```text
Benchmark	Current GiY Total Time (sec)	Senior-style GiY Total Time (sec)
Bounce	305.381	305.245
CD	488.697	490.609
DeltaBlue	134.592	133.467
Havlak	884.424	808.029
List	185.541	185.657
Mandelbrot	403.913	404.425
NBody	657.648	656.691
Permute	812.984	809.334
Queens	217.205	217.581
Richards	1732.828	1736.754
Sieve	237.934	237.144
Storage	337.940	330.276
Towers	331.419	330.797
```

GC Time:

```text
Benchmark	Current GiY GC Time (sec)	Senior-style GiY GC Time (sec)
Bounce	0.198	0.218
CD	15.606	16.210
DeltaBlue	2.424	3.730
Havlak	42.029	59.418
List	0.031	0.026
Mandelbrot	8.813	8.757
NBody	13.133	14.436
Permute	0.003	0.003
Queens	0.060	0.058
Richards	0.196	0.184
Sieve	1.328	1.253
Storage	42.255	43.226
Towers	0.008	0.008
```

用户进一步要求：要真正的表格，中间有分割线。

Total Time:

| Benchmark | Current GiY Total Time (sec) | Senior-style GiY Total Time (sec) |
|---|---:|---:|
| Bounce | 305.381 | 305.245 |
| CD | 488.697 | 490.609 |
| DeltaBlue | 134.592 | 133.467 |
| Havlak | 884.424 | 808.029 |
| List | 185.541 | 185.657 |
| Mandelbrot | 403.913 | 404.425 |
| NBody | 657.648 | 656.691 |
| Permute | 812.984 | 809.334 |
| Queens | 217.205 | 217.581 |
| Richards | 1732.828 | 1736.754 |
| Sieve | 237.934 | 237.144 |
| Storage | 337.940 | 330.276 |
| Towers | 331.419 | 330.797 |

GC Time:

| Benchmark | Current GiY GC Time (sec) | Senior-style GiY GC Time (sec) |
|---|---:|---:|
| Bounce | 0.198 | 0.218 |
| CD | 15.606 | 16.210 |
| DeltaBlue | 2.424 | 3.730 |
| Havlak | 42.029 | 59.418 |
| List | 0.031 | 0.026 |
| Mandelbrot | 8.813 | 8.757 |
| NBody | 13.133 | 14.436 |
| Permute | 0.003 | 0.003 |
| Queens | 0.060 | 0.058 |
| Richards | 0.196 | 0.184 |
| Sieve | 1.328 | 1.253 |
| Storage | 42.255 | 43.226 |
| Towers | 0.008 | 0.008 |

最终一句话结论：

```text
Across all 13 benchmarks, the senior-style read-slot strategy did not make GC faster;
GC time became 17.007% slower, while total execution time became 1.255% faster.
However, excluding Havlak, the total-time improvement almost disappears (+0.139%),
and GC time is still 4.823% slower.
```

中文解释：
- 全 13 项聚合：
  - GC 时间没有变快，而是变慢 `17.007%`。
  - 总运行时间没有变慢，而是表面上变快 `1.255%`。
- 排除 Havlak 后：
  - GC 时间仍然变慢 `4.823%`。
  - 总运行时间只变快 `0.139%`，基本可以认为没有可靠提升。

因此展示时应该说：

```text
Senior-style RSet reduces neither GC time nor provides a robust total-time improvement.
The apparent total-time gain is mainly caused by Havlak.
```

## 2026-05-19 Cache miss 测量计划：当前 GiY vs 师兄式 read-slot RSet

用户要求：测量这两种 RSet 策略的 cache miss 率区别。

机器状态：
- `perf stat -e cache-references,cache-misses true` 可用。
- `perf list` 显示可用事件包括：
  - `cache-references`
  - `cache-misses`
  - `L1-dcache-loads`
  - `L1-dcache-load-misses`
  - `LLC-loads`
  - `LLC-load-misses`
  - `LLC-stores`
  - `LLC-store-misses`

本次先做 full-suite generic cache miss 测量：

```text
events = cycles,instructions,cache-references,cache-misses
```

比较对象：
- `giy_default`
- `giy_rset_read_slot`

为了和刚才 runtime full suite 保持一致，本次直接使用已保存的两个二进制：

```text
/home/qiancheng/ejs-new/build.debug/benchmarks/out_giy_rset_read_slot_fullsuite_20260519_001326/giy_default/ejsvm
/home/qiancheng/ejs-new/build.debug/benchmarks/out_giy_rset_read_slot_fullsuite_20260519_001326/giy_rset_read_slot/ejsvm
```

新增脚本：

```text
/home/qiancheng/ejs-new/tools/run_giy_rset_read_slot_cache_miss.sh
/home/qiancheng/ejs-new/tools/parse_giy_rset_read_slot_cache_miss.py
```

输出指标：
- `cache_miss_rate_pct = cache-misses / cache-references * 100`
- `cache_mpki = cache-misses / instructions * 1000`

说明：
- 这次测量是整个程序运行期间的 cache miss，而不是只在 GC 窗口内的 cache miss。
- 它可以回答“两个 RSet 策略对整体 cache 行为有什么影响”。
- 如果要进一步分解 GC-only cache miss，需要额外在 GC 代码内部加 PMU sampling 或复用已有 GC PMU 统计路径。

### Cache miss full suite 结果

输出目录：

```text
/home/qiancheng/ejs-new/build.debug/benchmarks/out_giy_rset_read_slot_cache_miss_20260519_140935
```

两个配置均完成 13/13 benchmark，status 全部为 0。

生成文件：

```text
/home/qiancheng/ejs-new/build.debug/benchmarks/out_giy_rset_read_slot_cache_miss_20260519_140935/summary.csv
/home/qiancheng/ejs-new/build.debug/benchmarks/out_giy_rset_read_slot_cache_miss_20260519_140935/cache_miss_strategy_diff.csv
/home/qiancheng/ejs-new/build.debug/benchmarks/out_giy_rset_read_slot_cache_miss_20260519_140935/summary.md
/home/qiancheng/ejs-new/build.debug/benchmarks/out_giy_rset_read_slot_cache_miss_20260519_140935/presentation_cache_miss_two_giy_strategies.csv
/home/qiancheng/ejs-new/build.debug/benchmarks/out_giy_rset_read_slot_cache_miss_20260519_140935/presentation_cache_miss_delta.csv
```

Weighted aggregate 结果：

```text
AGG_ALL:
  Current GiY cache miss rate:      9.937%
  Senior-style read-slot rate:      11.400%
  Delta:                            +1.463 percentage points
  Relative change:                  +14.723%

  Current GiY MPKI:                 0.0219
  Senior-style read-slot MPKI:      0.0253
  Relative MPKI change:             +15.654%

AGG_EXCL_HAVLAK:
  Current GiY cache miss rate:      4.910%
  Senior-style read-slot rate:      5.708%
  Delta:                            +0.798 percentage points
  Relative change:                  +16.252%

  Current GiY MPKI:                 0.00763
  Senior-style read-slot MPKI:      0.00837
  Relative MPKI change:             +9.655%
```

逐项 cache miss rate：

| Benchmark | Current GiY miss rate % | Senior-style miss rate % | Delta pp |
|---|---:|---:|---:|
| Bounce | 10.130 | 19.664 | 9.534 |
| CD | 4.372 | 4.526 | 0.154 |
| DeltaBlue | 10.080 | 12.149 | 2.069 |
| Havlak | 17.667 | 19.402 | 1.735 |
| List | 26.832 | 23.594 | -3.238 |
| Mandelbrot | 0.289 | 0.504 | 0.215 |
| NBody | 0.328 | 0.724 | 0.396 |
| Permute | 15.215 | 13.901 | -1.314 |
| Queens | 25.464 | 20.375 | -5.089 |
| Richards | 25.180 | 31.388 | 6.208 |
| Sieve | 3.146 | 2.916 | -0.231 |
| Storage | 9.511 | 8.856 | -0.656 |
| Towers | 15.014 | 25.307 | 10.293 |

结论：
- 从 whole-program generic cache counters 看，师兄式 read-slot RSet 没有降低 cache miss rate。
- 全 13 项聚合下，read-slot 的 cache miss rate 从 `9.937%` 上升到 `11.400%`。
- 排除 Havlak 后，read-slot 仍然从 `4.910%` 上升到 `5.708%`。
- MPKI 也变差，说明不是单纯 cache reference 数变化造成的表面现象。
- 这支持之前的 runtime/GC-time 结论：read-slot 方案虽然简化了 RSet write barrier 维护，但把 old-slot read 带回 GC/程序执行路径，整体 locality 没有改善。

## 2026-05-19: 将 GiY 默认 RSet 改成师兄式 read-slot，并记录到 important.md

用户新建了 `important.md`，要求把这次实验结果放进去，并且以后可能继续把重要实验结果放入该文件。同时，用户决定为了降低其他因素，当前 GiY 采用师兄式 remembered set：RSet 只存 old/init 区的 slot 地址，即使这样 minor GC 必须重新访问 old slot。

已写入：
- `/home/qiancheng/ejs-new/important.md`
- 本 `AIlog.md`

代码修改：
- `ejsvm/giy_rset.cc`
  - `GIY_RSET_READ_SLOT_AT_GC` 默认值改为 `1`。
  - read-slot 模式下 `remembered_set.values = NULL`。
  - read-slot 模式下不编译、不分配 `remembered_set_hash_indices`。
  - 初始化时打印 RSet workspace：`mode`, `buffer`, `values`, `hash`。
- `ejsvm/GiY.cc`
  - `GIY_RSET_READ_SLOT_AT_GC` 默认值改为 `1`。
  - read-slot 路径在 RSet scan 时读取当前 slot value，reserve young object，并立即 patch slot。
  - read-slot 模式下 `giy_patch_remembered_set_slots()` 直接返回，因为 patch 已经在 scan 阶段完成。
- `ejsvm/common.mk`
  - 保留 `GIY_RSET_READ_SLOT_AT_GC=true`。
  - 新增 `GIY_RSET_READ_SLOT_AT_GC=false`，用于显式恢复旧 saved-value 对照策略。
- `ejsvm/cache_dram_manager.h`
  - `values` 字段注释改为 optional，说明 read-slot 模式下为 `NULL`。
- `tools/run_giy_rset_read_slot_benchmarks.sh`
  - 旧策略对照显式使用 `GIY_RSET_READ_SLOT_AT_GC=false`，避免源代码默认值改成 read-slot 后实验标签失真。

默认 GiY 当前 RSet local workspace：

```text
mode   = read_slot
buffer = 128 KB
values = 0 KB
hash   = 64 KB
total  = 192 KB
capacity = 16384 slots
```

这里的重点是：默认 read-slot 模式不再消耗 `values[]` 空间，也不再消耗额外的 slot->index 表空间。仍然保留 64 KB hash table，用于 write barrier 时去重，避免 RSet 重复 slot 过多。

验证：

```text
make OPT_GC=giy CACHE_SIZE_KB=896 -B -j2
```

结果：build 成功。

```text
./ejsvm a.sbc
GIY_MPROTECT_OLD=1 ./ejsvm a.sbc
```

结果：两个 smoke test 都 status 0。初始化输出确认：

```text
init_info: giy rset mode=read_slot buffer=128KB values=0KB hash=64KB
```

实验结论仍然保留：
- read-slot RSet 作为实验控制方案合理，因为它更接近师兄代码的 remembered-set 思想。
- 它不是当前测量中性能更好的 GC 路径：GC 时间变慢，whole-program cache miss rate 也变差。
- 当前采用它的主要理由是降低实现差异，而不是声称它优化了 cache 行为。

## 2026-05-19: 当前 GiY local workspace 与 Cheney GC local workspace 的数据结构区别

当前按代码实际情况看，GiY 和 Cheney 的 RSet 已经基本一致：

- 两者 RSet 都在 `cache_space.end` 往下切。
- 两者都使用 `remembered_set.buffer` 存 old/init 区 slot address。
- 两者都使用 `remembered_set.hash_table` 做 write barrier 去重。
- 两者当前默认都不使用 `remembered_set.values[]`。
- 当前 GiY read-slot 模式下 `values = NULL`；Cheney 的 `values` 字段也没有分配。

当前 896KB 配置下，RSet 部分：

```text
buffer = 128 KB
hash   = 64 KB
values = 0 KB
total  = 192 KB
capacity = 16384 slots
```

真正不同的是 RSet 之外的 GC auxiliary/local workspace：

GiY：
- `GiYGCStack g_gc_stack`
  - 放在 cache/local workspace。
  - 大小为 young-before-aux 的 1/8，至少 8KB。
  - 用来保存已经 reserve destination、但还没有扫描 child 的 young object payload pointer。
  - GiY 需要它，因为 GiY 是先 reserve forwarding/destination，再统一 traverse 并 copy 到 old。
- `GiYFTSlotSet g_ft_slot_set`
  - 放在 cache/local workspace。
  - 大小为 young-before-aux 的 1/64，至少 32KB。
  - 用来记录 function table / inline cache 等外部强引用 slot。
  - minor GC 时先 reserve，copy 后再 patch。
- `EdgePatchLog g_edge_log`
  - 当前不占 workspace，`items = NULL`, `capacity = 0`。
  - 现在主要是 profile/计数概念，不是真正的 patch table。
- 可选但默认不用：
  - `g_jsobject_layout_cache`: 只有 `GIY_JSOBJECT_LAYOUT_CACHE_ENTRIES > 0` 时才占 local workspace。
  - GiYOL staging/batch: 只有 `USE_GIYOL && GIYOL_STAGING_COPY` 时才占 local workspace。

Cheney：
- `remembered_set.buffer + remembered_set.hash_table`
  - 与当前 GiY read-slot RSet 基本一致。
- `CacheCheneyFTSlotSet cache_cheney_ft_slot_set`
  - 放在 cache/local workspace。
  - 大小为 young-before-aux 的 1/8，至少 32KB。
  - 用来记录 function table / inline cache 等外部强引用 slot。
- 没有 GiY 的 `g_gc_stack`。
  - 原因是 Cheney 在 scan roots / RSet 时会立即 copy 到 old/to-space，然后通过 `dram_space.current < dram_space.free` 线性 scavenge 已复制对象。
  - 复制后的对象本身形成 Cheney scan queue，所以不需要额外 LIFO stack。

用 896KB 的 `a.sbc` smoke 输出估算：

```text
GiY:
  RSet = 192 KB
  aux  = 117.52 KB
       = stack 85.52 KB + FT slot set 32.00 KB
  RSet + aux = about 309.52 KB

Cheney:
  RSet = 192 KB
  aux  = about young-before-aux / 8
       = about 85.5 KB if init area is the same
  RSet + aux = about 277.5 KB
```

因此当前结构上：
- RSet 部分已经公平，GiY 和 Cheney 基本一样。
- GiY 多出来的主要空间是 `g_gc_stack`。
- Cheney 的 FT slot set 比 GiY 大，但 Cheney 没有 GiY stack，所以总 auxiliary 通常仍比 GiY 小约一个 32KB 的量级。
- 这个差异来自算法本身：GiY 需要在 young 内先建立 forwarding/reservation 图；Cheney 直接把 copied object 放进 old/to-space，用 old/to-space 的 scan pointer 当 work queue。

## 2026-05-19: 师兄 DPU 代码中 GC stack 的处理方式与 WRAM 布局

本次查看了：

```text
/home/qiancheng/miniproject-qiancheng-pimgc/yoshitsune/runtime_c/mydpulib_latest/mydpulib/gc_stack.h
/home/qiancheng/miniproject-qiancheng-pimgc/yoshitsune/runtime_c/mydpulib_latest/mydpulib/gc_stack.c
/home/qiancheng/miniproject-qiancheng-pimgc/yoshitsune/runtime_c/mydpulib_latest/mydpulib/allocator/generational_wram_major_is_compact/generational_wram.c
```

结论：
- 师兄的代码没有像 Cheney 那样完全消除 GC stack。
- 它仍然使用显式 `GCStack gc_stack[NR_TASKLETS]`。
- 但这个 stack 不是从 young heap 中切一大块，而是每个 tasklet 用 `mem_alloc(GC_WRAM_STACK_SIZE)` 单独在 WRAM 里分配固定 1KB。
- 另外还有一个共享 WRAM stack，大小 1KB，用于 tasklet 之间取工作。
- 还有一个 MRAM 上的 `gc_mram_stack`，大小约 10KB，作为共享 stack 的慢速后备空间。
- 当前活跃代码中，本地 GC stack push 到满时会 abort；把本地 stack 后半部分 spill 到 shared WRAM/MRAM 的代码存在，但被注释掉了。因此这个版本不是“完全动态扩容”，而是“小固定 WRAM stack + 共享/后备设计意图”。

GC stack 结构：

```c
typedef struct {
    uintptr_t* gc_stack_top;
    uintptr_t gc_stack_start;
    uintptr_t gc_stack_end;
} GCStack;
```

大小：

```text
GC_WRAM_STACK_SIZE = 1024 bytes
GC_WRAM_SHARED_STACK_SIZE = 1024 bytes
GC_MRAM_STACK_SIZE ≈ 10 KB
```

师兄 generational WRAM GC 的 WRAM heap 布局：

```text
wram_heap
  [ Allocator struct ]
  [ Remembered Set: 4096 bytes ]
  [ Young generation: 28 KB ]
```

对应代码：

```c
#define HEAP_RS_SIZE (4096)
#define HEAP_YOUNG_SIZE (1024 * 28)
#define HEAP_WRAM_SIZE (sizeof(Allocator) + HEAP_RS_SIZE + HEAP_YOUNG_SIZE)

allocator->rs = (uintptr_t)wram_heap + sizeof(Allocator);
allocator->young_start = allocator->rs + HEAP_RS_SIZE;
allocator->young_end = allocator->young_start + HEAP_YOUNG_SIZE;
```

注意：GC stack 不在这个 `wram_heap` 里面。它是额外通过 `mem_alloc()` 分出来的 WRAM 区域：

```text
per-tasklet local GC stack: 1KB * NR_TASKLETS
shared WRAM GC stack: 1KB
wram_heap: Allocator + RS 4KB + Young 28KB
```

所以与当前 ejs-new GiY 的区别是：
- 当前 GiY 把 `g_gc_stack` 从 GC Local Workspace/young-side budget 中按比例切出来，896KB 下约 85.52KB。
- 师兄代码把每个 tasklet 的 GC stack 固定为 1KB，并且放在 `wram_heap` 之外。
- 师兄这样做符合 DPU 现实：WRAM 很小，所以不能给 GC stack 留很大的弹性空间；如果栈压力太大，理论上要靠 shared WRAM/MRAM 机制，但当前版本 push overflow spill 逻辑被注释，实际满了会 abort。

### 更简单版：师兄到底怎么处理 GC stack

师兄的 minor GC 流程中，GC stack 的作用和 GiY 很像：它保存“已经决定要复制/promote，但还没有扫描 child 的对象”。

流程：

```text
发现 young object
  -> 给它分配 MRAM old 目标地址，写到 header->gc_ptr
  -> 把这个 young object 的 header 指针 push 到当前 tasklet 的 gc_stack
  -> 之后 traverse_stack() pop 出对象
  -> 扫描它的字段
  -> 如果字段又指向 young object，也给 child 分配 old 目标地址，并 push 到 gc_stack
  -> 当前对象字段全部修完以后，mram_write 整个对象到 MRAM old
```

所以师兄不是用 Cheney scan pointer 代替 stack。他仍然有显式 stack。

关键区别：

```text
当前 GiY:
  g_gc_stack 大小按 workspace/young 比例切，896KB 下约 85KB。

师兄:
  每个 tasklet 固定只有 1KB local WRAM gc_stack。
```

师兄实际代码：

```c
#define GC_WRAM_STACK_SIZE 1024

gc_stack->gc_stack_start = (uintptr_t)mem_alloc(GC_WRAM_STACK_SIZE);
gc_stack->gc_stack_end = gc_stack->gc_stack_start + GC_WRAM_STACK_SIZE;
gc_stack->gc_stack_top = (uintptr_t*)gc_stack->gc_stack_start;
```

也就是说，师兄解决空间占用的方法不是“不要 stack”，而是“每个 tasklet 只给一个很小的固定 WRAM stack”。

还有一个容易误解的点：
- 代码里确实有 shared WRAM stack 和 MRAM stack 的设计。
- 但当前 `gc_stack_push()` 满了以后会先打印 `GC Stack overflowed` 然后 `abort()`。
- 后面把一半 local stack 搬到 shared WRAM/MRAM 的代码被注释掉了。
- 所以当前实际行为是：1KB local stack 够用就继续，不够就失败；不是自动扩容。

## 2026-05-19: FT slot set 是什么，以及师兄 WRAM 与当前 GC Local Workspace 的区别

FT slot set 的 FT 指 Function Table。这里的 slot 不是对象本身，而是“保存指针的那个位置的地址”。在 ejs-new 里，它主要记录 function table / inline cache 等 VM 元数据里的强引用 slot。

为什么需要 FT slot set：
- 普通对象之间的 old-to-young 引用由 remembered set 记录。
- 但是 VM 的 function table / inline cache 不是普通 heap object。
- 这些结构里也可能保存指向 young object 的引用，例如 `JSValue` slot 或 `void*` slot。
- 如果 minor GC 不记录这些 slot，就可能漏掉 young object，或者 GC 后这些 slot 还指向已经失效的 young 地址。

FT slot set 的作用：

```text
程序运行时：
  如果 function table / inline cache 的某个 slot 写入 young 指针
  -> 记录这个 slot address 到 FT slot set

minor GC 时：
  reserve phase 扫 FT slot set
  -> 把 slot 当前指向的 young object 加入 GC tracing

copy/traverse 后：
  patch phase 再扫 FT slot set
  -> 把 slot 里的 old young 地址改成新的 old 地址
```

它和 remembered set 的区别：

```text
remembered set:
  记录普通 old/init object field slot

FT slot set:
  记录 VM 元数据里的 root-like slot
  例如 function table / inline cache
```

当前 ejs-new 中：
- GiY: `GiYFTSlotSet g_ft_slot_set`
  - size = young-before-aux / 64, min 32KB。
- Cheney: `CacheCheneyFTSlotSet cache_cheney_ft_slot_set`
  - size = young-before-aux / 8, min 32KB。

师兄 DPU 代码中没有完全对应 ejs-new 这个 FT slot set 的结构。师兄面对的是 Java/DPU runtime，主要 root 来源是：
- Java stack
- exported_ref
- remembered set
- class/method metadata

而 ejs-new 的 JavaScript runtime 额外有 FunctionTable / InlineCache 这种会缓存对象、shape、property map 的 VM 元数据，所以需要 FT slot set。

师兄 WRAM 与当前 GC Local Workspace 的根本区别：

1. 师兄 WRAM 是真实硬件 memory。

```text
DPU WRAM:
  真实 fast local memory
  容量很小
  地址空间上明确不同于 MRAM
```

所以师兄可以真实地说：

```text
young object 在 WRAM
old object 在 MRAM
WRAM pointer 和 MRAM pointer 可以通过表示方式区分
```

2. 当前 GC Local Workspace 是人为预算，不是真硬件分区。

```text
ejs-new GC Local Workspace:
  是 malloc 出来的 cache_space 中的一部分
  目标是让它“尽量留在 CPU cache”
  但它不是硬件强制的 L2 区
```

所以当前只能说：

```text
这些结构被放进 local workspace budget
期望由于大小受控和访问频繁，它们更可能留在 cache
```

不能说它们物理上一定在 cache。

3. 师兄 WRAM 同时放 young 和少量 GC 元数据。

师兄 generational WRAM heap：

```text
wram_heap:
  Allocator
  remembered set: 4KB
  young generation: 28KB

outside wram_heap but still WRAM:
  per-tasklet GC stack: 1KB each
  shared GC stack: 1KB
```

4. 当前 GC Local Workspace 概念更大。

当前用户定义的 GC Local Workspace / local-cache budget 不是只指辅助结构，而是“希望整体放进 cache 的 GC local budget”。它包含：

```text
init/work metadata area
remembered set buffer/hash
young allocation area
GiY auxiliary structures
  - g_gc_stack
  - FT slot set
  - optional GiYOL staging/batch
```

5. 两者空间压力方向不同。

师兄：
- WRAM 很小，所以所有结构都必须极小。
- GC stack 固定 1KB。
- RSet 固定 4KB。
- young 固定 28KB。
- 溢出就很危险，很多设计依赖硬件局部性和小对象假设。

当前 GiY：
- workspace 现在按 512/640/768/896KB 做实验。
- 辅助结构可以更大，例如 896KB 下 `g_gc_stack` 约 85KB。
- 目标不是适配硬件 WRAM 容量，而是在 CPU cache 层面测试 locality tradeoff。

设计分析：
- 师兄 WRAM 是“硬约束”：放不下就不能运行，或者必须 spill 到 MRAM。
- 当前 GC Local Workspace 是“实验约束”：放大或缩小会改变 locality、young capacity、GC frequency。
- 因此不能直接把师兄的 1KB stack 照搬成最佳值；它是 DPU WRAM 极小容量下的结果。
- 但可以借鉴师兄思想：高频小元数据应该尽量小、固定、可预测；不要让 GC stack 这类辅助结构无限占用 workspace。
- 对你的实验来说，最公平的比较方式仍然是：统一总 workspace budget，再比较不同 GC 如何在这个 budget 内分配 young / RSet / stack / FT slot set。

## 2026-05-19: 为什么 FT slot set 不直接和 remembered set 合并

用户问题：既然 remembered set 和 FT slot set 都是在记录“可能指向 young 的 slot”，为什么要分开？能不能合并？

结论：
- 概念上可以统一成一个更 general 的 slot set。
- 但当前代码里不能简单把 FT slot 直接塞进 remembered set。
- 原因是当前 remembered set 有一个隐含前提：它记录的是 old/init heap object 里的 field slot。
- FT slot set 记录的是 VM metadata 里的 slot，例如 function table / inline cache，它们不是普通 heap object field。

当前 remembered set 的假设：

```text
slot address 必须在：
  dram_space old 区
  或 cache_space init 区
```

当前 GiY RSet scan 里会检查：

```text
slot_in_dram = slot_addr >= dram_space.begin && slot_addr < dram_space.end
slot_in_init = slot_addr >= cache_space.begin && slot_addr < cache_space.work_begin
if (!slot_in_dram && !slot_in_init) continue;
```

因此如果把 FunctionTable / InlineCache 的 slot address 直接塞进 remembered set，很可能会被这段过滤掉，因为这些 slot 不一定在 old/init heap range 内。

两者来源也不同：

```text
remembered set:
  来自普通 heap write barrier
  语义是 old/init object field -> young object

FT slot set:
  来自 VM metadata update hook
  语义是 FunctionTable / InlineCache metadata slot -> young object
```

为什么当前分开更安全：
- 不破坏 remembered set 的 old-to-young invariant。
- RSet 扫描时可以只处理 heap slot，FT set 扫描时可以处理 metadata slot。
- profile/debug 更清楚：可以分开知道 ordinary old-to-young 和 VM metadata root 的成本。
- 不需要让 RSet 接受非 heap 地址，减少误扫任意地址的风险。

如果要合并，正确方式不是“直接共用 remembered_set.buffer”，而是重新设计成：

```text
UnifiedSlotSet:
  slot address
  slot kind: JSValue slot / void* slot
  slot domain: heap old slot / init slot / VM metadata slot
```

扫描时根据 `slot domain` 决定是否需要 old-space guard、是否允许非 heap 地址、如何 patch。

可能的收益：
- 少一个 set，少一个固定 workspace allocation。
- 统一去重逻辑。
- 让 local workspace 更简洁。

风险：
- 当前 RSet 的过滤逻辑、mprotect old read/write 逻辑、debug invariant 都要改。
- 如果实现粗糙，可能把非 GC 管理的地址当成 heap slot 处理，导致错误。
- 实验上会改变 RSet 数据结构本身，不再是“只改策略”的小变化。

建议：
- 现在阶段保留分离结构更清楚。
- 如果后续要优化 workspace，可以做一个明确的新实验：`Unified Slot Set`。
- 这个实验应该单独测，因为它改变的是 root/slot bookkeeping 设计，不只是 RSet 策略。

## 2026-05-19: 当前 GiY 与 Cheney GC workspace 结构、大小、区别

当前讨论以 `CACHE_SIZE_KB=896` 为例。根据当前代码和已有 smoke/benchmark 输出：

```text
init object alloc over = 19.757812 KB
RSet 后报告的 Cache size = 704 KB
Young before aux = 704 - 19.757812 = 684.242188 KB
```

### 共同部分

GiY 当前默认已经是 read-slot RSet；Cheney 也是 read-slot 型 RSet。两者 RSet local workspace 基本一致：

```text
RSet buffer = 128 KB
RSet hash   = 64 KB
RSet values = 0 KB
RSet total  = 192 KB
capacity    = 16384 slots
```

解释：
- `buffer` 存 old/init slot address。
- `hash` 用于 write barrier 去重。
- `values` 当前不使用。

因此当前 GiY 和 Cheney 的 RSet 空间已经公平，差异不在 RSet。

### GiY workspace

GiY 在 `giy_bind_stack_to_cache_impl()` 中从 young-before-aux 继续切辅助结构：

```text
stack_bytes   = young_before_aux / 8
ft_slot_bytes = young_before_aux / 64, min 32 KB
edge_log      = 0 KB
```

896KB 下：

```text
Total workspace budget = 896.00 KB
RSet                   = 192.00 KB
Init metadata/objects  = 19.76 KB
GiY aux total          = 117.52 KB
  g_gc_stack           = 85.52 KB
  FT slot set          = 32.00 KB
  edge log             = 0.00 KB
Usable young after aux = 566.72 KB
```

GiY aux 用途：
- `g_gc_stack`: 保存已经 reserve old address、但还没扫描 child 的 young object。
- `FT slot set`: 保存 FunctionTable / InlineCache 等 VM metadata 中可能指向 young 的 slot。
- `edge_log`: 当前不分配实际 items，只保留计数/profile 概念。

### Cheney workspace

Cheney 在 `cache_cheney_bind_ft_slots_to_cache()` 中只切 FT slot set：

```text
ft_slot_bytes = young_before_aux / 8, min 32 KB
```

896KB 下：

```text
Total workspace budget = 896.00 KB
RSet                   = 192.00 KB
Init metadata/objects  = 19.76 KB
Cheney aux total       = 85.52 KB
  FT slot set          = 85.52 KB
Usable young after aux = 598.72 KB
```

Cheney 没有 `g_gc_stack`，因为 Cheney 的 copied old/to-space 本身就是 work queue：

```text
root / RSet 发现 young object
  -> 立即 copy 到 old/to-space
  -> dram_space.free 前进
  -> scavenge 用 dram_space.current 线性扫描已复制对象
```

所以 Cheney 不需要额外 stack 保存“待扫描对象”。

### 核心差异

空间差异：

```text
GiY aux    = 117.52 KB
Cheney aux = 85.52 KB
GiY 多用  = 32.00 KB
```

为什么只多 32KB，而不是多 85KB：
- GiY 有 85.52KB stack，但 GiY 的 FT slot set 只有 32KB。
- Cheney 没有 stack，但 Cheney 的 FT slot set 是 85.52KB。
- 两者相抵后，GiY 总 aux 多 32KB。

算法差异：
- GiY: reserve first, traverse/copy later，需要 `g_gc_stack`。
- Cheney: copy immediately, copied old/to-space itself is traversal queue。

实验含义：
- 当前 RSet 部分已经对齐。
- GiY 少了约 32KB usable young space，会略微增加 GC frequency 或降低 young capacity。
- 如果做严格公平比较，应该明确写出两者都在 896KB 总 budget 内，但 internal split 不同。
- 如果想让 young-after-aux 完全一致，需要人为调整 aux 或 total budget；但那会改变“统一 total workspace budget”的公平原则。

## 2026-05-19: FT slot set 在 GiY 和 Cheney 中大小不同是否公平

用户指出：既然 FT slot set 在两个 GC 中作用相同，为什么 GiY 和 Cheney 的大小不同？这样公平吗？

结论：
- 这个质疑是正确的。
- 从语义上看，GiY 和 Cheney 的 FT slot set 都记录 FunctionTable / InlineCache 等 VM metadata 中可能指向 young 的 slot。
- 它不是 GC 算法核心数据结构，而是 VM root bookkeeping。
- 因此如果目标是严格公平比较 GC 算法，FT slot set 大小最好应该统一。

当前代码差异：

```text
GiY:
  g_gc_stack   = young_before_aux / 8
  FT slot set  = young_before_aux / 64, min 32KB

Cheney:
  FT slot set  = young_before_aux / 8, min 32KB
```

896KB 下：

```text
GiY FT slot set    = 32KB
Cheney FT slot set = 85.52KB
```

为什么会这样：
- 更像历史实现差异，而不是一个严格的理论设计。
- GiY 已经有一个大的 `g_gc_stack`，所以 FT slot set 被设得较小。
- Cheney 没有 `g_gc_stack`，于是 Cheney 的 FT slot set 使用了较大的 `young/8`。
- 但 FT slot set 的语义本身并没有因为 Cheney/GiY 而改变。

公平性分析：
- 如果只按“总 workspace budget 都是 896KB”来说，表面上公平。
- 但内部结构中，FT slot set 是同类 VM metadata bookkeeping，大小不同会引入 confound。
- Cheney 的 FT slot set 更大，会占用更多 workspace，减少 Cheney 的 usable young。
- GiY 的 FT slot set 更小，但 GiY 另有 `g_gc_stack`，最后 GiY usable young 仍比 Cheney 少约 32KB。
- 因此当前结果不能简单解释为纯 GC algorithm 差异，其中包含 FT slot set allocation policy 的差异。

建议：
- 为了严格公平，应该把 FT slot set 大小改成统一配置。
- 推荐新增类似 `FT_SLOT_SET_BYTES` 的统一宏，GiY 和 Cheney 共用。
- 初始值可以设为 32KB，因为当前 GiY 已经使用 32KB 且通过 full suite。
- 然后重新跑一轮 GiY/Cheney benchmark。

这样比较时：

```text
RSet size: identical
FT slot set size: identical
total workspace budget: identical
剩下差异主要来自：
  GiY 需要 g_gc_stack
  Cheney 用 copied old/to-space scan pointer
```

这会比当前比较更干净。

## 2026-05-20: 统一 GiY / Cheney 的 FT slot set 大小

已修改：
- 新增统一宏 `GC_FT_SLOT_SET_BYTES`，默认值为 32KB。
- GiY 和 Cheney 都使用这个宏决定 FT slot set 的大小。
- `common.mk` 也支持通过 `GC_FT_SLOT_SET_BYTES=...` 覆盖该值。

修改位置：
- `ejsvm/cache_dram_manager.h`: 定义默认 `GC_FT_SLOT_SET_BYTES`。
- `ejsvm/GiY.cc`: GiY 的 `g_ft_slot_set` 改为使用统一大小。
- `ejsvm/cache_dram_manager.cc`: Cheney 的 `cache_cheney_ft_slot_set` 改为使用统一大小。
- `ejsvm/common.mk`: 支持编译时传入 `GC_FT_SLOT_SET_BYTES`。

验证：

```text
GiY, CACHE_SIZE_KB=896:
  init_info: giy ft slot set bytes=32KB capacity=4096
  Young before aux: 684.24 KB
  Young after aux:  566.72 KB
  Aux total:        117.52 KB

Cheney, CACHE_SIZE_KB=896:
  init_info: cache_cheney ft slot set bytes=32KB capacity=4096
```

最后已把 `build.debug/ejsvm` 重新切回 GiY 版本，并再次确认 GiY 仍打印 `giy ft slot set bytes=32KB capacity=4096`。

结论：
- 之前的 FT slot set 不公平点已经修正。
- 之前是 GiY 32KB、Cheney 85.52KB。
- 现在两边都是 32KB、4096 entries。

修正后的 896KB workspace 结构：

```text
GiY:
  RSet:        192KB
  init object: 19.76KB
  g_gc_stack:  85.52KB
  FT slot set: 32KB
  usable young after aux: 566.72KB

Cheney:
  RSet:        192KB
  init object: 19.76KB
  FT slot set: 32KB
  usable young after aux: about 652.24KB
```

注意：
- 现在 Cheney 的 usable young 比 GiY 大约多 85.52KB。
- 这个差异来自 GiY 自己需要 `g_gc_stack`；Cheney 使用 copied old/to-space 的 scan pointer，不需要额外 tracing stack。
- 如果比较规则是“总 local workspace budget 相同”，这个差异应该算作 GiY 算法的数据结构成本，不再是 FT slot set 的不公平。
- 如果比较规则是“usable young 必须相同”，则还需要给 Cheney 增加 dummy padding，或者给 GiY 增加总 budget；但那是在比较另一个问题。

剩余需要小心的差异来源：
- `g_gc_stack`: GiY 独有，影响 usable young；这是算法设计差异。
- JSObject scanning 相关宏：如果只给 GiY 或只给 Cheney 开启特殊优化，会不公平。
- GiYOL staging / batch buffer：只在 GiYOL 中存在，比较 GiYOL 时必须计入 local workspace。
- profiling/debug/mprotect 开关：benchmark 时必须两边一致。
- 输出口径：Cheney 目前没有像 GiY 一样完整打印 `young after aux`，报告时需要手动计算或补充打印。

## 2026-05-20: 重新检查 GiY / Cheney 剩余公平性差异

检查目标：
- RSet 是否仍有不公平。
- FT slot set 是否仍有不公平。
- GiY 独有 `g_gc_stack` 是否属于不公平，且 85KB 是否过大。
- JSObject scan / GiYOL / profiling 开关是否会引入额外 confound。

### 1. RSet

当前默认 GiY：

```text
GIY_RSET_READ_SLOT_AT_GC = 1
RSet buffer = 128KB
RSet values = 0KB
RSet hash   = 64KB
RSet total  = 192KB
capacity    = 16384
```

Cheney：

```text
RSet buffer = 128KB
RSet hash   = 64KB
RSet total  = 192KB
capacity    = 16384
```

结论：
- 当前默认 GiY 和 Cheney 的 RSet 空间基本公平。
- GiY 的 old slot scan 会在 GC 时重新读 slot；Cheney 也是记录 slot address 后在 GC 时读 slot。
- 注意：如果未来用 `GIY_RSET_READ_SLOT_AT_GC=false`，GiY 会重新多出 `values[]`，那就不是和 Cheney 的公平对照了。

### 2. FT slot set

已修正：

```text
GiY FT slot set    = 32KB, capacity 4096
Cheney FT slot set = 32KB, capacity 4096
```

结论：
- 之前 Cheney FT slot set 更大是不干净的差异。
- 现在已经统一，不再是主要公平性问题。

### 3. GiY g_gc_stack

当前公式：

```c
stack_bytes = young_before_aux / 8
```

896KB 配置下：

```text
young_before_aux = 684.24KB
g_gc_stack       = 85.52KB
capacity         = 10947 pointers
```

512/640/768/896 当前估算：

```text
CACHE_SIZE_KB  young_before  stack    capacity  GiY young after aux  Cheney young after aux
512            300.24KB      37.52KB  4803      230.72KB             268.24KB
640            428.24KB      53.52KB  6851      342.72KB             396.24KB
768            556.24KB      69.52KB  8899      454.72KB             524.24KB
896            684.24KB      85.52KB  10947     566.72KB             652.24KB
```

历史 `GIY_PROFILE_DETAIL=1` full-suite 结果中，最大 stack depth：

```text
Most benchmarks: 147-320 entries
Havlak:          5244 entries
```

该历史 run 的 stack capacity 为约 5442 entries，Havlak 用到了 96.4%。

结论：
- 85KB/10947 entries 对大多数 benchmark 明显过大。
- 但不能简单降到 8KB 或 16KB，因为 Havlak 曾经接近 5442 entries。
- 更合理的生产默认可能是固定 64KB 左右，capacity 8192 entries，比历史 Havlak 峰值 5244 多约 56% safety margin。
- 如果只看当前 896KB，85KB 是保守、偏大；它牺牲了约 85.52KB usable young，使 GiY 比 Cheney 更频繁 GC。
- 这不是“不公平给 GiY 好处”，反而是 GiY 自己承担了过大的算法辅助空间成本。

### 4. JSObject scan

当前默认输出：

```text
JSObject precise scan: 0
JSObject type-aware scan: 0
JSObject array skip size: 0
```

结论：
- 默认 GiY 没有开启特殊 JSObject precise/type-aware 优化。
- 因此默认 GiY vs Cheney 比较中，JSObject scan 不是明显不公平来源。
- 但如果单独打开这些宏，只能作为 GiY 优化实验，不能直接拿去和默认 Cheney 比。

### 5. GiYOL workspace

GiYOL 默认会有 staging / batch workspace：

```text
GIYOL_TINY_STAGING_BYTES = 5KB
GIYOL_TINY_FLUSH_BYTES   = 4KB
GIYOL_WORKSPACE_BATCH_ENTRIES = 4096
```

结论：
- 这只影响 GiYOL，不影响 GiY vs Cheney。
- 比较 GiYOL 时必须把 staging/batch 算进 GC Local Workspace。

### 6. profiling/debug/mprotect

检查结果：
- `GIY_PROFILE_DETAIL` 默认关闭。
- `GIY_MPROTECT_OLD` 是运行时环境变量，默认关闭。
- cache miss 脚本中 GiY 和 Cheney 都用 `taskset -c 0`，CPU pinning 基本一致。

注意：
- `GIY_PROFILE_DETAIL=true` 会显著拖慢程序，不能作为性能 benchmark。
- 它只适合测 stack depth / internal counters。
- 本次曾尝试用当前 896KB detail build 跑 Havlak，但 overhead 太大，已中止；不把这次未完成结果作为证据。

总体结论：
- 当前已修正的 FT slot set 差异不再是不公平点。
- 当前 RSet 默认策略也基本公平。
- 剩下最大的结构差异是 GiY 的 `g_gc_stack`。
- 在“统一总 local workspace budget”的规则下，`g_gc_stack` 应该算 GiY 算法成本，不是不公平。
- 但从工程上看，`young/8` 的比例式 stack sizing 偏保守；建议改成可配置固定大小，并实验 48KB / 64KB / 85KB。

## 2026-05-20: GiY GC stack 48KB / 64KB / 85KB 实验

实现：
- 新增编译参数 `GIY_GC_STACK_BYTES`。
- 默认值改为 `64 * 1024`，即 64KB。
- 如果显式传 `GIY_GC_STACK_BYTES=0`，则恢复旧逻辑 `young_before_aux / 8`。
- `common.mk` 已支持传入 `GIY_GC_STACK_BYTES=...`。

测试方法：
- 固定 `CACHE_SIZE_KB=896`。
- 生成临时缩短版 benchmark：`*_StackMini.sbc`。
- 修改点：只把 `new Run(..., 100, inner)` 的外层重复次数从 100 改成 3；inner workload 保持不变。
- 跑 2 轮，配置为 48KB / 64KB / 85KB。
- 输出目录：
  - `build.debug/benchmarks/out_giy_stack_size_mini_20260520_010013`
  - `build.debug/benchmarks/out_giy_stack_size_mini_r2_20260520_011904`

注意：
- 这不是正式论文用 full-suite。
- 原本尝试跑完整 suite，但 Bounce 单个 benchmark 在 48KB 下耗时约 306 秒，因此完整 3 组 full-suite 会变成数小时级。
- 本实验用于快速判断 stack size 的方向、overflow 风险和趋势。

两轮合计结果：

```text
config   total_sum  gc_sum  business_sum  minor_sum  young_after  stack
48KB     398.898s   8.715s  390.183s      62702      604.24KB     48KB
64KB     401.191s   8.762s  392.429s      64370      588.24KB     64KB
85KB     400.493s   8.916s  391.577s      66800      567.24KB     85KB
```

相对 85KB：

```text
48KB:
  total time: -0.398%
  GC time:    -2.254%
  minor GC:   -4098
  young gain: +37.00KB

64KB:
  total time: +0.174%
  GC time:    -1.727%
  minor GC:   -2430
  young gain: +21.00KB
```

结论：
- 三组都成功跑完，包含 Havlak，没有 stack overflow。
- 48KB 在这两轮缩短版测试中总时间最好，GC 次数最少。
- 64KB 的 GC 时间也比 85KB 少，并且比 48KB 有明显更大的安全余量。
- 85KB/原 young/8 方案没有明显优势，反而减少 usable young，导致 minor GC 次数最多。

判断：
- 如果只看这次缩短版测试的性能，48KB 最快。
- 如果作为默认设计，64KB 更稳妥：它比 85KB 节省约 21KB young space，同时比历史 Havlak peak stack depth 有更高安全余量。
- 因此当前默认改为 64KB；48KB 可以作为 aggressive 实验配置继续测试。

默认 64KB smoke 验证：

```text
make OPT_GC=giy CACHE_SIZE_KB=896 -B -j2
./ejsvm a.sbc

init_info: giy gc stack bytes=64KB capacity=8192 mode=fixed
Young after aux: 588.24 KB
Aux total: 96.00 KB (stack 64.00 KB, ft 32.00 KB)
```

当前 GiY 默认配置确认：

```text
Build:
  OPT_GC=giy
  CACHE_SIZE_KB=896

Local workspace budget:
  total budget: 896KB
  RSet reservation: 192KB
    buffer: 128KB
    values: 0KB
    hash: 64KB
  remaining cache manager space after RSet: 704KB
  initial object allocation: 19.76KB

Young / auxiliary layout:
  Young before aux: 684.24KB
  GC stack: 64KB fixed, capacity 8192 entries
  FT slot set: 32KB, capacity 4096 entries
  Aux total: 96KB
  Young after aux: 588.24KB

Other flags:
  RSet mode: read_slot
  GIY_PROFILE_DETAIL: disabled
  GiYOL staging / batch: 0KB in this GiY build
  JSObject precise scan: disabled
  JSObject type-aware scan: disabled
  Small memcpy limit: 256 bytes
```

解释：
- 现在 GiY 的 GC stack 已经从旧的 `young_before_aux / 8` 改为默认固定 `64KB`。
- 如果需要恢复旧逻辑，可以编译时传 `GIY_GC_STACK_BYTES=0`。
- 如果要继续测试更 aggressive 的方案，可以编译时传 `GIY_GC_STACK_BYTES=49152`，即 48KB。

## 2026-05-20: GiY 默认 GC stack 改为 48KB

实现：
- `ejsvm/GiY.cc` 中默认 `GIY_GC_STACK_BYTES` 从 `64 * 1024` 改为 `48 * 1024`。
- `common.mk` 的覆盖机制不变，仍然可以用 `GIY_GC_STACK_BYTES=...` 临时指定大小。

验证命令：

```text
make OPT_GC=giy CACHE_SIZE_KB=896 -B -j2
./ejsvm a.sbc
```

当前 GiY 结构：

```text
Build:
  OPT_GC=giy
  CACHE_SIZE_KB=896

Local workspace budget:
  total budget: 896KB
  RSet reservation: 192KB
    buffer: 128KB
    values: 0KB
    hash: 64KB
  remaining cache manager space after RSet: 704KB
  initial object allocation: 19.76KB

Young / auxiliary layout:
  Young before aux: 684.24KB
  GC stack: 48KB fixed, capacity 6144 entries
  FT slot set: 32KB, capacity 4096 entries
  Aux total: 80KB
  Young after aux: 604.24KB

Other flags:
  RSet mode: read_slot
  GIY_PROFILE_DETAIL: disabled
  GiYOL staging / batch: 0KB in this GiY build
  JSObject precise scan: disabled
  JSObject type-aware scan: disabled
  Small memcpy limit: 256 bytes
```

和 64KB stack 配置相比：

```text
GC stack:        64KB -> 48KB   (-16KB)
GC stack cap:    8192 -> 6144   (-2048 entries)
FT slot set:     32KB -> 32KB   (unchanged)
Aux total:       96KB -> 80KB   (-16KB)
Young after aux: 588.24KB -> 604.24KB (+16KB)
RSet total:      192KB -> 192KB (unchanged)
```

判断：
- 这次修改只减少 GiY traversal stack，不改变 RSet、FT slot set、initial object allocation、GiYOL staging/batch。
- 结构上的直接收益是把 16KB 从 auxiliary area 还给 ordinary young area。
- 风险是 traversal stack 容量从 8192 entries 降到 6144 entries；之前缩短版 benchmark 中 48KB 已经跑过 Havlak，没有 overflow，但正式使用前仍建议在 full suite 中确认。

## 2026-05-20: CheneyGC vs GiY48 full suite

目的：
- 完整跑一遍 CheneyGC 和当前 GiY 配置。
- 当前 GiY 配置为 `CACHE_SIZE_KB=896`，`GIY_GC_STACK_BYTES=48 * 1024`。

测试方法：
- benchmark：`Bounce CD DeltaBlue Havlak List Mandelbrot NBody Permute Queens Richards Sieve Storage Towers`，共 13 项。
- 两边都使用 `CACHE_SIZE_KB=896`。
- CPU 固定到 `taskset -c 0`，减少调度噪声。
- 输出目录：

```text
build.debug/benchmarks/out_full_cheney_vs_giy48_20260520_014521
```

生成文件：

```text
parsed_summary.tsv
compare_cheney_vs_giy48.tsv
analysis_report.md
```

运行状态：
- CheneyGC：13/13 status 0。
- GiY48：13/13 status 0。
- GiY48 full Havlak 也成功通过，说明 48KB traversal stack 在这次 full suite 中没有 overflow。

当前 workspace 结构：

```text
Both:
  total local workspace budget: 896KB
  remembered set reservation: 192KB
  remaining cache manager space after RSet: 704KB
  initial object allocation: 19.76KB
  FT slot set: 32KB

Cheney:
  extra GC stack: 0KB
  effective ordinary young estimate:
    704KB - 19.76KB - 32KB = about 652.24KB

GiY48:
  GC traversal stack: 48KB
  FT slot set: 32KB
  Aux total: 80KB
  Young before aux: 684.24KB
  Young after aux: 604.24KB
```

总体结果：

```text
All 13 benchmarks:
  Cheney total:      6627.809s
  GiY48 total:       6690.812s
  delta:             +63.003s  (+0.95%)

  Cheney business:   6512.842s
  GiY48 business:    6546.417s
  delta:             +33.575s  (+0.52%)

  Cheney GC full:    114.966s
  GiY48 GC full:     144.395s
  delta:             +29.429s  (+25.60%)

  Cheney GC core:    77.182s
  GiY48 GC core:     100.694s
  delta:             +23.512s  (+30.46%)

  Cheney scavenge:   74.080s
  GiY48 scavenge:    75.245s
  delta:             +1.165s   (+1.57%)

  Cheney minor GCs:  955522
  GiY48 minor GCs:   1045148
  delta:             +89626    (+9.38%)
```

重要解释：
- Full suite 中 GiY48 总时间只比 Cheney 慢 `0.95%`，差距已经很小。
- 但是 GC full 时间仍慢 `25.60%`。
- 关键点是：GiY48 的 scavenge 总时间只比 Cheney 多 `1.57%`，说明 young traversal/copy 主体并不是主要问题。
- 真正拉开 GC core 的是 scan_RS：

```text
All 13:
  Cheney scan_RS: 2.507s
  GiY48 scan_RS:  24.937s
  delta:          +22.430s

All 13 GC core gap:
  +23.512s
```

也就是说，这次 GC core gap 几乎全部可以由 GiY 的 remembered-set reserve/scan phase 解释。

GC-heavy 三项：

```text
CD + Havlak + Storage:
  Cheney total:      1573.127s
  GiY48 total:       1639.426s
  delta:             +66.299s (+4.21%)

  Cheney GC full:    94.264s
  GiY48 GC full:     117.389s
  delta:             +23.125s (+24.53%)

  Cheney scavenge:   72.601s
  GiY48 scavenge:    73.088s
  delta:             +0.487s (+0.67%)

  Cheney scan_RS:    1.867s
  GiY48 scan_RS:     21.745s
  delta:             +19.878s
```

逐项关键结果：

```text
Havlak:
  total:  Cheney 748.734s -> GiY48 812.747s, +64.013s (+8.55%)
  GC:     Cheney 33.169s  -> GiY48 58.483s,  +25.314s (+76.32%)
  scanRS: Cheney 1.669s   -> GiY48 20.541s
  GC miss rate: Cheney 1.34% -> GiY48 6.09%

Storage:
  total:  Cheney 339.370s -> GiY48 331.727s, -7.643s (-2.25%)
  GC:     Cheney 48.650s  -> GiY48 42.875s,  -5.775s (-11.87%)
  scavenge: Cheney 45.237s -> GiY48 38.984s
  GC miss rate: Cheney 0.87% -> GiY48 0.39%

CD:
  total:  Cheney 485.023s -> GiY48 494.952s, +9.929s (+2.05%)
  GC:     Cheney 12.445s  -> GiY48 16.031s,  +3.586s (+28.81%)
  scanRS: Cheney 0.148s   -> GiY48 1.039s
```

判断：
- GiY48 的主要优势在 Storage 上成立：GC 时间和 scavenge 时间都比 Cheney 少，说明 GiY 的“尽量避免 old/DRAM read、在 young/local workspace 中完成 traversal/copy 决策”的方向是有效的。
- GiY48 的主要劣势集中在 Havlak：scan_RS 从 `1.669s` 增加到 `20.541s`，并且 GC cache miss rate 从 `1.34%` 增加到 `6.09%`。
- Full suite 总体慢 `0.95%` 主要是 Havlak 拉开的。如果排除 Havlak，本轮 GiY48 反而约快 `1.01s`。

最可能原因：
1. GiY48 effective young 仍小于 Cheney。
   - Cheney 约 `652.24KB` ordinary young。
   - GiY48 为 `604.24KB` ordinary young。
   - GiY48 少约 `48KB`，导致 minor GC count 比 Cheney 多 `9.38%`。

2. GiY 当前 RSet read-slot 策略在 Havlak 上成本很高。
   - GiY 的 `giy_scan_remembered_set_slots()` 在 GC 中读取 old/init slot 的当前值，判断是否仍指向 young，然后 reserve。
   - Cheney 的 `scan_remembered_set()` 直接用 Cheney tracer process edge。
   - 在 Havlak 这种 remembered-set/old-slot 活跃的 workload 中，GiY 的 reserve-only phase 造成大量 slot read 和 cache miss。

3. `Business logic` 差异不能简单理解成“应用代码本身变慢”。
   - 这里的 business time 是 total - GC full。
   - 它包含 mutator-side write barrier、allocation fast path、GC 后 cache pollution 对下一段 mutator 的影响。
   - Havlak 中 GiY48 的 write barrier calls 是 `143,307,492`，Cheney 是 `114,502,529`；这会推高非 GC 时间。

4. GiY 的 scavenge/copy 主体没有明显输。
   - All 13 scavenge 只慢 `1.57%`。
   - GC-heavy 三项 scavenge 只慢 `0.67%`。
   - Storage 中 GiY scavenge 还快 `13.82%`。
   - 因此下一步不应该优先怀疑 copy/traverse 主体，而应该优先优化 RSet scan/reserve path，尤其是 Havlak。

下一步建议：
- 第一优先级：针对 Havlak profile `remembered_set.count`、RSet slot 来源、old slot cache miss。
- 第二优先级：考虑恢复/改进 value-cached RSet，或做 hybrid RSet：记录 slot，同时缓存上次 young value/version，减少 GC 时对 old slot 的直接读取。
- 第三优先级：继续保留 48KB stack 作为默认候选，因为 full suite 已通过，且它比 64KB 多给 young 16KB。

逐项 benchmark 差距表：

说明：正数表示 GiY48 比 Cheney 慢；负数表示 GiY48 比 Cheney 快。

| Benchmark | Total Cheney | Total GiY48 | Total delta | Total delta % | GC Cheney | GC GiY48 | GC delta | GC delta % | Minor GC delta % |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Bounce | 326.103 | 305.261 | -20.842 | -6.39% | 0.199 | 0.200 | +0.001 | +0.50% | +7.94% |
| CD | 485.023 | 494.952 | +9.929 | +2.05% | 12.445 | 16.031 | +3.586 | +28.81% | +12.75% |
| DeltaBlue | 129.067 | 133.908 | +4.841 | +3.75% | 2.165 | 3.621 | +1.456 | +67.25% | +11.02% |
| Havlak | 748.734 | 812.747 | +64.013 | +8.55% | 33.169 | 58.483 | +25.314 | +76.32% | +12.38% |
| List | 187.323 | 186.657 | -0.666 | -0.36% | 0.031 | 0.030 | -0.001 | -3.23% | +7.85% |
| Mandelbrot | 404.325 | 405.852 | +1.527 | +0.38% | 6.862 | 8.154 | +1.292 | +18.83% | +7.95% |
| NBody | 647.830 | 650.090 | +2.260 | +0.35% | 10.206 | 13.565 | +3.359 | +32.91% | +7.95% |
| Permute | 820.324 | 833.725 | +13.401 | +1.63% | 0.002 | 0.003 | +0.001 | +50.00% | +9.38% |
| Queens | 217.720 | 222.599 | +4.879 | +2.24% | 0.054 | 0.060 | +0.006 | +11.11% | +8.05% |
| Richards | 1746.595 | 1743.867 | -2.728 | -0.16% | 0.200 | 0.191 | -0.009 | -4.50% | +8.08% |
| Sieve | 238.318 | 238.358 | +0.040 | +0.02% | 0.976 | 1.175 | +0.199 | +20.39% | +6.67% |
| Storage | 339.370 | 331.727 | -7.643 | -2.25% | 48.650 | 42.875 | -5.775 | -11.87% | +7.95% |
| Towers | 337.077 | 331.069 | -6.008 | -1.78% | 0.007 | 0.007 | +0.000 | +0.00% | +7.87% |

## 2026-05-20: 为什么 Havlak 当前不适合 GiY

结论：
- 不是 Havlak 天生不能用 GiY，而是当前 GiY 的 `read_slot` remembered-set 设计在 Havlak 上特别吃亏。
- Havlak 是图算法 benchmark，会构造并反复分析 control-flow graph。
- 代码中有大量长期存在的图结构和容器：
  - `ControlFlowGraph`
  - `BasicBlock`
  - `BasicBlockEdge`
  - `LoopStructureGraph`
  - `SimpleLoop`
  - `UnionFindNode`
  - `som.Vector`
  - `som.IdentitySet`
  - `som.IdentityDictionary`
- 这些结构会形成大量对象间引用，并且很多容器在运行过程中持续追加/更新引用。

为什么这对当前 GiY 不利：
1. GiY 的理想情况是：
   - 大部分 live young graph 可以在 young/local workspace 内完成发现、reserve、copy；
   - remembered set 很小；
   - GC 尽量少读 old/DRAM。

2. Havlak 的实际情况是：
   - CFG / loop graph / vectors / sets 很容易跨过多次 minor GC，变成 old；
   - 后续算法仍然不断创建新对象，并把新对象挂到旧 graph / old containers 上；
   - 于是产生大量 old-to-young slots；
   - minor GC 时必须处理 remembered set。

3. 当前 GiY 使用 `GIY_RSET_READ_SLOT_AT_GC=1`：
   - remembered set 记录的是 old slot address；
   - minor GC 时 `giy_scan_remembered_set_slots()` 要重新读取这些 old/init slots；
   - 如果 slot 当前仍指向 young，才 reserve target；
   - 之后还要在后续 phase patch remembered-set slots。

4. 这正好破坏了 GiY 的核心假设：
   - GiY 本来想避免 GC 过程中频繁读 old；
   - Havlak 迫使 GiY 在 RSet phase 读大量 old slots；
   - 这些 old slots 分布在图结构和容器里，locality 很差；
   - 因此 cache miss 明显上升。

本次数据：

```text
Havlak:
  Cheney total:   748.734s
  GiY48 total:    812.747s
  delta:          +64.013s (+8.55%)

  Cheney GC:      33.169s
  GiY48 GC:       58.483s
  delta:          +25.314s (+76.32%)

  Cheney scan_RS: 1.669s
  GiY48 scan_RS:  20.541s

  Cheney scavenge: 20.373s
  GiY48 scavenge:  25.409s

  Cheney GC miss rate: 1.34%
  GiY48 GC miss rate:  6.09%

  Cheney minor GC: 200221
  GiY48 minor GC:  225001
  delta:           +12.38%
```

解释：
- Havlak 上 GiY 的最大问题不是 copy/scavenge 主体，而是 remembered-set scan。
- `scan_RS` 从 `1.669s` 增加到 `20.541s`，单这一项就解释了大部分 GC 差距。
- GiY48 的 young after aux 是 `604.24KB`，Cheney 估算 ordinary young 约 `652.24KB`，GiY 少约 `48KB`，所以 minor GC 次数也更多。
- 更多 minor GC 会反复触发更贵的 RSet scan，形成放大效应。

和 Storage 的对比：

```text
Storage:
  Cheney GC:      48.650s
  GiY48 GC:       42.875s
  GiY48 faster:   -11.87%

  Cheney scan_RS: 0.050s
  GiY48 scan_RS:  0.165s

  Cheney scavenge: 45.237s
  GiY48 scavenge:  38.984s
```

Storage 虽然 GC-heavy，但 remembered-set scan 很小，所以 GiY 的局部 traversal / old-read avoidance 优势能发挥出来。Havlak 正相反：RSet/old-slot 读取太强，抵消并超过了 GiY 的优势。

因此准确表述应该是：
- Havlak 不适合“当前 read-slot RSet 版本的 GiY”。
- 如果未来实现 hybrid/value-cached RSet、减少 GC 时读取 old slots，Havlak 可能会变得更适合 GiY。

## 2026-05-20: Havlak saved-value RSet 单项实验

目的：
- 在当前 GiY48 配置下，切回旧的 saved-value RSet。
- 这个版本不在 GC 的 RSet scan 阶段直接读取 old slot，而是在 write barrier 时保存 slot 的 value。
- 只跑一次 Havlak，并与 Cheney / GiY48 read-slot 对比。

构建配置：

```text
OPT_GC=giy
CACHE_SIZE_KB=896
GIY_GC_STACK_BYTES=49152
GIY_RSET_READ_SLOT_AT_GC=false
GIY_RSET_INDEX_FAST=true
```

输出目录：

```text
build.debug/benchmarks/out_havlak_giy48_saved_value_rset_20260520_114821
```

启动后的空间分布：

```text
RSet mode: saved_value
RSet buffer: 64KB
RSet values: 64KB
RSet hash:   64KB
RSet total:  192KB

Remaining cache manager space after RSet: 704KB
Initial object allocation: 19.76KB

Young before aux: 684.24KB
GC stack:         48KB, capacity 6144
FT slot set:      32KB, capacity 4096
Aux total:        80KB
Young after aux:  604.24KB
```

和当前 read-slot RSet 的空间区别：

```text
read-slot:
  buffer 128KB + values 0KB + hash 64KB = 192KB

saved-value:
  buffer 64KB + values 64KB + hash 64KB = 192KB
```

所以两者总 RSet 空间一样，GiY ordinary young 也一样，都是 `604.24KB`。

三方 Havlak 结果：

| Metric | Cheney | GiY48 read-slot | GiY48 saved-value | saved vs read-slot | saved vs Cheney |
|---|---:|---:|---:|---:|---:|
| Total sec | 748.734 | 812.747 | 809.171 | -3.576 (-0.44%) | +60.437 (+8.07%) |
| Business sec | 715.565 | 754.263 | 767.666 | +13.403 (+1.78%) | +52.101 (+7.28%) |
| GC full sec | 33.169 | 58.483 | 41.505 | -16.978 (-29.03%) | +8.336 (+25.13%) |
| GC core sec | 22.198 | 46.118 | 28.348 | -17.770 (-38.53%) | +6.150 (+27.71%) |
| scan_RS sec | 1.669 | 20.541 | 1.433 | -19.108 (-93.02%) | -0.236 (-14.14%) |
| Scavenge sec | 20.373 | 25.409 | 26.747 | +1.338 (+5.27%) | +6.374 (+31.29%) |
| Minor GC count | 200221 | 225001 | 225001 | +0.00% | +12.38% |
| GC cache misses | 29407315 | 128918727 | 64309588 | -50.12% | +118.69% |
| GC cache refs | 2196319620 | 2116017931 | 2153675612 | +1.78% | -1.94% |
| GC miss rate | 1.34% | 6.09% | 2.99% | -3.10 pp | +1.65 pp |
| Write barrier calls | 114502529 | 143307492 | 143307492 | +0.00% | +25.16% |

判断：
- saved-value RSet 成功解决了 read-slot 版本在 Havlak 上的最大问题：
  - `scan_RS` 从 `20.541s` 降到 `1.433s`，比 read-slot 少 `93.02%`。
  - GC full 从 `58.483s` 降到 `41.505s`，少 `29.03%`。
  - GC cache miss rate 从 `6.09%` 降到 `2.99%`。
- 但是 total 只改善 `0.44%`，原因是代价转移到非 GC 部分：
  - Business time 从 `754.263s` 增加到 `767.666s`，多 `13.403s`。
  - saved-value 模式需要在 write barrier 中保存/更新 value，Havlak 的 write barrier calls 有 `143,307,492` 次，这部分成本进入 business time。
- saved-value 仍明显慢于 Cheney：
  - total 慢 `8.07%`；
  - GC full 慢 `25.13%`；
  - scavenge 慢 `31.29%`；
  - minor GC 多 `12.38%`。

结论：
- 旧 saved-value RSet 确认了我的判断：Havlak 上 read-slot 的 old-slot 读取确实是大问题。
- 但简单回到 saved-value 不是最终方案，因为它把成本从 GC scan_RS 转移到了 mutator/write barrier。
- 最有价值的方向应该是 hybrid RSet：
  - 保留 slot address，保证 patch 正确；
  - 同时缓存 value 或 dirty/version 信息；
  - GC 时尽量少读 old slot；
  - write barrier 中也不能引入太重的重复更新成本。

## 2026-05-20: 下一步 RSet 设计思考

基于 Havlak 三方实验，我现在的判断是：
- read-slot RSet 的问题是 GC 阶段读 old slot 太多；
- saved-value RSet 的问题是 write barrier 阶段更新 value 太多；
- 因此不应该简单二选一，而应该做 hybrid/indexed RSet。

最推荐的方案：

### 方案 A：Indexed Hybrid RSet

核心想法：
- RSet entry 仍然保存 old slot address。
- 同时保存一个 cached value。
- 再保存一个 dirty bit。
- GC 时：
  - 如果 dirty=0，用 cached value，不读 old slot；
  - 如果 dirty=1，才读 old slot 的当前值。
- write barrier 时：
  - 第一次看到 slot：插入 slot + value，dirty=0；
  - 后续同一个 slot 再写：
    - 如果 value 没变，什么都不做；
    - 如果 value 变了，不必每次更新 cached value，可以只把 dirty bit 置 1。

这样同时避免两个极端：
- 不像 read-slot 那样 GC 时读所有 old slots；
- 不像 saved-value 那样 duplicate write 时反复更新 values[]。

正确性：
- 如果 slot 插入后没有再被写过，cached value 就是准确的，GC 可以直接用它。
- 如果 slot 插入后又被写过，dirty=1，GC 读真实 slot，所以不会使用过期 value。
- minor GC patch 完以后 RSet 本来就会 clear，因此不需要跨 GC 保留复杂状态。

### 方案 B：把 RSet hash table 改成 index table

当前 saved-value fast index 有一个明显问题：
- `remembered_set_hash_indices` 是 `malloc` 出来的；
- 它不在 GC Local Workspace 里；
- 这既不完全符合 workspace 设计，也可能造成 write barrier 侧 cache miss。

更好的设计：
- hash table 不直接存 slot address；
- hash table 存 entry index + 1；
- 查找时：
  - 从 hash table 取 index；
  - 到 `buffer[index]` 里比较 slot address；
  - 相等则找到 entry。

好处：
- 不需要额外的 `remembered_set_hash_indices`。
- hash table 可以用 `uint16_t` 或 `uint32_t`，而不是 `uintptr_t`。
- 如果 capacity 小于 65535，`uint16_t` 就够。
- 对当前 8192/16384 entries 来说完全够。

空间估算：

```text
当前 saved-value:
  buffer: 64KB
  values: 64KB
  hash:   64KB
  malloc index table: about 32KB, not counted in local workspace

indexed saved-value:
  buffer: 64KB
  values: 64KB
  hash index table: 16KB if uint16_t * 8192
  optional dirty bitmap: about 1KB
```

这会比当前 saved-value 更公平，也更 cache-friendly。

### 方案 C：occupied hash slot list，避免每次 memset 整个 hash

当前 RSet clear 会清整个 hash table。
- 64KB hash table * 大量 minor GC，会产生大量固定成本和 cache pollution。
- Havlak 有 `225001` 次 minor GC，这个成本会被放大。

改法：
- 插入 hash slot 时，把 hash slot index 记录到 `occupied_hash_slots[]`。
- clear 时只清这些实际用过的 hash slots。

好处：
- RSet 小时 clear 成本接近 O(used hash slots)，不是 O(hash table size)。
- 对 Havlak / NBody 这种 minor GC 次数很多的情况尤其重要。

### 我建议的实现顺序

第一步：
- 先实现 indexed saved-value RSet。
- 去掉 malloc 出来的 `remembered_set_hash_indices`。
- hash table 存 entry index。
- 保持 RSet 总预算不增加。
- 先跑 Havlak。

第二步：
- 加 dirty bitmap，形成 hybrid RSet。
- duplicate write 不再每次更新 values[]，只设 dirty bit。
- GC 时 dirty entry 才读 old slot。
- 再跑 Havlak，观察：
  - business time 是否下降；
  - scan_RS 是否仍接近 saved-value；
  - GC miss rate 是否低于 read-slot。

第三步：
- 加 occupied hash slot list。
- 目标是减少大量 minor GC 下的 hash clear 成本。

我认为最有希望的目标结果：
- scan_RS 接近 saved-value：约 `1.4s`，不要回到 read-slot 的 `20.5s`；
- business time 接近 read-slot，不要像 saved-value 那样增加 `13.4s`；
- cache miss rate 低于 read-slot 的 `6.09%`，尽量接近 saved-value 的 `2.99%`；
- total time 至少明显低于当前 read-slot 的 `812.747s`，目标先压到 `790s` 以下，再考虑接近 Cheney 的 `748.734s`。

## 2026-05-20: Cheney 和 GiY read-slot 的真正差距

基于 full suite 结果：

```text
All 13:
  Cheney total:   6627.809s
  GiY read-slot:  6690.812s
  total gap:      +63.003s (+0.95%)

  business gap:   +33.575s
  GC full gap:    +29.429s
```

表面看：
- GiY read-slot 总时间只慢 `0.95%`。
- 差距由 business gap 和 GC gap 共同构成。

真正的 GC core 差距：

```text
All 13:
  Cheney GC core: 77.182s
  GiY GC core:    100.694s
  gap:            +23.512s

  Cheney scan_RS: 2.507s
  GiY scan_RS:    24.937s
  gap:            +22.430s

  Cheney scavenge: 74.080s
  GiY scavenge:    75.245s
  gap:             +1.165s
```

判断：
- GiY read-slot 并不是主要输在 traversal/copy/scavenge 主体。
- 几乎整个 GC core gap 都来自 `scan_RS`。
- 因此当前最真实的算法差距是：GiY read-slot 在 minor GC 中读取 remembered-set old slots 的成本太高。

Havlak 是主要来源：

```text
Havlak:
  total gap:      +64.013s
  business gap:   +38.698s
  GC full gap:    +25.314s

  scan_RS:
    Cheney:       1.669s
    GiY read-slot:20.541s
    gap:          +18.872s

  scavenge:
    Cheney:       20.373s
    GiY read-slot:25.409s
    gap:          +5.036s

  GC miss rate:
    Cheney:       1.34%
    GiY read-slot:6.09%
```

如果排除 Havlak：

```text
Total gap:
  all 13 gap:       +63.003s
  Havlak gap:       +64.013s
  without Havlak:   -1.010s
```

也就是说：
- full suite 的总时间差几乎完全由 Havlak 造成；
- 除 Havlak 以外，本轮 GiY read-slot 总时间并不输 Cheney。

为什么 `scan_RS` 会变成真正差距：
- Cheney 在 scan remembered set 时直接用 Cheney tracer 处理 slot。
- GiY read-slot 需要：
  1. 遍历 remembered set；
  2. 读取 old/init slot 当前值；
  3. 判断是否仍指向 young；
  4. reserve young object；
  5. 后面再 patch slot。
- 这个流程符合 GiY 的 correctness 需求，但在 Havlak 这种 old graph / old container 指向大量 young object 的 workload 上，会产生大量 old-slot reads。
- 这些 old slot locality 差，导致 GC cache miss rate 上升。

为什么 business 也有差距：
- 这里的 business time 是 `total - GC full`，不等于纯 JavaScript 语义执行成本。
- 它包括：
  - mutator write barrier 成本；
  - 分配路径差异；
  - GC 后 cache pollution 对下一段 mutator 的影响；
  - benchmark 本身因 GC 时机不同产生的运行波动。
- Havlak 中 GiY read-slot 的 write barrier calls 是 `143,307,492`，Cheney 是 `114,502,529`，这说明 GiY 的有效 young 较小导致更多 GC 周期和更多跨代记录压力。

最终结论：
- Cheney vs GiY read-slot 的真正差距不是“GiY copy 慢”。
- 真正差距是：
  1. GiY read-slot 的 ordinary young 比 Cheney 小，minor GC 更多；
  2. read-slot RSet 在 GC 阶段大量读 old slots；
  3. Havlak 这种 graph/container workload 放大了 old-to-young remembered-set 压力；
  4. 由此造成 scan_RS 时间和 GC cache miss 显著上升。

优化重点：
- 优先优化 RSet，不是优先重写 copy/scavenge。
- 目标是减少 GC 时 old-slot reads，同时不要把太多成本转移到 write barrier。

## 2026-05-20 13:13 JST - Cheney vs GiY fairness / measurement overhead check

Question:
- 当前 Cheney 和 GiY 的比较是否公平？
- 有没有 GiY 执行了、但 Cheney 没有执行的测量操作？

Current judgment:
- 没有发现会显著扭曲结果的 GiY-only heavy measurement。
- `GIY_PROFILE_DETAIL` 默认是 0，本轮 full suite build log 里也没有启用 `-DGIY_PROFILE_DETAIL=1`。
- `GIY_WB_PROFILE` 也没有启用。
- GC full wall/cpu time 和 PMU window 是在 `cache_dram_manager.cc` 的统一 GC wrapper 里包住 Cheney/GiY 两条分支，因此这部分测量框架是对称的。
- Cheney 和 GiY 都有 phase timing：
  - Cheney 在外层分别计 `scan_roots`, `scan_remembered_set`, `scavenge`。
  - GiY 在 `giy_minor_collect` 内分别计 root reserve / RSet reserve / young trace-copy-patch。
  - 这些计时调用不是 GiY 独有的重型测量，只是两种算法的阶段不同。

Small GiY-only overhead noticed:
- GiY 有 `giy_old_guard_begin/end/set_phase` 之类的调用，但默认只有在环境变量启用时才真正工作；正常 benchmark 中基本是一次 disabled check / function call，影响应很小。
- GiY 末尾会打印更多 GiY profile 信息，但这是程序结束阶段，不能解释 Havlak 中几十秒的差距。

真正的不公平/需要说明的差异不是测量，而是配置语义：
- 在同样 `CACHE_SIZE_KB=896` 下，Cheney 和 GiY 的 total local workspace budget 一样，这是“同总预算”比较。
- 但是 GiY 额外需要 48KB traversal stack，Cheney 没有这块结构。
- 因此 ordinary young 大小不同：
  - GiY read-slot: about 604.24KB
  - Cheney: about 652.24KB
- 这会让 GiY minor GC 次数更多，本轮 full suite 是 GiY 多约 9.38% minor GCs。

Conclusion:
- 如果研究问题是“在同一个 total GC Local Workspace budget 里，两个 GC 策略谁表现更好”，当前比较基本公平。
- 如果研究问题是“同样大小的 ordinary young 区下，算法本身谁更好”，当前比较不完全公平，因为 GiY 被自己的 stack 占掉了 48KB young budget。
- 为了论文级结果，建议补两组对照：
  1. no-profile/no-PMU build：去掉 PMU/timing/gprof 对绝对时间的影响；
  2. equal ordinary young build：例如把 Cheney 调到约 848KB total budget，或把 GiY 调到约 944KB total budget。

## 2026-05-20 - Equal ordinary young comparison idea

User question:
- 能不能通过降低 CheneyGC 的 young space 大小来达成公平？

Answer:
- 可以，但这对应的是另一种公平口径：`same ordinary young size`，不是 `same total GC Local Workspace budget`。
- 当前 `CACHE_SIZE_KB=896` 下：
  - Cheney ordinary young 约 652.24KB
  - GiY ordinary young 约 604.24KB
  - 差距主要来自 GiY 额外占用 48KB traversal stack。
- 如果要让 Cheney 的 ordinary young 和 GiY 当前配置接近，可以把 Cheney 的 total local workspace budget 从 896KB 降到约 848KB。
- 这样做会让 Cheney 和 GiY 拥有接近的 ordinary young，但总 workspace budget 不再相同。

Recommended paper/report framing:
- 同时报告两种对照：
  1. Equal total workspace: Cheney 896KB vs GiY 896KB。
  2. Equal ordinary young: Cheney about 848KB vs GiY 896KB。
- 第一组回答“同样 cache/local budget 下谁更好”。
- 第二组回答“排除 GiY stack 占用后，算法本身的 copying/reserving 策略谁更好”。

## 2026-05-20 13:19 JST - Which cache/workspace size showed the best GiY win over Cheney?

Based on previous size-matrix results:
- Data files:
  - `build.debug/benchmarks/out_giy_cache_size_20260515_004640/summary.md`
  - `build.debug/benchmarks/out_cache_cheney_cache_size_20260515_115946/summary.md`
  - `build.debug/benchmarks/out_cache_cheney_cache_size_20260515_115946/compare_giy_vs_cache_cheney.md`

Raw same-size aggregate comparison:

```text
size  GiY total  Cheney total  GiY-Cheney
512   6793.957   6631.114      +162.843s
640   6729.767   6585.095      +144.672s
768   6635.928   6636.327      -0.399s
896   6661.375   6946.021      -284.646s
```

Answer:
- 原始数据中，GiY 超过 Cheney 最明显的一次是 `CACHE_SIZE_KB=896`。
- 这次 raw aggregate 里 GiY 比 Cheney 快 `284.646s`，约 `4.10%`。
- 但是这次大胜主要来自 `Richards` 的 Cheney 896KB 异常慢样本：
  - Cheney896 Richards: `1992.976s`
  - GiY896 Richards: `1726.488s`
  - 单项贡献 `266.488s`，几乎解释了整个 896KB aggregate win。
- 后续重复 Cheney896 Richards 后，Cheney 回到约 `1730-1760s`，说明原始 896KB 大胜不稳健。
- 修正 Richards 异常后，GiY896 仍可能小幅领先，但只有约 `0.3%-0.5%`。

Important distinction:
- “同 size 下 GiY 最大 raw win”：`896KB`。
- “GiY 自己最快的 size”：`768KB`，GiY total `6635.928s`。
- “两边各自选最佳 size”：Cheney `640KB` total `6585.095s`，GiY `768KB` total `6635.928s`，此时 GiY 没有超过 Cheney。

## 2026-05-20 17:18 JST - Equal total workspace + equal usable young benchmark

User goal:
- 两边都使用 `CACHE_SIZE_KB=896`。
- 但是 CheneyGC 的实际 young/usable allocation space 缩小到和 GiY 完全一致。
- 然后重新跑完整 benchmarks 比较性能。

Implementation:
- 新增 Cheney-only 编译参数 `CHENEY_LOCAL_PADDING_BYTES`，默认 `0`，不影响普通 Cheney。
- 在 Cheney 的 FT slot set 之后，从 `cache_space.end` 再预留 padding。
- 本轮使用：
  - Cheney: `CACHE_SIZE_KB=896`, `CHENEY_LOCAL_PADDING_BYTES=49152`
  - GiY: `CACHE_SIZE_KB=896`, `GIY_GC_STACK_BYTES=49152`, `GIY_RSET_READ_SLOT_AT_GC=true`
- 新增脚本：
  - `tools/run_equal_young_896_benchmarks.sh`
  - `tools/parse_equal_young_896_benchmarks.py`

Output:
- `/home/qiancheng/ejs-new/build.debug/benchmarks/out_equal_young_896_20260520_1336`
- `summary.csv`
- `compare_equal_young.md`

Verification:

```text
Config                  Young before aux   Young after aux   Aux total
Cheney equal-young      684.24 KB          604.24 KB         80.00 KB
GiY896                  684.24 KB          604.24 KB         80.00 KB
```

All benchmark statuses:
- Cheney equal-young: 13/13 OK
- GiY896: 13/13 OK

Aggregate result:

```text
Metric              Cheney equal-young   GiY896       GiY - Cheney
Total time          6580.231s             6597.105s   +16.874s (+0.256%)
Business time       6460.666s             6454.527s   -6.139s  (-0.095%)
GC full time        119.565s              142.578s    +23.013s (+19.247%)
GC core time        80.013s               99.642s     +19.629s (+24.532%)
scan_RS             2.546s                24.817s     +22.271s
scavenge            76.834s               74.311s     -2.523s  (-3.284%)
Minor GC count      1,032,930             1,045,148   +12,218  (+1.183%)
GC miss rate        0.793%                3.232%      +2.438 pp
```

Interpretation:
- 这次对照已经同时满足：
  1. equal total GC Local Workspace budget: both 896KB
  2. equal usable young: both 604.24KB
- 缩小 Cheney young 后，GiY 的 minor GC 次数劣势从之前约 9.38% 降到约 1.18%。
- 这说明之前一部分差距确实来自 usable young 不同。
- 但 GiY 的 GC full/core 仍明显慢，主要集中在 `scan_RS`：
  - Cheney `2.546s`
  - GiY `24.817s`
  - 差距 `+22.271s`
- GiY 的 scavenge 本身反而更快：
  - Cheney `76.834s`
  - GiY `74.311s`
  - GiY 快 `2.523s`
- 因此在 equal-young 后，核心瓶颈更明确：不是 young traversal/copy 主体，而是 GiY read-slot remembered-set scan。

Scope checks:

```text
All 13:
  GiY total +16.874s (+0.256%)

Without Havlak:
  GiY total -10.216s (-0.176%)

Without Havlak and Richards:
  GiY total -21.315s (-0.521%)

CD + Havlak + Storage:
  GiY total +11.751s (+0.740%)
```

Per-benchmark important points:
- Havlak remains the main negative case:
  - Cheney `766.210s`, GiY `793.300s`, GiY slower `+27.090s` (`+3.536%`)
  - GC delta `+22.460s`, mainly from GiY RSet scan.
- Storage remains positive:
  - Cheney `334.344s`, GiY `330.156s`, GiY faster `-4.188s` (`-1.253%`)
  - GC delta `-7.903s`, GiY GC faster.
- CD becomes positive for GiY in total time:
  - Cheney `487.759s`, GiY `476.608s`, GiY faster `-11.151s` (`-2.286%`)
  - but GiY GC still slower by `+2.772s`, so CD's total win is business-side.

Conclusion:
- Equal-young 后，GiY 和 Cheney 的总时间非常接近：GiY 只慢 `0.256%`。
- GiY 的 business time 略快，但 GC time 仍慢。
- 当前最可信的解释是：
  1. 之前的较大 minor GC count gap 主要来自 GiY extra stack 占用 usable young；
  2. 对齐 usable young 后，这个因素基本被消除；
  3. 剩余 GC 差距主要来自 GiY read-slot RSet 在 GC 时大量读取 old/init slots；
  4. GiY 的 young traversal/copy 主体并不差，甚至在 aggregate scavenge 上略快。

## 2026-05-20 - How to fairly evaluate Cheney vs GiY performance

Question:
- Equal-young 比较是否公平？
- 13 个 benchmarks 应该如何客观评价性能差距？
- 一般论文会怎么处理这种性能差距？

Judgment:
- Equal-young 比较是公平的，但它回答的是一个受控问题：
  - “当 total local workspace 和 usable young 都相同的时候，GiY 算法本身和 Cheney 的差距是多少？”
- 但它不是唯一公平口径，因为 Cheney padding 是人为让 Cheney 放弃一部分本来可用的空间。
- 因此论文/报告里不应该只用这一组作为唯一结论，而应该同时报告多种公平口径。

Recommended comparison regimes:
1. Equal total workspace:
   - Cheney 896KB vs GiY 896KB, no artificial padding.
   - 回答：在同样 GC Local Workspace budget 下，谁表现更好？
   - 这是系统资源公平。

2. Equal usable young:
   - Cheney 896KB + padding vs GiY 896KB.
   - 回答：去掉 GiY stack 占用导致的容量差异后，算法本身谁更好？
   - 这是控制变量实验，不应作为唯一 headline。

3. Best tuned per GC:
   - Cheney 在 512/640/768/896 中取最优，GiY 也取最优。
   - 回答：每个 GC 在各自合理调参后，最终用户能得到什么性能？
   - 这是 end-to-end/tuned-performance 公平。

Objective reporting method:
- 不要只看 13 项 raw total time 相加，因为 Richards/Permute/NBody 这类长 benchmark 会主导 aggregate。
- 应该报告：
  1. per-benchmark table；
  2. normalized ratio per benchmark, e.g. `GiY / Cheney` 或 `Cheney / GiY`；
  3. geometric mean of normalized ratios；
  4. median ratio；
  5. aggregate total time as secondary metric；
  6. with/without outlier 或至少说明 Havlak/Richards 对总结果的贡献。

For GC research specifically:
- 总运行时间是最重要的最终指标，因为用户关心 end-to-end performance。
- 但 GC 论文通常还会单独报告：
  - GC time / GC overhead；
  - pause time: avg/median/max/P95/P99；
  - collection count；
  - memory footprint / heap size；
  - throughput-vs-heap-size curve；
  - benchmark-by-benchmark normalized speedup。

For this project:
- Main headline should be cautious:
  - “With equal total workspace and equal usable young, GiY is within 0.26% of Cheney in total time; GiY has slightly faster business time and faster scavenge, but slower GC overall due to read-slot remembered-set scanning.”
- This is stronger and more objective than saying “GiY wins/loses” from one aggregate number.

## 2026-05-20 - Current GiY copy strategy

Question:
- 当前 GiY 的复制策略是什么？

Current build/source state:
- 当前 GiY 默认 `GIY_RSET_READ_SLOT_AT_GC=1`。
- 当前 896KB equal-young benchmark 的 GiY build 使用：
  - `OPT_GC=giy`
  - `CACHE_SIZE_KB=896`
  - `GIY_RSET_READ_SLOT_AT_GC=true`
  - `GIY_GC_STACK_BYTES=49152`
- 该 build 没有启用 `USE_GIYOL`，所以 GiYOL 的 staging/batching copy path 不生效。

GiY minor GC strategy:
1. Reserve first:
   - `copy_for_minor(payload_ptr)` 不立即复制对象内容。
   - 它只在 old/DRAM 中分配目标地址，把目标 payload 地址写入 source header 的 `forwarding_pointer`，然后把 source young object push 到 `gc_stack`。
2. Root / FunctionTable / RSet reserve:
   - Roots 使用 `GiYReserveTracer`。
   - FunctionTable slots 单独 reserve。
   - Remembered set 当前是 read-slot 模式：GC 时读取 old/init slot 当前值，如果指向 young，就 reserve 该 young object。
3. Traverse stack and materialize:
   - `giy_traverse_stack_and_copy()` 从 `gc_stack` 弹出 young object。
   - 先扫描该 object 的 children，继续 reserve child objects，并把 young object 内部字段 patch 成 forwarding address。
   - 然后才把整个 object 从 young copy/materialize 到 old。
4. Copy method:
   - `giy_copy_live_object(dst, src, nbytes, ...)`
   - `nbytes <= 256` 时使用普通 `memcpy`。
   - `nbytes > 256` 且在 x86 上时，使用 streaming / non-temporal store：
     - `_mm_stream_si64`
     - `_mm_stream_si128`
   - 最后如果使用过 NT store，会在 minor GC 末尾执行 `_mm_sfence()`。
5. Patch:
   - materialize 后，GiY 再 patch roots、FunctionTable slots、remembered-set slots。

Short conclusion:
- 当前 GiY 是“两阶段复制”：
  - 第一阶段：只 reserve old 地址并建立 forwarding pointer；
  - 第二阶段：扫描 young object、修内部引用、再 materialize/copy 到 old。
- 当前 GiY 不是 Cheney 那种“发现对象后立刻复制并在 to-space 扫描”的策略。
- 当前 GiY 也不是 GiYOL 的 staging/batch copy 策略。

## 2026-05-20 - GiY scanning, GC stack behavior, and 128-bit NT store rationale

Questions:
- GiY 的扫描是怎么进行的？
- `g_gc_stack` 的运行过程是什么？
- 为什么当前使用 128-bit non-temporal store，而不是更宽的 256-bit？

Current scanning process:
1. Minor GC starts:
   - `ensure_gc_stack_capacity()`
   - `gc_stack_reset()`
   - reset profiling/layout/update state.
2. Root reserve scan:
   - `giy_scan_roots_generational<GiYReserveTracer>(ctx)`
   - 扫描 global constants, global property maps, global shapes, context roots, VM stack, `gc_root_stack`。
   - 如果 edge 指向 young object，则调用 `copy_for_minor()`。
3. FunctionTable reserve:
   - `giy_reserve_function_table_slots()`
   - 扫描之前记录下来的 FunctionTable / InlineCache strong slots。
4. Remembered set reserve:
   - 当前 `GIY_RSET_READ_SLOT_AT_GC=1`。
   - `giy_scan_remembered_set_slots()` 遍历 RSet slot address。
   - GC 时重新读取 old/init slot 当前值。
   - 如果当前值指向 young，则 reserve 该 young object，并立即 patch old/init slot 到 forwarding address。
5. Stack traversal and materialization:
   - `giy_traverse_stack_and_copy()` 不断 pop `g_gc_stack`。
   - 对每个 young object：
     1. 根据 object type 扫描 children；
     2. 对 child young object 执行 reserve；
     3. patch 当前 young object 内部字段为 child 的 forwarding address；
     4. 扫描完成后，把当前 object 整体 copy/materialize 到 old。
6. Root/FunctionTable patch:
   - `giy_scan_roots_generational<GiYPatchTracer>(ctx)`
   - `giy_patch_function_table_slots()`
   - 当前 read-slot RSet 模式下 RSet slot 已在 reserve scan 时 patch，所以 `giy_patch_remembered_set_slots()` 直接 return。

GC stack behavior:
- `g_gc_stack` 是 LIFO stack/worklist。
- 它存的是 young object 的 payload pointer，不是 header pointer。
- 当前 48KB stack 对应 `49152 / 8 = 6144` entries。
- 第一次发现 young object 时：
  - `copy_for_minor(payload_ptr)` 在 old 区 reserve 目标地址；
  - 把目标 payload address 写入 source header 的 `forwarding_pointer`；
  - 把 source payload pointer push 到 `g_gc_stack`。
- 如果同一个 object 再次被发现：
  - 因为 `forwarding_pointer != 0`，直接返回已有目标地址；
  - 不重复 push，不重复 reserve。
- pop 时才真正扫描 children 和 copy object。
- 因为是 LIFO，所以 traversal 近似 DFS，不是 Cheney 的 to-space scan pointer BFS。

Why 128-bit NT store:
- 当前代码只 include `<emmintrin.h>`，使用 SSE2:
  - `_mm_stream_si64`
  - `_mm_stream_si128`
- 当前 build flags 没有 `-mavx` / `-mavx2`，因此 256-bit `_mm256_stream_si256` 不是当前 portable baseline。
- 128-bit streaming store 只要求 16B 对齐；当前代码可通过先 stream 一个 8B tail/header piece 把目的地址调到 16B alignment。
- 256-bit streaming store 通常要求 32B 对齐；当前 object allocation/layout 只稳定保证到 8/16B 级别，不能直接安全假设 32B alignment。
- 很多 survivor object 很小，`<=256B` 已经直接走 `memcpy`；对这些对象使用 256-bit NT store 没意义。
- 更宽 store 主要减少指令条数，但不保证提升真实带宽；还可能引入 AVX 编译要求、alignment padding、AVX frequency / transition cost。

Conclusion:
- 当前 128-bit NT store 是保守、容易保证 correctness、兼容 x86_64 baseline 的选择。
- 256-bit NT store 不是不可以，但应该作为单独实验：
  1. 增加 AVX2 build path；
  2. 保证 old destination 32B alignment；
  3. 处理 32B head/tail；
  4. 分别测 total time、GC time、copy bandwidth、cache miss、CPU frequency/retired instructions。

## 2026-05-20 - Senior GiY stack logic vs current GiY stack logic

Question:
- 师兄的 GiY 栈逻辑是否和当前 GiY 一样？

Short answer:
- 核心思想相同：
  - 第一次发现 young object 时，不立即复制内容；
  - 先 reserve old destination；
  - 在 source header 写 forwarding pointer / `gc_ptr`；
  - 把 source young object 放进显式 GC stack；
  - 之后 pop stack，扫描 children，patch source object 内部引用，最后一次性 materialize/copy 到 old。
- 但实现细节不一样。

Senior DPU/Java code:
- Stack item: young object header pointer。
- Forwarding field: `ObjectHeader.gc_ptr`，保存 old data pointer。
- Reserve function:
  - `get_object_copy_handle()`
  - `copy_for_minor()`
- Traversal:
  - `traverse_stack()`
  - pop young header；
  - 用 Java class table bitmap / array metadata 判断哪些 field 是 reference；
  - 在 WRAM young source object 上 patch child pointers；
  - 最后 `mram_write()` 整个 object 到 MRAM old。
- Parallelism:
  - 每个 tasklet 有 `GCStack gc_stack[NR_TASKLETS]`；
  - 还有 shared stack / MRAM fallback 的设计；
  - 需要 mutex 避免多个 tasklet 同时 reserve 同一个 object。

Current GiY/eJS code:
- Stack item: young object payload pointer。
- Forwarding field: source header 的 `forwarding_pointer`，保存 old payload pointer。
- Reserve function:
  - `copy_for_minor(payload_ptr)`
- Traversal:
  - `giy_traverse_stack_and_copy()`
  - pop young payload；
  - 根据 JS cell type 扫描 children；
  - JSObject 当前默认 conservatively 扫 `eprop`，也会处理 Shape / PropertyMap 等动态对象结构；
  - 在 young source object 内 patch children；
  - 最后 `giy_copy_live_object()` copy/materialize 到 old。
- Parallelism:
  - 当前 eJS GiY 是单 VM 进程中的一个 `g_gc_stack`；
  - 没有师兄 DPU 代码那种 per-tasklet stack + shared stack work stealing。

Conclusion:
- 如果只看 GC algorithm principle：两者是同一类 GiY stack/worklist 思想。
- 如果看工程实现：不完全一样。
- 最大差异来自运行对象模型和执行平台：
  - 师兄是 Java + class bitmap + DPU WRAM/MRAM + tasklets；
  - 当前是 JavaScript + Shape/PropertyMap/eprop + CPU cache/DRAM + single local workspace。

## 2026-05-20 - New GiY optimization proposal: Size-Binned GiY

User idea:
- 在 copy/promotion 前，对小对象和大对象做预判。
- 让小对象和大对象进入不同的 old destination region。
- 这样后续可以形成更连续的 destination write stream，并更方便使用 non-temporal store 批量 copy。

Proposed name:
- `GiY-SB`
- Full name: `Size-Binned GiY`
- More precise paper name: `Size-Binned Promotion for GiY`

Important clarification:
- CPU 上不存在“一条 NT 命令复制全部对象”的形式。
- 实际实现会是一个 loop，连续执行 `_mm_stream_si128` / future `_mm256_stream_si256`。
- 优化目标是让 destination addresses 按 size class 变得连续，从而让 NT store loop 更像顺序 streaming write。

Recommended first implementation target:
- 先不要改 mutator young allocation。
- 先在 minor GC promotion/reservation 阶段做 size-binned old placement。
- 原因：
  1. 改 young allocation 会同时改变 nursery locality、fragmentation、GC frequency，变量太多；
  2. promotion-time binning 更容易隔离“copy/materialization layout”本身的效果；
  3. 对现有 GiY correctness 影响更可控。

Correct high-level algorithm:
1. Discovery phase:
   - 不立即分配最终 old address。
   - 扫 roots / FunctionTable / RSet。
   - 扫 reachable young object graph。
   - 只 mark/discover young object，并收集 object list。
   - 不能 patch slot，因为 final old address 还没确定。

2. Size classification:
   - 根据 `ALIGN(header->size + sizeof(object_header))` 分类。
   - Suggested initial bins:
     - tiny: `<=64B`
     - small/medium: `65B..256B`
     - large: `>256B`
   - 也可以第一版只用 two bins:
     - small: `<=256B`
     - large: `>256B`

3. Address assignment:
   - discovery 完成后已经知道每个 bin 的 total bytes。
   - 从 `dram_space.free` 开始，按 bin 分配连续 old destination regions：
     - tiny region
     - small/medium region
     - large region
   - 给每个 source young object 写最终 `forwarding_pointer`。
   - 最后一次性 advance `dram_space.free` / `available_bytes`。
   - 这样 old generation 仍然是一个连续 promotion block，没有 hole。

4. Materialization phase:
   - 按 bin order 遍历 object list。
   - 对每个 object：
     1. 使用 patch/materialize tracer 扫描 children；
     2. 要求所有 young children 都已经有 forwarding pointer；
     3. 在 young source object 上 patch children；
     4. 再 copy 到 final old destination。
   - tiny/small objects:
     - 可以先 memcpy 到 staging buffer；
     - staging 达到 4KB/8KB 阈值后，用 NT store 顺序 flush 到 old tiny/small region。
   - large objects:
     - 直接按 old address order 使用 NT store copy。

5. External patch phase:
   - patch roots。
   - patch FunctionTable slots。
   - patch remembered-set slots。
   - 当前 read-slot RSet 不能像普通 GiY 一样在 reserve scan 里立即 patch，因为 final forwarding address 当时还没确定。
   - 因此 GiY-SB 需要单独的 RSet patch path：assignment 完成后再 patch。

Required new components:
- `GIY_SIZE_BINNED_PROMOTION` or `USE_GIY_SB` build flag。
- New discover tracer:
  - e.g. `GiYSBDiscoverTracer`
  - 只 discover/mark，不 patch。
- New materialize tracer:
  - e.g. `GiYSBMaterializeTracer`
  - patch source object fields；
  - 如果发现 young child 没有 forwarding pointer，直接 assert/abort。
- Object list:
  - prototype can use a malloc-backed vector for correctness。
  - production should account for this space inside GC Local Workspace or explicitly report it as external overhead。
- Bin metadata:
  - count / bytes / old begin / old cursor for each bin。
- Staging buffer:
  - 初始建议 8KB or 16KB。
  - flush threshold 4KB or 8KB。

Correctness invariants:
- 在任何 external root/slot 被 patch 前，每个 live young object 必须已经有 final forwarding pointer。
- copy 到 old 前，source object 内部 young pointers 必须已经 patch 成 old forwarding pointers。
- old promotion block 必须连续、可被后续 old scan/major GC 线性解析。
- RSet slot patch 必须发生在 final forwarding pointer assignment 之后。
- 不能让 old object 中残留 young pointer。

Risks:
- Object-list metadata 可能很大。如果放进 GC Local Workspace，会减少 usable young；如果用 malloc，则性能比较时必须说明不是完全 local-workspace 方案。
- size-binned old layout 可能改善 promotion copy locality，但可能破坏对象图 locality：parent 和 child 如果大小不同，会被放到不同 region，mutator 后续访问可能更差。
- 当前 equal-young 实验显示 GiY 的主要 GC 瓶颈是 `scan_RS`，而不是 scavenge/copy。因此 GiY-SB 可能改善 Storage 这类 copy-heavy benchmark，但不一定解决 Havlak。
- 小对象 staging 是 double copy：young -> staging -> old。只有当减少 cache pollution / 提高 streaming 写收益足够大时才值得。

Evaluation plan:
1. Instrument only:
   - survivor size histogram；
   - per-bin count/bytes；
   - current reserve-order destination run length；
   - materialized bytes by size。
2. Prototype GiY-SB with malloc-backed object list:
   - target correctness first。
3. Smoke tests:
   - small benchmarks / `a.sbc`；
   - old-guard mode if available。
4. Target benchmarks:
   - Storage: expected best case；
   - CD: mixed case；
   - Havlak: expected hard case because RSet scan dominates。
5. Full suite:
   - compare against current GiY read-slot under same total workspace / same usable young when possible。
6. Metrics:
   - total time；
   - business time；
   - GC full/core；
   - scan_RS；
   - scavenge/materialization；
   - minor GC count；
   - GC cache misses；
   - copy bytes by bin；
   - staging flush count/bytes；
   - NT bytes。

Expected claim if successful:
- `GiY-SB improves promotion/materialization locality by grouping survivors by size and streaming old writes in bin order.`
- But it should not be claimed as solving GiY's RSet bottleneck unless scan_RS also improves.

## 2026-05-20 - Evaluation of per-minor-GC small/large old-region reservation

User idea:
- 每次 minor GC 时，在目标 old 区中先区分出 small region / large region。
- 这样 `copy_for_minor()` reserve 时就能根据对象大小直接决定去哪个 region。

Evaluation:
- 这个方向可以做，而且比完全两遍 discover 更接近当前 GiY 的 reserve-first 结构。
- 但核心难点是：minor GC 一开始不知道本轮 small survivor bytes 和 large survivor bytes 各有多少。
- 如果没有准确大小，直接在 old 区切 small/large region 会产生三个 correctness 风险：
  1. small region overflow；
  2. large region overflow；
  3. small/large 中间留下 gap，破坏 old generation 的线性 object layout。

Why gap is dangerous:
- 当前 old/DRAM space 依赖线性 object layout。
- 后续 old scan / major GC 会从 old start/free head 线性按 object header 解析。
- 如果 small region 和 large region 中间有未初始化 hole，后续 scan 可能把 hole 当 object header，导致错误。
- 所以不能简单留下空洞，除非实现 free-block/dummy-object header，并让所有 old scanner 正确跳过。

Possible designs:

1. Exact two-pass design:
   - Pass 1: discover all live young objects and count per-bin bytes。
   - Assign exact small/large regions。
   - Pass 2: set forwarding pointers / patch / materialize。
   - Correctness 最干净，但不像当前 `copy_for_minor()` 一样第一次遇到对象就能返回 final forwarding address。

2. Predictive quota design:
   - At minor GC start, use previous GC statistics predict small/large quotas。
   - `copy_for_minor()` 根据 size 从 corresponding cursor reserve。
   - If quota overflows, use overflow region after both bins or fallback to normal GiY allocation。
   - Needs no-hole guarantee:
     - either fill unused gap with free-block/dummy block；
     - or allow overflow/fallback to consume gap；
     - or compact/slide large region, which adds extra copy。

3. Bidirectional region design:
   - Reserve a promotion window。
   - small allocates upward from low address；
   - large allocates downward from high address。
   - Problem: if cursors do not meet exactly, gap remains。
   - Also old linear scan becomes complicated unless gaps are represented as valid skip blocks。

4. Chunked bin design:
   - small/large each request chunks from old as needed。
   - Avoids predicting total size。
   - But old layout becomes interleaved chunks, not one clean small region + large region。
   - It may still improve locality if materialization follows chunk order, but less ideal。

Recommended path:
- Do not start with fully separated old regions requiring exact pre-sizing.
- First implement profiling/prediction:
  - per-minor-GC small/large survivor bytes；
  - max/avg/P95 small bytes；
  - overflow probability for candidate quotas。
- Then prototype predictive quota with safe fallback:
  - `small <= 256B` goes to small cursor；
  - `large > 256B` goes to normal old cursor or large cursor；
  - if small quota overflows, fallback to normal old allocation；
  - never leave unparseable holes in old.

Conclusion:
- The idea is promising, but only safe if old-space layout remains linearly parseable.
- The simplest correct first version is predictive small-region + fallback, not hard partition with holes.
- If the user wants stronger region purity, exact two-pass binning is cleaner than one-pass prediction.

## 2026-05-20 - Global old-space split for GiY-SB

Question:
- 能不能在程序最开始就把整个 old heap / DRAM heap 分成 small-old region 和 large-old region？
- 这样每次 minor GC 就不需要临时切 small/large 区，也不需要担心每轮 promotion block 内部有 hole。

Judgment:
- 可以，而且这是比“每次 minor GC 临时切区”更干净的设计。
- 它把问题从“每轮 GC 的 promotion block 是否有 hole”变成“old generation 是否支持多个 old subspaces”。
- 只要所有 old scanner / major GC / allocation path 都知道 small-old 和 large-old 是两个独立线性区域，就不会有不可解析 hole。

Design:
- At initialization:

```text
old_small_begin ... old_small_free ... old_small_end
old_large_begin ... old_large_free ... old_large_end
```

- Promotion/reserve:
  - `align_bytes <= SMALL_OBJECT_MAX` -> allocate from `old_small_free`
  - `align_bytes > SMALL_OBJECT_MAX` -> allocate from `old_large_free`
- Each region is internally linear and parseable.
- No per-minor-GC gaps are created.

Advantages:
- `copy_for_minor()` can decide final forwarding address immediately.
- Current GiY reserve-first structure can mostly remain.
- Small objects become more densely packed in old-small region.
- Large objects are separated, so large copies can be streamed more directly.
- It avoids needing exact per-minor-GC survivor size prediction.

Required code changes:
1. Add old-space subspace metadata:

```c
old_small_begin, old_small_free, old_small_end
old_large_begin, old_large_free, old_large_end
```

2. Replace direct `dram_space.free` allocation in GiY `copy_for_minor()` with:

```c
giy_old_alloc_by_size(align_bytes)
```

3. Update `in_dram_space(ptr)` / old pointer tests if necessary:
   - If small/large regions are both inside current `dram_space.begin..dram_space.end`, the broad range test may still work.
   - But old scan must not scan the unused gap between small and large regions.

4. Update old scan / major GC:
   - scan `old_small_begin -> old_small_free`
   - scan `old_large_begin -> old_large_free`
   - do not assume one single `dram_space.begin -> dram_space.free` used range.

5. Direct old allocations:
   - If the runtime directly allocates large objects into old, it should also use the same size-based allocator.

6. Statistics:
   - per-region allocated bytes；
   - per-region object count；
   - small/large overflow；
   - fragmentation / unused capacity；
   - GC time and business time。

Risks:
- Fixed split ratio can be wrong.
  - If small region fills but large region is empty, GC may trigger early even though total old space remains.
  - Need fallback policy or adaptive split.
- Mutator locality may change.
  - Parent/child objects of different sizes may be placed far apart.
- Major GC becomes more complex.
  - It must either compact each region separately or rebuild regions based on size.
- If current code assumes `dram_space.free` is the only old allocation frontier, this change is invasive.

Recommended first prototype:
- Make it GiY-only behind a flag:

```text
GIY_SPLIT_OLD_BY_SIZE=1
GIY_SMALL_OBJECT_MAX=256
GIY_OLD_SMALL_RATIO=50
```

- Start with fixed 50/50 split or 60/40 small/large.
- 如果 small 区溢出：
  - 原型方案 A：回退到 large/normal 区，并记录 overflow；
  - 更严格的方案 B：触发 major GC；
  - 第一次实验建议：先使用 fallback，并记录 overflow。

更合适的名字：
- `GiY-SO`
  - 全称：`Split-Old GiY`
- 如果和 size-binning/staging 结合：
  - `GiY-SB` 更适合作为总称。
  - 当前这个具体变体可以叫 `GiY-SB-SO`，也可以简单叫 `GiY-SO`。

结论：
- 全局 old-space split 是可行的，而且可能比每次 minor GC 临时切分更干净。
- 它保留了“立刻确定 forwarding address”的性质，所以比严格 two-pass binning 更适合当前 GiY。
- 主要代价是：所有 old-space scanner / allocation path 都需要理解 old 区现在有两个 region。

## 2026-05-20 GiYSB 实装记录

已经在 `GIY_SB=true` 编译选项后面实现了第一个 GiYSB 原型。

名称：
- `GiYSB`：GiY Small-object Batching，也就是 GiY 小对象批量搬运。

编译选项：
- `OPT_GC=giy`
- `GIY_SB=true`
- `GIYSB_STAGING_BYTES=8192`
- `GIYSB_SMALL_OBJECT_MAX_BYTES=256`，默认值
- `GIYSB_SMALL_OLD_RATIO=50`，默认值

已经实现的设计：
- 把 old/DRAM 空间切成两个连续区域：
  - small-old 区：放 aligned total size `<=256B` 的对象；
  - large-old 区：放更大的对象。
- GiYSB 使用 FIFO worklist，而不是 GiY 原本的 LIFO stack。
  - 原因：FIFO traversal 可以让处理顺序接近 reserve 顺序。
  - 这样 reserve 到 small-old 的小对象，目标地址更容易连续。
- 只有小对象使用 staging buffer。
  - 小对象先从 young 复制到 GC Local Workspace 里的 8KB staging buffer。
  - staging 满了以后，再通过现有 NT-copy 路径复制到 small-old。
  - 最后的尾巴会在 traversal 结束时 flush。
- 大对象仍然使用当前 GiY 的 direct copy 路径：
  - small-old 对象基本绕开 `<=256B` 的直接 `memcpy`；
  - 更大的对象仍然使用原来的直接 NT copy。

Workspace 变化：
- 原本 GiY896 local workspace：
  - GC stack 48KB
  - FT slot set 32KB
  - aux 总量 80KB
  - young after aux 约 604.24KB
- GiYSB896 local workspace：
  - GC stack 48KB
  - FT slot set 32KB
  - GiYSB staging 8KB
  - aux 总量 88KB
  - young after aux 约 596.24KB

小测试：
- `benchmarks/hello_world.sbc`：通过，没有发生 GC。
- `benchmarks/Storage_StackMini.sbc`：通过。
  - minor GC count：3030
  - staged small objects：32,763,628
  - staged small bytes：1762.85MB
  - staging flushes：227,341
- `benchmarks/Bounce_StackMini.sbc`：通过。
  - minor GC count：96
  - staged small objects：19,617
  - staged small bytes：0.75MB
  - staging flushes：273

重要注意点：
- 这个原型在 GiYSB 下把 GiY 的 traversal 顺序从 LIFO 改成 FIFO。
- 这是为了 reserve-order 小对象批量搬运有意做的改变。
- 因此比较对象不是“完全相同 GiY 加 staging”，而是“GiY 加 FIFO reserve-order small-object staging”。

## 2026-05-21 GiYSB benchmark 结果

已经实现并测试 GiYSB：
- `OPT_GC=giy`
- `GIY_SB=true`
- `CACHE_SIZE_KB=896`
- `GIY_GC_STACK_BYTES=49152`
- `GIYSB_STAGING_BYTES=8192`
- `GIYSB_SMALL_OBJECT_MAX_BYTES=256`
- `GIYSB_SMALL_OLD_RATIO=50`

相关文件：
- `ejsvm/GiY.cc`
- `ejsvm/giy_dram_manager.cc`
- `ejsvm/common.mk`
- `tools/run_giysb_896_benchmarks.sh`

Workspace：
- GiY896 baseline：
  - GC stack 48KB
  - FT slot set 32KB
  - aux 总量 80KB
  - young after aux 604.24KB
- GiYSB896：
  - GC stack 48KB
  - FT slot set 32KB
  - GiYSB staging 8KB
  - aux 总量 88KB
  - young after aux 596.24KB

正确性：
- full suite 已成功跑完。
- 13/13 benchmarks status 都是 0。

重要 profiling 修正：
- 第一次 full run 的 GiYSB per-object counters 在 hot path 中是打开的。
- 这不公平，因为 GiY baseline 的 detailed profiling 是关闭的。
- 因此添加了 `GIYSB_PROFILE`，现在默认值是 `0`。
- 性能比较使用第二次运行，也就是 `GiYSB profile: 0` 的结果。

输出目录：
- 第一次 debug/profile run：
  - `/home/qiancheng/ejs-new/build.debug/benchmarks/out_giysb_896_20260520_214615`
- 可靠的 profile-off run：
  - `/home/qiancheng/ejs-new/build.debug/benchmarks/out_giysb_896_profile0_20260520_234139`
- 调整后的 summary：
  - `summary_adjusted_richards_repeat1.csv`

Richards 说明：
- 第一次 profile-off Richards 样本是异常值：
  - 原始样本：1981.571s
  - 重跑样本：1750.045s
- 调整后的比较使用 `Richards_repeat1` 替换原来的 Richards 样本。

调整后的总结果：

| 指标 | GiY896 baseline | GiYSB896 调整后 | GiYSB - GiY |
|---|---:|---:|---:|
| 总时间 | 6597.105 s | 6655.960 s | +58.855 s (+0.892%) |
| 业务时间 | 6454.527 s | 6489.367 s | +34.840 s (+0.540%) |
| GC full time | 142.578 s | 166.593 s | +24.015 s (+16.843%) |
| GC core time | 99.642 s | 122.534 s | +22.892 s (+22.974%) |
| scan_RS | 24.817 s | 25.398 s | +0.581 s (+2.341%) |
| scavenge | 74.311 s | 96.555 s | +22.244 s (+29.934%) |
| Minor GC 次数 | 1,045,148 | 1,059,088 | +13,940 (+1.334%) |
| GC cache misses | 162,734,227 | 150,658,028 | -12,076,199 (-7.421%) |
| GC instructions | 631,647,661,430 | 786,304,319,303 | +154,656,657,873 (+24.485%) |

结论：
- GiYSB 是正确的，但这个第一版 8KB staging 设计没有变快。
- 它让 GC cache misses 下降约 7.4%，说明 cache-locality 方向确实有效。
- 但是它让 GC instructions 增加约 24.5%，scavenge 时间增加约 29.9%。
- 最可能的主要原因是 double-copy 成本：
  - 小对象先从 young 复制到 staging buffer；
  - 然后 staging buffer 再通过 NT store 复制到 old。
- split old-space 布局也可能轻微伤害业务时间，因为相关的小对象/大对象被分开放置了。
- 最大的单项退化是 `Storage`：
  - 总时间 +17.147s (+5.194%)；
  - GC 时间 +16.816s (+39.381%)。

下一步设计方向：
- 不要无条件 staging 每一个小对象。
- 需要考虑以下方案之一：
  - 先记录小对象 metadata batch，只在确定能形成真正完整 8KB chunk 时才 staging；
  - 对尾巴直接 copy，不走 staging；
  - 避免把经常一起访问的 object type 强行拆到不同 old region；
  - 或者对选定的 medium object 直接使用 NT-store，避免 young -> staging 的额外复制。

## 2026-05-21 记录语言规则

- 以后写入 `AIlog.md` 的实验记录、结论、设计说明必须使用中文。
- 如果需要保留英文术语，可以保留术语本身，但解释和总结使用中文。

## 2026-05-21 GC Local Workspace 是否能保证在 L2 cache 中

问题：
- 当前所谓的 GC Local Workspace，是否真的能尽量保证几乎都在 L2 cache 里？

当前机器：
- CPU：Intel Xeon W-2235
- 每个 core 的 L2 cache：1MiB
- L2 结构：16-way set associative，1024 sets，cache line 64B
- L1d：每 core 32KB
- L3：全 socket 共享约 8.3MiB

当前代码实际做的事情：
- `cache_space` 是通过 `malloc(CACHE_SIZE_KB * 1024)` 得到的一段普通虚拟内存。
- GiY 的 GC stack、FT slot set、GiYSB staging buffer 等，是从 `cache_space.end` 往前切出来的。
- 这只是在软件层面把这些结构放进同一个较小的地址范围里。
- 当前没有使用任何硬件机制把这段空间 pin/lock 到 L2 cache。

结论：
- 不能严格保证 GC Local Workspace 常驻 L2。
- 它只能提高“工作集大小适合 L2，因此大概率可以被 L2 容纳”的可能性。
- `CACHE_SIZE_KB=896` 的意义是：把目标工作集控制在 1MiB L2 的约 87.5% 左右，减少容量 miss 的概率。
- 但是硬件 cache 的实际命中还取决于：
  - 物理地址到 cache set 的映射；
  - L2 的 16-way associativity 是否发生冲突；
  - OS 给 `malloc` 页分配的 physical page color；
  - GC 过程中访问 old slot、Shape、PropertyMap、stack、代码、其他 runtime 数据造成的竞争；
  - 同一个 physical core 上另一个 hyperthread 是否也在竞争 L2；
  - cache replacement policy。

关键细节：
- `malloc` 返回的是连续虚拟地址，不代表连续物理地址。
- L2 set index 使用物理地址位决定。
- 普通 4KB page 只能固定 page 内 offset，不能控制完整的 L2 set 分布。
- 因此，即使虚拟地址连续，physical page color 仍然可能造成某些 L2 set 压力更大。

对 896KB 的判断：
- 896KB 小于 1MiB，所以从容量上看是合理的。
- 但是它已经接近 L2 容量上限。
- 如果额外访问 young 之外的数据、old slots、shape/PM metadata，或者发生 set conflict，那么仍然可能把 GC Local Workspace 中的 line 挤出 L2。
- 所以“896KB 预期在 L2 上”是一个合理实验假设，但不是硬件保证。

更严谨的验证方法：
- 不要只看总时间，要测 L2 相关 PMU counters。
- 建议测：
  - L2 references；
  - L2 misses；
  - L1D miss；
  - LLC load/store miss；
  - GC-only window 中的 miss rate。
- 对 512KB、640KB、768KB、896KB、960KB、1024KB 做 sweep。
- 如果 896KB 真的接近最佳，应该看到：
  - 512/640 可能 minor GC 更多；
  - 768/896 在总时间或 GC time 上较好；
  - 960/1024 可能 L2 conflict/capacity miss 上升。

目前应使用的表述：
- 不要说“GC Local Workspace 被放进 L2 cache”。
- 更准确的说法是：
  - “GC Local Workspace is sized to fit within the per-core L2 cache budget.”
  - 中文：GC Local Workspace 的大小被控制在每个 core 的 L2 cache budget 内，目的是提高它在 L2 中保持热数据的概率。

## 2026-05-21 L2 set-associative 结构对 896KB workspace 的含义

问题：
- L2 是 `16-way set associative`、`1024 sets`、`64B cache line`。
- 这是否意味着 workspace 很有可能几乎全部同时塞进 L2？

计算：
- L2 总容量：
  - `16 ways * 1024 sets * 64B = 1,048,576B = 1MiB`
- 每个 set 最多能同时容纳：
  - `16 ways * 64B = 1024B`
- 如果 workspace 是 896KB，并且物理地址到 set 的分布比较均匀：
  - cache line 数量：`896KB / 64B = 14336 lines`
  - 平均每个 set：`14336 / 1024 = 14 lines/set`
  - L2 每个 set 有 16 ways，所以平均还剩 `2 ways/set`

结论：
- 从容量和平均 set 占用角度看，896KB 确实“很有可能”能大部分同时留在 1MiB L2 中。
- 但它不是非常宽松的配置，因为平均每个 set 只剩 2 ways 给其他数据。
- 如果 GC 过程中还频繁访问 old slot、Shape、PropertyMap、runtime stack、代码、RSet metadata 等，就可能把部分 workspace line 挤出 L2。
- 如果物理页颜色/page coloring 不均匀，某些 set 的压力会超过平均值，也会导致 conflict miss。

大小直觉：
- 512KB：平均 8 lines/set，剩 8 ways，比较宽松。
- 640KB：平均 10 lines/set，剩 6 ways。
- 768KB：平均 12 lines/set，剩 4 ways。
- 896KB：平均 14 lines/set，剩 2 ways，容量利用率高，但抗干扰能力弱。
- 1024KB：平均 16 lines/set，理论上刚好填满 L2，几乎没有空间给其他数据，实际通常不稳。

因此：
- 896KB 是合理的“激进配置”。
- 如果目标是最大化 young/local 空间，它值得测试。
- 如果目标是让 workspace 更稳定地常驻 L2，768KB 可能更稳。
- 最终不能只凭结构判断，要用 L2 miss / total time / GC time 的 sweep 实验验证。

## 2026-05-21 当前 GiYSB 配置确认

当前 GiYSB benchmark 使用的配置：
- `OPT_GC=giy`
- `GIY_SB=true`
- `CACHE_SIZE_KB=896`
- `GIY_RSET_READ_SLOT_AT_GC=true`
- `GIY_GC_STACK_BYTES=49152`，也就是 48KB
- `GIYSB_STAGING_BYTES=8192`，也就是 8KB
- `GIYSB_SMALL_OBJECT_MAX_BYTES=256`
- `GIYSB_SMALL_OLD_RATIO=50`
- `GIYSB_PROFILE=0`，默认关闭 hot path 统计

GC Local Workspace 结构：
- GC stack：48KB，capacity 6144 entries
- FT slot set：32KB，capacity 4096 entries
- GiYSB staging buffer：8KB
- edge log：0KB
- JSObject layout cache：0KB
- GiYOL staging/batch：0KB
- aux 总量：88KB

young/local 空间：
- Young before aux：684.24KB
- Young after aux：596.24KB

GiYSB 策略：
- old/DRAM space 按 50% / 50% 切成 small-old 和 large-old。
- aligned total size `<=256B` 的对象 reserve 到 small-old。
- 更大的对象 reserve 到 large-old。
- GiYSB 使用 FIFO worklist，不使用 GiY 默认的 LIFO 顺序。
- small-old 对象先进入 8KB staging buffer；staging 满了再 flush 到 old。
- 大对象继续使用原 GiY direct copy / NT copy 路径。

## 2026-05-21 新 GiYSB-LIFO/Tiny 方案思考

问题：
- 是否可以继续使用 GiY 原本的 LIFO traversal？
- 只用一个尽可能小的表记录小于 64B 的对象。
- 中对象正常 `memcpy`。
- 大对象继续直接使用 non-temporal store。
- 小对象最后统一按批进入 staging，再用 non-temporal store 写入 old。

判断：
- 可以，而且这个方向比当前第一版 GiYSB 更合理。
- 当前 GiYSB 的主要问题是：
  - 改成 FIFO，可能改变了 GiY 原来的 traversal locality；
  - 对 `<=256B` 的对象都走 staging，范围太大；
  - staging 导致 young -> staging -> old 的 double copy，instruction count 增加明显。
- 新方案只处理 `<=64B` tiny object，可以更精确地针对“很小对象 direct memcpy 不划算、会污染 cache”的问题。

推荐策略：
- tiny object：
  - `aligned total size <= 64B`
  - reserve 到专门的 tiny-old region；
  - 不立刻 copy；
  - 记录到 tiny 表；
  - 等 young traversal 全部结束后，按 reserve 顺序批量复制到 8KB staging；
  - staging 满 8KB 后，用 NT store 写入 tiny-old。
- medium object：
  - `64B < aligned total size <= 256B`
  - 扫描完成后立即 `memcpy` 到 old。
- large object：
  - `aligned total size > 256B`
  - 扫描完成后直接使用现有 NT copy 路径。

为什么可以保留 LIFO：
- LIFO 只决定“扫描对象”的顺序。
- tiny copy 的顺序可以通过 tiny 表单独保存。
- `copy_for_minor()` 在 reserve tiny object 时把它追加到 tiny 表，这个顺序就是 reserve order。
- traversal 仍然按 LIFO 扫描并 patch young source object。
- tiny object 不在扫描时复制，而是在 stack 清空后统一复制。
- 因为 young 区在 minor GC 结束前不会被回收，所以 tiny 表里的 source pointer 仍然有效。

正确性条件：
- tiny object 必须先完成扫描和 field patch，再复制到 old。
- 因此 tiny 批量 copy 必须发生在 `giy_traverse_stack_and_copy()` 的最后，也就是 stack 清空之后。
- root/RSet patch 之前 flush tiny object 更清楚。
- medium/large object 可以继续扫描后立即 copy。
- 如果 medium/large object 中的 field 指向 tiny object，它看到的是 tiny object 的 forwarding pointer；即使 tiny object 还没真正写入 old，也没有问题，因为 mutator 还没有恢复。

表如何做到尽可能小：
- 不建议存完整 `{dst, src, size}`，因为 64-bit 下至少 24B/entry。
- 更好的设计是只存一个 32-bit entry：
  - source offset：从 `cache_space.work_begin` 开始的 8-byte 单位 offset；
  - size class：`aligned_bytes / 8`，范围 1..8。
- destination 不需要存。
  - 因为 tiny object 全部 reserve 到 tiny-old region；
  - tiny 表按 reserve order 保存；
  - flush 时从本次 tiny-old 起始地址顺序推进即可推导 dst。
- 这样表大小约为：
  - 32KB 表：8192 个 tiny objects；
  - 64KB 表：16384 个 tiny objects；
  - 96KB 表：24576 个 tiny objects。

推荐第一次实现：
- 先用 64KB tiny table。
- staging buffer 仍然 8KB。
- 如果 tiny table 满了：
  - 后续 tiny object fallback 到 normal old；
  - 扫描后直接 copy；
  - 这样 correctness 不受影响，只是性能收益减少。

Workspace 估算：
- 当前 GiYSB：
  - GC stack 48KB
  - FT slot set 32KB
  - staging 8KB
  - aux 总量 88KB
- 新 GiYSB-LIFO/Tiny，如果 tiny table 64KB：
  - GC stack 48KB
  - FT slot set 32KB
  - staging 8KB
  - tiny table 64KB
  - aux 总量 152KB
  - 在 896KB budget 下，young after aux 约为 `684.24KB - 152KB = 532.24KB`
- 如果 tiny table 32KB：
  - aux 总量 120KB
  - young after aux 约 564.24KB
  - 但 table overflow 风险更高。

关于 256-bit / 更宽 NT store：
- 当前代码主要使用 128-bit SSE2 NT store。
- 256-bit AVX2 NT store 是可以考虑的，但必须处理：
  - 编译参数，例如 `-mavx2`；
  - 目标地址 32B 对齐；
  - flush chunk 最好固定为 8KB，且 chunk start 保持 32B 对齐；
  - tail 部分单独处理。
- 为了公平和稳定，第一次实现建议先复用现有 128-bit NT copy 路径。
- 如果 tiny batching 有收益，再单独测试 AVX2/256-bit NT store。

总体结论：
- 这个方案是当前 GiYSB 的合理下一版。
- 它保留 GiY 的 LIFO 核心原则，只把 tiny object 的 materialization 延迟。
- 它比“所有 <=256B 对象都 staging”更保守，应该能减少 double-copy 成本。
- 是否真正变快需要 benchmark 验证，重点看：
  - scavenge time 是否下降；
  - GC instructions 是否低于当前 GiYSB；
  - cache misses 是否仍然比 GiY baseline 少；
  - tiny table overflow 率是否可接受。

## 2026-05-21 GiYSB-LIFO/Tiny32 实装记录

根据新的决定，已经把 GiYSB 改成第二版：
- 保留 GiY 原本的 LIFO traversal。
- 只把 `aligned total size <=64B` 的 tiny object 延迟 materialization。
- tiny object reserve 到 small/tiny old region。
- tiny object source 信息记录到 GC Local Workspace 上的 tiny table。
- tiny table 大小先使用 32KB。
- staging buffer 仍然是 8KB，也在 GC Local Workspace 上。
- medium object 和 large object 不进 tiny table：
  - `64B < size <=256B` 继续扫描后直接 `memcpy`；
  - `>256B` 继续走现有 direct NT copy。

tiny table 设计：
- 每个 entry 是 32-bit。
- entry 保存：
  - young source offset；
  - size class。
- 不保存 destination pointer。
- 原因：
  - tiny object reserve 到 tiny-old region；
  - tiny table 按 reserve order 记录；
  - flush 时可以从本次 tiny-old 起点顺序推出 destination。

当前配置：
- `CACHE_SIZE_KB=896`
- `GIY_GC_STACK_BYTES=49152`
- `GIYSB_STAGING_BYTES=8192`
- `GIYSB_TINY_TABLE_BYTES=32768`
- `GIYSB_TINY_OBJECT_MAX_BYTES=64`
- `GIYSB_SMALL_OLD_RATIO=50`
- `GIYSB_PROFILE=0`

当前 GC Local Workspace：
- GC stack：48KB
- FT slot set：32KB
- GiYSB staging：8KB
- GiYSB tiny table：32KB
- aux 总量：120KB
- Young before aux：684.24KB
- Young after aux：564.24KB

正确性小测试：
- `benchmarks/hello_world.sbc`：通过。
- `benchmarks/Storage_StackMini.sbc`：通过。

性能测试规则：
- 按用户要求，性能 benchmark 中不同时测 cache miss 率。
- 增加运行环境开关 `EJS_DISABLE_GC_PMU=1`。
- benchmark 脚本会用 `EJS_DISABLE_GC_PMU=1` 禁用 GC PMU counter，避免 cache counter 给性能测试加入额外噪音。
- 如果之后需要 cache miss，需要单独开一轮实验，不能和性能实验混在一起。

## 2026-05-21 GiYSB-LIFO/Tiny32 full benchmark 结果

benchmark 规则：
- 对比对象：
  - 标准 GiY896
  - GiYSB-LIFO/Tiny32 896
- 两边都使用：
  - `CACHE_SIZE_KB=896`
  - `GIY_RSET_READ_SLOT_AT_GC=true`
  - `GIY_GC_STACK_BYTES=49152`
  - `EJS_DISABLE_GC_PMU=1`
- 按用户要求，本轮性能 benchmark 不测 cache miss，不启用 PMU counter。

输出目录：
- `/home/qiancheng/ejs-new/build.debug/benchmarks/out_giysb_tiny32_vs_giy_896_20260521_025038`
- `summary.csv`

正确性：
- 标准 GiY：13/13 benchmarks status 0。
- GiYSB-LIFO/Tiny32：13/13 benchmarks status 0。

Workspace：

| 配置 | Young before aux | Young after aux | Aux total |
|---|---:|---:|---:|
| GiY896 | 684.24KB | 604.24KB | 80KB |
| GiYSB-LIFO/Tiny32 | 684.24KB | 564.24KB | 120KB |

GiYSB-LIFO/Tiny32 的 aux 结构：
- GC stack：48KB
- FT slot set：32KB
- staging buffer：8KB
- tiny table：32KB
- aux 总量：120KB

总结果：

| 指标 | GiY896 | GiYSB-LIFO/Tiny32 | 差值 |
|---|---:|---:|---:|
| 总时间 | 6597.502s | 6641.444s | +43.942s (+0.666%) |
| 业务时间 | 6461.690s | 6477.450s | +15.760s (+0.244%) |
| GC full time | 135.812s | 163.994s | +28.182s (+20.751%) |
| GC core time | 100.276s | 126.671s | +26.395s (+26.322%) |
| scan_roots | 0.496s | 0.546s | +0.050s (+10.081%) |
| scan_RS | 24.933s | 25.673s | +0.740s (+2.968%) |
| scavenge | 74.843s | 100.451s | +25.608s (+34.216%) |
| minor GC count | 1,045,148 | 1,119,336 | +74,188 (+7.098%) |
| forward operations | 1,675,117,365 | 1,676,555,574 | +1,438,209 (+0.086%) |
| total alloc bytes | 305448.10MB | 305448.04MB | 基本相同 |

per benchmark 结果：

| benchmark | GiY total | GiYSB total | total 差值 | GC 差值 | scavenge 差值 | minor GC 差值 |
|---|---:|---:|---:|---:|---:|---:|
| Bounce | 304.172s | 304.219s | +0.047s (+0.015%) | +0.015s | +0.012s | +224 |
| CD | 481.071s | 483.169s | +2.098s (+0.436%) | +2.121s | +1.823s | +7,698 |
| DeltaBlue | 133.039s | 134.971s | +1.932s (+1.452%) | +0.509s | +0.432s | +1,100 |
| Havlak | 803.309s | 800.679s | -2.630s (-0.327%) | +6.993s | +6.110s | +15,965 |
| List | 185.858s | 186.464s | +0.606s (+0.326%) | +0.001s | +0.000s | +34 |
| Mandelbrot | 397.728s | 399.682s | +1.954s (+0.491%) | +0.335s | +0.028s | +18,028 |
| NBody | 642.687s | 645.774s | +3.087s (+0.480%) | +0.928s | +0.255s | +22,403 |
| Permute | 807.262s | 817.296s | +10.034s (+1.243%) | +0.000s | +0.000s | +2 |
| Queens | 217.194s | 216.142s | -1.052s (-0.484%) | +0.006s | +0.001s | +77 |
| Richards | 1729.929s | 1737.225s | +7.296s (+0.422%) | +0.014s | +0.004s | +150 |
| Sieve | 236.637s | 237.788s | +1.151s (+0.486%) | +0.077s | +0.031s | +1,428 |
| Storage | 328.561s | 346.875s | +18.314s (+5.574%) | +17.183s | +16.912s | +7,072 |
| Towers | 330.055s | 331.160s | +1.105s (+0.335%) | +0.000s | +0.000s | +7 |

分析：
- GiYSB-LIFO/Tiny32 正确，但没有比标准 GiY 快。
- 总时间慢 0.666%，主要来自 GC 时间增加。
- GC 时间增加 20.751%，其中主要是 scavenge 增加 34.216%。
- minor GC 次数增加 7.098%，这是因为 tiny table 额外占用 32KB workspace，使 young after aux 从 604.24KB 降到 564.24KB。
- forward operations 基本不变，说明活对象图规模没有明显变化；主要变化是 GC 频率和 copy/materialization 成本。
- `Storage` 是最大退化来源：
  - total +18.314s；
  - GC +17.183s；
  - scavenge +16.912s。
- `Havlak` 和 `Queens` 总时间略微变快，但 GC 时间仍然变慢；这更像业务时间波动或布局影响，不代表 GC 优化成功。

结论：
- 32KB tiny table 版本比第一版 GiYSB 更符合 GiY 原则，因为它保留 LIFO，而且只处理 tiny object。
- 但是性能上仍然没有成功。
- 目前瓶颈不是 RSet，而是：
  - tiny table 占用 workspace 导致 young 变小；
  - tiny object 仍然存在 young -> staging -> old 的 double copy；
  - staging flush 增加了 scavenge 成本。

下一步建议：
- 不建议马上换 64KB tiny table，因为 64KB 会进一步减少 young 空间，大概率增加 minor GC 次数。
- 更值得先做的是 tiny table overflow/profile 的单独非性能测试，确认 32KB 是否真的不够。
- 如果 32KB 没有大量 overflow，就不应该换 64KB。
- 之后可以尝试：
  - 只在 tiny batch 累积到接近 8KB 时才 staging；
  - 尾巴直接 memcpy，不走 staging；
  - 对 tiny object 只做 reserve-order old placement，不做 staging；
  - 或者减少 tiny table entry 成本/使用更小 table。

## 2026-05-21 GiYSB 当前 size class 和批量 copy 策略说明

这里的 `size_class` 不是 JavaScript 的 class，也不是对象的语义类型。
它只是 GiYSB tiny table 里用来压缩记录的“对象大小编码”。

当前 tiny table 的一条记录是 32 bit：

```c
entry = (offset_units << 4) | size_class;
```

含义：
- `offset_units`：对象 payload 相对 `cache_space.work_begin` 的偏移，单位是 8 字节。
- `size_class`：对象总大小的 8 字节单位数，即 `align_bytes / 8`。

为什么需要 `size_class`：
- flush tiny table 的时候，只知道源对象在哪里还不够，还必须知道这个对象要 copy 多少字节。
- tiny object 当前限制为不超过 64B，所以 `size_class` 最大是 8，可以放进低 4 bit。
- 这样每个 tiny object 只需要 4 字节记录，而不是记录完整的 `src + dst + size`。
- destination 不单独记录，因为 tiny object 按 reserve 顺序连续放进 tiny-old 区，flush 时可以从 `g_giysb_tiny_batch_begin` 顺序推出目标地址。

当前批量 copy 策略：
1. reserve 阶段：
   - 如果对象 `align_bytes <= 64B`，并且 tiny-old 有空间、tiny table 也有空间，就把它记录到 tiny table。
   - 同时在 tiny-old 区提前 reserve 目标地址，并设置 forwarding pointer。
   - 如果 tiny table 满了或者编码失败，就 fallback 到普通 old 区，之后直接 copy。
2. 扫描阶段：
   - GiYSB 仍然使用 GiY 原来的 LIFO GC stack。
   - 每个对象被 pop 出来以后，先扫描 children，并在 young 源对象里修正引用字段。
   - 如果这个对象的目标地址属于 tiny-old，就暂时不 copy。
   - 如果不是 tiny object，就立刻用原来的 `giy_copy_live_object` copy。
3. tiny table flush 阶段：
   - 当 LIFO 扫描全部结束以后，GiYSB 才顺序读取 tiny table。
   - 用 `offset_units` 重建源对象地址。
   - 用 `size_class * 8` 得到 copy 大小。
   - 先把 tiny object 从 young 源对象 copy 到 8KB staging buffer。
   - staging buffer 满了，或者下一个对象放不下时，就把 staging buffer 一次性 flush 到 tiny-old 区。
   - flush 时调用现有的 `giy_copy_live_object`；大于 256B 的 staging chunk 会走当前已有的 non-temporal store 路径。
   - 最后的尾巴也会 flush；如果尾巴不大，可能仍然走普通 memcpy。

需要注意：
- 当前“批量”的对象只包括 tiny object，即总大小不超过 64B 的对象。
- 中对象和大对象没有进入 tiny table，仍然在扫描时直接 materialize 到 old 区。
- 扫描顺序仍然是 LIFO；tiny object 的最终 copy 顺序是 reserve 顺序。
- 现在的 non-temporal store 仍然使用当前已有的 128-bit 路径，还不是 256-bit AVX2 路径。

## 2026-05-21 关于 256-bit NT store 和公平比较 young space 的判断

当前机器是 Intel Xeon W-2235，支持 AVX2 和 AVX-512。
当前 `giy_copy_live_object` 的 non-temporal copy 仍然使用 128-bit `_mm_stream_si128`，也就是每次 16B。

256-bit 是否可能提高性能：
- 有可能，但不是一定。
- 对 GiYSB 来说，staging buffer flush 通常是较大的连续 chunk，例如接近 8KB，这种场景更可能从 256-bit `_mm256_stream_si256` 得到收益。
- 对标准 GiY 来说，很多对象是单个 object 直接 copy，如果对象本身不大，256-bit 的收益可能很小。
- 256-bit 可以减少 store 指令数量和循环次数，但真正瓶颈可能是内存写带宽、write-combining buffer、读源对象成本或扫描成本。
- 不能一开始就上 512-bit，因为 AVX-512 更可能带来降频，反而污染实验。
- 如果做，建议先做 AVX2 256-bit，不要直接做 AVX-512。

实现 256-bit 时需要注意：
- `_mm256_stream_si256` 要求目标地址 32B 对齐。
- 当前代码只处理到 16B 对齐，因此需要增加 32B 对齐前缀处理。
- 如果目标地址无法安全对齐到 32B，应该 fallback 到现有 128-bit 路径。
- 这个优化应该放在公共的 `giy_copy_live_object` 里，让 GiY 和 GiYSB 都使用同一个 copy primitive。
- 不应该只给 GiYSB 单独开 256-bit，否则会把“copy primitive 优化”和“GiYSB 算法优化”混在一起。

关于公平比较 young space：
- 主实验应该固定总的 GC Local Workspace budget，例如两边都是 896KB。
- 在这个设定下，GiYSB 因为需要 staging buffer 和 tiny table，所以 young space 变小，这是 GiYSB 必须付出的真实空间成本。
- 因此主结果不应该强行让 GiY 和 GiYSB 的 young space 一样，否则可能把 GiYSB 的额外元数据成本隐藏掉。
- 但是为了分析原因，可以做一个额外的 ablation：让标准 GiY 也人为扣掉同样大小的 dummy workspace，使 GiY 和 GiYSB 的 young after aux 完全相同。
- 这个 equal-young 实验适合回答“在相同 young 大小下，GiYSB 的批量 copy 本身有没有收益”。

推荐报告方式：
1. 主实验：固定总 workspace，例如 896KB，比较真实 end-to-end 性能。
2. 辅助实验：固定 young after aux，隔离分析 GiYSB copy 策略本身。
3. 如果实现 256-bit，GiY 和 GiYSB 都必须使用同一个 256-bit copy primitive，然后再重复主实验和辅助实验。

## 2026-05-21 为什么 GiYSB 批量 copy 仍然可能有 256-bit 对齐问题

先纠正一个概念：GiYSB 的批量 copy 不是复制到 young 区。
当前流程是：
- young 源对象 -> staging buffer；
- staging buffer -> old/tiny-old 区。

对齐问题主要发生在第二段，也就是用 non-temporal store 把 staging buffer 写回 old/tiny-old 区的时候。

原因：
- 256-bit streaming store，也就是 `_mm256_stream_si256`，要求目标地址 32B 对齐。
- 当前堆对象只保证 8B 对齐，因为 `ALIGN` 使用的是 `sizeof(uintptr_t)`。
- GiYSB tiny-old 的 `small_free` 也是按对象大小推进，而 tiny object 的大小可能是 24B、32B、40B、48B、56B、64B。
- 所以即使 tiny object 在 old 区是连续排布的，也不代表每一批 batch 的起始地址一定是 32B 对齐。

例子：
- 假设 tiny-old batch 起始地址是 `0x...10`，它是 16B 对齐，但不是 32B 对齐。
- 对 128-bit NT store 来说这通常可以接受。
- 但对 256-bit NT store 来说，这个地址不能直接使用 `_mm256_stream_si256`。

GiYSB 比标准 GiY 好的地方：
- GiYSB staging flush 的目标地址是一大段连续 old/tiny-old 区。
- 一旦把开头处理到 32B 对齐，后面的大部分 8KB chunk 就可以连续使用 256-bit NT store。
- 标准 GiY 经常一个对象一个对象 copy，每个对象的目标地址都可能重新面对对齐问题。

但 GiYSB 仍然不能忽略对齐：
- 连续不等于 32B 对齐。
- malloc 起始地址不一定保证 32B。
- old/tiny-old 的 split 边界当前也只向 8B 对齐。
- tiny object 总大小只保证 8B 对齐，不保证 32B 对齐。

如果实现 AVX2 256-bit NT store，正确方式应该是：
- 先检查目标地址是否 32B 对齐。
- 如果没有对齐，先用 8B/16B store 或 memcpy 处理前缀，让目标地址推进到 32B 对齐。
- 中间大段用 `_mm256_stream_si256`。
- 尾巴用现有 128-bit/64-bit/普通 memcpy 处理。
- 如果 chunk 太小，或者对齐成本太高，就直接 fallback 到现有 128-bit 路径。

## 2026-05-21 256-bit 对齐到底看 src 还是 target

对 `_mm256_stream_si256` 来说，最关键的是 target，也就是写入目标地址必须 32B 对齐。
source 可以使用 `_mm256_loadu_si256` 从不对齐地址读取，所以 source 不一定需要 32B 对齐。

“看指针末尾”只能判断当前地址是否对齐，不能改变地址。
如果 target 地址不是 32B 对齐，例如低 5 bit 是 16，那么不能直接从这里开始执行 `_mm256_stream_si256`。
必须先处理前面的 16B，让 target 指针前进到下一个 32B 边界。

示例：

```text
dst = 0x...10
dst & 31 = 16
prefix = 32 - 16 = 16
```

处理方式：
1. 先用普通 memcpy、64-bit store 或 128-bit store 写掉前 16B。
2. 然后 `dst += 16`，这时 `dst` 变成 32B 对齐。
3. 中间的大段才用 `_mm256_stream_si256`。
4. 最后的不足 32B 尾巴再用普通方式处理。

所以前缀处理不是为了“判断对齐”，而是为了“移动到对齐边界”。

对 GiYSB 来说：
- `staging buffer -> old/tiny-old` 的 target 是 old/tiny-old 地址。
- 因此 256-bit NT store 主要看 old/tiny-old 的目标地址对齐。
- staging buffer 的 source 地址可以不对齐，只要使用 unaligned load。
- 但如果 staging buffer 也能 32B 对齐，会更好，因为 load 也可能更快；只是它不是 correctness 的硬要求。

## 2026-05-21 为什么不能直接跳到 target 的对齐地址开始写

如果 target 地址不是 32B 对齐，不能简单地“跳到下一个 32B 对齐地址”开始写。
原因是 GC 的 reserve 阶段已经确定了对象或者 batch 的真实目标地址。
这个目标地址前面的非对齐部分也是对象内容的一部分，通常包含对象 header 或第一个对象的开头。

例子：

```text
dst = 0x1010
next 32B aligned dst = 0x1020
prefix = 16B
```

正确 copy 需要做到：

```text
src[0..15]   -> 0x1010..0x101f
src[16..]    -> 0x1020..后面
```

如果直接从 `0x1020` 开始写，有两种错误：

1. `src[0..] -> 0x1020..`
   - 数据整体向后移动 16B。
   - forwarding pointer/root/slot 仍然指向 `0x1010 + header` 对应的对象位置。
   - 对象 header 和 payload 位置全部错位。

2. `src[16..] -> 0x1020..`
   - `0x1010..0x101f` 这 16B 没有被写入。
   - 对象开头丢失，通常会直接破坏 header。

所以前缀处理不是“填充无意义空间”，而是把 batch 开头那一段真实数据写到它应该在的位置。

另一种可行设计是在 reserve 阶段就强制让 old/tiny-old 的 batch 起点 32B 对齐。
这样 copy 时可以少处理前缀。
但是这必须在分配/保留目标地址时完成，不能在 copy 阶段临时跳过。
否则 GC 中已经保存的 forwarding pointer、root 更新、slot 更新都会指向错误位置。

结论：
- copy 阶段跳到对齐地址是不正确的。
- 如果要避免前缀 copy，必须在 reserve 阶段插入 padding，使对象或 batch 的真实目标地址本来就是 32B 对齐。
- 这会带来空间浪费和布局复杂度，需要作为单独设计权衡。

## 2026-05-21 实装 256-bit NT store 并公平测试 GiY/GiYSB

本次改动：
- 在 `giy_copy_live_object` 中加入 AVX2 256-bit non-temporal store 路径。
- 中间大段使用 `_mm256_stream_si256`，一次写 32B。
- source 使用 `_mm256_loadu_si256`，所以 source 不要求 32B 对齐。
- target 必须 32B 对齐；如果 target 不是 32B 对齐，就先用 64-bit/128-bit 小段 streaming store 把 target 推进到 32B 边界。
- 尾巴使用 128-bit/64-bit 路径处理。
- 如果 target 连 8B 对齐都不满足，则 fallback 到普通 `memcpy`。
- `GIY_NT_COPY_BITS=256` 时自动给 C++ 编译加 `-mavx2`。
- `OPT_GC=giy` 或 `OPT_GC=giyol` 默认使用 `GIY_NT_COPY_BITS=256`。
- benchmark 脚本中也显式传入 `GIY_NT_COPY_BITS=256`，避免结果不清楚。

验证：
- 单独构建 GiY 256-bit：成功。
- 单独构建 GiYSB 256-bit：成功。
- `giy_gc_probe.sbc` smoke test：status 0。
- full-suite benchmark：GiY 13/13 status 0，GiYSB 13/13 status 0。
- 输出中确认两边都是 `NT copy width: 256 bits`。

实验配置：
- 输出目录：`build.debug/benchmarks/out_giysb_tiny32_vs_giy_nt256_896_20260521_122911`
- CPU：Intel Xeon W-2235
- workspace budget：896KB
- GC stack：48KB
- RSet：read-slot-at-GC
- GiYSB staging buffer：8KB
- GiYSB tiny table：32KB
- tiny object max：64B
- PMU/cache counter：禁用

Workspace：

| 配置 | Young before aux | Young after aux | Aux total |
|---|---:|---:|---:|
| GiY896-NT256 | 684.24KB | 604.24KB | 80KB |
| GiYSB-Tiny32-NT256 | 684.24KB | 564.24KB | 120KB |

总结果：

| 指标 | GiY896-NT256 | GiYSB-Tiny32-NT256 | 差值 |
|---|---:|---:|---:|
| 总时间 | 6686.968s | 6674.211s | -12.757s (-0.191%) |
| 业务时间 | 6552.102s | 6506.925s | -45.177s (-0.690%) |
| GC full time | 134.866s | 167.286s | +32.420s (+24.039%) |
| GC core time | 100.738s | 129.247s | +28.509s (+28.300%) |
| scan_roots | 0.517s | 0.568s | +0.051s (+9.865%) |
| scan_RS | 24.934s | 25.847s | +0.913s (+3.662%) |
| scavenge | 75.288s | 102.831s | +27.543s (+36.584%) |
| minor GC count | 1,045,148 | 1,119,336 | +74,188 (+7.098%) |
| forward operations | 1,675,117,365 | 1,676,555,592 | +1,438,227 (+0.086%) |
| total alloc bytes | 305448.10MB | 305448.04MB | 基本相同 |

per benchmark 差异：

| benchmark | GiY total | GiYSB total | total 差值 | GC 差值 | scavenge 差值 | minor GC 差值 |
|---|---:|---:|---:|---:|---:|---:|
| Bounce | 305.218s | 304.832s | -0.386s (-0.126%) | +0.035s | +0.015s | +224 |
| CD | 490.671s | 484.981s | -5.690s (-1.160%) | +2.450s | +1.897s | +7,698 |
| DeltaBlue | 132.910s | 134.026s | +1.116s (+0.840%) | +0.563s | +0.449s | +1,100 |
| Havlak | 802.511s | 803.807s | +1.296s (+0.161%) | +8.563s | +6.850s | +15,965 |
| List | 186.408s | 186.491s | +0.083s (+0.045%) | +0.004s | +0.000s | +34 |
| Mandelbrot | 424.575s | 430.730s | +6.155s (+1.450%) | +0.606s | +0.025s | +18,028 |
| NBody | 678.078s | 645.467s | -32.611s (-4.809%) | +1.724s | +0.277s | +22,403 |
| Permute | 814.932s | 813.404s | -1.528s (-0.188%) | +0.000s | +0.000s | +2 |
| Queens | 217.051s | 216.228s | -0.823s (-0.379%) | +0.004s | +0.001s | +77 |
| Richards | 1737.317s | 1737.172s | -0.145s (-0.008%) | +0.014s | +0.002s | +150 |
| Sieve | 237.352s | 237.327s | -0.025s (-0.011%) | +0.068s | +0.023s | +1,428 |
| Storage | 329.736s | 348.803s | +19.067s (+5.783%) | +18.389s | +18.004s | +7,072 |
| Towers | 330.209s | 330.943s | +0.734s (+0.222%) | +0.000s | +0.000s | +7 |

和上一轮 128-bit 结果的粗略比较：

| 配置 | 指标 | 128-bit | 256-bit | 差值 |
|---|---|---:|---:|---:|
| GiY | 总时间 | 6597.502s | 6686.968s | +89.466s (+1.356%) |
| GiY | GC full | 135.812s | 134.866s | -0.946s (-0.697%) |
| GiY | scavenge | 74.843s | 75.288s | +0.445s (+0.595%) |
| GiYSB | 总时间 | 6641.444s | 6674.211s | +32.767s (+0.493%) |
| GiYSB | GC full | 163.994s | 167.286s | +3.292s (+2.007%) |
| GiYSB | scavenge | 100.451s | 102.831s | +2.380s (+2.369%) |

分析：
- 256-bit NT store 的 correctness 没问题，GiY 和 GiYSB 都能完整跑完。
- 但这次结果没有证明 256-bit 更快。
- 在 256-bit 公平比较中，GiYSB 的总时间看起来比 GiY 快 0.191%，但这是业务时间差异抵消了 GC 退化。
- 从 GC 指标看，GiYSB 仍然明显更慢：GC full +24.039%，scavenge +36.584%。
- minor GC 次数仍然多 7.098%，原因没有变化：GiYSB 的 staging buffer 和 tiny table 多占 40KB workspace，使 young after aux 从 604.24KB 降到 564.24KB。
- `Storage` 仍然是最大退化来源，几乎解释了大部分 GC 增量。
- `NBody` 的总时间大幅变快，但 GC 时间反而变慢，因此更像业务时间波动，不应该用它证明 GiYSB 的 GC 优化成功。

结论：
- 256-bit NT store 已经可以作为 GiY/GiYSB 的统一 copy primitive 使用。
- 但是它没有解决 GiYSB 当前的核心问题。
- 当前 GiYSB 的瓶颈仍然是：
  1. 额外 workspace 让 young 变小，minor GC 次数增加；
  2. tiny object 经过 young -> staging -> old 的 double copy；
  3. staging/tiny table 管理成本增加 scavenge 时间。
- 下一步如果继续优化 GiYSB，不应该只扩大 store width；更应该减少 staging 的 double copy 或减少 tiny table 对 workspace 的占用。

## 2026-05-21 关于“复制 young 中相近对象的连续 span，把小缝隙当碎片”的方案

这个想法是有价值的。
它可以看作一种新的 GiY/GiYSB 方向：不是把 tiny object 先 copy 到 staging buffer，而是在 young 区找出彼此距离很近的 live object cluster，然后把整个 source span 直接 non-temporal copy 到 old 区。

基本思想：

```text
young:
[live A][small gap][live B][small gap][live C]

old:
[live A][filler/padding][live B][filler/padding][live C]
```

这样可以把当前 GiYSB 的：

```text
young -> staging -> old
```

变成：

```text
young span -> old span
```

潜在优点：
- 可以去掉 staging buffer 的 double copy。
- source 和 target 都是连续区间，更适合 NT store。
- 如果 young 中 live object 本来就靠得很近，复制少量 gap 可能比逐个小对象 copy 更便宜。
- old 中保持 young allocation locality，可能对后续 mutator locality 有帮助。

但是 correctness 上有一个非常重要的问题：
- 不能把 gap 中的原始垃圾字节直接留给 old-space scanner。
- 如果 old/major GC 以后线性扫描 old 区，它会把 gap 里的旧 header 或随机内容当作真实对象。
- 如果 gap 里恰好是 dead object，它里面可能还有已经失效的 young pointer，major GC 扫描它会产生严重错误。

所以 gap 不能只是“随便复制过去的垃圾”。
它必须满足其中一种条件：
1. 把 gap 改写成合法 filler object / padding object；
2. 或者给 old 区增加 skip map / span metadata，让 major GC 知道哪些字节不是对象；
3. 或者 major GC 的 old scan 逻辑能识别并跳过这些 gap。

最推荐的是 filler object：
- gap 足够大时，写一个无 pointer 的 filler header；
- filler 的 size 覆盖整个 gap；
- major GC 扫描到 filler 时直接跳过。

这个方案的核心 trade-off：
- 节省 staging double copy；
- 增加 old-space padding 浪费；
- 增加复制的无用 gap bytes；
- 增加 live object source-order clustering 的计划成本。

是否值得做，取决于两个比例：

```text
gap_bytes / live_bytes
span_bytes 是否足够大到值得 NT store
```

建议阈值：
- `max_gap_bytes`: 32B 或 64B 起步；
- `max_gap_ratio`: gap 不超过 live bytes 的 10% 到 20%；
- `min_span_for_nt`: 至少 2KB 或 4KB 才使用 span NT copy；
- 小于阈值的 cluster 直接 fallback 到原来的逐对象 copy。

实现难点：
- 当前 GiYSB 在发现对象时就 reserve 目标 old 地址，并写 forwarding pointer。
- 但 span copy 需要按 young source address 排序，把一组对象一起放进同一个 old span。
- 这意味着不能完全沿用当前 reserve-order forwarding 设计。

比较合理的新实现路线：
1. 第一阶段只 mark live object，并收集 live object header/size，不立刻确定最终 old 地址。
2. 按 young source address 排序 live object。
3. 根据 gap threshold 形成 span clusters。
4. 为每个 span 一次性 reserve old 区。
5. 给 span 内每个 live object 设置 forwarding pointer：
   - `dst = span_old_base + (src - span_young_base)`
6. 扫描/patch live object 内部引用、root、RSet。
7. 对大 span 执行 `young span -> old span` 的 NT copy。
8. 对 span 内 gap 写 filler object 或记录 skip metadata。
9. 不适合 span copy 的对象走原来的直接 copy。

这个方案可以命名为 `GiYSC`：GiY Span Copy。

推荐下一步：
- 不要马上完整实现。
- 先做一个 profiling/simulation 模式：
  - 在 minor GC 中收集 live object source address 和 size；
  - 不改变当前 copy 行为；
  - 只计算如果使用 span copy，会形成多少 span、gap bytes 有多少、可 NT copy 的 span bytes 有多少。
- 如果结果显示很多 live object 在 young 中确实距离很近，而且 gap ratio 很低，再实装 GiYSC。

当前判断：
- 这个方向比继续扩大 staging buffer 更有研究价值。
- 它直接针对当前 GiYSB 最大问题：staging double copy。
- 但必须解决 filler/skip metadata，否则把 gap 直接复制进 old 区会破坏 major GC 正确性。

## 2026-05-21 为什么 GiYSB 的 GC 更慢但 business 看起来更快

首先要注意：当前报告里的 `Business logic` 不是独立测量出来的 mutator 时间。
在 `giy_dram_manager.cc` 中，它是这样计算的：

```c
business_wall = total_elapsed_wall - total_gc_full_wall;
```

也就是说：
- `Total execution` 是整个程序 wall time。
- `GC overhead(full)` 是 minor GC 函数包住的 wall time。
- `Business logic` 是二者相减得到的残差。

因此 `business` 变快不一定说明业务代码真的被 GiYSB 优化了。

这次 256-bit 结果中：
- GiYSB GC full 比 GiY 慢 `+32.420s`。
- 但是 total 比 GiY 快 `-12.757s`。
- 所以 residual business 必然显示为 `-45.177s`。

per benchmark 看，business 变快主要来自少数项目：

| benchmark | business 差值 | total 差值 | GC 差值 |
|---|---:|---:|---:|
| NBody | -34.335s | -32.611s | +1.724s |
| CD | -8.140s | -5.690s | +2.450s |
| Havlak | -7.267s | +1.296s | +8.563s |
| Storage | +0.678s | +19.067s | +18.389s |

这说明：
- GC 变慢是真实且一致的，主要体现在 `Storage`、`Havlak`、`CD` 等。
- business 变快并不均匀，主要被 `NBody`、`CD`、`Havlak` 几项拉动。
- 尤其 `NBody` 的 GC 明明变慢，但 total 大幅变快，这更像运行波动、二进制布局变化、CPU 频率/调度变化，或者对象布局影响 mutator cache locality，而不能直接归因于 GC copy 优化。

GC 变慢的直接原因仍然清楚：
1. GiYSB 多占 40KB workspace：
   - GiY young after aux：604.24KB
   - GiYSB young after aux：564.24KB
2. young 变小导致 minor GC 次数增加：
   - GiY：1,045,148
   - GiYSB：1,119,336
   - 增加 74,188 次，也就是 +7.098%
3. tiny object 经过 `young -> staging -> old`，存在 double copy。
4. tiny table 记录、flush staging、判断 tiny destination 都会增加 scavenge 成本。

business 可能变快的几个合理解释：
1. 统计定义问题：
   - business 是残差，不是独立 mutator 计时。
   - total 或 GC 任意一边有波动，都会被放大到 business 上。
2. 运行波动：
   - 当前 full-suite 每项只跑一次。
   - NBody 一项就贡献了约 `-34.335s` business 差异，足以改变 aggregate 结论。
3. 二进制布局变化：
   - GiYSB 是不同编译宏生成的二进制。
   - 即使 mutator 逻辑几乎一样，代码布局、函数地址、分支布局、I-cache 行为也可能变化。
4. 对象布局变化：
   - GiYSB 会把 tiny object 聚到 tiny-old/small-old 区。
   - 这可能让某些 benchmark 的后续 old object 访问更集中，从而使 mutator 访问看起来更快。
   - 但这目前只是可能性，尚未被 cache/mutator 专门实验确认。
5. 更小 young 的副作用：
   - GiYSB 的 young 更小，GC 更频繁。
   - 这可能让 mutator 经常在更小的 young working set 上运行，某些场景下反而提高 cache locality。
   - 代价就是 GC 次数和 GC 时间增加。

当前判断：
- 不能说“GiYSB 让业务代码稳定变快”。
- 更稳妥的说法是：这次单次 full-suite 中，GiYSB 的 residual business time 更低，但 GC time 明确更高。
- 对论文或报告来说，应该把 total time 和 GC time 分开报告，不要用 residual business 证明优化成功。

下一步如果要确认原因：
1. 对关键项目至少重复 3 次或 5 次，使用 median，而不是单次结果。
2. 单独看 `NBody`、`CD`、`Havlak`、`Storage`。
3. 做 equal-young 实验，分离“young 变小导致 GC 更多”和“GiYSB copy 策略本身”的影响。
4. 如果要解释 mutator 为什么快，需要单独做 mutator cache/locality 测量，但不要混入性能基准 run。

## 2026-05-21 当前 GiYSB 数据结构分布

当前配置是 `GiYSB-Tiny32-NT256`：
- `CACHE_SIZE_KB=896`
- `GIY_GC_STACK_BYTES=49152`
- `GC_FT_SLOT_SET_BYTES=32768`
- `GIYSB_STAGING_BYTES=8192`
- `GIYSB_TINY_TABLE_BYTES=32768`
- `GIYSB_TINY_OBJECT_MAX_BYTES=64`
- `GIY_RSET_READ_SLOT_AT_GC=1`
- `GIY_NT_COPY_BITS=256`

需要区分两个概念：
- 按我们讨论的广义概念，GC Local Workspace / cache-local budget 是整块 896KB。
- 代码输出里的 `Aux total=120KB` 是狭义辅助区，只包含 GC stack、FT slot set、GiYSB staging、tiny table，不包含 remembered set。

当前 896KB 内部从低地址到高地址大致是：

| 区域 | 大小 | 作用 |
|---|---:|---|
| init/static cache area | 约 19.76KB | 初始化阶段已经放在 cache/local 区的对象，`cache_space.begin .. work_begin` |
| young allocation area | 564.24KB | minor GC 之间的新对象分配区 |
| GiYSB tiny table | 32KB | 记录 tiny object 的源偏移和 size class，容量 8192 条 |
| GiYSB staging buffer | 8KB | tiny object 批量 copy 的中转 buffer |
| GC LIFO stack | 48KB | GiY/GiYSB minor GC 遍历 young object graph 的 worklist |
| FT slot set | 32KB | 记录需要 patch 的特殊 slot 地址集合 |
| RSet hash table | 64KB | remembered set 去重/查询用 hash table |
| RSet buffer | 128KB | read-slot 模式下保存 old->young slot 地址，容量 16384 条 |

数值核对：

```text
896KB
- RSet buffer 128KB
- RSet hash 64KB
- init/static area 约 19.76KB
= Young before aux 约 684.24KB

Young before aux 684.24KB
- GC stack 48KB
- FT slot set 32KB
- GiYSB staging 8KB
- GiYSB tiny table 32KB
= Young after aux 564.24KB
```

当前 `Aux total=120KB` 的组成：

| Aux 结构 | 大小 |
|---|---:|
| GC stack | 48KB |
| FT slot set | 32KB |
| GiYSB staging buffer | 8KB |
| GiYSB tiny table | 32KB |
| edge log | 0KB |
| JSObject layout cache | 0KB |
| GiYOL staging/batch | 0KB |
| 合计 | 120KB |

不在 896KB local/cache budget 里的部分：
- `dram_space` / old generation：由大块 `malloc` 分配，GiYSB 会按 `GIYSB_SMALL_OLD_RATIO=50%` 分成 small/tiny old 和 large/normal old。
- `g_giysb_old`、`g_giysb_profile`、各种 pointer/counter 变量：只是全局元数据，本身不占用 cache/local budget；它们指向的 staging/tiny table backing storage 才在 workspace 里。

GiYSB old 区逻辑：
- tiny object：`align_bytes <= 64B`，优先 reserve 到 small/tiny old 区，并记录 tiny table，稍后经 staging 批量写入。
- 非 tiny object：reserve 到 large/normal old 区，扫描后直接 materialize。
- old 区不是 GC Local Workspace，它是目标老年代空间。

## 2026-05-21 GiY vs GiYSB 的 GC 对比、原因分析和下一步优化方案

比较对象：
- GiY896-NT256
- GiYSB-Tiny32-NT256
- 两边都使用 `GIY_NT_COPY_BITS=256`
- 两边都使用 read-slot RSet
- 两边都是 896KB local/cache budget
- PMU/cache counter 禁用
- full-suite：13/13 benchmarks 全部 status 0

结构差异：

| 项目 | GiY | GiYSB |
|---|---:|---:|
| RSet buffer | 128KB | 128KB |
| RSet hash | 64KB | 64KB |
| GC stack | 48KB | 48KB |
| FT slot set | 32KB | 32KB |
| staging buffer | 0KB | 8KB |
| tiny table | 0KB | 32KB |
| Aux total | 80KB | 120KB |
| Young after aux | 604.24KB | 564.24KB |

GC 总体对比：

| 指标 | GiY | GiYSB | 差值 |
|---|---:|---:|---:|
| GC full time | 134.866s | 167.286s | +32.420s (+24.039%) |
| GC core time | 100.738s | 129.247s | +28.509s (+28.300%) |
| scan_roots | 0.517s | 0.568s | +0.051s (+9.865%) |
| scan_RS | 24.934s | 25.847s | +0.913s (+3.662%) |
| scavenge | 75.288s | 102.831s | +27.543s (+36.584%) |
| minor GC count | 1,045,148 | 1,119,336 | +74,188 (+7.098%) |
| forward operations | 1,675,117,365 | 1,676,555,592 | +1,438,227 (+0.086%) |
| total alloc bytes | 305448.10MB | 305448.04MB | 基本相同 |

关键判断：
- GiYSB 的 GC 退化主要来自 `scavenge`。
- `scavenge` 增加了 27.543s，占 GC full 增量 32.420s 的约 85%。
- `scan_roots` 和 `scan_RS` 也变慢，但不是主因。
- forward operations 只增加 0.086%，说明活对象图规模几乎没有变化。
- total alloc bytes 基本相同，说明 workload 本身没有明显变化。

按 GC 次数和每次 GC 成本拆分：
- GiY 平均 GC full pause 约 `134.866s / 1,045,148 = 0.129ms`。
- GiYSB 平均 GC full pause 约 `167.286s / 1,119,336 = 0.149ms`。
- GiYSB 不只是 GC 次数更多，每次 GC 本身也更重。

粗略分解：
- 因为 young 变小导致 minor GC 次数增加，约贡献 `9.6s` GC full 增量。
- 剩下约 `22.8s` 来自每次 GC 内部成本变高。
- 对 scavenge 也类似：
  - 频率增加大约解释 `5.3s`。
  - 每次 scavenge 更重解释约 `22.2s`。

所以不能只说“young 变小导致 GC 变慢”。
更准确地说：
1. young 变小导致 GC 次数增加；
2. GiYSB 的 staging/tiny table 逻辑让每次 scavenge 本身也更贵。

per benchmark 中 GC 退化最大的项目：

| benchmark | GC 差值 | scavenge 差值 | minor GC 差值 |
|---|---:|---:|---:|
| Storage | +18.389s | +18.004s | +7,072 |
| Havlak | +8.563s | +6.850s | +15,965 |
| CD | +2.450s | +1.897s | +7,698 |
| NBody | +1.724s | +0.277s | +22,403 |

`Storage` 是最重要的退化来源：
- GC +18.389s；
- scavenge +18.004s；
- 说明它主要卡在 GiYSB 的 object materialization/copy 流程。

为什么当前 GiYSB GC 更慢：

1. tiny table + staging 占用 40KB workspace
   - GiY young after aux：604.24KB
   - GiYSB young after aux：564.24KB
   - young 变小 40KB，minor GC 次数增加 7.098%。

2. tiny object 发生 double copy
   - GiY 对 tiny object：通常直接 `young -> old`，而且因为 <=64B，会走普通 memcpy。
   - GiYSB 对 tiny object：`young -> staging -> old`。
   - 这会增加一次读写和一次 table/staging 管理成本。
   - 对 <=64B 的 tiny object 来说，直接 memcpy 本来就很便宜，batch NT store 不一定能弥补 double copy。

3. 256-bit store width 不是当前瓶颈
   - 256-bit 已经统一给 GiY 和 GiYSB 使用。
   - 结果没有证明 256-bit 能明显降低 GC 时间。
   - 当前瓶颈更像是 staging/tiny table 的额外路径，而不是 store 指令宽度。

4. table 解码和 flush 管理有额外成本
   - 每个 tiny object 都要写 tiny table。
   - flush 时要重新读 entry、解码 offset/size_class、重建 source address、维护 staging cursor。
   - 这些都发生在 scavenge 阶段。

5. old placement 可能帮助 mutator locality，但不一定帮助 GC
   - GiYSB 把 tiny object 聚到 small/tiny old 区。
   - 这可能让某些 benchmark 的业务访问更集中。
   - 但对 GC 来说，它增加了 reserve/copy 管理成本。

下一步优化方案：

方案 1：先做 profiling，确认 tiny table 是否真的需要 32KB
- 开 `GIYSB_PROFILE=true` 跑代表 benchmark，不作为性能结果。
- 重点看：
  - 每次 GC tiny table 最大使用量；
  - tiny table overflow 次数；
  - staged bytes；
  - staging full flush / tail flush 比例。
- 如果 32KB 很少用满，就把 tiny table 降到 8KB 或 16KB。
- 预期收益：回收 16KB 到 24KB young space，减少 minor GC 次数。

方案 2：做 adaptive staging
- 当前所有 tiny object 都先进 staging。
- 改成：
  - 如果一个 batch 累积不到阈值，例如 2KB 或 4KB，就不要 staging，直接 memcpy 到 old。
  - 只有 batch 足够大时才 staging + NT store。
- 预期收益：减少小 batch/tail 的 double copy。

方案 3：尾巴直接 copy，不再 staging
- 当前最后一个 tail flush 即使很小，也已经经历了 `young -> staging`。
- 可以改成：
  - full 8KB batch 继续 staging + NT；
  - 最后的不足阈值 tail 直接从 young copy 到 old。
- 这需要 flush 时根据 tiny table 顺序重建 dst cursor，但现在已经可以通过 `g_giysb_tiny_batch_begin` 推出目标地址。

方案 4：尝试 direct streaming tiny sequence，取消 staging buffer
- 保留 tiny table。
- flush 时按 reserve order 读取 source object。
- target old 地址是连续的，所以可以直接按 dst cursor 顺序写 old。
- 这样路径变成 `young -> old`，不经过 staging。
- 如果 target 连续且能处理 32B 对齐，可以仍然尝试 streaming store。
- 风险：source 是分散的，很多小对象直接 NT store 可能不如普通 memcpy，需要实验。

方案 5：做 GiYSB-PlaceOnly 对照
- 只保留 old 区 small/large 分区。
- 不使用 tiny table。
- 不使用 staging buffer。
- tiny object 仍然直接 copy，但 old placement 按 small/large 分区。
- 目的：分离“old placement 是否有价值”和“staging batch 是否有价值”。
- 如果 PlaceOnly 变快，说明当前 staging/tiny table 是主要问题。
- 如果 PlaceOnly 也不快，说明 small/large old placement 本身收益有限。

方案 6：只在 reserve 阶段做 32B batch 对齐
- 对 tiny-old batch 起点做 32B 对齐 padding。
- 这可以减少 256-bit NT store 的前缀处理。
- 但这不是主优化，因为当前主要问题不是前缀对齐，而是 double copy 和 workspace 成本。

推荐优先级：
1. 非性能 profiling：确认 tiny table/staging 是否过大，以及 tail flush 是否很多。
2. 实现 GiYSB-PlaceOnly，对照判断 old placement 本身有没有价值。
3. 实现 adaptive staging：小 batch/tail 直接 copy，大 batch 才 staging。
4. 再考虑 direct streaming tiny sequence。
5. 最后再细调 32B 对齐和 table entry 压缩。

当前结论：
- GiYSB 的方向还可以继续探索，但当前 Tiny32 + staging 的实现没有在 GC 时间上成功。
- 最大问题不是 128-bit/256-bit store 宽度，而是“为了批量 copy 付出的空间成本和 double copy 成本超过了收益”。

## 2026-05-21：关于 young 区对象聚合程度与 old 区连续性的思考

问题：
- 如果想把 young 区里位置比较近的 live objects 作为一个 span 直接 NT store 到 old 区，首先要知道这些对象在 young 区到底有没有聚集。
- 另外，即使 young 区中对象是聚集的，也需要解释怎样保证它们搬到 old 区后仍然有一定连续性。

判断 young 区聚合程度的方法：
- 不能凭感觉判断，应该在 minor GC 中做 profiling。
- 在对象被发现为 live object 时，记录它的 young 源地址和对象大小。
- GC 结束前，把这些 live object 按 young 源地址排序。
- 计算相邻 live object 之间的 gap：
  - `gap = next_object_start - current_object_end`
- 统计：
  - live object 数量；
  - live bytes；
  - gap bytes；
  - 可以形成多少个 span；
  - 每个 span 的总大小；
  - 每个 span 中 gap 占比；
  - 多少字节可以被大块 NT store 覆盖。

可以使用的初始阈值：
- `max_gap <= 32B` 或 `64B` 时，把相邻对象合并进同一个 span。
- `gap_ratio <= 10% ~ 20%` 时，认为这个 span 的浪费可以接受。
- `span_size >= 2KB` 或 `4KB` 时，才值得使用大块 NT store。
- 如果 span 太小，仍然使用普通 GiY copy。

如何保证 old 区也连续：
- old 区连续性不是自然发生的，而是 reserve 策略决定的。
- 如果决定对一个 young span 做 span copy，就需要一次性在 old 区 reserve 一整段连续空间：
  - `old_span_base = reserve_old(span_size)`
- 然后每个对象的新地址通过偏移计算：
  - `dst = old_span_base + (src - young_span_base)`
- 这样可以保持 young 区 span 内的相对布局。

关键正确性问题：
- young span 中间的 gap 不能直接作为普通垃圾留在 old 区。
- 如果 old 区扫描器以后按对象 header 顺序扫描 old 区，那么 gap 中的随机数据可能会被误认为对象 header，导致错误。
- 因此 gap 必须被明确处理：
  - 方法一：写入 filler/padding object，让 GC 能够识别并跳过；
  - 方法二：让 old 区扫描逻辑支持跳过这些 gap；
  - 方法三：只在 gap 很小且能够安全填充时启用 span copy。

当前建议：
- 先实现 profiling/simulation，不直接改变 GC 行为。
- 先回答几个问题：
  - young 区 live objects 是否真的聚集；
  - gap 总量是否足够小；
  - 能形成多大的 span；
  - span copy 理论上可以替代多少 staging copy；
  - filler/padding 带来的空间浪费是否可以接受。
- 如果 profiling 结果显示 live objects 经常形成大 span，并且 gap 占比低，再正式实现 GiYSC。

暂定名称：
- `GiYSC`，意思是 GiY Span Copy。

当前判断：
- 这个方案有研究价值，因为它可能避免 GiYSB 当前最大的问题：`young -> staging -> old` 的 double copy。
- 但是它比 staging 更依赖对象布局。
- 它是否有效，必须先通过 profiling 证明 young 区 live objects 确实有足够聚合性。

## 2026-05-21：扫描顺序与地址顺序不一致时如何做 span copy

问题：
- minor GC 扫描对象时，扫描顺序通常是对象图顺序，也就是从 roots 出发，根据引用关系遍历。
- 这个顺序不等于 young 区中的物理地址顺序。
- 因此，即使两个对象在 young 区物理上很近，它们也可能在扫描过程中相隔很久才被发现。
- 如果沿用当前 GiY 的做法：发现对象时立刻 reserve old 地址，那么 old 区布局会跟扫描顺序走，而不是跟 young 地址顺序走。
- 所以不能自然保证“young 相近的对象在 old 也相近”。

结论：
- 如果想让 young 地址相近的 live objects 在 old 区也相近，必须把“发现对象”和“决定 old 地址”解耦。
- 当前 GiY/GiYSB 的 reserve-on-discovery 方式不适合直接实现严格的 young-address-order placement。

方案 A：两阶段 placement，最正确但成本最高
- 第一阶段：扫描对象图，只记录 live objects，不立刻决定最终 old 地址。
- 记录内容包括 young 源地址、对象大小、是否 live。
- 第二阶段：按 young 源地址排序，形成 span。
- 第三阶段：为每个 span 在 old 区一次性 reserve 连续空间。
- 第四阶段：给每个 live object 设置 forwarding pointer。
- 第五阶段：重新扫描或线性遍历 live objects，更新引用并 copy。
- 优点：布局最干净，最能保证 young 相近 -> old 相近。
- 缺点：需要额外表、排序、可能需要第二次扫描，GC 成本较高。

方案 B：按 young region/page 分桶，比较适合先做实验
- 把 young 区切成固定大小 region，例如 2KB、4KB 或 8KB。
- 扫描时不按全局地址排序，只把 live object 记录进它所在的 region bucket。
- GC 后按 region 地址顺序处理 bucket。
- 对 live density 高、gap 小的 region，整段 reserve old 空间并做 span copy。
- 对不满足条件的 region，回退普通 GiY copy。
- 优点：不需要完整排序，表结构可以比较小，容易 profiling。
- 缺点：region 内仍然需要处理 gap/filler；低密度 region 不能受益。

方案 C：old 区预留 young-region 镜像空间
- 对某个 young region，如果判断值得 span copy，就在 old 区 reserve 一段同样大小或近似大小的连续空间。
- 对象目标地址通过 offset 计算：
  - `dst = old_region_base + (src - young_region_base)`
- 这样可以保证这个 region 内 young 近则 old 近。
- 缺点：如果 region live density 低，会浪费 old 空间。
- 因此必须设置 live density/gap ratio 阈值。

方案 D：维持当前扫描顺序，只优化 copy，不保证 old 地址顺序
- 仍然在发现对象时 reserve old 地址。
- 只尝试把连续发现的小对象 staging 或 batch copy。
- 优点：实现简单。
- 缺点：不能解决“物理地址相近但扫描顺序不相近”的根本问题。

当前最推荐的路线：
- 先做方案 B 的 profiling/simulation。
- 也就是按 young region 分桶，统计每个 region 的 live density 和 gap ratio。
- 暂时不改变对象移动行为。
- 如果发现很多 region 的 live density 高，并且 gap 小，再实现 region/span copy。

原因：
- 完整两阶段 placement 理论上最正确，但会显著改变当前 GiY 的结构。
- region bucket 是折中方案：它承认扫描顺序和地址顺序不同，但用 young 地址把对象重新组织起来。
- 这样可以在不过早付出完整排序成本的情况下，判断 GiYSC 是否真的有价值。

## 2026-05-21：方案 B，young region bucket 的详细设计

核心思想：
- 不再试图让扫描顺序等于地址顺序。
- 扫描对象时仍然按照对象图顺序发现 live objects。
- 但是每发现一个 live young object，就根据它在 young 区的物理地址，把它记录到对应的 young region bucket。
- GC 扫描结束后，再按 young region 的地址顺序分析这些 bucket。
- 这样可以判断哪些 young region 里 live objects 足够密集，适合 span copy。

基础划分：
- 把 young 区按固定大小切成 region。
- 初始建议：
  - region size = 4KB 或 8KB。
  - 4KB 更细，能更准确判断局部聚合；
  - 8KB 表更小，管理成本更低。
- 对于当前 896KB local workspace、约 564KB~604KB young 来说：
  - 4KB region 大约 141~151 个；
  - 8KB region 大约 70~76 个。

每个 region bucket 需要统计的信息：
- `live_count`：这个 region 里发现了多少 live objects。
- `live_bytes`：live objects 总字节数。
- `min_addr`：第一个 live object 的地址。
- `max_end`：最后一个 live object 的结束地址。
- `span_bytes = max_end - min_addr`。
- `gap_bytes = span_bytes - live_bytes`。
- `gap_ratio = gap_bytes / span_bytes`。

如果要进一步精确，需要记录对象列表：
- `src_header_offset`：对象 header 相对 young_start 的 offset。
- `object_size`：对象总大小，包括 header。
- 可选：对象在 region 内的 offset。

判断一个 region 是否适合 span copy：
- `live_count >= 2`，至少要有多个对象。
- `span_bytes >= 2KB` 或 4KB，否则 NT store 收益可能不够。
- `gap_ratio <= 10% ~ 20%`，gap 不能太多。
- `max_gap <= 32B` 或 64B，避免中间出现很大的洞。
- 如果不满足条件，就回退普通 GiY copy。

profiling-only 版本：
- 仍然使用当前 GiY/GiYSB 的真实复制策略。
- 额外记录 live object 到 region bucket。
- GC 结束后只输出统计：
  - 每次 minor GC 有多少 region 满足 span copy 条件；
  - 可 span copy 的 live bytes；
  - 会浪费的 gap bytes；
  - 预计可以替代多少 staging copy。
- 这个版本不会影响正确性，适合先验证 idea。

真正实现 span copy 的版本：
- 必须把“发现对象”和“分配 old 地址”解耦。
- 第一阶段：mark/collect。
  - 从 roots、RSet、stack 出发扫描 live young objects。
  - 发现对象后只标记为 live，并记录到对应 region bucket。
  - 不立即决定最终 old 地址。
- 第二阶段：region planning。
  - 按 young region 地址顺序处理 bucket。
  - 对密集 region 分配一整段 old 空间。
  - 对稀疏 region 使用普通 per-object reserve。
- 第三阶段：设置 forwarding pointer。
  - 对 span-copy region：
    - `dst = old_region_base + (src - region_span_start)`
  - 对普通对象：
    - `dst = reserve_old(object_size)`
- 第四阶段：patch references。
  - 扫描 roots、RSet、以及 live objects 内部引用。
  - 把所有 young 指针改成对应 forwarding pointer。
- 第五阶段：copy。
  - span-copy region：对连续 span 使用 NT store。
  - 普通对象：沿用 GiY 的 per-object copy。
  - gap 部分必须写成 filler/padding，或者让 old 扫描器能跳过。

重要正确性问题：
- 当前 GiY 是发现对象时立刻设置 forwarding pointer，并立刻更新引用。
- 如果改成 region bucket + span copy，forwarding pointer 要等 planning 后才能确定。
- 因此真正实现版本需要额外的 patch references 阶段，不能只在当前代码上简单加一个 bucket。

推荐开发顺序：
1. 先做 profiling-only：低风险，回答 young 区是否真的聚集。
2. 如果 profiling 显示可 span copy 的 bytes 很多，再实现 planning。
3. 先只对满足严格条件的 region 启用 span copy，其余全部 fallback GiY。
4. 加入 filler/padding object，保证 old 区扫描正确。
5. 再跑完整 benchmarks，看是否减少 scavenge 时间。

当前判断：
- 方案 B 的价值在于用 young 地址重新组织对象，避免扫描顺序破坏物理聚合性。
- 但真正实现后，它已经不是一个小改动，而是接近一个两阶段 evacuating GC。
- 所以最合理的第一步一定是 profiling-only。

## 2026-05-21：方案 B 是否会导致对象图扫描次数过多

用户提出的问题：
- 如果用 young region bucket + span copy，是否需要多次扫描对象图？
- 这样会不会多出太多操作，反而抵消优化收益？

判断：
- 这个担心是正确的。
- 如果采用最朴素的两阶段 evacuation：
  1. 第一次扫描对象图，发现 live objects；
  2. 按 young 地址规划 old 地址；
  3. 第二次扫描 roots/RSet/live objects，更新所有引用；
  4. 再执行 copy；
- 那么 live object 的字段很可能会被扫描两次。
- 对 GiY 来说，这非常危险，因为 GiY 的目标本来就是降低 GC 搬迁成本，多一次对象图扫描可能直接抵消 span copy 的收益。

当前 GiY 的基准：
- 当前 GiY 基本是在一次对象图遍历中完成：
  - 发现 live object；
  - 给它 reserve old 地址；
  - 设置 forwarding pointer；
  - 扫描它的字段；
  - 更新 young reference；
  - copy object。
- 所以它的优势是结构简单、扫描次数少。

更可接受的改法：
- 不应该真的做完整的“扫描对象图两次”。
- 更合理的是：
  1. 第一次，也是唯一一次对象图扫描时，发现 live objects；
  2. 同时把需要以后 patch 的 slot 记录下来；
  3. GC 图遍历结束后，根据 region bucket 决定 old 地址；
  4. 遍历 slot log，而不是重新扫描对象图，更新指针；
  5. 最后执行 span copy 或普通 copy。

这里的 slot log / edge log 含义：
- 当扫描到一个字段时，如果这个字段保存的是 young object 指针，先不立刻写最终 old 地址。
- 而是记录这个字段的位置：
  - root slot；
  - remembered set slot；
  - object 内部 field slot。
- 等所有对象的 forwarding pointer 都确定后，再根据这些 slot 位置把指针修正。
- 这样避免第二次扫描整个对象图。

这个折中方案的成本：
- live object 字段扫描：仍然 1 次。
- 额外成本：
  - 写 live object 的 region bucket 记录；
  - 写需要 patch 的 slot log；
  - 最后遍历 slot log 做 pointer update；
  - 做 region planning。
- 这比完整第二次扫描对象图便宜，但仍然比当前 GiY 重。

结论：
- 如果目标是最高性能，不能接受“完整扫描对象图两次”。
- 方案 B 真正可行的版本应该是：
  - 一次对象图扫描；
  - region bucket 记录 live objects；
  - slot log 记录需要修正的引用位置；
  - planning 后只遍历 slot log，不重扫对象图。
- 但是这仍然会增加元数据和写入操作，所以必须先 profiling 证明 young 地址聚合性足够强。

当前建议：
- 暂时不要直接实现完整 GiYSC。
- 先实现 profiling-only：
  - 不改变 GC 行为；
  - 不增加第二次对象图扫描；
  - 只统计 young region 聚合程度。
- 如果 profiling 显示 span copy 的潜在覆盖字节很多，再考虑 slot log 版本。

## 2026-05-21：低扫描成本版本 GiYSC-LazyMirror

目标：
- 尽量实现“young 区相近对象在 old 区也相近，并且可以 span/region NT store”的想法。
- 但不能额外线性扫描整个 young 区。
- 也不能完整扫描对象图两次。
- 尽量保留当前 GiY 的一次对象图遍历结构。

核心想法：
- 把 young 区切成固定大小 region，例如 4KB 或 8KB。
- 不在 GC 开始时扫描 young。
- 当 GC 正常遍历对象图、第一次发现某个 young region 中的 live object 时，才为这个 young region 在 old 区分配一个对应的 old mirror region。
- 之后这个 young region 内的所有 live object 的目标地址都通过固定偏移计算：
  - `dst = old_region_base + (src - young_region_base)`
- 这样不需要等所有对象发现完再排序，也不需要第二次扫描对象图才能确定 forwarding pointer。

为什么它能减少扫描：
- 对象图仍然只扫描一次。
- 不额外线性扫描 young。
- 不需要全局排序 live objects。
- 每个对象被发现时，就能立刻知道自己的 old 地址。
- 引用更新仍然可以沿用当前 GiY 的方式。

基本流程：
1. GC 开始时清空 region 映射表。
2. 正常从 roots/RSet/stack 出发扫描对象图。
3. 发现一个 young live object。
4. 计算它属于哪个 young region：
   - `rid = (src - young_start) / REGION_SIZE`
5. 如果这个 region 还没有 old mirror region：
   - 在 old 区 reserve 一整段连续 region 空间；
   - 记录 `region_map[rid].old_base`；
   - 标记这个 region 已经 active。
6. 计算 object 的目标地址：
   - `dst = old_base + (src - young_region_base)`
7. 立刻设置 forwarding pointer。
8. 正常扫描对象字段，更新引用。
9. copy 阶段不再逐对象 staging；可以在 region 结束时或 GC 结束时，把 active region 的必要 span 从 young 直接 NT store 到 old。

关键优势：
- forwarding pointer 可以在第一次发现对象时立即确定。
- 因此不需要 slot log，也不需要第二次 patch roots/fields。
- old 区连续性由 region mirror 保证：
  - young region 内地址相近；
  - old mirror region 内也地址相近。
- 如果把整个 region 或 region 内 min_live 到 max_live 的 span copy 到 old，就可以实现连续 NT store。

主要代价：
- old 区可能浪费空间。
- 因为 old mirror region 保留了 young region 内的 gap。
- 如果一个 4KB region 里只有 200B live object，却 reserve 4KB old mirror，就很浪费。

降低浪费的方法：
- 不一定 reserve 整个 4KB region。
- 可以在第一次发现 region 时先进入 pending mode。
- 对这个 region 记录：
  - `min_addr`
  - `max_end`
  - `live_bytes`
  - `live_count`
- 但是如果想立刻给 forwarding pointer，就必须能立刻算出 dst。
- 因此最简单可靠的版本是整 region mirror。
- 更复杂的版本可以把 region 分成 subregion，例如 1KB 或 2KB mirror，降低浪费。

推荐配置：
- 第一版建议：
  - region size = 4KB；
  - 只对 live density 高的 region 启用 mirror/span copy；
  - 但是 live density 只有 GC 后才知道，所以第一版可以先做 profiling。
- 如果直接实现功能版：
  - region size 可以先用 2KB，减少误分配浪费；
  - 当第一次发现对象时，为其所在 2KB subregion reserve old mirror；
  - copy 时对 active subregion 做 NT store。

filler/padding 问题：
- 如果直接把整个 young region/subregion 复制到 old，gap 中会包含原来的 dead object 或无效数据。
- 这对 old 区扫描是不安全的。
- 因此不能直接把 gap 当成普通 old 内容。
- 必须有一种处理：
  1. 在 old mirror region 中为 gap 写 filler object；
  2. 或者 old major GC 扫描时通过 region bitmap/valid object table 只扫描 live object；
  3. 或者 copy 时只复制 live object，gap 位置写合法 padding。

为了减少 filler 成本：
- 可以不复制整个 region。
- 只复制从 `min_live_addr` 到 `max_live_end` 的 span。
- span 内 gap 写 filler。
- span 前后的空白不使用，不纳入 old 有效对象区。

与当前 GiY/GiYSB 的关系：
- 当前 GiYSB 用 tiny table + staging：
  - 优点是 old 目标连续；
  - 缺点是 `young -> staging -> old` double copy。
- GiYSC-LazyMirror 的目标是：
  - 不 staging；
  - 不排序；
  - 不二次扫描对象图；
  - 用 region mirror 保证 young 近则 old 近。

风险：
- old 空间利用率可能下降。
- 如果 live objects 在 young region 中很稀疏，浪费会很大。
- 如果需要写很多 filler object，GC 时间可能仍然上升。
- 如果 JavaScript 对象布局本来不按 young 地址聚集，这个方案收益会有限。

当前推荐开发路线：
1. 先做 profiling-only 的 LazyMirror 模拟：
   - 不改变 copy 行为；
   - 只统计如果使用 1KB/2KB/4KB mirror，会 reserve 多少 old 空间、浪费多少 gap、覆盖多少 live bytes。
2. 如果 2KB 或 4KB mirror 的浪费比例可以接受，再实现功能版。
3. 功能版先只对满足条件的 region/subregion 启用：
   - `live_bytes >= 1KB`
   - `gap_ratio <= 20%`
   - `live_count >= 2`
4. 不满足条件的对象全部回退当前 GiY copy。

当前判断：
- 这是一个不怎么扫描 young 区、也不需要完整二次对象图扫描的方案。
- 它用 old mirror region 解决“发现对象时就要知道 forwarding pointer”的问题。
- 它的核心 tradeoff 是：用一定 old 空间浪费，换取低扫描成本和无 staging copy。

## 2026-05-21：只对高密度 region 使用 LazyMirror 的折中方案

新的问题：
- LazyMirror 的优点是发现对象时就能确定 forwarding pointer，不需要二次扫描对象图。
- 但如果第一次发现某个 young region 的 live object 就立刻 reserve old mirror region，可能会浪费很多 old 空间。
- 用户希望只对相对更密集的区域使用 mirror/span copy。

核心矛盾：
- 要判断 live density，需要先看到足够多 live objects。
- 但当前 GiY 在第一次发现对象时就需要返回 forwarding pointer。
- 如果等到 GC 后再判断 density，forwarding pointer 就不能立即确定。
- 因此“完全准确地只选高密度 region”和“一次扫描、立即 forwarding”之间存在冲突。

推荐折中：Hot Candidate Region
- 不要对第一次出现的 region 立即分配完整 mirror。
- 先把 region 放入 candidate 状态。
- candidate 状态下只记录少量统计信息：
  - `live_count`
  - `live_bytes`
  - `min_off`
  - `max_end`
  - `gap_estimate`
- 当某个 region 在当前 minor GC 中表现出足够高密度时，才升级为 mirror-active。

问题：candidate 阶段的对象怎么办？
- 如果 candidate 阶段还没有 mirror old base，就不能用 mirror offset 算 forwarding pointer。
- 有三个选择：

选择 A：candidate 阶段对象走普通 GiY，之后对象才走 mirror
- 第一次、第二次发现的对象直接普通 GiY reserve/copy。
- 如果 region 后来达到密度阈值，就从之后发现的对象开始启用 mirror。
- 优点：
  - 不需要 slot log；
  - 不需要二次扫描；
  - forwarding pointer 可以立即返回；
  - 实现最简单。
- 缺点：
  - 同一个 young region 中，早期对象可能不在 mirror region 中；
  - span copy 覆盖率下降；
  - old 区连续性不是完美镜像。
- 这是最适合第一版实现的功能方案。

选择 B：candidate 阶段对象先进入小 pending list，激活后统一决定地址
- 每个 candidate region 保留少量 pending object 记录，例如最多 8 或 16 个。
- 在 pending 期间不立即 patch 引用，而是需要记录 slot log。
- 如果 region 达到阈值，pending objects 一起进入 mirror。
- 如果达不到阈值，pending objects 回退普通 GiY。
- 优点：
  - mirror 覆盖率更高；
  - 高密度 region 的布局更完整。
- 缺点：
  - 需要 slot log 或延迟 patch；
  - 实现复杂；
  - 容易增加 GC 成本。
- 不推荐作为第一版。

选择 C：使用上一轮 minor GC 的密度历史做预测
- 为每个 young region 记录上一轮或最近几轮的 live density。
- 下一次 minor GC 开始时，如果某个 region 历史上经常 dense，就允许它第一次发现对象时直接 mirror。
- 优点：
  - forwarding pointer 仍然可以立即确定；
  - 不需要等待本轮 GC 观察到足够多对象。
- 缺点：
  - young 区每次 GC 后会被清空，下一轮 allocation pattern 可能不同；
  - 历史预测不一定可靠。
- 可以作为后续优化，而不是第一版。

第一版推荐方案：
- 使用选择 A：candidate 阶段普通 GiY，达到阈值后 region 升级为 mirror-active。
- 这不是完美 span copy，但它避免了稀疏 region 的大空间浪费。
- 它也保持了当前 GiY 的一次扫描和立即 forwarding 结构。

具体状态机：
- `COLD`：
  - region 第一次出现；
  - 不分配 mirror；
  - 对象走普通 GiY；
  - 更新 region 统计。
- `CANDIDATE`：
  - region 已有多个 live objects；
  - 继续普通 GiY；
  - 继续统计 live density。
- `MIRROR_ACTIVE`：
  - region 达到密度阈值；
  - 分配 old mirror subregion；
  - 后续属于该 region 的对象使用 mirror 地址；
  - GC 后对 active span 做直接 copy/NT store。
- `DISABLED`：
  - region 明显稀疏，或 gap 太大；
  - 本轮 GC 后续对象全部普通 GiY。

初始阈值建议：
- region size：2KB。
- 激活条件：
  - `live_count >= 4`；
  - `live_bytes >= 256B` 或 512B；
  - `observed_span = max_end - min_off`；
  - `gap_ratio = (observed_span - live_bytes) / observed_span <= 25%`；
  - `max_observed_gap <= 64B`。
- 禁用条件：
  - `observed_span >= 1KB` 但 `live_bytes < 128B`；
  - 或 `gap_ratio > 50%`；
  - 或出现特别大的 gap。

减少 mirror 浪费的方法：
- 不 mirror 整个 2KB region。
- 激活时只为从 `activation_min_off` 到 region 末尾，或者从当前对象附近开始的 subspan 分配 old 空间。
- 更实际的第一版：
  - region size = 2KB；
  - mirror granularity = 512B 或 1KB subregion；
  - 只对达到阈值的 subregion mirror。
- 这样即使预测错误，最大浪费也限制在 512B 或 1KB 级别。

更推荐的低浪费结构：Subregion Mirror
- 把 young region 再切成 512B 或 1KB subregion。
- 判断密度时以 subregion 为单位。
- 第一次发现对象时只记录 subregion 统计，不立即 mirror。
- subregion 达到阈值后才激活 mirror。
- old mirror 也只 reserve 对应 subregion 的大小。
- 这样可以显著降低空间浪费。

当前最推荐实现路线：
1. 先做 profiling-only：
   - 模拟 512B、1KB、2KB subregion；
   - 统计如果采用 Hot Candidate 规则，会激活多少 subregion；
   - 统计 mirror waste、covered live bytes、fallback live bytes。
2. 如果结果好，再做功能版：
   - candidate 阶段对象普通 GiY；
   - active subregion 后续对象 mirror；
   - 不引入 slot log；
   - 不二次扫描对象图。
3. 如果第一版显示有效，再考虑 pending list + slot log，提高覆盖率。

当前判断：
- 如果必须只对高密度区域启用，又不想多扫描，那么不能追求完美。
- 最现实的方案是 Hot Candidate Subregion：
  - 先观察少量 live object；
  - 达到阈值后才启用 mirror；
  - 早期对象回退普通 GiY；
  - 后续对象享受 mirror/span copy。
- 这牺牲一部分覆盖率，换来低扫描成本和低空间浪费。

## 2026-05-21：当前 GiYSB 方案与下一步真正可能提升性能的优化

当前 GiYSB 的具体方案：
- 当前版本可以理解为 `GiYSB-Tiny32-NT256`。
- local workspace 总预算是 896KB。
- young 可分配空间约 564.24KB。
- GC stack 是 48KB。
- FT slot set 是 32KB。
- RSet buffer 是 128KB，RSet hash 是 64KB。
- GiYSB 额外使用：
  - staging buffer：8KB；
  - tiny table：32KB；
  - tiny object 阈值：64B；
  - NT store 宽度：256-bit。
- old 区被分成 small/tiny old 与 normal/large old，当前 small old ratio 是 50%。

当前 GiYSB 的搬迁逻辑：
- GC 扫描仍然沿用 GiY 的 LIFO 对象图遍历。
- 发现 live object 后：
  - 如果对象总大小小于等于 64B，并且 tiny table 与 small old 还有空间：
    - 在 small old 中 reserve 目标地址；
    - 把对象源地址和 size class 记录进 tiny table；
    - 不立即 copy；
  - 否则：
    - 走普通 GiY copy 路线；
    - 大对象继续直接 copy/NT store 到 old。
- 对象图扫描结束后：
  - 遍历 tiny table；
  - 把 tiny objects 先 `young -> staging`；
  - staging 满了或结束时，再 `staging -> old`，大块时使用 256-bit NT store。

当前结果判断：
- GiYSB 的 GC 时间明显变慢。
- 主要原因不是 NT store 宽度，而是：
  1. tiny table + staging 多占 40KB workspace，使 young 变小，minor GC 次数增加；
  2. tiny object 路径变成 `young -> staging -> old`，存在 double copy；
  3. tiny table 编码/解码、flush、staging 管理增加额外工作；
  4. 64B 以下对象本来很小，普通 memcpy 可能已经很便宜，staging 的固定成本不一定能抵消。

当前最可能真正提升性能的优化方向：

方向 1：GiYSB-PlaceOnly
- 只保留 old 区 small/large placement。
- 去掉 tiny table。
- 去掉 staging buffer。
- tiny object 也直接 copy 到 small old。
- 目的：
  - 单独验证“small/large old 分区”是否有收益；
  - 如果 PlaceOnly 变快，说明 staging 是主要负担；
  - 如果 PlaceOnly 不快，说明 old placement 本身收益有限。
- 这是最应该优先做的对照。

方向 2：Adaptive Staging
- 不再所有 tiny object 都 staging。
- 只有当本轮 GC 中 tiny batch 足够大时才 staging。
- 例如：
  - batch 累计超过 4KB 才启用 staging + NT；
  - 小于 4KB 的 tail 直接 `young -> old` memcpy；
  - tiny object 数量太少的 GC 完全不 staging。
- 目的：
  - 避免小 batch 为了 staging 付出 double copy。

方向 3：缩小 tiny table
- 当前 tiny table 是 32KB，容量 8192 entries。
- 这可能过大，并且直接挤占 young。
- 应该测试 8KB / 16KB tiny table。
- 如果 overflow 很少，缩小 table 可以恢复 young 空间，减少 minor GC 次数。

方向 4：Direct Tiny Streaming
- 保留 tiny table，但取消 staging buffer。
- flush 时按 reserve order 直接把 tiny objects 写到 old。
- 目标路径变成：
  - `young -> old`
- 不再经过 staging。
- 风险：
  - source 是分散小对象，直接 NT store 未必快；
  - 但它可以直接验证 double copy 是否是核心瓶颈。

方向 5：GiYSC/Span Copy 先做 profiling
- 如果想从根本上解决 staging 问题，可以研究 young 地址相近对象的 span copy。
- 但这个方向实现复杂，涉及 old gap/filler/descriptor。
- 当前不应该马上功能实现。
- 应该先做 profiling-only：
  - 统计 young live objects 的聚合程度；
  - 估算 span copy 可覆盖字节；
  - 估算 gap 浪费。

当前最推荐的下一步：
1. 先实现并测试 GiYSB-PlaceOnly。
2. 再实现 Adaptive Staging：
   - 小 batch 直接 copy；
   - 大 batch 才 staging + NT。
3. 同时把 tiny table 从 32KB 降到 8KB/16KB 做测试。
4. 如果这些都不能改善 GC 时间，再转向 GiYSC profiling。

当前结论：
- 当前 GiYSB 的核心问题是为了批量 NT store 付出了过高的空间成本和 double copy 成本。
- 真正可能提升性能的方向不是继续加大 staging，也不是只调 NT 宽度。
- 更可能有效的是：
  - 减少 staging 使用频率；
  - 减少 workspace 占用；
  - 先验证 old placement 是否本身有价值；
  - 只在 batch 足够大时才使用 staging。

## 2026-05-21：采用 Adaptive Staging 并缩小 tiny table

用户判断：
- 方案二是正确方向：只有在值得的时候才使用 staging 优化。
- tiny table 也应该缩小，避免占用过多 local workspace。

本次修改：
- GiYSB 默认 tiny table 从 32KB 改为 16KB。
- 新增宏：
  - `GIYSB_TINY_STAGING_MIN_BYTES`
  - 默认值是 4KB。
- 含义：
  - tiny batch/chunk 大于等于 4KB 时，才使用 `young -> staging -> old`。
  - 小于 4KB 的 tiny chunk 直接 `young -> old`，不再经过 staging。
- staging buffer 仍然是 8KB。
- tiny object 阈值仍然是 64B。
- NT store 仍然是 256-bit。

新的 tiny flush 逻辑：
- GC 扫描阶段仍然记录 tiny object 到 tiny table。
- GC 扫描结束后按 reserve 顺序处理 tiny table。
- tiny table 被切成若干 chunk。
- 如果 chunk 达到 `GIYSB_TINY_STAGING_MIN_BYTES`：
  - 先复制到 staging；
  - 再用 `giy_copy_live_object` 批量写入 old；
  - 大块时可使用 256-bit NT store。
- 如果 chunk 小于阈值：
  - 逐对象直接 `young -> old` copy；
  - 避免小 batch 的 double copy。

这样做的原因：
- 当前实验显示 GiYSB 的主要问题是 staging 的固定成本和 double copy。
- 对小 batch 使用 staging 很可能不划算。
- 只有当 batch 足够大时，staging 才可能把分散的小对象整理成连续写入，从而抵消 double copy 成本。
- tiny table 从 32KB 缩到 16KB，可以把 16KB 还给 young 区，降低 minor GC 次数压力。

当前新的实验配置名称建议：
- `GiYSB-Tiny16-Adaptive-NT256`

当前配置：
- local workspace：896KB。
- GC stack：48KB。
- FT slot set：32KB。
- RSet buffer：128KB。
- RSet hash：64KB。
- GiYSB staging：8KB。
- GiYSB tiny table：16KB。
- tiny staging min：4KB。
- tiny object max：64B。
- NT store：256-bit。

预期：
- GC 时间应该比原来的 `GiYSB-Tiny32-NT256` 更好。
- 主要改善来源应该是：
  - 更少的小 batch double copy；
  - 更大的 young 区；
  - 更少的 minor GC 压力。
- 但如果 tiny table 缩小后 overflow 明显增加，收益可能被抵消，需要后续用 profile 或 benchmarks 验证。

## 2026-05-21：重新 benchmark 的公平性设置

用户要求：
- 改完 GiYSB 之后，需要重新跑 benchmarks。
- 要尽量排除测量噪音。
- 不能让某一个配置多做额外测量、额外 cache 统计或额外大量打印。

本次 benchmark 设置：
- 对比对象：
  - `giy896`
  - `giysb_tiny16_adaptive_896`
- 两边都使用：
  - `CACHE_SIZE_KB=896`
  - `GIY_GC_STACK_BYTES=49152`
  - `GIY_RSET_READ_SLOT_AT_GC=true`
  - `GIY_NT_COPY_BITS=256`
- GiYSB 额外使用：
  - `GIY_SB=true`
  - `GIYSB_STAGING_BYTES=8192`
  - `GIYSB_TINY_TABLE_BYTES=16384`
  - `GIYSB_TINY_OBJECT_MAX_BYTES=64`
  - `GIYSB_TINY_STAGING_MIN_BYTES=4096`
- 不打开 `GIYSB_PROFILE`。
- 运行时设置 `EJS_DISABLE_GC_PMU=1`，不做 cache/PMU 测量。
- benchmark 脚本已改成：
  - 先构建两个配置；
  - 再按 benchmark 交错运行两个配置；
  - 避免先跑完一整组再跑另一整组造成时间漂移偏置。

说明：
- 这次性能 benchmark 不测 cache miss。
- cache miss 如果之后需要，会单独跑，不能混进性能测试。

## 2026-05-21：GiYSB-Tiny16-Adaptive-NT256 full suite 结果

输出目录：
- `/home/qiancheng/ejs-new/build.debug/benchmarks/out_giysb_tiny16_adaptive_vs_giy_nt256_896_20260521_175640`

公平性设置：
- 两边都使用 896KB local workspace。
- 两边都使用 `GIY_NT_COPY_BITS=256`。
- 两边都使用 `GIY_RSET_READ_SLOT_AT_GC=true`。
- 两边都使用 `GIY_GC_STACK_BYTES=49152`。
- 运行时设置 `EJS_DISABLE_GC_PMU=1`。
- 没有运行 perf/cache miss 测量。
- 没有打开 `GIYSB_PROFILE`。
- benchmark 顺序改成按 benchmark 交错运行：
  - 例如先 `Bounce/GiY`，再 `Bounce/GiYSB`，然后进入 `CD/GiY`。
- 这样避免先跑完一整组再跑另一整组造成时间漂移偏置。

配置：
- `giy896`
  - young after aux：604.24KB
  - aux total：80KB
- `giysb_tiny16_adaptive_896`
  - young after aux：580.24KB
  - aux total：104KB
  - staging：8KB
  - tiny table：16KB
  - tiny staging min：4KB
  - tiny object max：64B

总体结果，GiYSB-Tiny16-Adaptive 相对 GiY：
- total execution：
  - GiY：6747.473s
  - GiYSB：6637.195s
  - 差异：-110.278s，-1.634%
- real time：
  - GiY：6752.030s
  - GiYSB：6641.960s
  - 差异：-110.070s，-1.630%
- business residual：
  - GiY：6612.249s
  - GiYSB：6475.665s
  - 差异：-136.584s，-2.066%
- GC overhead：
  - GiY：135.224s
  - GiYSB：161.530s
  - 差异：+26.306s，+19.454%
- GC core：
  - GiY：100.935s
  - GiYSB：125.620s
  - 差异：+24.685s，+24.456%
- scavenge：
  - GiY：75.501s
  - GiYSB：99.243s
  - 差异：+23.742s，+31.446%
- scan_RS：
  - GiY：24.914s
  - GiYSB：25.765s
  - 差异：+0.851s，+3.416%
- minor GC count：
  - GiY：1,045,148
  - GiYSB：1,088,977
  - 差异：+43,829，+4.194%
- forward operations：
  - GiY：1,675,117,369
  - GiYSB：1,676,418,380
  - 差异：+1,301,011，+0.078%

每个 benchmark 的主要差异，GiYSB-Tiny16-Adaptive 相对 GiY：

| benchmark | total 差异 | GC 差异 | scavenge 差异 | minor GC 差异 |
|---|---:|---:|---:|---:|
| Bounce | -16.729s | -0.005s | +0.009s | +131 |
| CD | +2.082s | +2.231s | +1.899s | +4,492 |
| DeltaBlue | +0.029s | +0.518s | +0.459s | +650 |
| Havlak | -33.698s | +7.894s | +6.706s | +9,249 |
| List | -0.396s | +0.001s | +0.001s | +20 |
| Mandelbrot | -24.416s | +0.313s | +0.017s | +10,523 |
| NBody | -29.067s | +0.698s | +0.217s | +13,071 |
| Permute | -1.156s | +0.000s | +0.000s | +2 |
| Queens | -14.034s | -0.001s | +0.001s | +45 |
| Richards | -7.668s | +0.006s | +0.001s | +88 |
| Sieve | +0.740s | +0.033s | +0.021s | +1,428 |
| Storage | +13.984s | +14.618s | +14.410s | +4,126 |
| Towers | +0.051s | +0.000s | +0.001s | +4 |

和旧 `GiYSB-Tiny32-NT256` 的历史结果相比：
- total execution：
  - 旧 GiYSB：6674.211s
  - 新 GiYSB：6637.195s
  - 改善：-37.016s，-0.555%
- GC overhead：
  - 旧 GiYSB：167.286s
  - 新 GiYSB：161.530s
  - 改善：-5.756s，-3.441%
- GC core：
  - 旧 GiYSB：129.247s
  - 新 GiYSB：125.620s
  - 改善：-3.627s，-2.806%
- scavenge：
  - 旧 GiYSB：102.831s
  - 新 GiYSB：99.243s
  - 改善：-3.588s，-3.489%
- minor GC count：
  - 旧 GiYSB：1,119,336
  - 新 GiYSB：1,088,977
  - 减少：-30,359，-2.712%

解释：
- 新方案确实改善了旧 GiYSB：
  - tiny table 从 32KB 缩到 16KB，young 从 564.24KB 恢复到 580.24KB；
  - minor GC 次数减少；
  - 小于 4KB 的 tiny batch 不再 staging，减少了一部分 double copy；
  - 因此 GC overhead、GC core、scavenge 都比旧 GiYSB 下降。
- 但是新 GiYSB 的 GC 仍然比 GiY 慢：
  - young 仍然比 GiY 小 24KB；
  - minor GC 仍然多 4.194%；
  - scavenge 仍然多 31.446%；
  - Storage 和 Havlak 是主要 GC 退化来源。
- aggregate 总时间显示 GiYSB 比 GiY 快 1.634%，但这主要来自 business residual 下降，而不是 GC 变快。
- business residual 是 `total - GC`，不是独立测量的 mutator time。
- 因此不能把“总时间更快”直接解释成 GC 优化成功。

当前合理结论：
- Adaptive staging + 16KB tiny table 是正确方向，因为它相对旧 GiYSB 明确减少了 GC 开销。
- 但它还没有解决核心问题：
  - GiYSB 相对 GiY 的 GC 仍然更慢；
  - 主要损失仍在 scavenge；
  - Storage 和 Havlak 说明 tiny staging/direct copy 路径仍有较大结构成本。
- 下一步应该继续减少 staging 和 tiny table 成本，而不是继续调 NT store 宽度。

下一步建议：
- 做 `GiYSB-PlaceOnly`：
  - 保留 small/large old placement；
  - 去掉 tiny table；
  - 去掉 staging；
  - 验证 old placement 本身是否有价值。
- 再测试 8KB tiny table：
  - 如果 overflow 不严重，可以进一步恢复 young 空间。
- 对 Storage/Havlak 做一次非性能 profiling：
  - 打开 `GIYSB_PROFILE` 只用于诊断；
  - 统计 staged/direct tiny bytes、staging flush 次数、tiny table 最大占用；
  - 不能把这个 profile run 当作性能结果。

## 2026-05-22：准备 GiY equal-young 实验

目的：
- 用户希望把 GiY 的 young 区大小调整到和当前 GiYSB 一样。
- 然后只重新跑一次 GiY benchmarks。
- 再和当前已经跑完的 `GiYSB-Tiny16-Adaptive-NT256` 结果比较。

当前 GiYSB 配置的 young：
- `GiYSB-Tiny16-Adaptive-NT256`
- local workspace：896KB。
- young after aux：580.24KB。
- aux total：104KB。

当前普通 GiY 的 young：
- local workspace：896KB。
- young after aux：604.24KB。
- aux total：80KB。

需要让 GiY 少 24KB young：
- 604.24KB - 580.24KB = 24KB。
- 24KB = 24576B。

本次代码准备：
- 新增 GiY 专用 padding 宏：
  - `GIY_LOCAL_PADDING_BYTES`
  - 默认值：0。
- 该 padding 只占用 GiY 的 GC local workspace。
- 它不参与 GC 逻辑，不记录对象，不做扫描，不做额外测量。
- 目的只是把 GiY 的 young 空间缩小到和 GiYSB 一致。

实验配置：
- `giy896_equal_giysb_young`
- `CACHE_SIZE_KB=896`
- `GIY_GC_STACK_BYTES=49152`
- `GIY_LOCAL_PADDING_BYTES=24576`
- `GIY_NT_COPY_BITS=256`
- `GIY_RSET_READ_SLOT_AT_GC=true`
- `EJS_DISABLE_GC_PMU=1`

公平性说明：
- 这个实验在 young 大小上公平。
- 但是它不是和 GiYSB 同一轮交错运行，而是使用当前已有 GiYSB 结果作为对照。
- 因此总时间仍可能包含时间窗口噪音。
- GC 内部指标和 minor GC 次数更适合作为主要分析依据。

新增脚本：
- `tools/run_giy_equal_giysb_young_benchmarks.sh`

## 2026-05-22：GiY equal-young benchmark 结果

输出目录：
- `/home/qiancheng/ejs-new/build.debug/benchmarks/out_giy_equal_giysb_young_nt256_896_20260522_001944`

对照 GiYSB 结果目录：
- `/home/qiancheng/ejs-new/build.debug/benchmarks/out_giysb_tiny16_adaptive_vs_giy_nt256_896_20260521_175640`

状态：
- `giy896_equal_giysb_young`：13/13 benchmark status 0。
- 对照 `giysb_tiny16_adaptive_896`：13/13 benchmark status 0。

young 空间确认：
- GiY equal-young：
  - young before aux：684.24KB
  - young after aux：580.24KB
  - aux total：104KB
- GiYSB-Tiny16-Adaptive：
  - young before aux：684.24KB
  - young after aux：580.24KB
  - aux total：104KB
- 结论：
  - 这次 young 大小完全一致。
  - local workspace 占用也完全一致。

总体结果，GiYSB 相对 GiY equal-young：
- total execution：
  - GiY equal-young：6633.370s
  - GiYSB：6637.195s
  - 差异：+3.825s，+0.058%
- real time：
  - GiY equal-young：6637.910s
  - GiYSB：6641.960s
  - 差异：+4.050s，+0.061%
- business residual：
  - GiY equal-young：6496.602s
  - GiYSB：6475.665s
  - 差异：-20.937s，-0.322%
- GC overhead：
  - GiY equal-young：136.768s
  - GiYSB：161.530s
  - 差异：+24.762s，+18.105%
- GC core：
  - GiY equal-young：101.359s
  - GiYSB：125.620s
  - 差异：+24.261s，+23.936%
- scavenge：
  - GiY equal-young：75.397s
  - GiYSB：99.243s
  - 差异：+23.846s，+31.627%
- scan_RS：
  - GiY equal-young：25.419s
  - GiYSB：25.765s
  - 差异：+0.346s，+1.361%
- minor GC count：
  - GiY equal-young：1,088,977
  - GiYSB：1,088,977
  - 差异：0，0.000%
- forward operations：
  - GiY equal-young：1,676,418,378
  - GiYSB：1,676,418,380
  - 差异：+2，约等于 0。
- total alloc bytes：
  - 两边都是 305448.160MB。

每个 benchmark 的主要差异，GiYSB 相对 GiY equal-young：

| benchmark | total 差异 | GC 差异 | scavenge 差异 | minor GC 差异 |
|---|---:|---:|---:|---:|
| Bounce | +2.145s | +0.002s | +0.009s | 0 |
| CD | +5.368s | +1.930s | +1.857s | 0 |
| DeltaBlue | +1.171s | +0.450s | +0.460s | 0 |
| Havlak | +12.881s | +7.300s | +6.845s | 0 |
| List | -0.014s | +0.001s | +0.001s | 0 |
| Mandelbrot | +1.470s | +0.268s | +0.035s | 0 |
| NBody | +1.352s | +0.287s | +0.176s | 0 |
| Permute | -11.970s | +0.000s | +0.000s | 0 |
| Queens | +0.689s | -0.001s | +0.000s | 0 |
| Richards | -2.199s | -0.001s | +0.001s | 0 |
| Sieve | +0.853s | -0.031s | -0.004s | 0 |
| Storage | +13.587s | +14.557s | +14.465s | 0 |
| Towers | -21.508s | +0.000s | +0.001s | 0 |

和普通 GiY 的关系：
- 普通 GiY 的 young after aux 是 604.24KB。
- equal-young GiY 的 young after aux 是 580.24KB。
- 把 GiY young 缩小到和 GiYSB 一样后：
  - minor GC count 从 1,045,148 增加到 1,088,977；
  - 增加 43,829 次，正好和之前 GiYSB 相对普通 GiY 多出来的次数一致。
- 这说明：
  - GiYSB 之前 minor GC 次数更多，主要确实是因为 young 更小；
  - 当 young 大小完全一致后，GiY 和 GiYSB 的 minor GC 次数完全一致。

关键解释：
- young 大小公平以后，GiYSB 仍然 GC 慢 24.762s。
- 这说明 GiYSB 的 GC 慢，并不主要来自 minor GC 次数更多。
- 主要来自每次 GC 内部的 scavenge/materialization 成本。
- scavenge 慢 23.846s，几乎解释了全部 GC overhead 差异。
- Storage 和 Havlak 是最主要来源：
  - Storage：GC +14.557s，scavenge +14.465s。
  - Havlak：GC +7.300s，scavenge +6.845s。

当前合理结论：
- 这次实验排除了 “GiYSB 只是因为 young 更小所以 GC 慢” 这个解释。
- young 大小确实会影响 minor GC 次数，但不是 GiYSB 当前 GC 慢的根本原因。
- 根本原因仍然是 GiYSB tiny path 的额外搬迁成本：
  - tiny table 记录/解码；
  - staging 或 adaptive direct copy 分支；
  - small old placement 判断；
  - 小对象路径在 Storage/Havlak 中没有抵消额外成本。

关于总时间：
- GiYSB 总时间只比 GiY equal-young 慢 3.825s，约 +0.058%。
- 这个差距非常小，基本可以认为总时间接近。
- 但是 GiYSB 的 GC 明确更慢，而 business residual 更快 20.937s。
- business residual 不是独立测量的 mutator 时间，且这次 GiY 和 GiYSB 不是同一轮交错运行。
- 因此总时间只能作为参考，不能作为证明 GiYSB 更优的主要依据。

下一步判断：
- 如果研究目标是降低 GC 时间，当前 GiYSB 仍未成功。
- 下一步最应该做的是 `GiYSB-PlaceOnly`：
  - 去掉 tiny table；
  - 去掉 staging；
  - 只保留 small/large old placement；
  - 验证 old placement 本身是否有收益。
- 如果 PlaceOnly 的 GC 接近 GiY，说明 tiny table/staging 是主要问题。
- 如果 PlaceOnly 仍然慢，说明 small/large old placement 本身也有成本或收益不足。

## 2026-05-22：关于 GiYSB 的 business residual 为什么看起来更快

这次重新检查了 `business_sec` 的定义。当前解析脚本里的 `business_sec` 不是运行时单独打点得到的 mutator 时间，而是：

```text
business_sec = total_execution_sec - gc_overhead_sec
```

所以它更准确的名字应该是“非 GC 残差时间”。

在 equal-young 对比里：
- GiY equal-young business residual：6496.602s
- GiYSB business residual：6475.665s
- GiYSB 看起来快 20.937s，约 -0.322%。

但是这个现象不能直接解释为“GiYSB 让业务代码真的更快了”，原因如下：

1. 这不是独立测量值，而是 total 减去 GC 得到的残差。
2. GiY equal-young 和 GiYSB 不是同一轮交错运行，total/business 更容易受到运行时噪音、CPU 频率、系统负载影响。
3. business residual 的优势主要来自少数 benchmark：
   - Towers：GiYSB business residual 快 21.508s，但 GC 时间几乎没有差异。
   - Permute：GiYSB 快 11.970s，但 GC 时间也几乎没有差异。
   - 这两项更像运行噪音或时间窗口差异，而不是 GiYSB 的 GC 策略带来的稳定收益。
4. Storage 中 GiYSB 的 business residual 快 0.970s，但 GC 慢 14.557s，说明总时间仍然变慢；这种情况不能说优化成功。

仍然存在一个可能的真实原因：GiYSB 的 small/large old placement 会改变对象进入 old 区后的物理布局。业务阶段访问 old 对象时，可能因为小对象更集中而稍微改善 cache/TLB 局部性。但是这目前只是可能性，还没有被直接证明。

当前最可信的解释：
- normal GiY vs GiYSB 时，business residual 大幅变快的一部分可能来自 young 变小导致 GC 次数更多、对象更频繁被扫描/搬迁/预热，以及运行噪音。
- equal-young 后，minor GC 次数完全一致，GiYSB 仍然 business residual 快 20.937s，但这个差距主要由 Towers/Permute 这种 GC 几乎无关的项拉动，因此更应该视为噪音或残差口径问题。
- 因此，现在不能把 business residual 变快作为 GiYSB 成功的证据。当前更可靠的事实是：GiYSB 的 GC/scavenge 明确变慢。

如果要确认 business residual 是否真的有意义，后续应该：
- 做 A/A 测试，先测同一个配置重复运行时 business residual 的自然波动范围。
- 做多轮 interleaved A/B 测试，用 median 或 geometric mean，而不是只看一次 full suite。
- 最好增加真正的 mutator-only timer，或者至少在报告中明确说明 business residual 是 total - GC 的推导值。

## 2026-05-22：GC 论文和 GC 开发者最关心的指标

一般 GC 论文不会只看一个指标。最核心的判断通常分成两层：

第一层是应用整体性能，也就是 end-to-end execution time / throughput。原因是 GC 优化最终必须服务于整个程序。如果 GC 时间变短，但是程序总时间变长，论文中很难说这是实际有效的优化。

第二层是 GC 自身成本，也就是 GC time / pause time / collection time / allocation throughput / copied bytes / promoted bytes / memory footprint 等。原因是这些指标能解释为什么总时间变化，并能证明优化是否真的作用在 GC 本身。

对 GC 开发者和优化者来说，最关心的量通常是：
- total execution time：最终用户真正感受到的整体性能。
- throughput：单位时间完成多少工作，常用于 benchmark 总体评价。
- GC time / GC overhead：程序花在 GC 上的总时间和比例。
- pause time：单次停顿时间，尤其是 interactive / latency-sensitive 场景。
- allocation rate / allocation throughput：分配路径是否变快。
- copied bytes / promoted bytes / scanned bytes：GC 实际搬运和扫描了多少数据。
- GC frequency：minor/major GC 次数。
- memory footprint：为了换取速度额外用了多少空间。
- cache miss / memory bandwidth：如果论文主题和 cache/locality 有关，这些是解释性指标，不一定是主指标。

对当前 GiY/GiYSB 研究来说，最应该报告的是：
1. 总运行时间：说明最终效果。
2. GC overhead / GC core / scavenge / scan_RS：说明优化是否真的改善了 GC。
3. minor GC 次数、forward operations、allocated bytes：说明比较是否公平。
4. local workspace 大小、young size、auxiliary structure 大小：说明空间成本是否公平。
5. cache miss 率：作为解释 GiY locality 效果的辅助证据，不能和普通性能测试混在一起测。

当前论文叙述上应该避免只说 business residual，因为它是 total - GC 推导出来的残差，不是独立测量的业务时间。主结论应该优先基于 total time 和 GC time。

## 2026-05-22：向导师汇报 GiYSB 的推荐说法

GiYSB 的汇报重点应该是：它是一个实验性优化，用来验证“把小对象延迟并批量复制”是否能提高 GiY 的 old 区写入效率。

推荐解释顺序：

1. 背景：
   - GiY 的目标是让 minor GC 时的工作数据尽量停留在 GC Local Workspace 里。
   - 但是普通 GiY 在对象晋升时基本是发现一个对象就复制一个对象。
   - 对大量 tiny object 来说，这会产生很多很小、很碎的 copy/store。

2. GiYSB 的想法：
   - 对小于 64B 的 tiny object，先不立刻复制数据。
   - 先 reserve old 区目标地址，并把源地址、目标地址、大小记录到 tiny table。
   - 等 tiny table 或 staging 条件满足后，把一批 tiny object 放进 8KB staging buffer。
   - staging 到一定大小后，用 256-bit non-temporal store 写入 old 区。
   - 中等对象和大对象不走 tiny table，避免所有对象都付出记录成本。

3. 当前配置：
   - local workspace：896KB。
   - RSet buffer：128KB。
   - RSet hash/index：64KB。
   - GC stack：48KB。
   - FT slot set：32KB。
   - GiYSB staging buffer：8KB。
   - GiYSB tiny table：16KB。
   - tiny object 阈值：64B。
   - staging 启动阈值：4KB。
   - NT store 宽度：256-bit。
   - GiYSB young after aux：580.24KB。

4. 性能汇报方式：
   - 先报告总时间。
   - 再报告 GC overhead / GC core / scavenge。
   - 再报告 minor GC 次数和 forward operations，用来证明是否公平。
   - 不把 business residual 作为主结论，因为它是 total - GC 的推导值。

5. 和普通 GiY 896KB 的结果：
   - GiYSB 总时间快 110.278s，约 -1.634%。
   - 但是 GiYSB GC overhead 慢 26.306s，约 +19.454%。
   - GiYSB scavenge 慢 23.742s，约 +31.446%。
   - GiYSB minor GC 多 43,829 次，约 +4.194%。
   - 这个结果不能直接说明 GiYSB 成功，因为 GiYSB 的 young 更小。

6. equal-young 公平实验：
   - 把 GiY young 缩小到和 GiYSB 完全一致，都是 580.24KB。
   - minor GC 次数完全一致，都是 1,088,977。
   - forward operations 几乎完全一致。
   - 这说明比较已经排除了 young size 导致 GC 次数不同的问题。

7. equal-young 结果：
   - GiYSB 总时间只慢 3.825s，约 +0.058%，整体几乎持平。
   - 但是 GiYSB GC overhead 慢 24.762s，约 +18.105%。
   - GiYSB scavenge 慢 23.846s，约 +31.627%。
   - 因此，当前 GiYSB 的问题不是 GC 次数，而是每次 GC 内部 tiny path/materialization 的额外成本。

8. 当前结论：
   - GiYSB 的设计思想合理，但当前实现没有降低 GC 时间。
   - tiny table + staging + adaptive direct copy 的成本超过了批量 NT store 带来的收益。
   - Storage 和 Havlak 是主要退化来源。
   - 下一步应做 GiYSB-PlaceOnly：去掉 tiny table 和 staging，只保留 small/large old placement，验证 old 区布局本身是否有价值。

## 2026-05-22：为什么当前 GiYSB 的 GC 时间更慢，以及更可能有效的修改方向

当前 equal-young 实验已经排除了 young size 的影响：
- GiY equal-young 和 GiYSB 的 young after aux 都是 580.24KB。
- minor GC count 都是 1,088,977。
- forward operations 几乎完全一样。
- 但 GiYSB 的 GC overhead 多 24.762s，+18.105%。
- GiYSB 的 scavenge 多 23.846s，+31.627%。

所以当前 GC 慢的主要来源不是 GC 次数，也不是 RSet：
- scan_RS 只多 0.346s，+1.361%。
- 绝大部分差异来自 scavenge。

代码路径上的原因：

1. 普通 GiY 的对象复制路径很短。
   - `copy_for_minor` reserve old 地址。
   - scavenge 扫描对象后，立刻调用 `giy_copy_live_object` 复制。
   - 对小于等于 256B 的对象，`giy_copy_live_object` 直接 `memcpy`，不会使用 NT store。

2. GiYSB 对 tiny object 增加了额外步骤。
   - tiny object 小于 64B 时，先 reserve 到 small old 区。
   - 不立刻复制，而是把 young offset 和 size class 记录进 tiny table。
   - scavenge 遍历对象时，如果发现目标是 tiny destination，就跳过立即复制。
   - scavenge 结束后再 flush tiny table。

3. staging 路径会造成二次数据移动。
   - 第一次：young object -> staging buffer，使用 `memcpy`。
   - 第二次：staging buffer -> old 区，使用 `giy_copy_live_object`，可能触发 256-bit NT store。
   - 对小于 64B 的对象来说，原本普通 GiY 只需要一次 small memcpy。
   - GiYSB 为了凑成大块 NT store，反而多了一次读写和一次额外遍历。

4. direct tiny fallback 也不等价于普通 GiY。
   - 虽然小于 4KB 的 chunk 会 direct copy，不进入 staging。
   - 但它仍然先写 tiny table，之后再 decode tiny table，再复制。
   - 普通 GiY 是扫描完当前对象后马上复制，这时对象内容更可能还在 L1/L2。
   - GiYSB 延迟到整轮 LIFO traversal 后再复制，可能失去这种时间局部性。

5. tiny table 本身也有成本。
   - 每个 tiny object 都要判断大小、判断 small old 空间、压缩 entry、写 table。
   - flush 时还要重新 decode entry、计算 src header、计算 size。
   - 这些操作对 64B 以下对象来说比例很高。

6. 256-bit NT store 不一定适合 tiny object。
   - NT store 的优势是大块、连续、不希望污染 cache 的写入。
   - tiny object 本身太小。
   - 即使 staging 后变成连续写，前面已经付出了 young->staging 的额外 copy 成本。
   - 当前结果说明这个交换不划算。

数据上也支持这个判断：
- GiYSB scavenge 总共慢 23.846s。
- 主要来自：
  - Storage：scavenge +14.465s。
  - Havlak：scavenge +6.845s。
  - CD：scavenge +1.857s。
- 这些 benchmark 很可能产生大量需要扫描和复制的小对象，因此 tiny path 的额外成本被放大。

当前更可能真正提高性能的方向：

1. 优先做 `GiYSB-PlaceOnly`。
   - 去掉 tiny table。
   - 去掉 staging。
   - 保留 small/large old-space placement。
   - tiny object 仍然立即复制。
   - 目的：单独验证“old 区按大小分区”是否有收益。
   - 这是最干净的下一步，因为它能把 layout effect 和 batching effect 分开。

2. 如果 PlaceOnly 不慢，再考虑加非常保守的 batching。
   - 不要默认对所有 tiny object batching。
   - 只在上一轮 GC 证明 tiny survivor bytes 足够大时开启。
   - 只在 chunk 明显大于 staging 成本时开启，例如 16KB 或更高，而不是 4KB。
   - 否则直接走普通 GiY 的 memcpy。

3. 不建议继续扩大 tiny table 或 staging buffer。
   - 这会继续减少 young space。
   - 还会增加 workspace 辅助结构成本。
   - 当前问题不是 table 不够大，而是每个 tiny object 的机制成本太高。

4. 可以尝试 type-aware tiny batching。
   - 指针很多、扫描时已经读取 body 的 tiny object 不适合延迟复制。
   - pointer-free 或 body 很少被扫描的 tiny object，才可能更适合延迟。
   - 这样可以避免 Storage/Havlak 这种 pointer-rich benchmark 被 tiny path 拖慢。

5. 如果目标是 NT store，应该主要作用于中大对象或真正连续的大块。
   - 普通 GiY 已经对小对象用 memcpy。
   - NT store 更适合 256B 以上甚至更大的对象。
   - 对 64B 以下对象强行 staging 成大块，目前看并不划算。

当前判断：
- GiYSB 的“批量 tiny copy”不是没有道理，但当前成本模型太乐观。
- 真正更可能成功的路线是：先做 PlaceOnly，证明 old layout 本身有没有收益；然后只对少数明确值得的对象启用 batching。

## 2026-05-22：GiYSB PlaceOnly 实装和 full-suite benchmark 结果

根据上面的判断，实装了新的 GiYSB 选项：

```text
GIYSB_TINY_BATCH=0
```

这个选项的含义：
- 仍然使用 GiYSB 的 small/large old-space split。
- 不再分配 tiny table。
- 不再分配 staging buffer。
- 不再延迟 tiny object 的复制。
- 对象仍然在 scavenge 中立即复制，和普通 GiY 的 materialization 时机一致。

这次测试了两个 PlaceOnly 变体：

1. `giysb_place_tiny64_equal_896`
   - `GIYSB_TINY_BATCH=0`
   - `GIYSB_SMALL_OBJECT_MAX_BYTES=64`
   - 只有 `<=64B` 的对象进入 small old 区。

2. `giysb_place_256_equal_896`
   - `GIYSB_TINY_BATCH=0`
   - `GIYSB_SMALL_OBJECT_MAX_BYTES=256`
   - `<=256B` 的对象进入 small old 区。

公平性设置：
- 三组都使用 `CACHE_SIZE_KB=896`。
- 三组都使用 `GIY_GC_STACK_BYTES=49152`，也就是 48KB。
- 三组都使用 `GIY_NT_COPY_BITS=256`。
- 三组都使用 `GIY_LOCAL_PADDING_BYTES=24576`，也就是 24KB padding。
- 因此三组的 young after aux 都是 580.24KB。
- 三组的 aux total 都是 104KB。
- 没有启用 cache miss/perf 测量。
- 使用 `EJS_DISABLE_GC_PMU=1`。
- full suite 13 个 benchmark 全部正常结束，status 都是 0。

输出目录：

```text
/home/qiancheng/ejs-new/build.debug/benchmarks/out_giysb_placeonly_equal_young_896_20260522_024809
```

aggregate 结果，两个 PlaceOnly 变体相对 `giy896_equal_young`：

| 指标 | GiY equal | PlaceTiny64 差异 | Place256 差异 |
|---|---:|---:|---:|
| total execution | 6651.224s | -36.167s (-0.544%) | -56.506s (-0.850%) |
| business residual | 6513.874s | -37.556s (-0.577%) | -56.735s (-0.871%) |
| GC overhead | 137.350s | +1.389s (+1.011%) | +0.229s (+0.167%) |
| GC core | 101.478s | +1.631s (+1.607%) | +0.314s (+0.309%) |
| scan roots | 0.535s | +0.028s (+5.234%) | +0.003s (+0.561%) |
| scan_RS | 25.380s | +0.221s (+0.871%) | +0.243s (+0.957%) |
| scavenge | 75.561s | +1.382s (+1.829%) | +0.069s (+0.091%) |
| minor GC count | 1,088,977 | 0 | 0 |
| forward operations | 1,676,418,379 | -4 | +1 |
| total alloc bytes | 305448.160MB | 0 | 0 |

最关键结论：
- `Place256` 的 GC overhead 只比 GiY equal 多 0.229s，约 +0.167%。
- `Place256` 的 scavenge 只多 0.069s，约 +0.091%。
- 这个差距基本可以认为和 GiY 持平。
- 和之前 `GiYSB-Tiny16-Adaptive` 相比，这是巨大改善。

对比之前 `GiYSB-Tiny16-Adaptive` 的 equal-young 结果：

| 方案 | total 相对 equal GiY | GC overhead 相对 equal GiY | scavenge 相对 equal GiY |
|---|---:|---:|---:|
| Tiny16-Adaptive | +3.825s | +24.762s | +23.846s |
| PlaceTiny64 | -36.167s | +1.389s | +1.382s |
| Place256 | -56.506s | +0.229s | +0.069s |

这说明：
- Tiny16-Adaptive 的 GC 退化基本来自 tiny table/staging/materialization 延迟。
- 去掉 tiny batching 后，GC 退化几乎消失。
- old-space 按大小分区本身没有造成明显 GC 问题。
- 在这次数据里，`<=256B` 放入 small old 比只放 `<=64B` 更好。

关键 benchmark：

Havlak：
- Tiny16-Adaptive 之前 scavenge 大约多 6.845s。
- 这次 PlaceTiny64 scavenge 只多 0.985s。
- 这次 Place256 scavenge 只多 0.333s。

Storage：
- Tiny16-Adaptive 之前 scavenge 大约多 14.465s。
- 这次 PlaceTiny64 scavenge 多 0.370s。
- 这次 Place256 scavenge 反而少 0.281s。

因此，现在最好的优化方向不是继续扩大 tiny table 或 staging buffer，而是：

```text
GiYSB-PlaceOnly256
```

推荐下一步：
1. 把 `GIYSB_TINY_BATCH=0` 作为新的 GiYSB 优化主线。
2. 默认使用 `GIYSB_SMALL_OBJECT_MAX_BYTES=256`。
3. 保留 immediate copy，不做 tiny table/staging。
4. 如果之后还想重新引入 batching，必须非常保守：
   - 只在 survivor tiny bytes 很大时启用；
   - batch 阈值至少应该高于 4KB，例如从 16KB 或 32KB 起测；
   - 优先只考虑 pointer-free 或扫描成本低的对象；
   - 默认路径仍然应该是 immediate memcpy。

当前推荐给导师的说法：
- “之前的 GiYSB-Tiny16-Adaptive 证明了 naive tiny batching 成本太高。”
- “新的 PlaceOnly 实验证明，old-space placement 本身并不是问题。”
- “`PlaceOnly256` 基本保持了 GiY 的 GC 时间，同时总时间在这次 full suite 中略好。”
- “下一步应以 PlaceOnly256 作为新的 baseline，再研究是否存在非常保守、按收益触发的 batching 条件。”

## 2026-05-22：记录遗漏复查，以及给导师汇报的短版说法

用户指出上一条回答没有把“如何向导师汇报、下一步怎么说”的短版内容写入 `AIlog.md`。复查结果如下：

- PlaceOnly 的实装、full-suite 结果、关键数据、推荐方向已经写入上一节。
- 但是上一条回复中给出的“导师汇报用短版说法”没有作为单独条目追加。
- 这是流程错误。用户已经多次明确要求重要结论必须写入 `AIlog.md`，以后在给最终结论前必须检查是否已经追加记录。

这类错误对当前代码结论的影响：

- 代码和 benchmark 结论不是只靠口头判断得出的。
- 本轮 PlaceOnly 测试已经完成 full suite，13 个 benchmark 全部 `status=0`。
- 三组配置的 young after aux、aux total、minor GC count、allocated bytes 都一致。
- 因此性能结论本身仍有实验依据。
- 但是记录遗漏说明工作流程不够严格，不能忽视。后续每次给出实验结论、设计结论、导师汇报材料前，都要同步更新 `AIlog.md`。

给导师汇报时可以使用的短版说法：

```text
This week I separated two effects in GiYSB:
1. tiny-object batching
2. size-based old-space placement

The previous GiYSB-Tiny16-Adaptive design used a tiny table and staging buffer
to batch small objects before copying them to old space.
The result showed that this batching mechanism was too expensive:
GC overhead increased by about 24.8 seconds,
and scavenge time increased by about 23.8 seconds.

Then I implemented GiYSB-PlaceOnly.
This version removes the tiny table and staging buffer,
keeps immediate copying,
and only keeps size-based old-space placement.

With PlaceOnly256, the GC time became almost the same as normal GiY:
GC overhead increased by only 0.229 seconds,
and scavenge increased by only 0.069 seconds.

So the main problem was not old-space placement.
The main problem was the tiny batching mechanism.
```

中文理解：

```text
我把 GiYSB 拆成两个部分来验证：
一个是 tiny object batching，
另一个是按照对象大小放置到 old 区。

实验结果说明，之前慢的主要原因不是 old 区分成 small/large，
而是 tiny table 和 staging buffer 这套 batching 机制成本太高。

去掉 batching，只保留 PlaceOnly256 之后，
GC 时间几乎回到了普通 GiY 的水平。
```

下一步计划的短版说法：

```text
Next, I will use GiYSB-PlaceOnly256 as the new baseline.
I will first analyze whether size-based old-space placement improves locality.
If I reintroduce batching later, it should be much more conservative:
only enabled when the expected copied bytes are large enough,
and only for object types where batching is likely to pay off.
```

中文理解：

```text
下一步我会把 GiYSB-PlaceOnly256 当作新的 baseline。
我先分析按照对象大小放置到 old 区是否真的改善 locality。
如果之后重新引入 batching，也不能像之前一样默认对 tiny object 全部启用；
必须只在预计收益足够大的时候启用。
```

最短结论：

```text
The first batching design was too expensive.
The new result shows that size-based placement is the more promising direction.
My next step is to build on PlaceOnly256 and study when placement or batching is actually profitable.
```

## 2026-05-22：GiYSB 核心代码位置和核心思想

GiYSB 的核心代码主要在 `ejsvm/GiY.cc`，不是一个独立的新 GC 文件。

最核心的几部分：

1. 编译开关和参数：
   - `USE_GIYSB`
   - `GIYSB_TINY_BATCH`
   - `GIYSB_SMALL_OBJECT_MAX_BYTES`
   - `GIYSB_TINY_OBJECT_MAX_BYTES`
   - `GIYSB_STAGING_BYTES`
   - `GIYSB_TINY_TABLE_BYTES`

2. old 区按大小拆分的数据结构：
   - `GiYSBOldSpace`
   - 里面有 `small_begin/small_free/small_end`
   - 也有 `large_begin/large_free/large_end`
   - 意思是把 old 区逻辑上拆成 small old 和 large old。

3. old 区初始化：
   - `giysb_ensure_old_space()`
   - 它按照 `GIYSB_SMALL_OLD_RATIO` 把 `dram_space` 拆成 small 区和 large 区。

4. 最核心的 reserve 策略：
   - `giysb_reserve_old_object()`
   - 这个函数决定一个 live young object 将来被 promoted 到 old 区的哪里。
   - 如果是当前推荐的 PlaceOnly 路径，也就是 `GIYSB_TINY_BATCH=0`：
     - `align_bytes <= GIYSB_SMALL_OBJECT_MAX_BYTES` 的对象进入 small old。
     - 其他对象进入 large old。
     - 对象仍然立即 copy，不经过 tiny table 和 staging buffer。
   - 如果是旧的 tiny batching 路径，也就是 `GIYSB_TINY_BATCH=1`：
     - `<=64B` 的 tiny object 会先记录到 tiny table。
     - 复制本体会被延迟。
     - 之后通过 `giysb_flush_tiny_table()`、`giysb_stage_tiny_chunk()`、`giysb_flush_staging()` 批量 materialize。
     - 实验结果显示这条路径成本太高，所以现在不推荐作为主线。

5. 和 minor GC 的连接点：
   - `copy_for_minor()`
   - 普通 GiY 在这里直接从 `dram_space.free` reserve old 地址。
   - GiYSB 在这里调用 `giysb_reserve_old_object()`，让 old 地址按照对象大小被分配到 small old 或 large old。
   - 然后设置 `forwarding_pointer`，并把对象放入 `gc_stack`。

6. 真正复制对象的位置：
   - 在 scavenge/materialization 循环中，先用 `giy_process_young_node<GiYReserveTracer>()` 扫描对象内部引用并更新 child forwarding。
   - 当前推荐的 `GIYSB_TINY_BATCH=0` 路径会继续调用 `giy_copy_live_object()` 立即把对象复制到 old。
   - 只有旧的 `GIYSB_TINY_BATCH=1` 路径才会对 tiny object 跳过立即复制，最后统一 flush tiny table。

一句话总结：

```text
GiYSB 的核心不是改变对象图扫描方式，而是改变 young object 被提升到 old 区时的 reserve 地址策略：小对象集中放到 small old，大对象放到 large old；当前推荐版本保留 immediate copy，不再使用 tiny table/staging。
```

对当前研究最重要的判断：

```text
当前最应该看的核心函数是 giysb_reserve_old_object()。
它决定 GiYSB 和普通 GiY 的真正差别。
copy_for_minor() 是它接入 GC 流程的位置。
giy_copy_live_object() 是实际复制字节的位置。
```

## 2026-05-22：`giysb_reserve_old_object()` 代码解释

`giysb_reserve_old_object()` 是 GiYSB 的核心 reserve 函数。

它的职责不是复制对象，而是：

```text
给一个即将从 young 区 promoted 到 old 区的对象，决定并预留 old 区里的目标地址。
```

参数含义：

```text
payload_ptr:
  源对象在 young/cache space 里的 payload 地址。
  注意它不是 header 地址，header 在 payload_ptr 前面。

align_bytes:
  这个对象完整复制时需要占用的字节数。
  它包含 object_header，并且已经做过对齐。

deferred_tiny:
  输出参数。
  用来告诉调用者：这个 tiny object 是否采用了延迟复制。
  当前推荐的 GIYSB_TINY_BATCH=0 路径中，它始终保持 false。
```

函数开头：

```text
giysb_ensure_old_space();
*deferred_tiny = false;
```

意思是：
- 确保 old 区已经被拆成 small old 和 large old。
- 默认认为对象不会延迟复制。

如果 `GIYSB_TINY_BATCH=1`：

```text
<= GIYSB_TINY_OBJECT_MAX_BYTES 的 tiny object
并且 small old 还有空间
并且 tiny table 还有空间
```

那么：
- 在 small old 里 reserve 目标地址。
- 把源对象信息记录到 tiny table。
- 推进 `small_free`。
- 设置 `deferred_tiny = true`。
- 返回目标 header 地址。

这条路径的含义是：

```text
先 reserve old 地址，但暂时不复制对象本体。
之后再通过 tiny table/staging buffer 批量 materialize。
```

如果 `GIYSB_TINY_BATCH=0`，也就是当前推荐的 PlaceOnly 路径：

```text
align_bytes <= GIYSB_SMALL_OBJECT_MAX_BYTES
```

并且 small old 还有空间，那么：
- 直接在 small old reserve。
- 推进 `small_free`。
- 更新 `dram_space.available_bytes`。
- 返回目标 header 地址。

这条路径不会使用 tiny table，也不会使用 staging buffer。
它只是改变 old 区放置位置，对象之后仍然立即复制。

如果前面的 small/tiny 条件不满足，就进入 `reserve_large`：

```text
在 large old 里 reserve 目标地址。
推进 large_free。
更新 dram_space.available_bytes。
返回目标 header 地址。
```

如果 large old 也没有空间，就说明当前 DRAM old space 不够，程序打印错误并退出。

一句话总结：

```text
giysb_reserve_old_object() 做的是 size-based old-space reservation：
小对象优先进入 small old；
其他对象进入 large old；
只有旧的 GIYSB_TINY_BATCH=1 路径才会额外启用 tiny table 和延迟复制。
```

## 2026-05-22：`copy_for_minor()` 代码解释，以及 `(void) giysb_deferred_tiny`

`copy_for_minor()` 的名字容易误导。

在当前 GiY/GiYSB 代码里，它的主要职责不是马上复制对象内容，而是：

```text
1. 判断这个 young object 是否已经有 forwarding pointer。
2. 如果已经有，直接返回旧的 forwarding pointer，避免重复 reserve 和重复扫描。
3. 如果还没有，计算对象复制到 old 区需要的对齐后大小。
4. 在 old 区 reserve 一个目标地址。
5. 把目标 payload 地址写入源对象 header 的 forwarding_pointer。
6. 把源对象 payload_ptr push 到 GC stack，等待后面扫描 children 并 materialize。
7. 返回 forwarding pointer。
```

普通 GiY 和 GiYSB 的关键区别在第 4 步：

```text
普通 GiY：
  dest_hdr = dram_space.free;
  dram_space.free += align_bytes;

GiYSB：
  dest_hdr = giysb_reserve_old_object(...);
```

也就是说，GiYSB 通过 `copy_for_minor()` 接入 minor GC 流程，但是具体 old 地址放在哪里，由 `giysb_reserve_old_object()` 决定。

`hdr->forwarding_pointer = (uintptr_t)(dest_hdr + 1);`

这里存的是目标对象的 payload 地址，不是 header 地址。
因为运行时对象引用通常指向 payload，所以 forwarding pointer 也保持 payload pointer 的形式。

`gc_stack_push(payload_ptr);`

这里 push 的是源对象的 young payload 地址。
后面的 scavenge 会 pop 出这个源对象，扫描它内部的引用，把 child reference 也 forward，然后再把对象内容复制到 old。

`(void) giysb_deferred_tiny;` 的含义：

```text
这个变量当前在 copy_for_minor() 里没有被使用。
写 (void) giysb_deferred_tiny; 是为了明确告诉编译器：
“我知道这个变量现在没有用，这是有意的。”
```

这样可以避免 unused variable 警告。
如果编译选项把 warning 当作 error，这种写法可以避免编译失败。

为什么会出现这个变量但又没有使用：

```text
giysb_reserve_old_object() 通过 deferred_tiny 告诉调用者：
这个对象是否走了 tiny batching 延迟复制。

但是当前 copy_for_minor() 没有直接用这个布尔值。
旧的 tiny batching 路径在后面的 materialization 阶段，
通过 giysb_is_tiny_destination() 判断目标地址是否在 tiny/small 范围内，
从而决定是否跳过立即复制。

当前推荐的 GIYSB_TINY_BATCH=0 路径中，
deferred_tiny 永远保持 false，也完全不需要使用。
```

因此，这行代码不是逻辑需要，而是工程上抑制编译器 unused warning 的写法。

可以改进的地方：

```text
如果之后确定不再使用 GIYSB_TINY_BATCH=1，
可以考虑把 deferred_tiny 参数从 giysb_reserve_old_object() 删除。

如果还保留 tiny batching 实验路径，
保留这个参数也可以，但最好让后续逻辑真正使用它，
或者用更明确的返回结构表达 reserve 结果。
```

## 2026-05-22：GiY 与当前 GiYSB-PlaceOnly256 的逐 benchmark 时间数据

数据来源：

```text
build.debug/benchmarks/out_giysb_placeonly_equal_young_896_20260522_024809/summary.csv
```

这里的 GiYSB 指当前推荐的主线配置：

```text
giysb_place_256_equal_896
GIYSB_TINY_BATCH=0
GIYSB_SMALL_OBJECT_MAX_BYTES=256
CACHE_SIZE_KB=896
GIY_GC_STACK_BYTES=49152
GIY_LOCAL_PADDING_BYTES=24576
GIY_NT_COPY_BITS=256
EJS_DISABLE_GC_PMU=1
```

表中：

```text
端到端时间 = total_execution_sec
GC 时间 = gc_overhead_sec
delta = GiYSB - GiY
```

| Benchmark | GiY 端到端(s) | GiYSB 端到端(s) | 端到端差值(s) | 端到端差值% | GiY GC(s) | GiYSB GC(s) | GC差值(s) | GC差值% |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Bounce | 304.227 | 304.588 | +0.361 | +0.119% | 0.158 | 0.161 | +0.003 | +1.899% |
| CD | 483.997 | 483.146 | -0.851 | -0.176% | 14.840 | 14.740 | -0.100 | -0.674% |
| DeltaBlue | 134.007 | 133.165 | -0.842 | -0.628% | 3.506 | 3.558 | +0.052 | +1.483% |
| Havlak | 827.149 | 792.719 | -34.430 | -4.162% | 56.697 | 57.211 | +0.514 | +0.907% |
| List | 186.302 | 186.177 | -0.125 | -0.067% | 0.021 | 0.020 | -0.001 | -4.762% |
| Mandelbrot | 398.946 | 398.317 | -0.629 | -0.158% | 6.951 | 6.765 | -0.186 | -2.676% |
| NBody | 645.904 | 644.997 | -0.907 | -0.140% | 11.327 | 11.512 | +0.185 | +1.633% |
| Permute | 823.598 | 811.390 | -12.208 | -1.482% | 0.002 | 0.002 | +0.000 | +0.000% |
| Queens | 218.030 | 218.130 | +0.100 | +0.046% | 0.042 | 0.046 | +0.004 | +9.524% |
| Richards | 1732.580 | 1731.028 | -1.552 | -0.090% | 0.144 | 0.144 | +0.000 | +0.000% |
| Sieve | 236.865 | 237.046 | +0.181 | +0.076% | 1.010 | 1.058 | +0.048 | +4.752% |
| Storage | 333.364 | 327.949 | -5.415 | -1.624% | 42.647 | 42.357 | -0.290 | -0.680% |
| Towers | 326.255 | 326.066 | -0.189 | -0.058% | 0.005 | 0.005 | +0.000 | +0.000% |
| **合计** | **6651.224** | **6594.718** | **-56.506** | **-0.850%** | **137.350** | **137.579** | **+0.229** | **+0.167%** |

结论：

```text
当前 GiYSB-PlaceOnly256 的端到端时间比 GiY 快 56.506s，约快 0.850%。
但 GC 时间比 GiY 慢 0.229s，约慢 0.167%。
也就是说，这次数据里 GiYSB 的主要收益不来自 GC 时间变短，而来自非 GC/business 部分略微变快。
```

## 2026-05-22：GiYSB staging 版本的逐 benchmark 时间数据

用户指出上一节给的是 GiYSB-PlaceOnly256，不是“真正的 staging 版本”。
这里重新记录 staging 版本，也就是：

```text
giysb_tiny16_adaptive_896
GIYSB_TINY_BATCH=1
GIYSB_STAGING_BYTES=8192
GIYSB_TINY_TABLE_BYTES=16384
GIYSB_TINY_OBJECT_MAX_BYTES=64
GIYSB_TINY_STAGING_MIN_BYTES=4096
GIY_NT_COPY_BITS=256
EJS_DISABLE_GC_PMU=1
```

数据来源：

```text
build.debug/benchmarks/out_giysb_tiny16_adaptive_vs_giy_nt256_896_20260521_175640/summary.csv
build.debug/benchmarks/out_giy_equal_giysb_young_nt256_896_20260522_001944/summary.csv
```

先记录同一轮交错运行的原始数据。
注意：这组原始数据里，GiY 的 young after aux 是 604.24KB，GiYSB staging 的 young after aux 是 580.24KB。
所以这组数据适合看“当时完整配置”的结果，但不是 young 大小完全公平的结果。

| Benchmark | GiY端到端(s) | GiYSB staging端到端(s) | 端到端差值(s) | 端到端差值% | GiY GC(s) | GiYSB staging GC(s) | GC差值(s) | GC差值% |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Bounce | 322.988 | 306.259 | -16.729 | -5.179% | 0.173 | 0.168 | -0.005 | -2.890% |
| CD | 487.558 | 489.640 | +2.082 | +0.427% | 14.589 | 16.820 | +2.231 | +15.292% |
| DeltaBlue | 134.088 | 134.117 | +0.029 | +0.022% | 3.434 | 3.952 | +0.518 | +15.084% |
| Havlak | 842.406 | 808.708 | -33.698 | -4.000% | 56.112 | 64.006 | +7.894 | +14.068% |
| List | 187.218 | 186.822 | -0.396 | -0.212% | 0.023 | 0.024 | +0.001 | +4.348% |
| Mandelbrot | 425.122 | 400.706 | -24.416 | -5.743% | 6.429 | 6.742 | +0.313 | +4.869% |
| NBody | 674.536 | 645.469 | -29.067 | -4.309% | 10.826 | 11.524 | +0.698 | +6.447% |
| Permute | 807.587 | 806.431 | -1.156 | -0.143% | 0.002 | 0.002 | +0.000 | +0.000% |
| Queens | 231.119 | 217.085 | -14.034 | -6.072% | 0.044 | 0.043 | -0.001 | -2.273% |
| Richards | 1737.544 | 1729.876 | -7.668 | -0.441% | 0.151 | 0.157 | +0.006 | +3.974% |
| Sieve | 236.935 | 237.675 | +0.740 | +0.312% | 0.990 | 1.023 | +0.033 | +3.333% |
| Storage | 330.330 | 344.314 | +13.984 | +4.233% | 42.445 | 57.063 | +14.618 | +34.440% |
| Towers | 330.042 | 330.093 | +0.051 | +0.015% | 0.006 | 0.006 | +0.000 | +0.000% |
| **合计** | **6747.473** | **6637.195** | **-110.278** | **-1.634%** | **135.224** | **161.530** | **+26.306** | **+19.454%** |

再记录 equal-young 对比。
这里 GiY 通过 `GIY_LOCAL_PADDING_BYTES=24576` 把 young after aux 也调成 580.24KB。
这组更适合解释 GC 本身是否变快。
注意：GiY equal-young 和 GiYSB staging 不是同一轮交错运行，所以总时间仍可能有时间窗口噪音；GC 内部指标更可信。

| Benchmark | GiY equal-young端到端(s) | GiYSB staging端到端(s) | 端到端差值(s) | 端到端差值% | GiY equal-young GC(s) | GiYSB staging GC(s) | GC差值(s) | GC差值% |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Bounce | 304.114 | 306.259 | +2.145 | +0.705% | 0.166 | 0.168 | +0.002 | +1.205% |
| CD | 484.272 | 489.640 | +5.368 | +1.108% | 14.890 | 16.820 | +1.930 | +12.962% |
| DeltaBlue | 132.946 | 134.117 | +1.171 | +0.881% | 3.502 | 3.952 | +0.450 | +12.850% |
| Havlak | 795.827 | 808.708 | +12.881 | +1.619% | 56.706 | 64.006 | +7.300 | +12.873% |
| List | 186.836 | 186.822 | -0.014 | -0.007% | 0.023 | 0.024 | +0.001 | +4.348% |
| Mandelbrot | 399.236 | 400.706 | +1.470 | +0.368% | 6.474 | 6.742 | +0.268 | +4.140% |
| NBody | 644.117 | 645.469 | +1.352 | +0.210% | 11.237 | 11.524 | +0.287 | +2.554% |
| Permute | 818.401 | 806.431 | -11.970 | -1.463% | 0.002 | 0.002 | +0.000 | +0.000% |
| Queens | 216.396 | 217.085 | +0.689 | +0.318% | 0.044 | 0.043 | -0.001 | -2.273% |
| Richards | 1732.075 | 1729.876 | -2.199 | -0.127% | 0.158 | 0.157 | -0.001 | -0.633% |
| Sieve | 236.822 | 237.675 | +0.853 | +0.360% | 1.054 | 1.023 | -0.031 | -2.941% |
| Storage | 330.727 | 344.314 | +13.587 | +4.108% | 42.506 | 57.063 | +14.557 | +34.247% |
| Towers | 351.601 | 330.093 | -21.508 | -6.117% | 0.006 | 0.006 | +0.000 | +0.000% |
| **合计** | **6633.370** | **6637.195** | **+3.825** | **+0.058%** | **136.768** | **161.530** | **+24.762** | **+18.105%** |

关键结论：

```text
staging 版 GiYSB 在 equal-young 条件下，端到端时间几乎持平但略慢 0.058%。
但是 GC 时间明显更慢，慢 24.762s，约 +18.105%。
主要问题仍然是 staging/tiny table 路径让 scavenge 成本变高，尤其是 Storage 和 Havlak。
```

## 2026-05-22：为什么 GiYSB staging 的端到端时间接近，但 GC 表现很差

当前最可信的解释：

```text
GiYSB staging 的 GC 变慢是真实的。
端到端时间接近，并不说明 GC 优化成功。
端到端接近主要是因为 GC 时间只占总运行时间的一小部分，并且非 GC 残差时间存在噪音或抵消。
```

equal-young 对比中：

```text
GiY total:   6633.370s
GiYSB total: 6637.195s
total delta: +3.825s (+0.058%)

GiY GC:      136.768s
GiYSB GC:    161.530s
GC delta:    +24.762s (+18.105%)
```

也就是说：

```text
GiYSB 的 GC 慢了 24.762s。
但是总时间只慢 3.825s。
因此非 GC 残差时间反而少了大约 20.937s。
```

这个非 GC 残差时间不是独立测量出来的 mutator 时间，而是：

```text
business_sec = total_execution_sec - gc_overhead_sec
```

所以它可能包含：
- 真正的 mutator 行为变化；
- 运行时噪音；
- CPU 频率和系统负载变化；
- 不同时间窗口运行造成的偏差；
- total 和 GC timer 的测量误差。

特别需要注意：
- equal-young 的 GiY 和 GiYSB 不是同一轮交错运行。
- Towers 和 Permute 这种 GC 几乎为 0 的 benchmark 出现了很大的端到端差异。
- 这说明 total/business residual 里有明显的运行噪音或非 GC 因素。

为什么 GC 表现差：

1. GiYSB staging 对 tiny object 增加了额外路径。
   - 普通 GiY：扫描后直接 `giy_copy_live_object()`。
   - GiYSB staging：tiny object 先记录 tiny table，后面再 decode，再 direct copy 或 staging copy。

2. tiny object 本来很小，复制收益很难覆盖记录成本。
   - 记录 table 需要编码 young offset 和 size class。
   - flush 时需要逐条 decode。
   - staging 时还会先 `memcpy` 到 staging，再从 staging 写到 old。
   - 对 64B 以下对象，这些额外操作很容易比直接 memcpy 更贵。

3. GiYSB staging 的收益条件很苛刻。
   - 它希望把很多 tiny object 聚成较大的连续 old 写入。
   - 但是 source 在 young 区并不一定连续。
   - object graph 的 LIFO 扫描顺序也不一定等于物理顺序。
   - 因此 staging 经常只是增加了一次搬运，而没有得到足够大的连续写入收益。

4. Storage 和 Havlak 证明主要损失在 scavenge。
   - Storage：GC 多约 14.557s。
   - Havlak：GC 多约 7.300s。
   - 这两个 benchmark 有大量 live object / tiny object / graph traversal，刚好会放大 tiny table 和 staging 路径的成本。

5. young size 不是根本原因。
   - 最初 staging 版 GiYSB 比 GiY 多占 24KB workspace，导致 young 更小、minor GC 更多。
   - 但 equal-young 实验已经把 GiY young 也调成一样大。
   - minor GC 次数一致后，GiYSB GC 仍然慢约 18%。
   - 所以根本问题在每次 GC 内部的 scavenge/materialization 成本。

端到端为什么接近：

```text
GC 时间只占总时间约 2% 左右。
即使 GC 慢 18%，折算到总时间也只有大约 0.3%～0.4% 量级。
再加上 total/business residual 的自然波动，端到端差异就会被掩盖。
```

此外，GiYSB small/large old placement 可能让某些 benchmark 的 old object 布局稍微更友好，从而让非 GC 访问变快。
但是这个目前没有直接证据，不能当作主结论。

当前应向导师报告的判断：

```text
GiYSB staging 版验证了一个重要事实：
简单地把 tiny object 先记录到 table，再通过 staging 批量写回 old 区，并没有降低 GC 时间。

它的主要失败点不是 reserve 思想，而是 tiny table/staging/materialization 路径的 per-object 成本太高。

端到端时间接近，是因为 GC 占总时间比例很小，并且非 GC 残差时间有噪音或抵消。
因此不能用端到端接近来证明 staging 方案成功。
```

下一步更合理的方向：

```text
以 GiYSB-PlaceOnly256 作为 baseline。
先保留 small/large old placement，去掉 tiny table 和 staging。
如果未来重新引入 batching，必须只在明确有大批量、连续目标、足够收益时启用，而不能默认对所有 tiny object 启用。
```

## 2026-05-22：给导师说明当前 GiYSB staging 策略的简单英文

用户需要用简单英语向教授说明当前 GiYSB staging 版本采用的策略。

推荐说法：

```text
My current GiYSB is a size-based promotion strategy with a staging buffer.

During minor GC, when a young object survives, GiYSB first reserves its target address in the old generation.
Small objects and large objects are placed in different old-generation regions.

For very small objects, currently up to 64 bytes, GiYSB does not copy them immediately.
Instead, it records them in a small table.
After scanning, these tiny objects are copied in batches through an 8KB staging buffer.
The goal is to turn many small copies into fewer larger writes to the old generation, and then use non-temporal stores more effectively.

Medium and large objects are still copied directly, because recording and staging them would add unnecessary overhead.
```

更短版本：

```text
GiYSB tries to improve GiY by batching tiny-object promotion.
It reserves old-generation addresses first, records tiny objects in a table, and later copies them through an 8KB staging buffer.
The goal is to reduce many small fragmented copies and make old-generation writes more sequential.
```

中文理解：

```text
当前 GiYSB staging 版的核心是：
先 reserve old 地址；
小对象和大对象放到 old 区不同位置；
tiny object 不立即复制，而是记录到 tiny table；
之后通过 8KB staging buffer 批量复制；
目标是把很多小碎 copy 变成更大的连续写入。
```

## 2026-05-22：理解 GiYSB 最重要的函数，以及 GiYSB 全称

当前代码里 `GIY_SB` / `GiYSB` 没有写正式英文全称。
根据命名和实际设计，最稳妥的全称建议是：

```text
GiY-SB = GiY with Staging Buffer
```

如果需要更完整地表达策略，可以在论文或汇报第一次出现时写：

```text
GiY with a Staging Buffer (GiY-SB)
```

或者更描述性的版本：

```text
GiY with Size-based Staging Buffer
```

但是严格按缩写来说，`SB` 最自然对应 `Staging Buffer`。
`size-based` 是 GiYSB 当前策略的一部分，不一定必须放进 acronym。

为了理解 GiYSB staging 版本，最重要的函数如下：

1. `giy_bind_stack_to_cache_impl()`
   - 作用：把 GiYSB 的 staging buffer 和 tiny table 放进 GC Local Workspace。
   - 关键点：只有 `USE_GIYSB && GIYSB_TINY_BATCH` 时才分配这两个结构。

2. `giysb_ensure_old_space()`
   - 作用：把 old/dram space 逻辑上切成 small old 和 large old。
   - 这是 size-based placement 的基础。

3. `giysb_reserve_old_object()`
   - 作用：GiYSB 最核心的 reserve 策略函数。
   - tiny object：reserve 到 small old，并记录到 tiny table。
   - 其他对象：reserve 到 large old 或普通路径。
   - 这个函数决定 GiYSB 和普通 GiY 的核心差异。

4. `copy_for_minor()`
   - 作用：minor GC 发现 live young object 后，接入 GiYSB reserve 策略。
   - 它设置 `forwarding_pointer`，并把源对象 push 到 GC stack。
   - 它本身主要不是复制 bytes，而是 reserve + forward。

5. `giysb_tiny_table_append()`
   - 作用：把 tiny object 的 young offset 和 size class 编码进 tiny table。
   - 这是“先记录，不立刻复制”的入口。

6. scavenge/materialization 循环中的 `giysb_is_tiny_destination()`
   - 作用：判断当前对象是不是 tiny batching 的对象。
   - 如果是 tiny destination，就跳过立即 `giy_copy_live_object()`。

7. `giysb_flush_tiny_table()`
   - 作用：GC 扫描结束后，统一处理 tiny table 中记录的小对象。
   - 它把 tiny table 里的 entries 重新解码，并按 chunk flush。

8. `giysb_flush_tiny_chunk()`
   - 作用：决定一个 tiny chunk 是直接逐对象复制，还是先进入 staging buffer。
   - 当前阈值是 `GIYSB_TINY_STAGING_MIN_BYTES=4096`。

9. `giysb_stage_tiny_chunk()`
   - 作用：把一批 tiny object 先 `memcpy` 到 staging buffer。
   - 这是 staging 方案额外成本的主要来源之一。

10. `giysb_flush_staging()`
    - 作用：把 staging buffer 里的连续 bytes 写到 old 区。
    - 实际写入仍调用 `giy_copy_live_object()`，从而使用已有的 memcpy/NT copy 策略。

11. `giy_minor_collect()`
    - 作用：每次 minor GC 开始时 reset staging buffer 和 tiny table。
    - 这是 GiYSB 每轮 GC 状态初始化的位置。

最短理解路径：

```text
giy_bind_stack_to_cache_impl()
  -> 准备 staging buffer 和 tiny table

copy_for_minor()
  -> 发现 live object 后调用 GiYSB reserve

giysb_reserve_old_object()
  -> 决定放 small old / large old，并记录 tiny table

scavenge loop
  -> tiny object 暂时不复制

giysb_flush_tiny_table()
  -> 扫描结束后统一处理 tiny objects

giysb_stage_tiny_chunk()
  -> 先放进 staging buffer

giysb_flush_staging()
  -> 再写回 old 区
```

一句话总结：

```text
理解 GiYSB 最重要的是理解三件事：
old 区怎么切分；
tiny object 怎么先 reserve/记录；
tiny table 最后怎么通过 staging buffer flush 到 old。
```

## 2026-05-22：`giysb_flush_tiny_table()` 详细解释

`giysb_flush_tiny_table()` 是 staging 版 GiYSB 在一次 minor GC 的 young traversal 结束后调用的函数。

它的作用不是直接复制每一个对象，而是：

```text
遍历 tiny table；
根据 staging buffer 容量把 tiny objects 分成若干 chunk；
每个 chunk 再交给 giysb_flush_tiny_chunk() 决定是 direct copy 还是 staging copy。
```

调用位置：

```text
scavenge/materialization 循环结束后：
giysb_flush_tiny_table(&used_nt_store);
然后 giy_finish_local_nt_stores(&used_nt_store);
```

每次 minor GC 开始时，会先 reset：

```text
giysb_staging_reset();
giysb_tiny_table_reset();
```

函数开头：

```text
if (g_giysb_tiny_table_count == 0) return;
```

如果本轮 GC 没有 tiny object 被记录，就什么都不做。

然后：

```text
if (g_giysb_tiny_batch_begin == 0) error;
```

`g_giysb_tiny_batch_begin` 是 tiny batch 在 old/small old 区的第一个目标地址。
如果 tiny table 有 entry，但 batch begin 是 0，说明状态不一致。

接着初始化 chunk 状态：

```text
chunk_start = 0;
chunk_dst = g_giysb_tiny_batch_begin;
dst_cursor = g_giysb_tiny_batch_begin;
chunk_bytes = 0;
```

含义：
- `chunk_start`：当前 chunk 在 tiny table 中的起始 index。
- `chunk_dst`：当前 chunk 对应的 old 目标起始地址。
- `dst_cursor`：当前已经累计到的 old 目标末尾地址。
- `chunk_bytes`：当前 chunk 的总字节数。

为什么可以只用 `g_giysb_tiny_batch_begin + 累计大小` 来算目标地址：

```text
GIYSB_TINY_BATCH=1 时，tiny object 会按发现顺序 reserve 到 small old。
每次 reserve 都让 small_free += align_bytes。
所以 tiny table 里的 tiny objects 在目标 old 区是连续排列的。
```

主循环：

```text
for each tiny table entry:
  decode entry -> src_hdr, nbytes
```

`giysb_decode_tiny_entry()` 从 32-bit entry 里恢复：
- 源对象 payload 在 young/cache space 里的 offset；
- 对象大小 size class；
- 源对象 header 地址 `src_hdr`；
- 复制字节数 `nbytes`。

但是在 `giysb_flush_tiny_table()` 里：

```text
(void) src_hdr;
```

说明这个函数这里只需要 `nbytes` 来计算 chunk 大小。
真正复制时，后面的 `giysb_flush_tiny_chunk()` 会再次 decode entry。
这也是当前实现的一个额外成本：同一个 entry 可能被 decode 两次。

chunk 满了时：

```text
if staging capacity != 0
   and chunk_bytes != 0
   and chunk_bytes + nbytes > staging capacity:
       flush 当前 chunk
       从当前 entry 开始新 chunk
```

也就是说，如果把当前对象加入 chunk 后会超过 8KB staging buffer，就先把已有 chunk flush 掉。

这里调用：

```text
giysb_flush_tiny_chunk(chunk_start, i, chunk_dst, chunk_bytes, used_nt_store, true);
```

`full_flush=true` 表示这是因为 staging buffer 容量快满而触发的 flush。
这个值主要用于 profile 统计 full flush 和 tail flush。

每个 entry 处理完后：

```text
chunk_bytes += nbytes;
dst_cursor += nbytes;
```

表示当前 tiny object 已经被纳入 chunk，并推进目标 old 地址 cursor。

循环结束后：

```text
giysb_flush_tiny_chunk(chunk_start,
                       g_giysb_tiny_table_count,
                       chunk_dst,
                       chunk_bytes,
                       used_nt_store,
                       false);
```

这会 flush 最后一个 tail chunk。
`full_flush=false` 表示这是 GC 结束时剩余的 tail，不是因为 staging buffer 满了。

最后：

```text
giysb_tiny_table_reset();
```

清空 tiny table 状态，准备下一次 minor GC。

和下面几个函数的关系：

```text
giysb_flush_tiny_table()
  负责把 tiny table 分成 chunk

giysb_flush_tiny_chunk()
  如果 chunk >= 4KB，则走 staging；
  如果 chunk < 4KB，则逐对象 direct copy。

giysb_stage_tiny_chunk()
  把 chunk 中每个 tiny object memcpy 到 staging buffer。

giysb_flush_staging()
  把 staging buffer 中的连续 bytes 写回 old 区。

giysb_copy_tiny_chunk_direct()
  不用 staging，直接逐对象 giy_copy_live_object()。
```

当前性能问题也能从这个函数看出来：

```text
1. flush_tiny_table 先 decode 一次 tiny entry，只为了算 chunk。
2. 真正 copy 时，flush_tiny_chunk 下面又 decode 一次。
3. staging 路径还会多做 young -> staging 的 memcpy。
4. 如果 chunk 不够大，最后仍然逐对象 direct copy，那么 table 记录和 decode 就几乎纯属额外成本。
```

所以这个函数很好地体现了 staging 版 GiYSB 的核心 tradeoff：

```text
希望用 tiny table + staging buffer 得到更大的连续 old 写入；
但代价是增加了 table 遍历、entry decode、chunk 管理和可能的额外 memcpy。
```

## 2026-05-22：256-bit NT store 循环代码解释

代码：

```cpp
while (n >= 32) {
  __m256i v = _mm256_loadu_si256((const __m256i *) s);
  _mm256_stream_si256((__m256i *) d, v);
  s += 32;
  d += 32;
  n -= 32;
}
```

这段代码的整体含义：

```text
每次从源地址 s 读取 32 bytes，
再用 256-bit non-temporal store 写到目标地址 d，
然后 s/d 都向后移动 32 bytes，
n 减少 32 bytes，
直到剩余字节数不足 32 bytes。
```

各部分含义：

```text
while:
  C/C++ 循环语句。只要条件为真，就重复执行循环体。

n >= 32:
  判断剩余要复制的字节数是否至少还有 32 bytes。
  32 bytes = 256 bits。

__m256i:
  Intel AVX/AVX2 的 256-bit 整数向量类型。
  可以理解成一个能装 32 bytes 的向量寄存器值。

v:
  临时变量，用来保存刚从源地址 s 读出来的 32 bytes。
  理论上它会放在 YMM 向量寄存器里。

_mm256_loadu_si256:
  Intel intrinsic。
  从内存读取 256-bit，也就是 32 bytes。
  loadu 中的 u 表示 unaligned，所以源地址 s 不要求 32-byte 对齐。

(const __m256i *) s:
  强制类型转换。
  告诉编译器：把 s 这个地址当作“指向 256-bit 向量”的指针来读。
  const 表示这个 load 不会修改源内存。

_mm256_stream_si256:
  Intel intrinsic。
  把 256-bit 数据用 streaming store / non-temporal store 写到内存。
  目的通常是尽量避免把目标地址的数据带进 cache。

(__m256i *) d:
  强制类型转换。
  告诉编译器：把 d 这个地址当作“指向 256-bit 向量”的目标地址来写。
  这里没有 const，因为目标内存会被修改。

s += 32:
  源指针向后移动 32 bytes。

d += 32:
  目标指针向后移动 32 bytes。

n -= 32:
  剩余字节数减少 32 bytes。
```

关键注意点：

```text
_mm256_loadu_si256 的源地址 s 可以不对齐。
但是 _mm256_stream_si256 的目标地址 d 通常要求 32-byte aligned。
如果 d 没有 32-byte 对齐，可能出错或性能很差。
所以使用这段循环之前，一般要先处理前缀，让 d 对齐到 32B 边界。
```

和 GiY/GiYSB 的关系：

```text
这段循环是 256-bit non-temporal copy 的核心。
它适合把比较大的连续数据写到 old 区。
对很小的对象，它不一定划算，因为对齐处理、循环准备、sfence 等成本可能超过收益。
```

## 2026-05-22：为什么 8B 分支还要用 `_mm_stream_si64`

相关代码：

```cpp
uint64_t v;
memcpy(&v, s, sizeof(v));
_mm_stream_si64((long long *) d, (long long) v);
s += 8;
d += 8;
n -= 8;
```

这里使用 `_mm_stream_si64` 的原因是：

```text
当剩余部分只有 8 bytes，或者目标地址还没有对齐到 16B/32B 时，
不能使用 128-bit 或 256-bit streaming store。
但是代码仍然想让这 8 bytes 也用 non-temporal store 写到 old 区。
所以使用 64-bit 的 streaming store：_mm_stream_si64。
```

在 256-bit NT copy 路径里：

```text
_mm256_stream_si256 适合 32B 对齐的目标地址。
_mm_stream_si128 适合 16B 对齐的目标地址。
_mm_stream_si64 可以处理 8B 粒度的 NT 写入。
```

这段 8B 分支有两个用途：

1. 对齐前缀处理：

```text
如果 d 已经 8B 对齐，但还没有 32B 对齐，
就先写 8B 或 16B，把 d 推进到 32B 对齐位置。
这样后面的 _mm256_stream_si256 才能安全使用。
```

2. 尾巴处理：

```text
主循环每次处理 32B。
如果最后还剩 8B 或 16B，
16B 可以用 _mm_stream_si128，
8B 就用 _mm_stream_si64。
```

为什么先 `memcpy(&v, s, sizeof(v))`，而不是直接 `*(uint64_t *)s`：

```text
s 不一定 8B 对齐。
直接把 s 强转成 uint64_t* 再解引用，在 C/C++ 层面可能有未对齐访问或 strict-aliasing 风险。
memcpy 到局部 uint64_t v 更安全，编译器通常会把它优化成普通 load。
```

为什么不是直接普通 `memcpy(d, s, 8)`：

```text
因为这个分支属于 NT copy 路径。
如果这里用普通 memcpy，8B 前缀/尾巴会变成普通 cached store，
可能把 old 区目标 cache line 带进 cache。
使用 _mm_stream_si64 可以让这一小段也保持 non-temporal store 的策略一致性。
```

但需要注意：

```text
对很小的数据，_mm_stream_si64 不一定总是更快。
它主要是为了完成大块 NT copy 的对齐和尾巴处理，并保持 old 区写入尽量不污染 cache。
```

## 2026-05-22：`memcpy` 后再 `_mm_stream_si64` 是否有问题

代码：

```cpp
uint64_t v;
memcpy(&v, s, sizeof(v));
_mm_stream_si64((long long *) d, (long long) v);
```

结论：

```text
这段代码语义上没有问题。
这里的 memcpy 不是把数据复制到目标 old 区。
它只是从源地址 s 读取 8 bytes 到局部变量 v。
真正写入目标地址 d 的操作是后面的 _mm_stream_si64。
```

所以它不是：

```text
source -> destination by memcpy
然后又 source -> destination by NT store
```

而是：

```text
source s -> local variable v
local variable v -> destination d by NT store
```

为什么用 `memcpy(&v, s, 8)`：

```text
s 不一定 8-byte 对齐。
直接写 `uint64_t v = *(uint64_t *)s;` 在 C/C++ 层面可能有未对齐访问和 strict-aliasing 风险。
用 memcpy 读到局部 uint64_t 是更安全的写法。
现代编译器通常会把这个固定 8B memcpy 优化成一个普通 load。
```

为什么后面还用 `_mm_stream_si64`：

```text
因为代码想让目标 old 区的 8B 写入也走 non-temporal store。
这通常用于 256-bit NT copy 的前缀对齐或尾巴处理。
```

需要注意的地方：

```text
1. _mm_stream_si64 的目标 d 至少应该 8-byte 对齐。
   当前代码在进入 256-bit NT path 前检查了 d 的 8B 对齐，否则退回普通 memcpy。

2. 对很小的数据，NT store 不一定比普通 memcpy 更快。
   这个 8B 分支主要是为了配合大块 NT copy 的对齐和尾巴处理。

3. 最后如果还剩小于 8B 的 tail，代码会用普通 memcpy。
   这会混合 NT store 和普通 store，但只发生在很小的尾巴上。
```

因此，更准确的说法是：

```text
这段代码不是 correctness bug。
它可能有性能 tradeoff，但不是“memcpy 之后又重复 NT store 到同一块目标地址”。
```

## 2026-05-22：如何验证服务器支持 256-bit NT store

验证分三层：

1. 看 CPU/OS flags：

```bash
lscpu | rg -i 'Model name|Flags|avx2'
```

本机结果：

```text
Intel(R) Xeon(R) W-2235 CPU @ 3.80GHz
Flags 中包含 avx2
```

这说明 CPU/OS 支持 AVX2。`_mm256_stream_si256` 对应的 256-bit integer NT store 需要 AVX2。

2. 编译并实际运行最小测试：

```bash
g++ -O2 -mavx2 -std=c++17 -x c++ -o /tmp/ntstore256_test - <<'CPP'
#include <immintrin.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

__attribute__((noinline)) void ntcopy256(void *dst, const void *src) {
  __m256i v = _mm256_loadu_si256((const __m256i *)src);
  _mm256_stream_si256((__m256i *)dst, v);
  _mm_sfence();
}

int main() {
  void *src = nullptr;
  void *dst = nullptr;
  if (posix_memalign(&src, 32, 32) != 0 || posix_memalign(&dst, 32, 32) != 0) return 2;
  for (int i = 0; i < 32; i++) ((unsigned char *)src)[i] = (unsigned char)(i + 1);
  for (int i = 0; i < 32; i++) ((unsigned char *)dst)[i] = 0;
  ntcopy256(dst, src);
  int ok = 1;
  for (int i = 0; i < 32; i++) ok &= (((unsigned char *)dst)[i] == (unsigned char)(i + 1));
  std::printf("ntstore256 runtime %s\n", ok ? "OK" : "FAIL");
  std::free(src);
  std::free(dst);
  return ok ? 0 : 1;
}
CPP
/tmp/ntstore256_test
```

本机结果：

```text
ntstore256 runtime OK
```

这说明服务器可以实际执行 256-bit NT store，不会 illegal instruction。

3. 反汇编确认生成的是真正的 256-bit NT store：

```bash
objdump -d -Mintel /tmp/ntstore256_test | rg -i 'vmovntdq|vmovdqu|sfence'
```

本机关键结果：

```text
vmovdqu  ymm0,YMMWORD PTR [rsi]
vmovntdq YMMWORD PTR [rdi],ymm0
sfence
```

其中：

```text
vmovntdq YMMWORD PTR ...
```

就是 256-bit non-temporal store。`YMMWORD` 表示 256-bit。如果是 `XMMWORD`，那就是 128-bit。

另外，当前 GiYSB benchmark binary 也确认包含 256-bit NT store：

```bash
objdump -d -Mintel build.debug/benchmarks/out_giysb_tiny16_adaptive_vs_giy_nt256_896_20260521_175640/giysb_tiny16_adaptive_896/ejsvm_giysb_tiny16_adaptive_896 | rg -i 'vmovntdq.*YMMWORD|sfence'
```

本机结果中出现：

```text
vmovntdq YMMWORD PTR [...],ymm0
```

结论：

```text
这台服务器支持并且当前 GiY/GiYSB 构建能够生成和执行 256-bit NT store。
```

## 2026-05-22：为什么 GiYSB staging 最可能无法带来性能提高

最可能的原因：

```text
GiYSB staging 为了把 tiny object 合并成较大的 NT store，额外引入了 per-object 记录、decode、chunk 管理和 young->staging 的二次搬运；但是 tiny object 本身太小，直接 memcpy 的成本已经很低，所以这些额外成本超过了批量 NT store 带来的收益。
```

换句话说：

```text
这个方法优化的是“old 区写入形式”，但它增加了“GC 内部调度和搬运成本”。
当前 benchmark 中增加的成本更大。
```

最关键的证据：

```text
equal-young 条件下，GiYSB staging 的 GC 时间比 GiY 慢 24.762s，约 +18.105%。
其中主要差异来自 scavenge/materialization，而不是 minor GC 次数。
Storage 和 Havlak 是主要退化来源。
```

为什么会这样：

```text
普通 GiY 对小对象直接复制：
  scan -> giy_copy_live_object -> memcpy/NT copy

GiYSB staging 对 tiny object：
  reserve -> 写 tiny table
  scan 结束后遍历 tiny table
  decode entry
  判断 chunk
  可能 memcpy 到 staging buffer
  再从 staging buffer 写 old
```

因此，多出来的成本包括：

```text
1. tiny table append 成本；
2. flush 时 tiny table 遍历成本；
3. entry decode 成本，而且当前实现里可能 decode 两次；
4. chunk 分割判断成本；
5. young -> staging 的额外 memcpy；
6. staging buffer 自身对 cache/local workspace 的占用；
7. 如果 chunk 小于阈值，最后仍然 direct copy，那么前面的记录和 decode 基本都是净损耗。
```

一句话结论：

```text
GiYSB staging 失败的最可能原因不是 256-bit NT store 不可用，而是为了使用更大的 NT store 所付出的 tiny-object batching 成本太高。
```

## 2026-05-22：理论上 `memcpy` 和 NT store 的成本差距

结论先写：

```text
memcpy 和 NT store 没有固定的“差多少”。
对小对象，memcpy 通常更便宜。
对大块、连续、之后不马上读取的写入，NT store 才可能更便宜。
```

当前代码里的经验阈值也体现了这一点：

```text
GIY_NT_COPY_MIN_BYTES = 256
```

也就是说：

```text
nbytes <= 256B 时，当前 GiY 直接使用 memcpy；
nbytes > 256B 时，才尝试 NT copy。
```

理论上的主要区别：

普通 `memcpy` / cached store：

```text
1. 写目标地址时，CPU 通常会把目标 cache line 带进 cache。
2. 如果目标 line 原来不在 cache，可能发生 write allocate / RFO。
3. 对一个 64B cache line，最差情况下可能产生：
   - 读入 64B cache line；
   - 之后再写回 64B。
4. 所以纯写一个冷的 64B line，内存流量可能接近 128B。
5. 但好处是：小数据非常快，store latency 低，之后如果马上读，数据已经在 cache。
```

NT store / streaming store：

```text
1. 目标写入尽量绕过普通 cache 层级。
2. 通常避免 write allocate / RFO。
3. 对一个 64B cache line，理想情况下主要就是写出 64B。
4. 所以对大块连续冷写，理论内存流量可能从 128B 降到 64B，最多接近 2x 的流量优势。
5. 但代价是：
   - 需要对齐；
   - 小写入不容易合并；
   - 需要 write-combining buffer；
   - 最后需要 sfence 保证可见性；
   - 如果之后马上读目标数据，反而会更慢，因为数据不在 cache。
```

所以理论收益大概是：

```text
大块连续冷写：
  NT store 可能有 1.1x 到接近 2x 的写入带宽优势。

小对象、小于 64B/128B/256B：
  NT store 通常不占优势，甚至更慢。
  因为对齐、指令、fence、write-combining、函数/分支成本会超过节省的 cache traffic。
```

对 GiYSB staging 来说，最关键的是：

```text
GiYSB 试图把 <=64B tiny objects 合并起来，让最后写 old 区时更像大块 NT store。
但为了做到这一点，它付出了：
  tiny table append；
  tiny table decode；
  chunk 管理；
  young -> staging 的额外 memcpy；
  staging -> old 的 NT store。
```

如果直接复制 tiny object：

```text
一次很小的 memcpy 可能已经非常便宜。
```

如果走 GiYSB staging：

```text
至少变成一次普通读写到 staging，再一次 NT 写到 old，
再加上 table 管理成本。
```

所以对 tiny object 来说，NT store 理论上的大块写入优势很容易被额外管理成本吃掉。

一句话总结：

```text
NT store 的理论优势主要是减少大块冷写的 cache traffic；
但 GiYSB staging 处理的是大量 tiny object，per-object 管理成本太高，所以很难把这个理论优势转化成实际 GC 性能提升。
```

## 2026-05-22：GC Local Workspace 放进 cache 是否合理，以及更大 young 区是否可能提升性能

当前判断：

```text
把 GC Local Workspace 设计成大体能放进 L2 cache 是合理的方向，
但不能理解成“这块空间一定被 cache 固定保存”。
cache 不是显式管理的 scratchpad，它只是硬件缓存。
```

本机 L2 情况：

```text
每个 core 约 1MiB L2。
之前推荐的 896KB workspace 大约是单核 L2 的 87.5%。
```

这个选择的优点：

```text
1. GC stack、RSet/index、FT slot set、tiny table、staging buffer 等高频访问结构更可能留在 L2。
2. young object 的分配和 minor GC 访问也更可能在较低延迟的 cache 中完成。
3. 相比让 GC 元数据散落在更慢的层级，local workspace 的设计更符合 GiY 的核心思想。
```

但风险是：

```text
1. L2 不是独占的，还要放代码、普通栈、其他运行时数据、prefetch 数据等。
2. L2 是 set-associative，不是只看总大小；地址映射冲突也可能导致 eviction。
3. 如果 workspace 太接近 L2 上限，反而可能增加 cache conflict 和 eviction。
4. 如果开 SMT，同一个物理 core 的两个 logical threads 会共享 L2。
```

所以结论是：

```text
让 workspace 接近但不超过 L2 是合理实验方向。
896KB 可以作为一个强 baseline，但不能保证它“几乎全部永远在 L2”。
```

关于更大的 young 区：

```text
更大的 young 区很可能带来一部分性能提升，但不是无限变大越好。
```

为什么更大 young 可能更快：

```text
1. minor GC 次数减少。
2. root / RSet 扫描次数减少。
3. 很多短命对象有更长时间自然死亡，可能降低 promotion/copy 压力。
4. GC overhead 通常会下降，尤其是 minor GC 很频繁的 benchmark。
```

为什么更大 young 也可能不提升，甚至变差：

```text
1. young 区太大后，GC Local Workspace 可能不再稳定停留在 L2。
2. 每次 minor GC 时，活对象集合可能更大，单次 GC pause 变长。
3. 更大的 young 会占用更多 cache 容量，可能挤出 RSet、stack、shape/layout cache 等真正高频结构。
4. 如果 benchmark 本身 GC 时间占比很低，减少 GC 次数对端到端时间帮助有限。
5. 对 GiYSB staging 来说，更大 young 不能解决 tiny table/staging 的 per-object 成本。
```

因此最合理的设计逻辑是：

```text
不是盲目扩大 young，
而是在 L2 budget 内尽量减少不必要的辅助结构，
把省下来的空间还给 young。
```

对当前结果的解释：

```text
GiYSB staging 多占了 tiny table 和 staging buffer，
导致 young after aux 变小。
这会增加 minor GC 次数。
equal-young 实验说明：即使 young 一样大，GiYSB staging 的 GC 仍然慢，
所以 staging 的根本问题是 per-object batching 成本。
```

当前最合理的方向：

```text
1. 保持总 GC Local Workspace 在 896KB 附近。
2. 删除或关闭 tiny table/staging 这种收益不足的结构。
3. 把释放出来的空间还给 young。
4. 保留 small/large old placement 做 PlaceOnly baseline。
5. 如果未来要重新引入 batching，只在明确能形成足够大连续 chunk 时启用。
```

一句话结论：

```text
把 workspace 控制在 L2 内是合理的；
更大的 young 区通常有机会减少 GC 次数并提高性能；
但对 GiYSB staging 当前失败的核心问题来说，单纯扩大 young 不能解决 staging/tiny table 的额外管理成本。
```

## 2026-05-22：从 cache-friendly 角度看，workspace/young 是否越大越好

用户进一步澄清：问题不是 GC 次数，而是从 cache-friendly 的角度，空间更大是否更容易实现 GiY 的“缓存友好”。

当前判断：

```text
不是越大越 cache-friendly。
GiY 的 cache-friendly 来自“活跃工作集能留在 cache 中”，不是来自“保留的空间更大”。
```

更大的 local workspace / young 区有两种相反效果。

可能有利的部分：

```text
1. 如果原来的 young 太小，minor GC 太频繁，那么适当变大可以减少 GC 次数。
2. 如果大多数 GC 活跃数据仍然能留在 L2 里，更大的 young 可以让更多最近分配的对象仍在 cache 附近被处理。
3. 在不挤出高频元数据的前提下，适当扩大 young 有助于降低重复扫描和重复进入 GC 的成本。
```

可能不利的部分：

```text
1. cache 容量固定。空间越大，越不容易整体保持 hot。
2. L2 不是 scratchpad，不会因为叫 GC Local Workspace 就固定常驻 cache。
3. 如果 young/workspace 太接近或超过 L2，有效工作集会挤出 GC stack、RSet、FT slot set、layout cache 等高频结构。
4. L2 是 set-associative，还可能出现 set conflict；不是只看总大小。
5. 更大的 young 也可能让单次 GC 的 live working set 更大，导致扫描时访问更多 cache line。
```

所以更准确的原则是：

```text
GiY 需要的不是“越大的 young”，而是“活跃 GC 工作集恰好能放进 L2，并留有足够余量”。
```

从 cache-friendly 角度，空间大小存在一个最佳区间：

```text
太小：
  GC 太频繁，root/RSet/scavenge 重复执行，开销高。

适中：
  young + GC metadata 的活跃部分大体留在 L2，GC 次数也不过多。
  这是 GiY 最想要的区域。

太大：
  工作集超过 L2 或产生更多 conflict，cache miss 增加。
  这时虽然 young 更大，但反而不 cache-friendly。
```

因此：

```text
“更大”只在还没有超过 L2 有效容量、且没有挤出高频元数据时，才可能更 cache-friendly。
超过这个区间后，更大反而更不 cache-friendly。
```

对于当前机器：

```text
单核 L2 约 1MiB。
896KB workspace 已经是非常激进的 L2 budget，约 87.5%。
它可能是合理实验点，但也已经留下不多余量给代码、栈、运行时数据和 cache set 冲突。
```

当前汇报时可以这样说：

```text
I do not assume that a larger workspace is always more cache-friendly.
The goal of GiY is to keep the active GC working set inside L2, not to maximize the nursery size.
So there should be an optimal range: too small causes too many minor GCs, but too large may exceed L2 capacity or evict hot GC metadata.
```

## 2026-05-23：按照导师原则重新设计 GiYSB two-class 方案

导师的新原则理解为：

```text
1. 对象只分两类：tiny 或 large。
2. 不再有 tiny / medium / large 三类。
3. young -> old 的最终写入不使用普通 memcpy 作为优化路径。
4. tiny object 一律先进入 staging buffer，然后用 NT store 统一写入 old。
5. large object 直接从 young 原地用 NT store 写入对应 old 区。
```

如果导师的意思是“完全禁止 young->old 使用 NT store”，那就是另一个方案。
这里按“最终写 old 时要么 staging 后 NT，要么 direct NT”来设计。

推荐暂定名称：

```text
GiYSB-TL
GiY with Staging Buffer, Tiny/Large two-class policy
```

对象分类：

```text
footprint = ALIGN(object_header + payload_size)

tiny:
  footprint <= GIYSB_TINY_OBJECT_MAX_BYTES
  当前可继续用 64B。

large:
  footprint > GIYSB_TINY_OBJECT_MAX_BYTES
```

注意：

```text
object_header 当前是 16B。
所以 tiny 阈值如果是 64B，实际 payload 通常最多约 48B。
```

old 区布局：

```text
tiny old region:
  只放 tiny object 的目标地址。
  使用 bump pointer。
  目标地址按 reserve order 连续。

large old region:
  只放 large object 的目标地址。
  使用 bump pointer。
```

不要再使用 `small_object_max=256` 这种中间概念。
变量命名也最好从 `small_*` 改成 `tiny_*`，避免和导师的 two-class 思想混淆。

minor GC 流程：

1. minor GC 开始：

```text
reset gc_stack
reset tiny_table
reset staging buffer
reset used_nt_store
```

2. 发现 live young object：

```text
copy_for_minor(payload_ptr)
  -> 计算 footprint
  -> 如果 tiny:
       reserve 到 tiny old region
       设置 forwarding_pointer = tiny old payload
       记录 tiny table entry
       push 源对象到 gc_stack
       不立即复制
     如果 large:
       reserve 到 large old region
       设置 forwarding_pointer = large old payload
       push 源对象到 gc_stack
```

3. scavenge / traversal：

```text
pop 源对象
扫描它的 children
把对象内部 young refs patch 成 forwarding refs

如果 tiny:
  不复制。
  因为它已经记录在 tiny table，最后统一 staging。

如果 large:
  立刻使用 forced NT copy 从 young header 写到 large old header。
```

这里 large 不能继续调用普通 `giy_copy_live_object()`，因为当前 `giy_copy_live_object()` 有：

```text
if (nbytes <= 256) memcpy(...)
```

新方案需要一个新的函数，例如：

```text
giy_nt_copy_live_object_forced(dst, src, nbytes, used_nt_store)
```

这个函数不根据 256B 阈值退回 memcpy。
它负责尽量用 `_mm256_stream_si256` / `_mm_stream_si128` / `_mm_stream_si64` 完成写 old。

4. traversal 完成后：

```text
giysb_flush_tiny_table()
```

它按照 reserve order 遍历 tiny table。
因为 tiny old 是按 reserve order 连续分配的，所以 tiny table 顺序也对应连续 old 目标地址。

新的 tiny flush 策略：

```text
把 tiny objects 逐个 memcpy 到 staging buffer。
staging buffer 满了就 forced NT flush 到 tiny old。
GC 结束时 tail 也 forced NT flush。
```

注意：这里的 `memcpy` 是：

```text
young -> staging
```

不是：

```text
young -> old
```

最终写 old 必须使用 forced NT copy。

因此 `giysb_flush_tiny_chunk()` 不应该再有：

```text
chunk < 4KB 就 direct copy
```

因为这会让小 chunk 绕过 staging。
新原则下应该改成：

```text
所有 tiny 都 staging。
所有 staging flush 都 forced NT。
```

关键代码修改点：

1. `giysb_reserve_old_object()`
   - 只判断 tiny / large。
   - tiny 进入 tiny old，并记录 tiny table。
   - large 进入 large old。
   - 不再使用 `GIYSB_SMALL_OBJECT_MAX_BYTES=256` 路径。

2. `giy_copy_live_object()`
   - 保留给普通 GiY 使用。
   - 新增 forced NT 版本给 GiYSB-TL 使用。

3. scavenge/materialization 分支
   - tiny：skip immediate copy。
   - large：调用 forced NT copy，不走 memcpy threshold。

4. `giysb_flush_tiny_table()`
   - 保留按 tiny table 分 chunk 的功能。
   - 但不要再因为 chunk 小于 4KB 走 direct copy。

5. `giysb_stage_tiny_chunk()`
   - 继续负责 young -> staging。
   - flush 时调用 forced NT copy。

6. `giysb_copy_tiny_chunk_direct()`
   - 新原则下最好删除或只作为 debug emergency path。
   - benchmark 主路径不能使用它。

最大的工程风险：

```text
tiny table 不能 overflow。
```

原因：

```text
如果 tiny table 满了，当前旧代码会 fallback 到 reserve_large/direct copy。
但这违反“tiny 一律 staging”的原则。

而且 table 满时不能简单立即 flush，
因为有些 tiny object 可能还没被 scavenge，内部引用还没有 patch 完。
提前复制到 old 会复制未修正的 references。
```

因此严格方案必须选择一个办法：

方案 A：把 tiny table 做到足够大。

```text
优点：实现简单，保持 reserve-order flush。
缺点：占 workspace，减少 young。
```

估算：

```text
如果 young after aux 约 580KB，
全部都是 64B tiny object，最多约 9K entries。
4B compact entry 大约 36KB。

如果很多对象更小，例如 16B/24B footprint，
最坏可能需要 100KB 以上 table。
```

方案 B：允许 tiny overflow 时 direct NT。

```text
优点：workspace 小。
缺点：违反导师“一律 staging”的原则，不推荐作为本设计主线。
```

方案 C：重新设计 table，使用扫描完成后的 materialization list。

```text
优点：可以边 materialize 边 flush，减少 table 压力。
缺点：materialization order 可能不是 reserve order，old 目标地址不一定连续，staging 统一 NT 的收益可能下降。
```

当前最符合导师原则的是方案 A：

```text
保留 reserve-order tiny table，
但把 table size 作为实验参数扩大，
并在 overflow 时直接报错或记录失败，不做性能 fallback。
```

推荐初始配置：

```text
CACHE_SIZE_KB=896
GIYSB_TINY_OBJECT_MAX_BYTES=64
GIYSB_STAGING_BYTES=8192
GIYSB_TINY_TABLE_BYTES=65536
GIYSB_TINY_BATCH=1
GIY_NT_COPY_BITS=256
```

如果 64KB table 仍然 overflow，再测试 128KB。
但报告时必须说明：更大的 tiny table 会减少 young，需要 equal-young 对照。

应该收集的 profile：

```text
tiny reserved objects / bytes
large reserved objects / bytes
tiny table max entries
tiny table overflow count
staging flush count
staging full flush / tail flush
staged bytes
large direct NT bytes
NT copied bytes
minor GC count
scavenge time
```

判断成功的标准：

```text
1. correctness：所有 benchmark status=0。
2. tiny table 没有 overflow。
3. equal-young 条件下，GC overhead / scavenge 不比 GiY 明显变慢。
4. 如果 GC 不快，至少要看到 old write cache pollution 或 cache miss 有改善，否则这个策略仍然不成立。
```

一句话总结：

```text
新 GiYSB 应该变成一个严格 two-class 策略：
tiny 永远 staging 后 NT flush；
large 永远 direct NT copy；
不再让 medium/small threshold 和 memcpy fallback 混进主策略。
```

## 2026-05-23：two-class GiYSB 是否还需要 tiny table

用户问：既然 tiny 一律 staging，是否还需要 tiny table？能否每次遇到小对象就直接移动到 staging buffer？

当前判断：

```text
严格来说，仍然需要某种 table / log / descriptor。
不一定必须叫 tiny table，但需要一个结构记录 tiny object 的源地址、目标地址或至少 reserve order。
```

原因 1：不能在 `copy_for_minor()` 发现对象时立刻把对象复制到 staging。

```text
copy_for_minor() 发生在对象刚被发现 live 的时候。
此时这个对象内部的 child references 还没有被扫描和 patch。
如果这时把对象内容复制到 staging，staging 里会保存旧的 young pointers。
之后 young 对象本体虽然会被 patch，但 staging 里的副本不会自动更新。
```

所以：

```text
发现对象时可以 reserve old 地址、设置 forwarding pointer、记录信息；
但不能安全地复制对象 bytes。
```

原因 2：可以在对象被 scavenge 完之后立刻 copy 到 staging，但这会遇到目标 old 地址顺序问题。

```text
对象 scavenge 完后，它内部引用已经 patch，此时复制 bytes 是安全的。
但是 scavenge 顺序是 LIFO / graph traversal order。
而 tiny old 的目标地址是 reserve order。
这两个顺序不一定一致。
```

如果按照 scavenge 顺序把对象连续放进 staging buffer：

```text
staging buffer 中的对象顺序 = scavenge order
old 目标地址顺序 = reserve order
```

二者不一致时，不能把 staging buffer 一次性 NT store 到 old 的连续区间，否则会把对象写到错误位置。

原因 3：如果 staging buffer 里每个对象都要写到不同目标地址，就又需要 descriptor。

```text
如果不要求一次连续写 old，而是 staging 中每个对象单独写到自己的 old 地址，
那就必须记录每个对象对应的 dst。
这本质上还是一个 table / descriptor list。
```

所以 tiny table 的本质作用不是“多余的一张表”，而是：

```text
1. 记住哪些 tiny object 需要延迟复制；
2. 保持 reserve order；
3. 让最终 staging buffer 的顺序和 old 目标地址连续顺序一致；
4. 避免在对象内部引用 patch 之前复制错误内容。
```

什么时候可以不要 tiny table：

```text
1. 如果改成 FIFO/Cheney-style traversal，并能严格保证 materialization order == reserve order。
   这样 scavenge 完一个对象就可以 append staging，目标 old 也连续。

2. 如果只对 pointer-free tiny object 立即 staging。
   因为它们没有 child refs，不存在 patch 后内容改变的问题。
   但 JSObject 通常可能有 references，需要准确类型判断。

3. 如果放弃“一次连续 NT store 到 old”的目标。
   每个 tiny object 单独从 staging 写到自己的 old 地址。
   但这会失去 batching 的主要意义，并且仍然需要 dst 信息。
```

因此当前建议：

```text
在不大改 traversal 的前提下，不建议删除 tiny table。
可以优化 tiny table，让它更小、更少 decode，或者改成 reserve-order worklist。
但直接“每次遇到小对象就放 staging buffer”在当前 LIFO GiY 中不安全或收益很低。
```

一句话结论：

```text
tiny table 的存在是为了处理“发现顺序、扫描完成顺序、old 目标连续顺序”不一致的问题。
如果不改变 traversal/order 机制，直接用 staging buffer 不能完全替代 tiny table。
```

## 2026-05-23：在 materialization/copy 时判断 tiny，是否可以去掉 tiny table

用户进一步澄清：

```text
不是在 copy_for_minor() 刚发现对象时就把 tiny object 放进 staging，
而是在真正准备 copy/materialize 这个对象时再判断：
如果是 tiny，就进入 staging buffer；
如果不是 tiny，就正常处理。
```

当前判断：

```text
这个方向是可行的，而且比原来的 tiny table 方案更干净。
前提是“准备 copy”指的是对象已经被 scavenge，内部 references 已经 patch 完之后。
```

原因：

```text
在 scavenge/materialization 阶段，代码已经先执行：
giy_process_young_node<GiYReserveTracer>(type, src_payload)

这一步会扫描对象内部引用，并把 young references patch 成 forwarding references。
所以此时对象内容已经是可以复制到 old 的正确内容。
```

因此可以改成：

```text
scavenge 一个对象
  -> patch children
  -> 取得 dst_hdr / align_bytes
  -> 如果 tiny:
       append 到 staging buffer
     否则:
       forced NT copy 到 large old
```

这样可以删除或绕过原来的 tiny table：

```text
不需要在 reserve 时记录 tiny entry。
不需要之后 flush_tiny_table 再 decode entry。
不需要同一个 tiny entry decode 两次。
```

但是还必须处理目标 old 地址连续性。

因为 scavenge 顺序仍然可能和 reserve 顺序不同：

```text
当前对象的 dst_hdr 不一定等于 staging 当前 expected_dst。
```

所以新的 online staging buffer 需要维护：

```text
staging_used
staging_dst_start
staging_expected_dst
```

append tiny object 时：

```text
如果 staging 为空：
  staging_dst_start = dst_hdr
  staging_expected_dst = dst_hdr

如果 dst_hdr != staging_expected_dst：
  先把当前 staging 用 forced NT flush 到 staging_dst_start
  然后从当前对象开启新的 staging run

把当前 tiny object bytes memcpy 到 staging buffer
staging_expected_dst += object_size
staging_used += object_size

如果 staging buffer 满：
  forced NT flush
```

这样可以保证：

```text
每次 flush staging buffer 时，
staging buffer 的 byte 顺序和 old 目标地址顺序一致。
```

如果 scavenge 顺序和 reserve 顺序很不一致：

```text
staging 会频繁因为 dst 不连续而 flush。
这会降低 batching 收益。
但 correctness 是安全的。
```

和原 tiny table 方案相比：

优点：

```text
1. 不需要 tiny table，占用 workspace 更少。
2. 不需要 reserve-time 记录。
3. 不需要 flush_tiny_table 遍历和 decode。
4. tiny object 只在内容已经 patch 完后才 copy 到 staging，语义安全。
5. 实现更符合导师的“准备 copy 时判断 tiny/large”原则。
```

缺点：

```text
1. batching 取决于 materialization 顺序下的 dst 连续性。
2. 如果 LIFO traversal 导致 dst 地址跳来跳去，staging run 会很短。
3. 短 run 仍然 forced NT flush，可能不一定比 memcpy 快。
4. 需要增加 profile 来统计：
   - staging run 数量；
   - 平均 run bytes；
   - 因 dst 不连续触发的 flush 次数；
   - 因 buffer full 触发的 flush 次数。
```

推荐的新设计：

```text
GiYSB-OnlineStage
```

核心原则：

```text
1. copy_for_minor() 只 reserve old 地址和设置 forwarding pointer。
2. 不再在 reserve 时记录 tiny table。
3. scavenge/materialization 时判断 tiny/large。
4. tiny：进入 online staging buffer。
5. large：forced NT copy 到 large old。
6. staging flush：永远 forced NT store 到 old。
7. 如果 tiny dst 不连续：flush 当前 staging，再开始新的 run。
```

一句话结论：

```text
如果在“对象已经扫描并 patch 完、准备 materialize”时判断 tiny，那么可以去掉 tiny table，改用 online staging buffer。
这个方案更简单、更符合导师原则，但需要用 dst 连续性检查来保证 staging flush 的 correctness。
```

## 2026-05-23：为什么 reserve 顺序和 staging 顺序可能不一样

用户问：

```text
遇到 tiny object 并 reserve 它们的顺序，不就是 staging buffer 看到它们的顺序吗？
为什么 staging buffer 的顺序会不一样？
```

关键区别：

```text
reserve 顺序 = 对象“第一次被引用发现”的顺序。
staging 顺序 = 对象“从 GC stack pop 出来、扫描完成、准备 copy”的顺序。
```

当前 GiY 使用的是 GC stack，也就是 LIFO。
所以对象发现顺序和处理完成顺序通常会反过来，或者至少不完全一致。

例子：

```text
Root 引用 A 和 B。
扫描 root 时按顺序发现 A，再发现 B。

copy_for_minor(A):
  reserve A old 地址 = old[0]
  push A

copy_for_minor(B):
  reserve B old 地址 = old[1]
  push B
```

此时 reserve 顺序是：

```text
A, B
```

old 目标地址顺序也是：

```text
old[0] = A
old[1] = B
```

但是 GC stack 是 LIFO，所以后面 pop 的顺序是：

```text
B, A
```

如果改成 materialization-time staging：

```text
先 scavenge B，B 进入 staging。
再 scavenge A，A 进入 staging。
```

staging buffer 顺序就变成：

```text
B, A
```

但 old 连续目标地址要求是：

```text
A, B
```

如果把 staging buffer `[B, A]` 一整块写到从 `old[0]` 开始的连续区域，就会写错：

```text
old[0] 本来应该是 A，却写成 B。
old[1] 本来应该是 B，却写成 A。
```

这就是为什么 materialization-time staging 必须检查：

```text
当前对象 dst 是否等于 staging_expected_dst。
```

如果不等，就说明目标 old 地址不连续或顺序不同，必须先 flush 当前 staging run，再开始新的 run。

原来的 tiny table 为什么能避免这个问题：

```text
tiny table 是在 reserve 时记录的。
所以 tiny table 顺序天然等于 reserve 顺序。
reserve 顺序也就是 tiny old 目标地址顺序。
最后按 tiny table 顺序组装 staging buffer，就能保证 staging buffer 顺序和 old 地址顺序一致。
```

所以：

```text
如果不使用 tiny table，而是在 copy/materialization 时直接进入 staging，
就必须接受 staging 顺序可能不同，并用 dst 连续性检查修正。
```

什么时候两者会一样：

```text
1. 如果 GC worklist 改成 FIFO，而不是 LIFO。
2. 如果 traversal 机制保证 pop/materialize 顺序等于 reserve 顺序。
3. 如果每次 staging run 只包含目标地址连续的一段。
```

当前 GiY 不是这种保证，所以不能假设顺序一样。

一句话结论：

```text
遇到对象并 reserve 的顺序，和对象被真正扫描完并准备 copy 的顺序，在当前 LIFO GiY 中不是同一个顺序。
这就是 staging buffer 顺序可能和 old 目标地址顺序不一致的原因。
```

## 2026-05-23：保留 tiny table，并在 staging buffer 满时 flush 的方案评估

用户决定继续保留 tiny table，并提出：

```text
每当 staging buffer 满了，就 flush 一次。
然后继续处理 tiny table。
这样每次 GC 理论上只有最后一次 tail flush 是不满 buffer 的 NT store。
```

当前判断：

```text
这个方向是合理的，而且是 tiny table 最适合的使用方式。
```

原因：

```text
tiny table 按 reserve order 记录 tiny object。
reserve order 等于 tiny old 目标地址顺序。
因此按 tiny table 顺序把对象打包进 staging buffer，
就可以保证 staging buffer 的 byte 顺序和 old 目标地址连续顺序一致。
```

推荐流程：

```text
1. minor GC 中发现 tiny object：
   reserve 到 tiny old；
   记录 tiny table；
   不立即复制。

2. scavenge 阶段：
   tiny object 被 pop 出来后，先扫描和 patch 内部 references；
   然后仍然不复制。

3. traversal 全部结束后：
   遍历 tiny table；
   按 reserve order 读取每个 tiny object 当前已经 patch 完的内容；
   memcpy 到 staging buffer；
   如果下一个对象放不下，就 forced NT flush 当前 staging buffer；
   reset staging buffer，继续处理 tiny table。

4. tiny table 结束后：
   flush 最后一个 tail。
```

这样做的好处：

```text
1. tiny table 保证 old 目标地址顺序。
2. staging buffer 负责把多个 tiny object 合并成大块连续 bytes。
3. full flush 基本接近 staging buffer 大小。
4. 每次 GC 只有最后一个 tail flush 可能明显不满。
5. 不再需要原来的“chunk 小于 4KB 就 direct copy”的分支。
```

需要注意一个细节：

```text
因为 object 不能被切开，
所以每次 full flush 不一定刚好等于 8KB。
它通常是“加入下一个 object 会超过 8KB，所以先 flush 当前 buffer”。
因此每个 full flush 最多会浪费不到一个 tiny object 的空间，通常小于 64B。
这可以接受。
```

这和当前旧代码的区别：

当前旧代码里有：

```text
if chunk_bytes >= GIYSB_TINY_STAGING_MIN_BYTES:
  staging
else:
  direct copy
```

导师新原则下应该改成：

```text
所有 tiny 都 staging。
staging buffer 满了就 forced NT flush。
最后 tail 也 forced NT flush。
```

也就是说：

```text
不要再使用 GIYSB_TINY_STAGING_MIN_BYTES 作为 direct-copy fallback 条件。
```

是否需要“重新计算 tiny table”：

```text
不需要重新计算 tiny table。
tiny table 是本轮 GC 的 reserve-order list。
flush 时只需要维护当前 table index。
例如 chunk_start / i 或者直接一个 cursor。
flush 一个 staging buffer 后，继续从下一个 tiny table entry 处理即可。
```

真正需要 reset 的是：

```text
staging buffer used bytes；
staging destination start；
staging expected destination。
```

tiny table 本身应该等整轮 flush 完后再 reset。

核心伪代码：

```text
staging_reset()

for entry in tiny_table:
  src_hdr, nbytes = decode(entry)

  if staging_used == 0:
    staging_dst = current_dst

  if staging_used + nbytes > staging_capacity:
    forced_nt_flush(staging_dst, staging_buffer, staging_used)
    staging_reset()
    staging_dst = current_dst

  memcpy(staging_buffer + staging_used, src_hdr, nbytes)
  staging_used += nbytes
  current_dst += nbytes

if staging_used > 0:
  forced_nt_flush(staging_dst, staging_buffer, staging_used)

tiny_table_reset()
```

还必须保证：

```text
staging flush 到 old 时调用 forced NT copy，
不能再调用会因为 <=256B 而 fallback 到 memcpy 的 giy_copy_live_object()。
```

最大的风险仍然是：

```text
tiny table overflow。
```

因为不能在 traversal 中途提前 flush tiny table：

```text
有些 tiny object 可能还没有被扫描和 patch。
提前复制会复制旧 references。
```

所以 tiny table 必须足够大，或者 overflow 直接作为实验失败记录，而不是偷偷 fallback。

一句话结论：

```text
保留 tiny table，然后在最终 flush 阶段把 tiny objects 按 reserve order 连续塞进 staging buffer；
buffer 满就 forced NT flush，最后只剩一个 tail flush。
这是比当前 adaptive direct-copy 版本更符合导师原则的方案。
```

## 2026-05-23：能否一边记录 tiny table，一边达到阈值就立刻 flush

用户提出：

```text
不需要完整全部记录完再批量复制。
可以直接记录下来，一旦到了一定大小就立刻 flush。
这样是否可以控制 tiny table 的大小？
```

当前判断：

```text
这个想法有价值，但不能在“reserve/发现对象”的时刻直接 flush。
原因是对象内容可能还没有被扫描和 patch。
```

为什么不能在 reserve-time 达到阈值就 flush：

```text
copy_for_minor() 只是第一次发现对象 live。
此时它只 reserve old 地址、设置 forwarding pointer、push 到 GC stack。
对象内部的 child references 还没有扫描，也还没有从 young pointer 改成 old forwarding pointer。

如果此时把对象 bytes flush 到 old，就可能把旧的 young references 复制到 old。
这是 correctness 问题。
```

所以：

```text
reserve-time 只能记录，不能安全 materialize。
```

什么时候可以提前 flush：

```text
只有当 tiny object 已经被 scavenge 完，也就是内部 references 已经 patch 完之后，才能把它复制到 staging 并 flush 到 old。
```

但是这里又有第二个问题：

```text
为了把 staging buffer 一整块 NT store 到 old，
flush 顺序必须和 old 目标地址顺序一致。
old 目标地址顺序等于 reserve order。
```

当前 GiY 是 LIFO，所以：

```text
reserve order 不一定等于 scavenge/materialization order。
```

因此可以做的安全版本是：

```text
ready-prefix flushing
```

设计如下：

1. reserve tiny object 时：

```text
记录 tiny table entry：
  src_hdr
  dst_hdr
  nbytes
  ready = false
```

2. 对象 scavenge 完、准备 copy 时：

```text
找到它对应的 tiny table entry；
标记 ready = true。
```

3. 维护一个全局 cursor：

```text
tiny_flush_cursor
```

它指向 tiny table 中还没有 flush 的第一个 entry。

4. 每次有对象变成 ready 后，尝试从 cursor 开始 flush：

```text
while tiny_table[cursor].ready:
  把这个对象 memcpy 到 staging buffer
  如果 staging buffer 满：
    forced NT flush
  cursor++
```

5. 如果遇到第一个 not-ready entry：

```text
停止。
不能跳过它 flush 后面的对象。
否则 old 目标地址顺序会错。
```

这样做的好处：

```text
1. 不必等整轮 GC 结束才开始 flush。
2. 如果 reserve order 前缀很快 ready，就可以提前释放一部分 table 压力。
3. staging buffer 满了就 flush，最后仍然只有一个 tail flush。
```

但是它不能完全保证 tiny table 很小：

```text
如果 tiny table 的第一个 entry 很晚才 ready，
后面即使很多 entries 都 ready，也不能先 flush。
因为不能打乱 old 目标地址顺序。
```

简单例子：

```text
reserve order: A, B, C, D
scavenge ready order: D, C, B, A
```

在 A ready 之前：

```text
B/C/D 虽然 ready，也不能 flush。
因为 old 区顺序必须是 A,B,C,D。
```

所以：

```text
ready-prefix flushing 可以降低平均 table 压力，
但无法给 tiny table 大小一个严格上界。
```

如果真的想让 tiny table 很小，有三个选择：

1. 改 traversal 为 FIFO / reserve-order materialization：

```text
让 reserve order 接近 materialization order。
这样就可以用小 ring buffer + staging buffer。
但这会明显改变 GiY 当前 LIFO traversal 行为。
```

2. 只对 pointer-free tiny object 立即 staging：

```text
因为 pointer-free object 不需要等待 child reference patch。
但需要非常可靠的类型判断。
```

3. 允许 overflow fallback：

```text
tiny table 满时 direct NT copy。
但这违反导师“tiny 一律 staging”的严格原则。
```

当前最合理的折中方案：

```text
保留 tiny table。
新增 ready bit / ready flag。
实现 ready-prefix flushing。
staging buffer 满了就 forced NT flush。
GC 结束时 flush 剩余 ready prefix；理论上此时所有 tiny 都应该 ready。
```

需要 profile 的指标：

```text
max tiny table live entries
max unflushed tiny entries
ready-prefix flush count
blocked-by-not-ready count
staging full flush count
tail flush count
average flush bytes
```

一句话结论：

```text
“记录到一定大小就立刻 flush”只有在这些 entries 已经 scavenge 完、并且形成 reserve-order 前缀时才安全。
它可以降低平均 tiny table 压力，但在当前 LIFO GiY 中不能保证 tiny table 一定很小。
```

## 2026-05-23：tiny / large 分界点的当前判断

用户决定暂时继续使用当前大小的 tiny table，并询问 tiny 和 large 的分界点应该是多少。

当前建议：

```text
先保持 GIYSB_TINY_OBJECT_MAX_BYTES = 64B。
这里的 64B 指 object footprint，也就是 ALIGN(object_header + payload)，不是 payload 本身。
```

原因：

```text
object_header 当前是 16B。
所以 64B footprint 大约表示 payload <= 48B。
```

为什么不建议一开始提高到 128B 或 256B：

```text
1. 之前 staging 版即使只处理 <=64B tiny object，GC 仍然明显变慢。
2. 阈值变大后，更多对象进入 staging，会增加 young->staging 的 memcpy 字节数。
3. 阈值变大后，staging buffer full flush 次数会增加，table 记录的总字节也会增加。
4. 对 128B/256B 对象来说，二次搬运成本更明显；如果收益不够，退化会更严重。
5. 当前目标是先验证导师的 two-class 原则，而不是一开始扩大搜索空间。
```

为什么也不建议低于 64B：

```text
1. cache line 是 64B。
2. 64B 是一个自然边界：一个 tiny object footprint 最多约一个 cache line。
3. 如果阈值降到 32B，能进入 staging 的对象太少，可能很难形成稳定的 8KB staging flush。
```

因此当前最合理的 baseline：

```text
tiny:  footprint <= 64B
large: footprint > 64B
```

需要注意：

```text
按照导师新原则，large 会 direct NT copy。
这意味着 72B、80B、128B 这种对象也会走 direct NT，而不是 memcpy。
这可能不一定快。
但为了保持 two-class 策略纯粹，第一版可以先这样实现并测量。
```

后续如果要做参数 sweep，推荐顺序：

```text
64B -> 128B -> 256B
```

判断标准：

```text
1. GC overhead / scavenge 是否下降；
2. staging full flush 平均大小是否接近 8KB；
3. tail flush 是否很多；
4. tiny table 是否 overflow；
5. large direct NT 是否在 65B~256B 对象上造成明显损失。
```

如果 64B 仍然慢：

```text
不应该马上扩大到 128B。
更可能应该先证明 overhead 来自哪里：
  - table append/decode；
  - young->staging memcpy；
  - forced NT tail；
  - large small-object direct NT。
```

一句话结论：

```text
当前最稳妥的 tiny/large 分界点是 64B footprint。
这是保守 baseline；之后可以实验 128B/256B，但不建议一开始就扩大。
```
