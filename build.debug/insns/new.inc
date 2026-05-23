#define dst regbase[r0]
#define con v1

DEFLABEL(HEAD):
INSN_COUNT1(new,con);Lnew0:
TLnew_any:
switch((unsigned int) get_ptag(con).v){ // vmgen.newsynth.DecisionDiagram$PTNode@550(2)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_FLONUM:
case TV_SPECIAL:
case TV_FIXNUM:
default:
Lnew1:
TLnew_number_object:
TLnew_flonum:
TLnew_boolean_object:
TLnew_builtin:
TLnew_array:
TLnew_string_object:
TLnew_fixnum:
TLnew_iterator:
TLnew_regexp:
TLnew_simple_object:
TLnew_special:
TLnew_string:
{ // normal_string vmgen.newsynth.DecisionDiagram$Leaf@435

dst = JS_UNDEFINED;
/* Local Variables:      */
/* mode: c               */
/* c-basic-offset: 2     */
/* indent-tabs-mode: nil */
/* End:                  */
}
break;
case TV_GENERIC:
Lnew2:switch((unsigned int) get_htag(con).v){ // vmgen.newsynth.DecisionDiagram$HTNode@173c(2)
case HTAGV_SIMPLE_OBJECT:
case 5:
case 4:
case 3:
case 2:
case 1:
case 0:
case HTAGV_ARRAY:
case HTAGV_BUILTIN:
case HTAGV_ITERATOR:
case HTAGV_REGEXP:
case HTAGV_BOXED_STRING:
case HTAGV_BOXED_NUMBER:
case HTAGV_BOXED_BOOLEAN:
default:
goto Lnew1;
break;
case HTAGV_FUNCTION:
Lnew3:
TLnew_function:
{ // normal_function vmgen.newsynth.DecisionDiagram$Leaf@1f5d

  JSValue obj;
  save_context();
  obj = create_simple_object_with_constructor(context, con);
  dst = obj;
  update_context();
}
break;
}// vmgen.newsynth.DecisionDiagram$HTNode@173c
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@550
#undef dst
#undef con

