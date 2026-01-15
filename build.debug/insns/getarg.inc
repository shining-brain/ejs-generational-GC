#define dst regbase[r0]
#define link i1
#define index s2

DEFLABEL(HEAD):
INSN_COUNT0(getarg);Lgetarg0:
TLgetarg:
{ // vmgen.newsynth.DecisionDiagram$Leaf@550

/*
   getarg dst link index
   $dst = value of the index-th argument in the link-th function frame
 */

  FunctionFrame *fr;
  JSValue arguments;
  int i;
  fr = get_lp(context);
  for (i = 0; i < link; i++) {
    fr = fframe_prev(fr);
  }
  arguments = fframe_arguments(fr);
  dst = get_array_element(context, arguments, index);
}
#undef dst
#undef link
#undef index

