// Keep one headless project session for separately built and verified rename batches.
// args: queue-directory first-batch last-batch export-directory
//@category AVP2
import java.nio.file.*;
import ghidra.app.script.GhidraScript;

public class SyncNamesQueuePE extends GhidraScript {
    @Override public void run() throws Exception {
        String[] args=getScriptArgs();
        Path queue=Paths.get(args[0]);
        int first=Integer.parseInt(args[1]),last=Integer.parseInt(args[2]);
        Path export=Paths.get(args[3]);
        Files.createDirectories(queue);
        // Earlier post-scripts applied/exported batch 1 in this same session.
        Files.writeString(queue.resolve("batch01.done"),"applied and exported\n");
        for(int n=first;n<=last;n++) {
            String tag=String.format("batch%02d",n);
            Path ready=queue.resolve(tag+".ready");
            while(!Files.exists(ready)) { monitor.checkCancelled();Thread.sleep(500); }
            String csv=queue.resolve(tag+"_sync.csv").toString();
            println("QUEUE START "+tag);
            runScript("SyncNamesPE.java",new String[]{csv,"-",queue.resolve(tag+"_revert.csv").toString(),"Step4Names"});
            runScript("ExportSymbolsPE.java",new String[]{export.resolve("symbols.csv").toString(),export.resolve("symbols_summary.txt").toString()});
            Files.writeString(queue.resolve(tag+".done"),"applied and exported\n");
            println("QUEUE DONE "+tag);
        }
    }
}
