// Define strings at the addresses listed in a TSV (first column SSSS:OOOO; header lines skipped) and on the command
// line; a string ends at its NUL or at the next instruction (strings embedded in code without a NUL),
// and remove functions/instructions that analysis left in non-executable blocks (DGROUP, overlay manager data).
// Usage: -postScript DefineStrings.java <strings.tsv> [<extra address> ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import java.nio.file.*;
import java.util.*;

public class DefineStrings extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        // 1. cleanup of non-executable blocks
        int delf = 0;
        for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
            if (b.isExecute() || !b.isInitialized()) continue;
            AddressSet s = new AddressSet(b.getStart(), b.getEnd());
            List<Function> fs = new ArrayList<>();
            for (Function f : currentProgram.getFunctionManager().getFunctions(s, true)) fs.add(f);
            for (Function f : fs) { currentProgram.getFunctionManager().removeFunction(f.getEntryPoint()); delf++; }
            InstructionIterator ii = currentProgram.getListing().getInstructions(s, true);
            List<Address> ins = new ArrayList<>();
            while (ii.hasNext()) ins.add(ii.next().getAddress());
            for (Address a : ins) {
                Instruction i = getInstructionAt(a);
                if (i != null) clearListing(i.getMinAddress(), i.getMaxAddress());
            }
        }
        // 2. strings
        List<String> addrs = new ArrayList<>();
        for (String line : Files.readAllLines(Paths.get(args[0]))) {
            String a = line.split("\t")[0];
            if (a.matches("[0-9a-fA-F]{4}:[0-9a-fA-F]{4}")) addrs.add(a);
        }
        for (int i = 1; i < args.length; i++) addrs.add(args[i]);
        int ok = 0, bad = 0;
        AddressFactory af = currentProgram.getAddressFactory();
        for (String s : addrs) {
            Address a = af.getAddress(s);
            try {
                // a string ends at its NUL or, for strings embedded in code without one, at the next instruction
                int len = 0;
                while (len < 4096 && getByte(a.add(len)) != 0 && getInstructionContaining(a.add(len)) == null) len++;
                boolean terminated = getByte(a.add(len)) == 0 && getInstructionContaining(a.add(len)) == null;
                Data d = getDataAt(a);
                if (d != null && d.hasStringValue()) { ok++; continue; }
                if (len == 0 || getInstructionContaining(a) != null) { bad++; continue; }
                if (terminated) {
                    clearListing(a, a.add(len));
                    createAsciiString(a);
                } else {
                    clearListing(a, a.add(len - 1));
                    createAsciiString(a, len);
                }
                ok++;
            } catch (Exception e) {
                bad++;
                println("string at " + s + ": " + e.getMessage());
            }
        }
        println("removed " + delf + " functions in data blocks; strings defined " + ok + ", failed " + bad);
    }
}
