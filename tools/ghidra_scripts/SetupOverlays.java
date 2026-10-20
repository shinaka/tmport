// Prepare a flattened Borland VROOMM program (tools/gg/vroomm.py flatten) for analysis. Run as a preScript.
//  * creates one memory block per overlay from the file bytes after the MZ load module (overlays.tsv:
//    name, Ghidra segment, file offset, size, DS value); Ghidra's MZ loader would otherwise move the first
//    bytes of an overlay into the previous block when they contain a RETF;
//  * names and configures the resident blocks (blocks.tsv: Ghidra segment, name, kind, DS value or "-"):
//    kind dgroup + dgroup_part blocks are joined into one non-executable DGROUP block (the loader splits it at
//    relocation targets inside it), kinds data and stack become non-executable, a DS value is set as the DS
//    register context of the block (the overlay manager runs with its own data segment);
//  * disassembles each overlay stub entry (a JMP FAR written by the flattener) and its overlay target and
//    creates functions at both (entries.tsv: stub entry, target, label);
//  * creates functions at the addresses in the first column of any further TSV (e.g. farcode.tsv: far code
//    pointers that the relocation table shows in DGROUP);
//  * switches off "Decompiler Switch Analysis" (in segmented code it put Borland CS:[BX+T] table targets into
//    wrong segments and disassembled data; FixSwitches.java reads the tables instead).
// Usage: -preScript SetupOverlays.java <blocks.tsv> <entries.tsv> [<overlays.tsv> [<functions.tsv> ...]]
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.address.*;
import ghidra.program.model.lang.Register;
import ghidra.program.model.mem.*;
import java.math.BigInteger;
import java.nio.file.*;
import java.util.*;

public class SetupOverlays extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        setAnalysisOption(currentProgram, "Decompiler Switch Analysis", "false");
        Memory mem = currentProgram.getMemory();
        SegmentedAddressSpace sp = (SegmentedAddressSpace) currentProgram.getAddressFactory().getDefaultAddressSpace();
        Register dsR = currentProgram.getProgramContext().getRegister("ds");
        Register csR = currentProgram.getProgramContext().getRegister("cs");

        // blocks.tsv
        Map<Integer, String[]> rows = new LinkedHashMap<>();
        for (String line : Files.readAllLines(Paths.get(args[0]))) {
            String[] p = line.split("\t");
            if (p.length < 3 || !p[0].matches("[0-9a-fA-F]{4}")) continue;
            rows.put(Integer.parseInt(p[0], 16), p);
        }
        // segments without a block of their own (e.g. stubs referenced only from overlays): split at their start
        for (int seg : new TreeSet<>(rows.keySet())) {
            if (blockAtSeg(seg) != null) continue;
            Address a = sp.getAddress(seg, 0);
            MemoryBlock b = mem.getBlock(a);
            if (b == null || b.getStart().equals(a)) continue;
            try { mem.split(b, a); println("split block for segment " + hex(seg)); }
            catch (Exception e) { println("split failed " + hex(seg) + ": " + e); }
        }
        // overlays.tsv
        int nb = 0;
        if (args.length > 2) {
            ghidra.program.database.mem.FileBytes fb = mem.getAllFileBytes().get(0);
            for (String line : Files.readAllLines(Paths.get(args[2]))) {
                String[] p = line.split("\t");
                if (p.length < 4) continue;
                int seg = Integer.parseInt(p[1], 16);
                MemoryBlock b = mem.createInitializedBlock(p[0], sp.getAddress(seg, 0), fb,
                        Long.parseLong(p[2]), Long.parseLong(p[3]), false);
                b.setRead(true); b.setWrite(true); b.setExecute(true);
                b.setSourceName("VROOMM overlay (tools/gg/vroomm.py)");
                currentProgram.getProgramContext().setValue(csR, b.getStart(), b.getEnd(), BigInteger.valueOf(seg));
                if (p.length > 4 && p[4].matches("[0-9a-fA-F]{4}"))
                    currentProgram.getProgramContext().setValue(dsR, b.getStart(), b.getEnd(),
                            BigInteger.valueOf(Integer.parseInt(p[4], 16)));
                nb++;
            }
        }
        // names, flags and DS context of the resident blocks
        int renamed = 0;
        for (MemoryBlock b : mem.getBlocks()) {
            if (!(b.getStart() instanceof SegmentedAddress)) continue;
            String[] p = rows.get(((SegmentedAddress) b.getStart()).getSegment());
            if (p == null || p[2].equals("dgroup_part")) continue;
            String n = p[1].trim();
            if (!b.isInitialized()) n = n + "u";
            try { b.setName(n); renamed++; } catch (Exception e) { println("rename failed " + b.getName() + ": " + e); }
            if (p[2].equals("data") || p[2].equals("stack")) b.setExecute(false);
            if (p.length > 3 && p[3].matches("[0-9a-fA-F]{4}"))
                currentProgram.getProgramContext().setValue(dsR, b.getStart(), b.getEnd(),
                        BigInteger.valueOf(Integer.parseInt(p[3], 16)));
        }
        // DGROUP: join the parts back into one non-executable block so that DS:XXXX = dgroup:XXXX
        for (Map.Entry<Integer, String[]> e : rows.entrySet()) {
            if (!e.getValue()[2].equals("dgroup")) continue;
            MemoryBlock dg = blockAtSeg(e.getKey());
            for (Map.Entry<Integer, String[]> q : rows.entrySet()) {
                if (!q.getValue()[2].equals("dgroup_part")) continue;
                MemoryBlock part = blockAtSeg(q.getKey());
                if (dg == null || part == null) { println("DGROUP join: missing block " + hex(q.getKey())); continue; }
                dg = mem.join(dg, part);
            }
            if (dg != null) { dg.setName(e.getValue()[1].trim()); dg.setExecute(false); }
        }
        // stub entries and their targets
        AddressFactory af = currentProgram.getAddressFactory();
        int fn = createFunctions(af, args[1], true);
        int extra = 0;
        for (int i = 3; i < args.length; i++) extra += createFunctions(af, args[i], false);
        println("overlay blocks created " + nb + ", blocks renamed " + renamed + ", functions created " + fn
                + " (stub entries and targets) + " + extra + " (listed)");
    }

    // entries: columns 1 (target) then 0 (stub thunk); otherwise column 0 only
    private int createFunctions(AddressFactory af, String file, boolean entries) throws Exception {
        int fn = 0;
        for (String line : Files.readAllLines(Paths.get(file))) {
            String[] p = line.split("\t");
            if (p.length < 1 || !p[0].matches("[0-9a-fA-F]{4}:[0-9a-fA-F]{4}")) continue;
            for (int i = entries ? 1 : 0; i >= 0; i--) {
                Address a = af.getAddress(p[i]);
                if (a == null || getFunctionAt(a) != null) continue;
                new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
                if (new CreateFunctionCmd(a).applyTo(currentProgram, monitor)) fn++;
            }
        }
        return fn;
    }

    private MemoryBlock blockAtSeg(int seg) {
        for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
            if (b.getStart() instanceof SegmentedAddress && ((SegmentedAddress) b.getStart()).getSegment() == seg)
                return b;
        }
        return null;
    }

    private static String hex(int v) { return Integer.toHexString(v); }
}
