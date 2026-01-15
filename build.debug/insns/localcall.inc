#define d d0

DEFLABEL(HEAD):
INSN_COUNT0(localcall);Llocalcall0:
TLlocalcall:
{ // vmgen.newsynth.DecisionDiagram$Leaf@550

/*
   localcall disp
 */
  lcall_stack_push(context, pc);
  set_pc_relative(d);
}
#undef d

