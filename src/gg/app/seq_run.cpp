#include "gg/app/seq_run.h"

#include <exception>

#include "util/log.h"

namespace tmport::gg {

int16_t seqRun(std::span<const StateFn> table, int16_t& state, int16_t prev) {
    // 12e0:000f Seq_Run
    int16_t r = 0;
    while (true) {
        if (state < 0 || static_cast<size_t>(state) >= table.size()) {
            log::error("Seq_Run: state {} is outside its table of {} entries", state, table.size());
            return 0;
        }
        const StateFn& f = table[static_cast<size_t>(state)];
        if (!f) return 0;
        r = f();
        if (r == -2) {
            state = prev;
        } else if (r == -1) {
            return r;
        } else if (r == 0) {
            prev = state;
            state = static_cast<int16_t>(state + 1);
        } else {
            prev = state;
            state = r;
        }
    }
}

void gameExit(int code) { throw GgExit(code); }

void ExitHandlers::run() {
    // __exit 1000:02c7: the atexit functions in reverse order of registration.
    while (!fns_.empty()) {
        auto fn = std::move(fns_.back());
        fns_.pop_back();
        try {
            if (fn) fn();
        } catch (const std::exception& e) {
            log::error("shutdown: {}", e.what());
        }
    }
}

}  // namespace tmport::gg
