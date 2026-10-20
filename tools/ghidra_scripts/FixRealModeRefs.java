// Correct two artefacts of Ghidra's x86 real-mode language in programs whose segments are not 64-KB aligned
// (e.g. a flattened VROOMM program). The language computes both from the linear address (ia.sinc):
//   rel16     = page(inst_next) | ((inst_next + disp) & 0xFFFF)
//   currentCS = (inst_next >> 4) & 0xF000
//  1. A near CALL/JMP rel16 (E8/E9) whose source and target lie on different sides of a 64-KB linear boundary is
//     resolved into the wrong 64-KB page. The script replaces its flow references with one overriding reference to
//     CS:((IP + disp) & 0xFFFF) (CALL_OVERRIDE_UNCONDITIONAL / JUMP_OVERRIDE_UNCONDITIONAL, which the decompiler
//     honours), removes unnamed functions and the code flow that only the wrong targets created, disassembles the
//     real targets one instruction at a time (so further crossing branches are corrected before they are followed),
//     creates functions at the real call targets and recomputes the bodies of the functions in the affected blocks.
//  2. A memory operand with a CS: override is resolved with CS = the 64-KB page segment instead of the code
//     segment. The script moves those operand references to codeSegment:offset. This fixes the listing and the
//     cross-references; the decompiler's own arithmetic still uses the page segment.
// Idempotent; run it after analysis and again after scripts that disassemble more code. Headless re-analyses what
// post-scripts changed, and that re-creates page-based CS references, so run it once more with -noanalysis
// (tools/gg/ghidra_import.sh does).
// Usage: -postScript FixRealModeRefs.java [report.tsv]      (report rows are appended: near/removed/cleared/cs)
import ghidra.app.script.GhidraScript;
import ghidra.app.plugin.core.clear.ClearFlowAndRepairCmd;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class FixRealModeRefs extends GhidraScript {
    private SegmentedAddressSpace sp;
    private ReferenceManager rm;
    private Listing listing;
    private Memory mem;
    private PrintWriter rep;
    private final Set<Address> wrongTargets = new LinkedHashSet<>();
    private final Set<Address> callTargets = new LinkedHashSet<>();
    private final Deque<Address> work = new ArrayDeque<>();
    private final Set<Address> touched = new LinkedHashSet<>();
    private int nearFixed, csFixed, funcsRemoved, flowsCleared, funcsCreated, disassembled;

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        sp = (SegmentedAddressSpace) currentProgram.getAddressFactory().getDefaultAddressSpace();
        rm = currentProgram.getReferenceManager();
        listing = currentProgram.getListing();
        mem = currentProgram.getMemory();
        rep = args.length > 0 ? new PrintWriter(new FileWriter(args[0], true)) : null;
        List<MemoryBlock> straddling = new ArrayList<>();
        for (MemoryBlock b : mem.getBlocks()) {
            if (code(b) && (b.getStart().getOffset() >> 16) != (b.getEnd().getOffset() >> 16)) straddling.add(b);
        }
        for (int round = 0; round < 20; round++) {
            int before = nearFixed;
            for (MemoryBlock b : straddling) {
                for (Instruction ins : listing.getInstructions(new AddressSet(b.getStart(), b.getEnd()), true)) {
                    checkNear(b, ins);
                }
            }
            cleanup();
            for (MemoryBlock b : straddling) addDangling(b);
            drain();
            for (Address t : callTargets) {
                if (getFunctionAt(t) == null && listing.getInstructionAt(t) != null
                        && getFunctionContaining(t) == null) {
                    if (new CreateFunctionCmd(t).applyTo(currentProgram, monitor)) funcsCreated++;
                }
            }
            if (nearFixed == before) break;
        }
        Set<Function> fix = new LinkedHashSet<>();
        for (MemoryBlock b : straddling) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(new AddressSet(b.getStart(), b.getEnd()), true))
                fix.add(f);
        }
        for (Address a : touched) {
            Function f = getFunctionContaining(a);
            if (f != null) fix.add(f);
        }
        for (Function f : fix) CreateFunctionCmd.fixupFunctionBody(currentProgram, f, monitor);
        for (MemoryBlock b : mem.getBlocks()) {
            if (code(b)) fixCsRefs(b);
        }
        if (rep != null) rep.close();
        StringBuilder sb = new StringBuilder();
        for (MemoryBlock b : straddling) sb.append(b.getName()).append(' ');
        println("blocks straddling 64 KB: " + sb.toString().trim() + "; near branches fixed " + nearFixed
                + ", functions removed " + funcsRemoved + ", flows cleared " + flowsCleared + ", instructions disassembled "
                + disassembled + ", functions created " + funcsCreated + ", bodies recomputed " + fix.size()
                + "; CS-relative references fixed " + csFixed);
    }

    private boolean code(MemoryBlock b) {
        return b.isExecute() && b.isInitialized() && b.getStart() instanceof SegmentedAddress;
    }

    /** Same linear address (a segmented address can be written with different segments). */
    private static boolean same(Address a, Address b) {
        return a.getAddressSpace().equals(b.getAddressSpace()) && a.getOffset() == b.getOffset();
    }

    private int segOf(MemoryBlock b) {
        return ((SegmentedAddress) b.getStart()).getSegment();
    }

    /** Index of the opcode byte after prefixes, or -1 when an operand-size/address-size prefix is present. */
    private static int opcodeIndex(byte[] bytes) {
        int p = 0;
        while (p < bytes.length) {
            int c = bytes[p] & 0xff;
            if (c == 0x66 || c == 0x67) return -1;
            if (c == 0x26 || c == 0x2e || c == 0x36 || c == 0x3e || c == 0xf0 || c == 0xf2 || c == 0xf3) p++;
            else break;
        }
        return p < bytes.length ? p : -1;
    }

    private static boolean hasCsPrefix(byte[] bytes) {
        for (byte x : bytes) {
            int c = x & 0xff;
            if (c == 0x2e) return true;
            if (!(c == 0x26 || c == 0x36 || c == 0x3e || c == 0xf0 || c == 0xf2 || c == 0xf3 || c == 0x66 || c == 0x67)) return false;
        }
        return false;
    }

    /** Returns the real target of a near CALL/JMP rel16 (after fixing its references), or null for other instructions. */
    private Address checkNear(MemoryBlock b, Instruction ins) throws Exception {
        byte[] bytes;
        try { bytes = ins.getBytes(); } catch (MemoryAccessException e) { return null; }
        int p = opcodeIndex(bytes);
        if (p < 0) return null;
        int op = bytes[p] & 0xff;
        if ((op != 0xe8 && op != 0xe9) || bytes.length != p + 3) return null;
        int disp = (short) ((bytes[p + 1] & 0xff) | ((bytes[p + 2] & 0xff) << 8));
        int seg = segOf(b);
        long off = ins.getAddress().getOffset() - seg * 16L;
        Address correct = sp.getAddress(seg, (int) ((off + bytes.length + disp) & 0xffff));
        boolean ok = false, wrong = false;
        for (Reference r : ins.getReferencesFrom()) {
            if (!r.getReferenceType().isFlow()) continue;
            if (same(r.getToAddress(), correct)) ok = true; else wrong = true;
        }
        if (ok && !wrong) return correct;
        for (Reference r : ins.getReferencesFrom()) {
            if (!r.getReferenceType().isFlow()) continue;
            if (!same(r.getToAddress(), correct)) wrongTargets.add(r.getToAddress());
            rm.delete(r);
        }
        RefType t = op == 0xe8 ? RefType.CALL_OVERRIDE_UNCONDITIONAL : RefType.JUMP_OVERRIDE_UNCONDITIONAL;
        Reference nr = rm.addMemoryReference(ins.getAddress(), correct, t, SourceType.USER_DEFINED, 0);
        rm.setPrimary(nr, true);
        if (op == 0xe8) callTargets.add(correct);
        work.add(correct);
        touched.add(ins.getAddress());
        nearFixed++;
        if (rep != null) rep.printf("near\t%s\t%s\t%s%n", ins.getAddress(), op == 0xe8 ? "CALL" : "JMP", correct);
        return correct;
    }

    /** Removes unnamed functions and code flows that exist only because of wrong targets. */
    private void cleanup() throws Exception {
        List<Address> ws = new ArrayList<>(wrongTargets);
        wrongTargets.clear();
        for (Address w : ws) {
            if (!mem.contains(w)) continue;
            boolean referenced = false;
            for (Reference r : rm.getReferencesTo(w)) {
                if (r.getReferenceType().isFlow()) { referenced = true; break; }
            }
            if (referenced) continue;
            Function f = getFunctionAt(w);
            if (f != null) {
                if (f.getSymbol().getSource() != SourceType.DEFAULT) continue;   // named by an analyst: keep
                removeFunction(f);
                funcsRemoved++;
                if (rep != null) rep.printf("removed\t%s\tfunction\t-%n", w);
            }
            Instruction i = listing.getInstructionAt(w);
            if (i == null) continue;
            Instruction prev = listing.getInstructionBefore(w);
            if (prev != null && prev.getFallThrough() != null && same(w, prev.getFallThrough())) continue;  // real code
            new ClearFlowAndRepairCmd(w, false, false, false).applyTo(currentProgram, monitor);
            flowsCleared++;
            if (rep != null) rep.printf("cleared\t%s\tflow\t-%n", w);
        }
    }

    private void addDangling(MemoryBlock b) {
        for (Instruction ins : listing.getInstructions(new AddressSet(b.getStart(), b.getEnd()), true)) {
            for (Address f : ins.getFlows()) push(f);
            if (ins.hasFallthrough()) push(ins.getFallThrough());
        }
    }

    private void push(Address a) {
        if (a == null || !mem.contains(a)) return;
        MemoryBlock b = mem.getBlock(a);
        if (!code(b)) return;
        if (listing.getInstructionContaining(a) != null || listing.getDefinedDataContaining(a) != null) return;
        work.add(a);
    }

    /** Disassembles the work list one instruction at a time, following corrected flows. */
    private void drain() throws Exception {
        while (!work.isEmpty() && !monitor.isCancelled()) {
            Address a = work.poll();
            if (!mem.contains(a) || !code(mem.getBlock(a))) continue;
            if (listing.getInstructionContaining(a) != null || listing.getDefinedDataContaining(a) != null) continue;
            new DisassembleCommand(a, new AddressSet(a), false).applyTo(currentProgram, monitor);
            Instruction ins = listing.getInstructionAt(a);
            if (ins == null) continue;
            disassembled++;
            Address near = checkNear(mem.getBlock(a), ins);
            if (near != null) push(near);
            else for (Address f : ins.getFlows()) push(f);
            if (ins.hasFallthrough()) push(ins.getFallThrough());
        }
    }

    private void fixCsRefs(MemoryBlock b) throws Exception {
        int seg = segOf(b);
        for (Instruction ins : listing.getInstructions(new AddressSet(b.getStart(), b.getEnd()), true)) {
            byte[] bytes;
            try { bytes = ins.getBytes(); } catch (MemoryAccessException e) { continue; }
            if (!hasCsPrefix(bytes)) continue;
            long page = ((ins.getAddress().getOffset() + ins.getLength()) >> 4) & 0xf000;
            if (page == seg) continue;
            for (Reference r : ins.getReferencesFrom()) {
                // Ghidra's own references (also those through constant-propagated registers, e.g. CS:[BX]) are
                // page-based; references this script added are USER_DEFINED and skipped, so a re-run changes nothing
                if (!r.isMemoryReference() || r.getReferenceType().isFlow() || r.getSource() == SourceType.USER_DEFINED) continue;
                int opi = r.getOperandIndex();
                if (opi < 0 || !ins.getDefaultOperandRepresentation(opi).contains("CS:")) continue;
                long disp = r.getToAddress().getOffset() - page * 16;
                if (disp < 0 || disp > 0xffff) continue;
                Address to = sp.getAddress(seg, (int) disp);
                if (same(to, r.getToAddress())) continue;
                boolean primary = r.isPrimary();
                RefType t = r.getReferenceType();
                rm.delete(r);
                Reference nr = rm.addMemoryReference(ins.getAddress(), to, t, SourceType.USER_DEFINED, opi);
                if (primary) rm.setPrimary(nr, true);
                csFixed++;
                if (rep != null) rep.printf("cs\t%s\t%s\t%s%n", ins.getAddress(), r.getToAddress(), to);
            }
        }
    }
}
