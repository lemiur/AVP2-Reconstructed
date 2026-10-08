// Verified zero padding following the final RtlUnwind import thunk; no executable bytes are changed.
//@category AVP2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.ByteDataType;
import ghidra.program.model.data.ArrayDataType;
import ghidra.program.model.symbol.SourceType;
public class RemoveD3drenTailPaddingPE extends GhidraScript {
 @Override public void run() throws Exception {
  Address at=toAddr(0x10045886L);
  for(int n=0;n<10;n++) if(getByte(at.add(n))!=0) throw new IllegalStateException("Tail padding bytes changed");
  currentProgram.getFunctionManager().removeFunction(at);
  currentProgram.getListing().clearCodeUnits(at,at.add(9),false);
  createData(at,new ArrayDataType(ByteDataType.dataType,10,1));
  for(var symbol:currentProgram.getSymbolTable().getSymbols(at))
   if(symbol.getName().startsWith("FUN_")) symbol.delete();
  createLabel(at,"TextTailZeroPadding",true,SourceType.USER_DEFINED);
  println("Removed false function at 10045886; defined ten zero padding bytes");
 }
}
