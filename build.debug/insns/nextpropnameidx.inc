#define dst regbase[r0]
#define itr v1

DEFLABEL(HEAD):
INSN_COUNT1(nextpropnameidx,itr);Lnextpropnameidx0:
TLnextpropnameidx_string:
TLnextpropnameidx_regexp:
TLnextpropnameidx_builtin:
TLnextpropnameidx_flonum:
TLnextpropnameidx_number_object:
TLnextpropnameidx_array:
TLnextpropnameidx_string_object:
TLnextpropnameidx_special:
TLnextpropnameidx_boolean_object:
TLnextpropnameidx_simple_object:
TLnextpropnameidx_function:
TLnextpropnameidx_any:
TLnextpropnameidx_iterator:
TLnextpropnameidx_fixnum:
{ // normal_builtin vmgen.newsynth.DecisionDiagram$Leaf@54

/*
   nextpropnameidx itr dst
     itr : iterator for enumerating properties in obj
     dst : destination
   $dst = the next property name of $obj in $itr
 */

  JSValue res = JS_UNDEFINED;
  iterator_get_next_propname(itr, &res);
  dst = res;
}
#undef dst
#undef itr

