#define dst regbase[r0]

DEFLABEL(HEAD):
INSN_COUNT1(not,v1);Lnot0:
TLnot_any:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@550(5)
case TV_GENERIC:
Lnot1:
TLnot_builtin:
TLnot_simple_object:
TLnot_string_object:
TLnot_array:
TLnot_boolean_object:
TLnot_iterator:
TLnot_function:
TLnot_number_object:
TLnot_regexp:
{ // normal_array vmgen.newsynth.DecisionDiagram$Leaf@54

  dst = JS_FALSE;
}
break;
case TV_FIXNUM:
default:
Lnot2:
TLnot_fixnum:
{ // normal_fixnum vmgen.newsynth.DecisionDiagram$Leaf@dbb

  dst = true_false(v1 == FIXNUM_ZERO);
}
break;
case TV_STRING:
case 3:
case 2:
case 1:
Lnot3:
TLnot_string:
{ // normal_string vmgen.newsynth.DecisionDiagram$Leaf@118b

  dst = true_false(v1 == gconsts.g_string_empty);
}
break;
case TV_SPECIAL:
Lnot4:
TLnot_special:
{ // normal_special vmgen.newsynth.DecisionDiagram$Leaf@1c49

  dst = v1 == JS_TRUE ? JS_FALSE : JS_TRUE;
}
break;
case TV_FLONUM:
Lnot5:
TLnot_flonum:
{ // normal_flonum vmgen.newsynth.DecisionDiagram$Leaf@1f5d

  dst = true_false(v1 == gconsts.g_flonum_nan);
}
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@550
#undef dst

