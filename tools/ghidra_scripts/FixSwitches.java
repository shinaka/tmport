// Resolve Borland C++ switch jumps in real-mode code by reading their tables (run after analysis, with the decompiler
// switch analyzer switched off: in segmented code it put targets in the wrong segment and disassembled data).
//  dense:  CMP BX,n ; JA default ; SHL BX,1 ; JMP word ptr CS:[BX+T]     (2E FF A7 lo hi)   n+1 targets at CS:T
//  sparse: MOV BX,T ... MOV AX,CS:[BX] ; CMP ; JZ ... ; JMP word ptr CS:[BX+2n] (2E FF 67 d8) n keys at CS:T, targets at CS:T+2n
// Adds COMPUTED_JUMP references, disassembles the targets and fixes the containing function bodies.
// Usage: -postScript FixSwitches.java [report.tsv]
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class FixSwitches extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        PrintWriter rep = args.length > 0 ? new PrintWriter(new FileWriter(args[0])) : null;
        if (rep != null) rep.println("jump\tkind\tcount\ttable\ttargets");
        Memory mem = currentProgram.getMemory();
        ReferenceManager rm = currentProgram.getReferenceManager();
        Set<Function> touched = new HashSet<>();
        int nsw = 0, nt = 0, bad = 0;
        // scan raw bytes (some jumps may not be disassembled yet)
        for (MemoryBlock b : mem.getBlocks()) {
            if (!b.isExecute() || !b.isInitialized()) continue;
            int size = (int) b.getSize();
            byte[] buf = new byte[size];
            b.getBytes(b.getStart(), buf);
            SegmentedAddress start = (SegmentedAddress) b.getStart();
            int seg = start.getSegment();
            int base = start.getSegmentOffset();
            for (int i = 0; i + 4 < size; i++) {
                if ((buf[i] & 0xff) != 0x2e || (buf[i + 1] & 0xff) != 0xff) continue;
                int modrm = buf[i + 2] & 0xff;
                List<Integer> targets = new ArrayList<>();
                String kind;
                int table;
                if (modrm == 0xa7) {
                    kind = "dense";
                    table = (buf[i + 3] & 0xff) | ((buf[i + 4] & 0xff) << 8);
                    int n = -1;
                    for (int k = i - 3; k >= Math.max(0, i - 16); k--) {
                        if ((buf[k] & 0xff) == 0x83 && (buf[k + 1] & 0xff) == 0xfb) { n = buf[k + 2] & 0xff; break; }
                        if ((buf[k] & 0xff) == 0x81 && (buf[k + 1] & 0xff) == 0xfb) { n = (buf[k + 2] & 0xff) | ((buf[k + 3] & 0xff) << 8); break; }
                    }
                    if (n < 0 || n > 256) { if (rep != null) rep.printf("%04x:%04x\tdense\t?\t%04x\tno bound%n", seg, base + i, table); bad++; continue; }
                    for (int k = 0; k <= n; k++) targets.add(readWord(seg, table + 2 * k));
                } else if (modrm == 0x67) {
                    kind = "sparse";
                    int disp = buf[i + 3] & 0xff;
                    int n = disp / 2;
                    table = -1;
                    for (int k = i - 3; k >= Math.max(0, i - 40); k--) {
                        if ((buf[k] & 0xff) == 0xbb && k + 6 < size && (buf[k + 3] & 0xff) == 0x2e && (buf[k + 4] & 0xff) == 0x8b) {
                            table = (buf[k + 1] & 0xff) | ((buf[k + 2] & 0xff) << 8);
                            break;
                        }
                    }
                    if (table < 0 || n == 0) { if (rep != null) rep.printf("%04x:%04x\tsparse\t%d\t?\tno table%n", seg, base + i, n); bad++; continue; }
                    for (int k = 0; k < n; k++) targets.add(readWord(seg, table + disp + 2 * k));
                } else continue;
                Address jmp = addr(seg, base + i);
                // sanity: all targets inside this block
                boolean ok = true;
                for (int t : targets) {
                    Address ta = addr(seg, t);
                    if (!b.contains(ta)) ok = false;
                }
                if (!ok) { if (rep != null) rep.printf("%04x:%04x\t%s\t%d\t%04x\ttarget outside block%n", seg, base + i, kind, targets.size(), table); bad++; continue; }
                Instruction ins = getInstructionAt(jmp);
                if (ins == null) {
                    new DisassembleCommand(jmp, null, true).applyTo(currentProgram, monitor);
                    ins = getInstructionAt(jmp);
                }
                if (ins == null) { bad++; continue; }
                for (Reference r : ins.getReferencesFrom()) {
                    if (r.getReferenceType().isFlow() || r.getReferenceType().isComputed()) rm.delete(r);
                }
                StringBuilder sb = new StringBuilder();
                Set<Integer> seen = new TreeSet<>();
                for (int t : targets) {
                    Address ta = addr(seg, t);
                    rm.addMemoryReference(jmp, ta, RefType.COMPUTED_JUMP, SourceType.USER_DEFINED, CodeUnit.MNEMONIC);
                    if (seen.add(t)) {
                        new DisassembleCommand(ta, null, true).applyTo(currentProgram, monitor);
                        nt++;
                    }
                    sb.append(String.format("%04x,", t));
                }
                // keep the table bytes as data words
                try {
                    Address ta0 = addr(seg, table);
                    int words = kind.equals("dense") ? targets.size() : 2 * targets.size();
                    clearListing(ta0, ta0.add(2L * words - 1));
                    for (int k = 0; k < words; k++) createWord(ta0.add(2L * k));
                    createLabel(ta0, String.format("switchtab_%04x_%04x", seg, base + i), true);
                } catch (Exception e) { /* table overlaps code: leave it */ }
                Function f = getFunctionContaining(jmp);
                if (f != null) touched.add(f);
                nsw++;
                if (rep != null) rep.printf("%04x:%04x\t%s\t%d\t%04x\t%s%n", seg, base + i, kind, targets.size(), table, sb);
            }
        }
        for (Function f : touched) CreateFunctionCmd.fixupFunctionBody(currentProgram, f, monitor);
        if (rep != null) rep.close();
        println("switches resolved " + nsw + ", targets " + nt + ", unresolved " + bad + ", functions fixed " + touched.size());
    }

    private Address addr(int seg, int off) {
        SegmentedAddressSpace sp = (SegmentedAddressSpace) currentProgram.getAddressFactory().getDefaultAddressSpace();
        return sp.getAddress(seg, off & 0xffff);
    }

    private int readWord(int seg, int off) throws Exception {
        Address a = addr(seg, off);
        return (getByte(a) & 0xff) | ((getByte(a.add(1)) & 0xff) << 8);
    }
}
