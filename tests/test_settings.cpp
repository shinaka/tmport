// tmport.ini, paths and the one-time users.dat import (save/settings.h). Temporary directories only.
#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include "platform/engine_setup.h"
#include "save/settings.h"
#include "save/user_record.h"
#include "util/file_util.h"

using namespace tmport;
using namespace tmport::save;

namespace {

namespace fs = std::filesystem;

fs::path tempDir(const char* name) {
    const fs::path d = fs::temp_directory_path() / "tmport_tests" / name;
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return d;
}

bool writeText(const fs::path& p, const std::string& s) {
    return writeWholeFile(p, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(s.data()), s.size()));
}

}  // namespace

TEST_CASE("ini: parse, get, set keeps comments, order and unknown keys") {
    IniFile ini = IniFile::parse(
        "; TM Port settings\r\n"
        "[Engine]\r\n"
        "data = D:\\Games\\TMTWINCD ; not a comment\r\n"
        "window_scale=3\r\n"
        "\r\n"
        "[custom]\r\n"
        "foo = bar\r\n");
    CHECK(ini.get("engine", "DATA") == "D:\\Games\\TMTWINCD ; not a comment");
    CHECK(ini.get("ENGINE", "window_scale") == "3");
    CHECK(ini.get("engine", "missing") == std::nullopt);
    ini.set("engine", "window_scale", "2");
    ini.set("engine", "integer_scale", "true");
    ini.set("input", "mode", "first-event");
    const std::string t = ini.text();
    CHECK(t ==
          "; TM Port settings\n"
          "[Engine]\n"
          "data = D:\\Games\\TMTWINCD ; not a comment\n"
          "window_scale = 2\n"
          "integer_scale = true\n"
          "\n"
          "[custom]\n"
          "foo = bar\n"
          "\n"
          "[input]\n"
          "mode = first-event\n");
    IniFile again = IniFile::parse(t);
    CHECK(again.get("input", "mode") == "first-event");
    CHECK(again.get("custom", "foo") == "bar");
}

TEST_CASE("settings: defaults, round trip, bad values") {
    const EngineSettings d = EngineSettings::fromIni(IniFile{});
    CHECK(d == EngineSettings{});
    CHECK(d.inputMode == InputModeSetting::Lossless);
    CHECK(d.pauseOnFocusLoss);
    CHECK(d.midiSet == MidiSet::Extended);

    EngineSettings s;
    s.dataPath = "C:\\Data\\TM.cue";
    s.windowScale = 3;
    s.integerScale = true;
    s.fullscreen = true;
    s.pauseOnFocusLoss = false;
    s.cursorScale = 2;
    s.inputMode = InputModeSetting::FirstEvent;
    s.tapLatch = true;
    s.midiSet = MidiSet::Base;
    s.midiBackend = MidiBackend::Null;
    s.importedOriginal = true;
    s.importFrom = "E:\\old\\users.dat";
    IniFile ini;
    s.toIni(ini);
    CHECK(EngineSettings::fromIni(IniFile::parse(ini.text())) == s);

    const EngineSettings bad = EngineSettings::fromIni(IniFile::parse(
        "[engine]\nwindow_scale = huge\npause_on_focus_loss = maybe\ncursor_scale = 99\n[input]\nmode = ???\n"
        "[audio]\nmidi_set = b\nmidi_backend = WinMM\n"));
    CHECK(bad.windowScale == 2);
    CHECK(bad.pauseOnFocusLoss);
    CHECK(bad.cursorScale == 0);
    CHECK(bad.inputMode == InputModeSetting::Lossless);
    CHECK(bad.midiSet == MidiSet::Base);
    CHECK(bad.midiBackend == MidiBackend::WinMM);
}

TEST_CASE("paths: layout under explicit directories") {
    const fs::path dir = tempDir("paths");
    const fs::path pref = dir / "pref", exe = dir / "exe";
    fs::create_directories(exe);
    Paths p = Paths::under(pref, exe);
    CHECK(p.iniPath == pref / "tmport.ini");
    CHECK(p.usersDat == pref / "users.dat");
    CHECK(p.screenshotDir == pref / "screenshots");
    REQUIRE(writeText(exe / "tmport.ini", "[engine]\n"));
    p = Paths::under(pref, exe);
    CHECK(p.iniPath == exe / "tmport.ini");  // a portable ini next to the executable wins
}

TEST_CASE("settings: one-time import of the original users.dat") {
    const fs::path dir = tempDir("import");
    const fs::path source = dir / "TLCWIN" / "TMTWINCD" / "users.dat";
    const fs::path dest = dir / "pref" / "users.dat";
    EngineSettings s;
    CHECK(importOriginalUsersDat(s, source, dest) == ImportResult::SourceMissing);
    CHECK_FALSE(s.importedOriginal);  // tried again next start

    std::vector<uint8_t> bytes(kFileSize);
    for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<uint8_t>(i * 31);
    REQUIRE(writeWholeFile(source, bytes));
    CHECK(importOriginalUsersDat(s, source, dest) == ImportResult::Imported);
    CHECK(s.importedOriginal);
    CHECK(readWholeFile(dest) == bytes);  // byte for byte
    CHECK(importOriginalUsersDat(s, source, dest) == ImportResult::AlreadyDone);

    EngineSettings fresh;
    CHECK(importOriginalUsersDat(fresh, source, dest) == ImportResult::DestExists);
    CHECK(fresh.importedOriginal);

    // loadSettingsAndImport records the flag in tmport.ini (and keeps the user's other lines).
    const fs::path pref = dir / "pref2";
    const Paths paths = Paths::under(pref, dir / "noexe");
    REQUIRE(writeText(paths.iniPath, "; mine\n[saves]\nimport_from = " + pathToUtf8(source) + "\n"));
    ImportResult r{};
    const EngineSettings loaded = loadSettingsAndImport(paths, &r);
    CHECK(r == ImportResult::Imported);
    CHECK(loaded.importedOriginal);
    CHECK(readWholeFile(paths.usersDat) == bytes);
    const auto ini = IniFile::load(paths.iniPath);
    REQUIRE(ini);
    CHECK(ini->get("saves", "imported_original") == "true");
    CHECK(ini->text().rfind("; mine\n", 0) == 0);
    // Next start: nothing to do.
    loadSettingsAndImport(paths, &r);
    CHECK(r == ImportResult::AlreadyDone);
}

TEST_CASE("settings -> EngineConfig") {
    EngineSettings s;
    s.windowScale = 3;
    s.integerScale = true;
    s.pauseOnFocusLoss = false;
    s.cursorScale = 1;
    s.inputMode = InputModeSetting::FirstEvent;
    s.tapLatch = true;
    const EngineConfig c = engineConfigFromSettings(s);
    CHECK(c.windowScale == 3);
    CHECK(c.integerScale);
    CHECK_FALSE(c.pauseOnFocusLoss);
    CHECK(c.cursorScale == 1);
    CHECK(c.inputMode == InputMode::FirstEventPerDrain);
    CHECK(c.tapLatch);
    CHECK(engineConfigFromSettings(EngineSettings{}).inputMode == InputMode::Lossless);
    CHECK(parseInputMode("first-event", InputMode::Lossless) == InputMode::FirstEventPerDrain);
    CHECK(parseInputMode("bogus", InputMode::FirstEventPerDrain) == InputMode::FirstEventPerDrain);
    // A missing explicit ini gives the defaults.
    CHECK(loadEngineSettings(tempDir("noini") / "tmport.ini") == EngineSettings{});
}
