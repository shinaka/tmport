// Create functions at Borland/Win16 prologues in code not yet covered by any function.
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;

public class FindMoreFunctions extends GhidraScript {
    // Patterns: mov ax,ds; nop; inc bp; push bp; mov bp,sp  (exported far)
    //           inc bp; push bp; mov bp,sp                   (far, Windows prologue)
    //           push bp; mov bp,sp                           (near/far)
    static final byte[][] PATS = {
        {(byte)0x8c, (byte)0xd8, (byte)0x90, (byte)0x45, (byte)0x55, (byte)0x8b, (byte)0xec},
        {(byte)0x1e, (byte)0x58, (byte)0x90, (byte)0x45, (byte)0x55, (byte)0x8b, (byte)0xec},
        {(byte)0x45, (byte)0x55, (byte)0x8b, (byte)0xec},
        {(byte)0x55, (byte)0x8b, (byte)0xec},
    };

    @Override
    public void run() throws Exception {
        Memory mem = currentProgram.getMemory();
        Listing listing = currentProgram.getListing();
        int created = 0;
        for (int pass = 0; pass < 3; pass++) {
            int before = created;
            for (MemoryBlock b : mem.getBlocks()) {
                if (!b.isExecute()) continue;
                long size = b.getSize();
                byte[] buf = new byte[(int) size];
                b.getBytes(b.getStart(), buf);
                for (int i = 0; i < size; i++) {
                    for (byte[] p : PATS) {
                        if (i + p.length > size) continue;
                        boolean m = true;
                        for (int k = 0; k < p.length; k++) if (buf[i + k] != p[k]) { m = false; break; }
                        if (!m) continue;
                        Address a = b.getStart().add(i);
                        if (getFunctionContaining(a) != null) break;
                        Instruction ins = listing.getInstructionContaining(a);
                        if (ins != null && !ins.getAddress().equals(a)) break;
                        // Only accept if previous byte ends a function-ish (ret/retf/jmp) or is padding.
                        if (i > 0) {
                            int prev = buf[i - 1] & 0xff;
                            boolean ok = prev == 0xc3 || prev == 0xcb || prev == 0x00 || prev == 0x90 || prev == 0xcc;
                            if (i > 2) {
                                int p3 = buf[i - 3] & 0xff;
                                if (p3 == 0xc2 || p3 == 0xca) ok = true;  // ret n / retf n
                            }
                            if (i > 1 && (buf[i - 2] & 0xff) == 0xeb) ok = true;
                            if (!ok) break;
                        }
                        new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
                        CreateFunctionCmd cmd = new CreateFunctionCmd(a);
                        if (cmd.applyTo(currentProgram, monitor)) created++;
                        break;
                    }
                }
            }
            println("pass " + pass + ": created " + (created - before));
            if (created == before) break;
        }
        // Report coverage
        long code = 0, covered = 0;
        for (MemoryBlock b : mem.getBlocks()) {
            if (!b.isExecute()) continue;
            code += b.getSize();
            AddressSet s = new AddressSet(b.getStart(), b.getEnd());
            for (Function f : currentProgram.getFunctionManager().getFunctions(s, true)) covered += f.getBody().getNumAddresses();
        }
        println("created " + created + " functions; coverage " + covered + "/" + code);
    }
}
