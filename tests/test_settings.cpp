// tmport.ini, paths and the one-time users.dat import (save/settings.h). Temporary directories only.
#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include "mischief/util/file_util.h"
#include "platform/engine_setup.h"
#include "save/settings.h"
#include "save/user_record.h"

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
    return mischief::writeWholeFile(p, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(s.data()), s.size()));
}

}  // namespace

TEST_CASE("settings: defaults, round trip, bad values") {
    const EngineSettings d = EngineSettings::fromIni(mischief::save::IniFile{});
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
    mischief::save::IniFile ini;
    s.toIni(ini);
    CHECK(EngineSettings::fromIni(mischief::save::IniFile::parse(ini.text())) == s);

    const EngineSettings bad = EngineSettings::fromIni(mischief::save::IniFile::parse(
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
    REQUIRE(mischief::writeWholeFile(source, bytes));
    CHECK(importOriginalUsersDat(s, source, dest) == ImportResult::Imported);
    CHECK(s.importedOriginal);
    CHECK(mischief::readWholeFile(dest) == bytes);  // byte for byte
    CHECK(importOriginalUsersDat(s, source, dest) == ImportResult::AlreadyDone);

    EngineSettings fresh;
    CHECK(importOriginalUsersDat(fresh, source, dest) == ImportResult::DestExists);
    CHECK(fresh.importedOriginal);

    // loadSettingsAndImport records the flag in tmport.ini (and keeps the user's other lines).
    const fs::path pref = dir / "pref2";
    const Paths paths = Paths::under(pref, dir / "noexe");
    REQUIRE(writeText(paths.iniPath, "; mine\n[saves]\nimport_from = " + mischief::pathToUtf8(source) + "\n"));
    ImportResult r{};
    const EngineSettings loaded = loadSettingsAndImport(paths, &r);
    CHECK(r == ImportResult::Imported);
    CHECK(loaded.importedOriginal);
    CHECK(mischief::readWholeFile(paths.usersDat) == bytes);
    const auto ini = mischief::save::IniFile::load(paths.iniPath);
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
    // [debug]: show_treasure starts with the boxes on and enables F9; keys only enables F9.
    CHECK_FALSE(c.debugKeys);
    CHECK_FALSE(c.showTreasure);
    const EngineSettings dbg = EngineSettings::fromIni(mischief::save::IniFile::parse("[debug]\nshow_treasure = true\n"));
    CHECK(dbg.showTreasure);
    CHECK(engineConfigFromSettings(dbg).showTreasure);
    CHECK(engineConfigFromSettings(dbg).debugKeys);
    const EngineSettings keys = EngineSettings::fromIni(mischief::save::IniFile::parse("[DEBUG]\nKeys = true\n"));
    CHECK(engineConfigFromSettings(keys).debugKeys);
    CHECK_FALSE(engineConfigFromSettings(keys).showTreasure);
    CHECK(parseInputMode("first-event", InputMode::Lossless) == InputMode::FirstEventPerDrain);
    CHECK(parseInputMode("bogus", InputMode::FirstEventPerDrain) == InputMode::FirstEventPerDrain);
    // A missing explicit ini gives the defaults.
    CHECK(loadEngineSettings(tempDir("noini") / "tmport.ini") == EngineSettings{});
}

TEST_CASE("settings: [engine] edition and the [dos] section") {
    const EngineSettings d = EngineSettings::fromIni(mischief::save::IniFile{});
    CHECK_FALSE(d.edition);
    CHECK(d.dosDataPath.empty());
    CHECK(d.dos == dos::DosSettings{});
    CHECK(d.dos.look == dos::Look::Ega);
    CHECK(d.dos.sound == dos::SoundDevice::Tandy);
    CHECK_FALSE(d.dos.slowCpu);
    CHECK(d.dos.frameSpeed == dos::FrameSpeed::Reference);
    CHECK_FALSE(d.dos.squarePixels);
    CHECK(d.dos.windowScale == 3);

    const EngineSettings s = EngineSettings::fromIni(mischief::save::IniFile::parse(
        "[engine]\nedition = DOS\n"
        "[dos]\ndata = D:\\TM-DOS\nlook = Tandy\nsound = speaker\nslow_cpu = yes\nframe_speed = period\n"
        "square_pixels = true\nwindow_scale = 5\n"));
    CHECK(s.edition == Edition::Dos1990);
    CHECK(s.dosDataPath == "D:\\TM-DOS");
    CHECK(s.dos.look == dos::Look::Tandy);
    CHECK(s.dos.sound == dos::SoundDevice::Speaker);
    CHECK(s.dos.slowCpu);
    CHECK(s.dos.frameSpeed == dos::FrameSpeed::Period);
    CHECK(s.dos.squarePixels);
    CHECK(s.dos.windowScale == 5);
    // The Windows keys are not touched by the new ones.
    CHECK(s.windowScale == 2);
    CHECK(EngineSettings::fromIni(mischief::save::IniFile::parse("[engine]\nedition = win\n")).edition == Edition::WinCd);
    CHECK_FALSE(EngineSettings::fromIni(mischief::save::IniFile::parse("[engine]\nedition = auto\n")).edition);

    // Round trip.
    mischief::save::IniFile ini;
    s.toIni(ini);
    CHECK(EngineSettings::fromIni(mischief::save::IniFile::parse(ini.text())) == s);
    EngineSettings autoEd = s;
    autoEd.edition.reset();
    mischief::save::IniFile ini2;
    autoEd.toIni(ini2);
    CHECK(ini2.get("engine", "edition") == "auto");
    CHECK(EngineSettings::fromIni(mischief::save::IniFile::parse(ini2.text())) == autoEd);

    // Bad values keep their defaults.
    const EngineSettings bad = EngineSettings::fromIni(mischief::save::IniFile::parse(
        "[engine]\nedition = amiga\n[dos]\nlook = svga\nsound = gus\nslow_cpu = sometimes\nframe_speed = turbo\n"
        "window_scale = 0\n"));
    CHECK_FALSE(bad.edition);
    CHECK(bad.dos == dos::DosSettings{});
}
