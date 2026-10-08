// Remove obsolete generated secondary labels only at reviewed addresses.
//@category AVP2
import java.nio.file.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SymbolType;
public class RemoveScopedGeneratedAliasesPE extends GhidraScript {
 @Override public void run() throws Exception {
  int removed=0;
  for(String line:Files.readAllLines(Paths.get(getScriptArgs()[0]))) {
   if(line.isBlank()||line.startsWith("address")) continue;
   var at=toAddr(Long.parseLong(line.split(",")[0],16));
   for(var symbol:currentProgram.getSymbolTable().getSymbols(at)) {
    if(symbol.getSymbolType()!=SymbolType.LABEL || !symbol.getName().matches("(?:FUN|DAT|unk)_[0-9a-fA-F]{8}")) continue;
    if(symbol.isPrimary()) throw new IllegalStateException("Reviewed address still has generated primary label: "+at);
    symbol.delete();removed++;
   }
  }
  println("Removed "+removed+" obsolete generated secondary labels at reviewed addresses");
 }
}
