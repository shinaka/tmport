#include "app/game_main.h"

#include "mischief/util/log.h"

namespace tmport::app {

void runRestartPoint(const std::function<void(int restartValue)>& body) {
    int r = 0;  // setjmp returns 0 the first time
    for (;;) {
        try {
            body(r);
            return;
        } catch (const RestartToSignIn&) {
            r = 1;  // longjmp(DS:5eda, 1)
        } catch (const QuitGame&) {
            return;  // longjmp(DS:5eda, 2) -> shutdown
        }
    }
}

int runGameMain(AppContext& ctx, const GameMainFn& entry) {
    if (!entry) return 0;
    for (;;) {
        try {
            entry(ctx);
            return 0;
        } catch (const QuitGame&) {
            return 0;
        } catch (const RestartToSignIn&) {
            mischief::log::warn("restart requested outside the game's restart point: starting over");
        }
    }
}

}  // namespace tmport::app
