#define dst regbase[r0]

DEFLABEL(HEAD):
INSN_COUNT2(lessthan,v1,v2);Llessthan0:
TLlessthan_string_object_any:
TLlessthan_fixnum_any:
TLlessthan_any_any:
TLlessthan_function_any:
TLlessthan_string_any:
TLlessthan_flonum_any:
TLlessthan_boolean_object_any:
TLlessthan_array_any:
TLlessthan_iterator_any:
TLlessthan_builtin_any:
TLlessthan_special_any:
TLlessthan_simple_object_any:
TLlessthan_regexp_any:
TLlessthan_number_object_any:
switch((unsigned int) get_ptag(v2).v){ // vmgen.newsynth.DecisionDiagram$PTNode@550(5)
case TV_SPECIAL:
Llessthan1:
TLlessthan_any_special:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@41d(3)
case TV_GENERIC:
Llessthan2:switch((unsigned int) get_htag(v1).v){ // vmgen.newsynth.DecisionDiagram$HTNode@7c(2)
case HTAGV_ITERATOR:
Llessthan3:
TLlessthan_string_object_regexp:
TLlessthan_simple_object_string_object:
TLlessthan_regexp_array:
TLlessthan_builtin_simple_object:
TLlessthan_string_object_simple_object:
TLlessthan_boolean_object_simple_object:
TLlessthan_regexp_regexp:
TLlessthan_iterator_number_object:
TLlessthan_simple_object_boolean_object:
TLlessthan_array_array:
TLlessthan_string_object_iterator:
TLlessthan_iterator_array:
TLlessthan_iterator_string:
TLlessthan_function_number_object:
TLlessthan_array_simple_object:
TLlessthan_string_object_function:
TLlessthan_number_object_function:
TLlessthan_boolean_object_string_object:
TLlessthan_iterator_fixnum:
TLlessthan_builtin_iterator:
TLlessthan_flonum_iterator:
TLlessthan_builtin_string_object:
TLlessthan_function_string_object:
TLlessthan_iterator_regexp:
TLlessthan_iterator_flonum:
TLlessthan_iterator_special:
TLlessthan_function_boolean_object:
TLlessthan_builtin_regexp:
TLlessthan_iterator_function:
TLlessthan_simple_object_builtin:
TLlessthan_number_object_builtin:
TLlessthan_simple_object_function:
TLlessthan_array_boolean_object:
TLlessthan_function_function:
TLlessthan_array_regexp:
TLlessthan_string_iterator:
TLlessthan_regexp_number_object:
TLlessthan_number_object_simple_object:
TLlessthan_array_function:
TLlessthan_array_string_object:
TLlessthan_builtin_number_object:
TLlessthan_simple_object_regexp:
TLlessthan_builtin_function:
TLlessthan_boolean_object_function:
TLlessthan_iterator_iterator:
TLlessthan_boolean_object_number_object:
TLlessthan_string_object_boolean_object:
TLlessthan_iterator_simple_object:
TLlessthan_function_array:
TLlessthan_regexp_function:
TLlessthan_number_object_boolean_object:
TLlessthan_simple_object_array:
TLlessthan_function_builtin:
TLlessthan_boolean_object_builtin:
TLlessthan_number_object_iterator:
TLlessthan_string_object_builtin:
TLlessthan_fixnum_iterator:
TLlessthan_boolean_object_array:
TLlessthan_number_object_array:
TLlessthan_number_object_string_object:
TLlessthan_boolean_object_boolean_object:
TLlessthan_regexp_simple_object:
TLlessthan_string_object_string_object:
TLlessthan_special_iterator:
TLlessthan_simple_object_number_object:
TLlessthan_string_object_array:
TLlessthan_number_object_number_object:
TLlessthan_builtin_boolean_object:
TLlessthan_simple_object_simple_object:
TLlessthan_array_builtin:
TLlessthan_iterator_builtin:
TLlessthan_number_object_regexp:
TLlessthan_function_iterator:
TLlessthan_boolean_object_regexp:
TLlessthan_iterator_string_object:
TLlessthan_regexp_string_object:
TLlessthan_function_regexp:
TLlessthan_regexp_iterator:
TLlessthan_regexp_builtin:
TLlessthan_string_object_number_object:
TLlessthan_simple_object_iterator:
TLlessthan_function_simple_object:
TLlessthan_builtin_array:
TLlessthan_regexp_boolean_object:
TLlessthan_boolean_object_iterator:
TLlessthan_array_number_object:
TLlessthan_iterator_boolean_object:
TLlessthan_array_iterator:
TLlessthan_builtin_builtin:
{ // normal_number_object normal_boolean_object vmgen.newsynth.DecisionDiagram$Leaf@16

  v1 = object_to_primitive(context, v1, HINT_NUMBER);
  v2 = object_to_primitive(context, v2, HINT_NUMBER);
  goto USELABEL(HEAD);
}
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
Llessthan4:
TLlessthan_regexp_string:
TLlessthan_function_special:
TLlessthan_string_object_string:
TLlessthan_simple_object_string:
TLlessthan_number_object_special:
TLlessthan_regexp_flonum:
TLlessthan_simple_object_fixnum:
TLlessthan_regexp_special:
TLlessthan_string_object_fixnum:
TLlessthan_number_object_flonum:
TLlessthan_array_flonum:
TLlessthan_boolean_object_flonum:
TLlessthan_builtin_fixnum:
TLlessthan_number_object_fixnum:
TLlessthan_simple_object_flonum:
TLlessthan_builtin_special:
TLlessthan_builtin_flonum:
TLlessthan_string_object_flonum:
TLlessthan_builtin_string:
TLlessthan_array_special:
TLlessthan_number_object_string:
TLlessthan_boolean_object_string:
TLlessthan_function_string:
TLlessthan_boolean_object_special:
TLlessthan_simple_object_special:
TLlessthan_string_object_special:
TLlessthan_array_string:
TLlessthan_function_flonum:
TLlessthan_array_fixnum:
TLlessthan_regexp_fixnum:
TLlessthan_boolean_object_fixnum:
TLlessthan_function_fixnum:
{ // normal_array normal_special vmgen.newsynth.DecisionDiagram$Leaf@54

  v1 = object_to_primitive(context, v1, HINT_NUMBER);
  goto USELABEL(HEAD);
}
break;
}// vmgen.newsynth.DecisionDiagram$HTNode@7c
break;
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
Llessthan5:
TLlessthan_string_special:
TLlessthan_special_special:
TLlessthan_special_string:
{ // normal_string normal_special vmgen.newsynth.DecisionDiagram$Leaf@281

  v1 = to_number(context, v1);
  v2 = to_number(context, v2);
  goto USELABEL(HEAD);
}
break;
case TV_FLONUM:
case TV_FIXNUM:
default:
Llessthan6:
TLlessthan_fixnum_string:
TLlessthan_flonum_special:
TLlessthan_fixnum_special:
TLlessthan_flonum_string:
{ // normal_fixnum normal_special vmgen.newsynth.DecisionDiagram$Leaf@7a5

  v2 = to_number(context, v2);
  goto USELABEL(HEAD);
}
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@41d
break;
case TV_FIXNUM:
default:
Llessthan7:
TLlessthan_any_fixnum:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@b26(4)
case TV_GENERIC:
goto Llessthan2;
break;
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
Llessthan8:
TLlessthan_string_fixnum:
TLlessthan_special_fixnum:
TLlessthan_string_flonum:
TLlessthan_special_flonum:
{ // normal_special normal_fixnum vmgen.newsynth.DecisionDiagram$Leaf@f3

  v1 = to_number(context, v1);
  goto USELABEL(HEAD);
}
break;
case TV_FLONUM:
Llessthan9:
TLlessthan_fixnum_flonum:
TLlessthan_flonum_fixnum:
TLlessthan_flonum_flonum:
{ // normal_flonum normal_flonum vmgen.newsynth.DecisionDiagram$Leaf@bfb

  double x1 = to_double(context, v1);
  double x2 = to_double(context, v2);
  dst = true_false(x1 < x2);
}
break;
case TV_FIXNUM:
default:
Llessthan10:
TLlessthan_fixnum_fixnum:
{ // normal_fixnum normal_fixnum vmgen.newsynth.DecisionDiagram$Leaf@207f

  dst = true_false(((intjsv_t) v1) < ((intjsv_t) v2));
}
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@b26
break;
case TV_FLONUM:
Llessthan11:
TLlessthan_any_flonum:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@173c(3)
case TV_GENERIC:
goto Llessthan2;
break;
case TV_STRING:
case 3:
case 2:
case 1:
case TV_SPECIAL:
goto Llessthan8;
break;
case TV_FLONUM:
case TV_FIXNUM:
default:
goto Llessthan9;
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@173c
break;
case TV_GENERIC:
Llessthan12:
TLlessthan_any_array:
TLlessthan_any_number_object:
TLlessthan_any_regexp:
TLlessthan_any_string_object:
TLlessthan_any_simple_object:
TLlessthan_any_boolean_object:
TLlessthan_any_builtin:
TLlessthan_any_function:
TLlessthan_any_iterator:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@1986(2)
case TV_GENERIC:
goto Llessthan3;
break;
case TV_STRING:
case 3:
case 2:
case 1:
case TV_FLONUM:
case TV_SPECIAL:
case TV_FIXNUM:
default:
Llessthan13:switch((unsigned int) get_htag(v2).v){ // vmgen.newsynth.DecisionDiagram$HTNode@1679(2)
case HTAGV_ITERATOR:
goto Llessthan3;
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
Llessthan14:
TLlessthan_string_function:
TLlessthan_fixnum_string_object:
TLlessthan_flonum_function:
TLlessthan_special_simple_object:
TLlessthan_string_builtin:
TLlessthan_flonum_builtin:
TLlessthan_string_simple_object:
TLlessthan_fixnum_array:
TLlessthan_string_number_object:
TLlessthan_flonum_string_object:
TLlessthan_special_array:
TLlessthan_special_function:
TLlessthan_fixnum_regexp:
TLlessthan_fixnum_function:
TLlessthan_special_boolean_object:
TLlessthan_special_number_object:
TLlessthan_special_builtin:
TLlessthan_flonum_boolean_object:
TLlessthan_fixnum_simple_object:
TLlessthan_string_boolean_object:
TLlessthan_flonum_array:
TLlessthan_flonum_number_object:
TLlessthan_special_string_object:
TLlessthan_fixnum_builtin:
TLlessthan_string_array:
TLlessthan_flonum_simple_object:
TLlessthan_string_regexp:
TLlessthan_flonum_regexp:
TLlessthan_fixnum_boolean_object:
TLlessthan_string_string_object:
TLlessthan_special_regexp:
TLlessthan_fixnum_number_object:
{ // normal_string normal_simple_object vmgen.newsynth.DecisionDiagram$Leaf@241

  v2 = object_to_primitive(context, v2, HINT_NUMBER);
  goto USELABEL(HEAD);
}
break;
}// vmgen.newsynth.DecisionDiagram$HTNode@1679
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@1986
break;
case TV_STRING:
case 3:
case 2:
case 1:
Llessthan15:
TLlessthan_any_string:
switch((unsigned int) get_ptag(v1).v){ // vmgen.newsynth.DecisionDiagram$PTNode@1c98(4)
case TV_GENERIC:
goto Llessthan2;
break;
case TV_SPECIAL:
goto Llessthan5;
break;
case TV_FLONUM:
case TV_FIXNUM:
default:
goto Llessthan6;
break;
case TV_STRING:
case 3:
case 2:
case 1:
Llessthan16:
TLlessthan_string_string:
{ // normal_string normal_string vmgen.newsynth.DecisionDiagram$Leaf@a14

  dst = true_false(strcmp(string_to_cstr(v1), string_to_cstr(v2)) < 0);
}
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@1c98
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@550
#undef dst

