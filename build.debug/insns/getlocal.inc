#define dst regbase[r0]
#define link i1
#define index s2

DEFLABEL(HEAD):
INSN_COUNT0(getlocal);Lgetlocal0:
TLgetlocal:
{ // vmgen.newsynth.DecisionDiagram$Leaf@550

/*
   getlocal dst link index
   $dst = value of the index-th local variable in the link-th function frame
 */

  FunctionFrame *fr;
  int i;
  fr = get_lp(context);
  for (i = 0; i < link; i++) fr = fframe_prev(fr);
  dst = fframe_locals_idx(fr, index);
}
#undef dst
#undef link
#undef index

