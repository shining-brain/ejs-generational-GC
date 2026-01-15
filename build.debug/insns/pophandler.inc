
DEFLABEL(HEAD):
INSN_COUNT0(pophandler);Lpophandler0:
TLpophandler:
{ // vmgen.newsynth.DecisionDiagram$Leaf@550

  UnwindProtect *p;
  p = context->exhandler_stack_top;
  if (p != NULL) {
    context->exhandler_stack_top = p->prev;
    p->prev = context->exhandler_pool;
    context->exhandler_pool = p;
  }
}

