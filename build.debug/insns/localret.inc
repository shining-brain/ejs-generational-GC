
DEFLABEL(HEAD):
INSN_COUNT0(localret);Llocalret0:
TLlocalret:
{ // vmgen.newsynth.DecisionDiagram$Leaf@550

/*
   localret
 */

  InstructionDisplacement disp;
  int newpc;
  lcall_stack_pop(context, &newpc);
  disp = (InstructionDisplacement) (newpc - pc);
  set_pc_relative(disp);
  // NEXT_INSN_INCPC();   /* need INCPC; local_call_stack has the address of local call */
}

