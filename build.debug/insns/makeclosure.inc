#define dst regbase[r0]
#define subscr s1

DEFLABEL(HEAD):
INSN_COUNT0(makeclosure);Lmakeclosure0:
TLmakeclosure:
{ // vmgen.newsynth.DecisionDiagram$Leaf@550

  /*
   * makeclosure dst subscr
   *  dst : destination register
   *  subscr : subscript of the function table
   *  $dst = new closure
   */
  /*
   * `subscr' is the subscript of the function table EXCEPT the
   * main function.  Since the main function comes first in the
   * function table, the subecript should be added by 1.
   */
  Subscript ss = subscr + 1;
  save_context();
  dst = new_function_object(context, DEBUG_NAME("insn:makeclosure"),
                            gshapes.g_shape_Function, ss);
  update_context();
/* Local Variables:      */
/* mode: c               */
/* c-basic-offset: 2     */
/* indent-tabs-mode: nil */
/* End:                  */
}
#undef dst
#undef subscr

