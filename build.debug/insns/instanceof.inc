#define dst regbase[r0]

DEFLABEL(HEAD):
INSN_COUNT2(instanceof,v1,v2);Linstanceof0:
TLinstanceof_any_string:
TLinstanceof_any_regexp:
TLinstanceof_any_string_object:
TLinstanceof_any_function:
TLinstanceof_any_simple_object:
TLinstanceof_any_number_object:
TLinstanceof_any_iterator:
TLinstanceof_any_flonum:
TLinstanceof_any_special:
TLinstanceof_any_fixnum:
TLinstanceof_any_any:
TLinstanceof_any_builtin:
TLinstanceof_any_boolean_object:
TLinstanceof_any_array:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@550(2)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_FLONUM:
case TV_SPECIAL:
case TV_FIXNUM:
default:
Linstanceof1:
TLinstanceof_builtin_simple_object:
TLinstanceof_iterator_builtin:
TLinstanceof_array_fixnum:
TLinstanceof_regexp_string_object:
TLinstanceof_boolean_object_special:
TLinstanceof_number_object_special:
TLinstanceof_array_flonum:
TLinstanceof_fixnum_function:
TLinstanceof_flonum_number_object:
TLinstanceof_string_function:
TLinstanceof_function_special:
TLinstanceof_iterator_special:
TLinstanceof_simple_object_string_object:
TLinstanceof_iterator_number_object:
TLinstanceof_string_object_iterator:
TLinstanceof_number_object_iterator:
TLinstanceof_array_string:
TLinstanceof_string_object_simple_object:
TLinstanceof_fixnum_regexp:
TLinstanceof_boolean_object_simple_object:
TLinstanceof_regexp_special:
TLinstanceof_special_array:
TLinstanceof_iterator_string:
TLinstanceof_simple_object_special:
TLinstanceof_regexp_regexp:
TLinstanceof_boolean_object_boolean_object:
TLinstanceof_fixnum_boolean_object:
TLinstanceof_builtin_array:
TLinstanceof_string_object_regexp:
TLinstanceof_simple_object_regexp:
TLinstanceof_function_regexp:
TLinstanceof_flonum_regexp:
TLinstanceof_iterator_function:
TLinstanceof_array_iterator:
TLinstanceof_iterator_simple_object:
TLinstanceof_function_flonum:
TLinstanceof_flonum_flonum:
TLinstanceof_boolean_object_number_object:
TLinstanceof_simple_object_iterator:
TLinstanceof_flonum_fixnum:
TLinstanceof_regexp_flonum:
TLinstanceof_function_fixnum:
TLinstanceof_regexp_boolean_object:
TLinstanceof_function_string_object:
TLinstanceof_string_object_boolean_object:
TLinstanceof_flonum_any:
TLinstanceof_builtin_fixnum:
TLinstanceof_number_object_string:
TLinstanceof_function_number_object:
TLinstanceof_number_object_boolean_object:
TLinstanceof_regexp_iterator:
TLinstanceof_array_boolean_object:
TLinstanceof_string_object_array:
TLinstanceof_fixnum_array:
TLinstanceof_string_array:
TLinstanceof_flonum_boolean_object:
TLinstanceof_boolean_object_string:
TLinstanceof_builtin_regexp:
TLinstanceof_fixnum_string:
TLinstanceof_special_function:
TLinstanceof_number_object_number_object:
TLinstanceof_special_number_object:
TLinstanceof_string_regexp:
TLinstanceof_array_string_object:
TLinstanceof_simple_object_array:
TLinstanceof_string_fixnum:
TLinstanceof_string_string_object:
TLinstanceof_special_regexp:
TLinstanceof_string_object_number_object:
TLinstanceof_iterator_flonum:
TLinstanceof_function_simple_object:
TLinstanceof_special_iterator:
TLinstanceof_fixnum_builtin:
TLinstanceof_string_iterator:
TLinstanceof_special_string:
TLinstanceof_array_regexp:
TLinstanceof_fixnum_simple_object:
TLinstanceof_special_boolean_object:
TLinstanceof_special_special:
TLinstanceof_special_simple_object:
TLinstanceof_special_fixnum:
TLinstanceof_fixnum_special:
TLinstanceof_fixnum_fixnum:
TLinstanceof_simple_object_string:
TLinstanceof_string_simple_object:
TLinstanceof_flonum_special:
TLinstanceof_fixnum_any:
TLinstanceof_special_flonum:
TLinstanceof_regexp_array:
TLinstanceof_flonum_builtin:
TLinstanceof_string_boolean_object:
TLinstanceof_string_flonum:
TLinstanceof_string_special:
TLinstanceof_simple_object_flonum:
TLinstanceof_string_object_flonum:
TLinstanceof_simple_object_fixnum:
TLinstanceof_string_object_fixnum:
TLinstanceof_number_object_simple_object:
TLinstanceof_special_builtin:
TLinstanceof_fixnum_iterator:
TLinstanceof_regexp_simple_object:
TLinstanceof_array_simple_object:
TLinstanceof_fixnum_number_object:
TLinstanceof_flonum_function:
TLinstanceof_function_array:
TLinstanceof_string_any:
TLinstanceof_function_boolean_object:
TLinstanceof_boolean_object_string_object:
TLinstanceof_regexp_string:
TLinstanceof_string_builtin:
TLinstanceof_regexp_fixnum:
TLinstanceof_simple_object_boolean_object:
TLinstanceof_fixnum_flonum:
TLinstanceof_boolean_object_flonum:
TLinstanceof_number_object_fixnum:
TLinstanceof_flonum_simple_object:
TLinstanceof_array_array:
TLinstanceof_string_object_string_object:
TLinstanceof_builtin_number_object:
TLinstanceof_string_number_object:
TLinstanceof_simple_object_number_object:
TLinstanceof_number_object_regexp:
TLinstanceof_number_object_flonum:
TLinstanceof_iterator_fixnum:
TLinstanceof_string_object_string:
TLinstanceof_flonum_string:
TLinstanceof_builtin_iterator:
TLinstanceof_iterator_array:
TLinstanceof_array_number_object:
TLinstanceof_builtin_flonum:
TLinstanceof_function_string:
TLinstanceof_string_object_special:
TLinstanceof_function_iterator:
TLinstanceof_special_string_object:
TLinstanceof_number_object_string_object:
TLinstanceof_flonum_array:
TLinstanceof_special_any:
TLinstanceof_iterator_string_object:
TLinstanceof_boolean_object_array:
TLinstanceof_iterator_regexp:
TLinstanceof_boolean_object_regexp:
TLinstanceof_builtin_string:
TLinstanceof_builtin_string_object:
TLinstanceof_boolean_object_fixnum:
TLinstanceof_flonum_string_object:
TLinstanceof_regexp_number_object:
TLinstanceof_array_special:
TLinstanceof_boolean_object_iterator:
TLinstanceof_number_object_array:
TLinstanceof_builtin_special:
TLinstanceof_fixnum_string_object:
TLinstanceof_string_string:
TLinstanceof_iterator_boolean_object:
TLinstanceof_iterator_iterator:
TLinstanceof_simple_object_simple_object:
TLinstanceof_builtin_boolean_object:
TLinstanceof_flonum_iterator:
{ // normal_flonum normal_flonum vmgen.newsynth.DecisionDiagram$Leaf@16

  dst = JS_FALSE;
/* Local Variables:      */
/* mode: c               */
/* c-basic-offset: 2     */
/* indent-tabs-mode: nil */
/* End:                  */
}
break;
case TV_GENERIC:
Linstanceof2:
TLinstanceof_number_object_any:
TLinstanceof_iterator_any:
TLinstanceof_boolean_object_any:
TLinstanceof_builtin_any:
TLinstanceof_string_object_any:
TLinstanceof_array_any:
TLinstanceof_function_any:
TLinstanceof_regexp_any:
TLinstanceof_simple_object_any:
switch((unsigned int) get_ptag(v2).v){ // vmgen.newsynth.DecisionDiagram$PTNode@41d(2)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_FLONUM:
case TV_SPECIAL:
case TV_FIXNUM:
default:
goto Linstanceof1;
break;
case TV_GENERIC:
Linstanceof3:switch((unsigned int) get_htag(v1).v){ // vmgen.newsynth.DecisionDiagram$HTNode@118b(2)
case HTAGV_ITERATOR:
goto Linstanceof1;
break;
case HTAGV_SIMPLE_OBJECT:
case 5:
case 4:
case 3:
case 2:
case 1:
case 0:
case HTAGV_ARRAY:
case HTAGV_FUNCTION:
case HTAGV_BUILTIN:
case HTAGV_REGEXP:
case HTAGV_BOXED_STRING:
case HTAGV_BOXED_NUMBER:
case HTAGV_BOXED_BOOLEAN:
default:
Linstanceof4:switch((unsigned int) get_htag(v2).v){ // vmgen.newsynth.DecisionDiagram$HTNode@537(2)
case HTAGV_SIMPLE_OBJECT:
case 5:
case 4:
case 3:
case 2:
case 1:
case 0:
case HTAGV_ARRAY:
case HTAGV_ITERATOR:
case HTAGV_REGEXP:
case HTAGV_BOXED_STRING:
case HTAGV_BOXED_NUMBER:
case HTAGV_BOXED_BOOLEAN:
default:
goto Linstanceof1;
break;
case HTAGV_FUNCTION:
case HTAGV_BUILTIN:
Linstanceof5:
TLinstanceof_string_object_builtin:
TLinstanceof_simple_object_builtin:
TLinstanceof_number_object_function:
TLinstanceof_builtin_function:
TLinstanceof_string_object_function:
TLinstanceof_function_function:
TLinstanceof_builtin_builtin:
TLinstanceof_simple_object_function:
TLinstanceof_boolean_object_builtin:
TLinstanceof_boolean_object_function:
TLinstanceof_array_builtin:
TLinstanceof_regexp_function:
TLinstanceof_regexp_builtin:
TLinstanceof_number_object_builtin:
TLinstanceof_function_builtin:
TLinstanceof_array_function:
{ // normal_boolean_object normal_function vmgen.newsynth.DecisionDiagram$Leaf@185

/*
   instanceof dst r1 r2
     $dst = $r1 instanceof $r2
 */

  JSValue ctor_prototype = get_prop(v2, gconsts.g_string_prototype);
  if (!is_jsobject(ctor_prototype))
    dst = JS_FALSE;
  else {
    JSValue __proto__ = v1;
    JSValue ret = JS_FALSE;
    while ((__proto__ = get_prop(__proto__,
                                 gconsts.g_string___proto__)) != JS_EMPTY)
      if (__proto__ == ctor_prototype) {
        ret = JS_TRUE;
        break;
      }
    dst = ret;
  }
}
break;
}// vmgen.newsynth.DecisionDiagram$HTNode@537
break;
}// vmgen.newsynth.DecisionDiagram$HTNode@118b
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@41d
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@550
#undef dst

