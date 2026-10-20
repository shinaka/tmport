// Apply analyst names from a names folder (*.tsv: address<TAB>name<TAB>...) to the program.
// Creates functions at code addresses that are not yet function entries.
// Rows from files whose name starts with "dgroup" are always data labels (never functions), so a data
// segment that the loader marked executable (the DOS EXE's DGROUP) is not disassembled by accident.
// A signature (third column) containing _Noreturn marks the function non-returning; those rows are applied
// first, so that disassembly from the other named entries already stops after calls to them.
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
        // rows in file order; _Noreturn functions first
        List<String[]> first = new ArrayList<>(), rest = new ArrayList<>();
        List<Boolean> firstData = new ArrayList<>(), restData = new ArrayList<>();
        for (File f : files) {
            boolean dataOnly = f.getName().startsWith("dgroup");
            for (String line : Files.readAllLines(f.toPath())) {
                String[] p = line.split("\t");
                boolean nr = !dataOnly && p.length > 2 && p[2].contains("_Noreturn");
                (nr ? first : rest).add(p);
                (nr ? firstData : restData).add(dataOnly);
            }
        }
        // _Noreturn functions: create them all with just their entry instruction and mark them before any body is
        // disassembled; the main loop below then disassembles from the entry's successors
        Set<Address> pending = new HashSet<>();
        for (String[] p : first) {
            Address a = p[0].matches("[0-9a-fA-F]{4}:[0-9a-fA-F]{4}") ? af.getAddress(p[0]) : null;
            MemoryBlock b = a == null ? null : currentProgram.getMemory().getBlock(a);
            if (b == null || !b.isExecute() || getFunctionContaining(a) != null) continue;
            new DisassembleCommand(a, new AddressSet(a), false).applyTo(currentProgram, monitor);
            if (getInstructionAt(a) == null || !new CreateFunctionCmd(a).applyTo(currentProgram, monitor)) continue;
            created++;
            getFunctionAt(a).setNoReturn(true);
            pending.add(a);
        }
        for (Address a : pending) {
            Instruction ins = getInstructionAt(a);
            AddressSet next = new AddressSet();
            for (Address f : ins.getFlows()) next.add(f);
            if (ins.getFallThrough() != null) next.add(ins.getFallThrough());
            if (!next.isEmpty()) new DisassembleCommand(next, null, true).applyTo(currentProgram, monitor);
        }
        for (Address a : pending) CreateFunctionCmd.fixupFunctionBody(currentProgram, getFunctionAt(a), monitor);
        first.addAll(rest);
        firstData.addAll(restData);
        for (int i = 0; i < first.size(); i++) {
            String[] p = first.get(i);
            boolean dataOnly = firstData.get(i);
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
                    if (func != null) {
                        func.setName(name, SourceType.USER_DEFINED);
                        if (p.length > 2 && p[2].contains("_Noreturn")) func.setNoReturn(true);
                        fn++;
                        continue;
                    }
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
        println("functions named " + fn + " (created " + created + "), labels " + labels + ", stale labels removed " + stale + ", failed " + bad);
    }
}
