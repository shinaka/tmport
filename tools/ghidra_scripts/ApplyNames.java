// Apply analyst names from a names folder (*.tsv: address<TAB>name<TAB>...) to the program.
// Creates functions at code addresses that are not yet function entries.
// Rows from files whose name starts with "dgroup" are always data labels (never functions), so a data
// segment that the loader marked executable (the DOS EXE's DGROUP) is not disassembled by accident.
// Usage: -postScript ApplyNames.java <names_dir>      (e.g. re/names for TMWin, re/names/dos for TMDos)
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.nio.file.*;
import java.util.*;

public class ApplyNames extends GhidraScript {
    @Override
    public void run() throws Exception {
        File dir = new File(getScriptArgs()[0]);
        AddressFactory af = currentProgram.getAddressFactory();
        SymbolTable st = currentProgram.getSymbolTable();
        Set<String> used = new HashSet<>();
        int fn = 0, created = 0, labels = 0, bad = 0, stale = 0;
        File[] files = dir.listFiles((d, n) -> n.endsWith(".tsv"));
        Arrays.sort(files);
        for (File f : files) {
            boolean dataOnly = f.getName().startsWith("dgroup");
            for (String line : Files.readAllLines(f.toPath())) {
                String[] p = line.split("\t");
                if (p.length < 2 || !p[0].matches("[0-9a-fA-F]{4}:[0-9a-fA-F]{4}")) continue;
                String name = p[1].trim().replaceAll("[^A-Za-z0-9_]", "_");
                if (name.isEmpty() || Character.isDigit(name.charAt(0))) name = "n_" + name;
                Address a = af.getAddress(p[0]);
                if (a == null) { bad++; continue; }
                if (used.contains(name)) name = name + "_" + p[0].replace(':', '_');
                used.add(name);
                MemoryBlock b = currentProgram.getMemory().getBlock(a);
                if (b == null) { bad++; continue; }
                try {
                    if (b.isExecute() && !dataOnly) {
                        Function func = getFunctionAt(a);
                        if (func == null && getFunctionContaining(a) == null) {
                            new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
                            if (new CreateFunctionCmd(a).applyTo(currentProgram, monitor)) created++;
                            func = getFunctionAt(a);
                        }
                        if (func != null) { func.setName(name, SourceType.USER_DEFINED); fn++; continue; }
                    }
                    Symbol s = st.createLabel(a, name, SourceType.USER_DEFINED);
                    s.setPrimary();
                    labels++;
                    // drop stale analyst labels left at this address by earlier runs (renamed aliases)
                    for (Symbol old : st.getSymbols(a)) {
                        if (!old.equals(s) && old.getSource() == SourceType.USER_DEFINED
                                && old.getSymbolType() == SymbolType.LABEL && !old.isPrimary()) {
                            if (old.delete()) stale++;
                        }
                    }
                } catch (Exception e) {
                    bad++;
                }
            }
        }
        println("functions named " + fn + " (created " + created + "), labels " + labels + ", stale labels removed " + stale + ", failed " + bad);
    }
}
