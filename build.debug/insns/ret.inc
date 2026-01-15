
DEFLABEL(HEAD):
INSN_COUNT0(ret);Lret0:
TLret:
{ // vmgen.newsynth.DecisionDiagram$Leaf@550

  JSValue *stack;

  if (fp == border) return 1;
  stack = &get_stack(context, 0);
  restore_special_registers(context, stack, fp - 4);
  set_sp(context, fp - 5);
  update_context();
}

