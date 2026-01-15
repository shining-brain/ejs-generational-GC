#define newfl i0

DEFLABEL(HEAD):
INSN_COUNT0(setfl);Lsetfl0:
TLsetfl:
{ // vmgen.newsynth.DecisionDiagram$Leaf@550


  int oldfl = get_sp(context) - fp + 1;

  if (fp + newfl > regstack_limit) {
    save_context();
    LOG_EXIT2(context, "register stack overflow\n");
  }
  set_sp(context, fp + newfl - 1);
  while (++oldfl <= newfl) {
    regbase[oldfl] = JS_UNDEFINED;
  }
}
#undef newfl

