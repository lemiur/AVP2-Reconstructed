// Sync decomp research into the open program: first the function starts of a splits CSV (addr,name,evidence: shrink
// the function Ghidra merged them into, create the function), then the names of a sync CSV (address,name,kind,...;
// kind func|data).  Functions are renamed (an empty name resets to the default FUN_ name); data names become the primary label (an existing label of that name is
// reused, older labels stay as secondary ones).  Old primary names go to <revertCsv>; NOTE bookmarks in <category>.
// args: <sync.csv> <splits.csv or -> <revertCsv> <category>
//@category AVP2

import java.io.File;
import java.io.PrintWriter;
import java.nio.file.Files;
import java.util.*;

import ghidra.app.script.GhidraScript;
import ghidra.app.util.NamespaceUtils;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class SyncNamesPE extends GhidraScript {

	static String[] splitCsv(String line) {
		List<String> out = new ArrayList<>();
		StringBuilder cur = new StringBuilder();
		boolean q = false;
		for (int i = 0; i < line.length(); i++) {
			char c = line.charAt(i);
			if (c == '"') {
				if (q && i + 1 < line.length() && line.charAt(i + 1) == '"') { cur.append('"'); i++; }
				else q = !q;
			}
			else if (c == ',' && !q) { out.add(cur.toString()); cur.setLength(0); }
			else cur.append(c);
		}
		out.add(cur.toString());
		return out.toArray(new String[0]);
	}

	static List<String> splitQualified(String q) {
		List<String> out = new ArrayList<>();
		int depth = 0, start = 0;
		for (int i = 0; i < q.length(); i++) {
			char c = q.charAt(i);
			if (c == '<') depth++;
			else if (c == '>') depth--;
			else if (depth == 0 && c == ':' && i + 1 < q.length() && q.charAt(i + 1) == ':') {
				out.add(q.substring(start, i));
				start = i + 2;
				i++;
			}
		}
		out.add(q.substring(start));
		return out;
	}

	List<String[]> rows(String path) throws Exception {
		List<String[]> out = new ArrayList<>();
		for (String l : Files.readAllLines(new File(path).toPath())) {
			if (l.isBlank() || l.startsWith("#")) continue;
			out.add(splitCsv(l));
		}
		return out;
	}

	@Override
	public void run() throws Exception {
		String[] a = getScriptArgs();
		String category = a[3];
		BookmarkManager bm = currentProgram.getBookmarkManager();
		SymbolTable st = currentProgram.getSymbolTable();
		int nSplit = 0, nf = 0, nd = 0, same = 0, fail = 0;
		try (PrintWriter rev = new PrintWriter(a[2])) {
			rev.println("address,kind,previous_name");

			if (!a[1].equals("-")) {
				List<String[]> sp = rows(a[1]);
				for (String[] c : sp.subList(1, sp.size())) {
					Address at = toAddr(Long.parseLong(c[0].trim(), 16));
					if (getFunctionAt(at) != null) continue;
					Function cont = getFunctionContaining(at);
					if (cont != null) {
						AddressSet keep = new AddressSet(cont.getBody());
						Address end = cont.getBody().getMaxAddress();
						if (end.compareTo(at) >= 0) keep.delete(new AddressRangeImpl(at, end));
						rev.printf("%s,split_of,\"%s %s\"%n", c[0], cont.getEntryPoint(), cont.getBody());
						cont.setBody(keep);
					}
					Function f = createFunction(at, null);
					if (f == null) { println("SPLIT FAILED " + c[0]); fail++; continue; }
					bm.setBookmark(at, BookmarkType.NOTE, category, "split from " +
						(cont == null ? "?" : cont.getName(true)) + ": " + c[2]);
					nSplit++;
				}
			}

			List<String[]> rs = rows(a[0]);
			List<String> h = Arrays.asList(rs.get(0));
			int iA = h.indexOf("address"), iN = h.indexOf("name"), iK = h.indexOf("kind"), iR = h.indexOf("reason");
			for (String[] c : rs.subList(1, rs.size())) {
				Address at = toAddr(Long.parseLong(c[iA].trim(), 16));
				String full = c[iN].trim();
				List<String> parts = splitQualified(full);
				String name = parts.remove(parts.size() - 1);
				try {
					Namespace ns = parts.isEmpty() ? currentProgram.getGlobalNamespace()
						: NamespaceUtils.createNamespaceHierarchy(String.join("::", parts), null, currentProgram,
							SourceType.IMPORTED);
					if (c[iK].trim().equals("func")) {
						Function f = getFunctionAt(at);
						if (f == null) { println("NO FUNCTION at " + c[iA]); fail++; continue; }
						if (full.isEmpty()) {                       // back to Ghidra's default FUN_ name
							if (f.getSymbol().getSource() == SourceType.DEFAULT) { same++; continue; }
							rev.printf("%s,func,\"%s\"%n", c[iA], f.getName(true));
							f.getSymbol().setName(null, SourceType.DEFAULT);
							bm.setBookmark(at, BookmarkType.NOTE, category, "synced (" + c[iR].trim() + ")");
							nf++;
							continue;
						}
						if (f.getName(true).equals(full)) { same++; continue; }
						for (Symbol s : st.getSymbols(at))          // a label of the wanted name would clash
							if (s != f.getSymbol() && s.getName().equals(name) && s.getParentNamespace().equals(ns))
								s.delete();
						rev.printf("%s,func,\"%s\"%n", c[iA], f.getName(true));
						f.getSymbol().setNameAndNamespace(name, ns, SourceType.IMPORTED);
						nf++;
					}
					else {
						Symbol old = st.getPrimarySymbol(at);
						if (old != null && old.getName(true).equals(full)) { same++; continue; }
						Symbol s = st.getSymbol(name, at, ns);
						if (s == null) s = st.createLabel(at, name, ns, SourceType.IMPORTED);
						rev.printf("%s,data,\"%s\"%n", c[iA], old == null ? "" : old.getName(true));
						s.setPrimary();
						nd++;
					}
					bm.setBookmark(at, BookmarkType.NOTE, category, "synced (" + c[iR].trim() + "): " + full);
				}
				catch (Exception e) {
					println("FAILED " + c[iA] + " " + full + ": " + e);
					fail++;
				}
			}
		}
		println(String.format("SyncNamesPE: splits %d, functions %d, data %d, already same %d, failed %d", nSplit, nf,
			nd, same, fail));
		if (fail != 0) throw new IllegalStateException("Name synchronization failed for " + fail + " entries");
	}
}
