// Export decompiled C, disassembly, call graph, strings and imports for every function.
// Usage (headless): -postScript ExportAll.java <outdir>
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.data.*;
import java.io.*;
import java.util.*;

public class ExportAll extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File out = new File(args[0]);
        out.mkdirs();
        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        try (PrintWriter seg = new PrintWriter(new FileWriter(new File(out, "segments.tsv")))) {
            seg.println("name\tstart\tend\tsize\tflags");
            for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
                seg.printf("%s\t%s\t%s\t%d\t%s%s%s%n", b.getName(), b.getStart(), b.getEnd(), b.getSize(),
                        b.isRead() ? "r" : "-", b.isWrite() ? "w" : "-", b.isExecute() ? "x" : "-");
            }
        }

        try (PrintWriter imp = new PrintWriter(new FileWriter(new File(out, "imports.tsv")))) {
            for (Symbol s : currentProgram.getSymbolTable().getExternalSymbols()) {
                StringBuilder refs = new StringBuilder();
                for (Reference r : getReferencesTo(s.getAddress())) refs.append(r.getFromAddress()).append(' ');
                imp.printf("%s\t%s\t%s%n", s.getParentNamespace().getName(), s.getName(), refs.toString().trim());
            }
        }

        DecompInterface di = new DecompInterface();
        DecompileOptions opts = new DecompileOptions();
        di.setOptions(opts);
        di.toggleCCode(true);
        di.toggleSyntaxTree(true);
        di.setSimplificationStyle("decompile");
        di.openProgram(currentProgram);

        File fdir = new File(out, "functions");
        fdir.mkdirs();
        try (PrintWriter all = new PrintWriter(new FileWriter(new File(out, "decompiled.c")));
             PrintWriter asm = new PrintWriter(new FileWriter(new File(out, "disassembly.asm")));
             PrintWriter idx = new PrintWriter(new FileWriter(new File(out, "functions.tsv")))) {
            idx.println("entry\tname\tsize\tcallers\tcallees");
            FunctionIterator it = fm.getFunctions(true);
            int n = 0;
            while (it.hasNext() && !monitor.isCancelled()) {
                Function f = it.next();
                if (f.isExternal() || f.isThunk() && f.getBody().getNumAddresses() == 0) continue;
                StringBuilder callers = new StringBuilder();
                for (Function c : f.getCallingFunctions(monitor)) callers.append(c.getEntryPoint()).append(',');
                StringBuilder callees = new StringBuilder();
                for (Function c : f.getCalledFunctions(monitor)) callees.append(c.isExternal() ? c.getName(true) : c.getEntryPoint().toString()).append(',');
                idx.printf("%s\t%s\t%d\t%s\t%s%n", f.getEntryPoint(), f.getName(), f.getBody().getNumAddresses(), callers, callees);

                String c;
                DecompileResults r = di.decompileFunction(f, 180, monitor);
                if (r != null && r.decompileCompleted() && r.getDecompiledFunction() != null) {
                    c = r.getDecompiledFunction().getC();
                } else {
                    c = "// DECOMPILATION FAILED: " + (r == null ? "null" : r.getErrorMessage()) + "\n";
                }
                String hdr = "// ==== " + f.getName() + " @ " + f.getEntryPoint() + " (size " + f.getBody().getNumAddresses() + ")\n";
                all.print(hdr);
                all.println(c);
                String fname = f.getEntryPoint().toString().replace(':', '_') + ".c";
                try (PrintWriter pf = new PrintWriter(new FileWriter(new File(fdir, fname)))) {
                    pf.print(hdr);
                    pf.println(c);
                }

                asm.print(hdr);
                InstructionIterator ii = listing.getInstructions(f.getBody(), true);
                while (ii.hasNext()) {
                    Instruction ins = ii.next();
                    StringBuilder bytes = new StringBuilder();
                    try { for (byte b : ins.getBytes()) bytes.append(String.format("%02x", b & 0xff)); } catch (Exception e) { }
                    String cmt = "";
                    Reference[] refs = ins.getReferencesFrom();
                    for (Reference ref : refs) {
                        Symbol s = getSymbolAt(ref.getToAddress());
                        if (s != null) cmt += " ; -> " + s.getName(true);
                        else {
                            Data d = getDataAt(ref.getToAddress());
                            if (d != null && d.hasStringValue()) cmt += " ; \"" + String.valueOf(d.getValue()).replace("\n", "\n") + "\"";
                        }
                    }
                    asm.printf("  %s  %-16s %s%s%n", ins.getAddress(), bytes, ins.toString(), cmt);
                }
                asm.println();
                n++;
                if (n % 100 == 0) println("exported " + n + " functions");
            }
            println("exported " + n + " functions total");
        }

        try (PrintWriter st = new PrintWriter(new FileWriter(new File(out, "strings.tsv")))) {
            DataIterator dit = listing.getDefinedData(true);
            while (dit.hasNext()) {
                Data d = dit.next();
                if (!d.hasStringValue()) continue;
                StringBuilder refs = new StringBuilder();
                for (Reference r : getReferencesTo(d.getAddress())) {
                    Function f = getFunctionContaining(r.getFromAddress());
                    refs.append(f != null ? f.getName() : r.getFromAddress().toString()).append(',');
                }
                st.printf("%s\t%s\t%s%n", d.getAddress(), String.valueOf(d.getValue()).replace("\n", "\n").replace("\t", "\t"), refs);
            }
        }
        di.dispose();
    }
}
