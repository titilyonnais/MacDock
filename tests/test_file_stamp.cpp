// Relecture de la nuit (plan 69) : le Dock ne recharge sa config que si ses propres fichiers ont changé.
#include <windows.h>

#include "minitest.h"
#include "test_helpers.h"
#include "../src/config/file_stamp.h"

TEST_CASE(file_stamp_follows_its_own_file_only) {
    const std::wstring dir = md::testTempDir();
    const std::wstring settings = dir + L"\\settings.json", recents = dir + L"\\menubar-recent.json";
    CHECK(!md::fileStamp(settings).exists);   // absent
    md::testWriteFile(settings, "{\"a\":1}");
    const md::FileStamp first = md::fileStamp(settings);
    CHECK(first.exists);
    CHECK(first.size == 7);
    CHECK(md::fileStamp(settings) == first);   // rien n'a bougé
    md::testWriteFile(recents, "[1,2,3]");     // la barre écrit ses récents à côté
    CHECK(md::fileStamp(settings) == first);
    Sleep(20);
    md::testWriteFile(settings, "{\"a\":2}");   // même taille, nouvelle écriture
    CHECK(!(md::fileStamp(settings) == first));
    DeleteFileW(settings.c_str());
    CHECK(!md::fileStamp(settings).exists);
}

TEST_CASE(config_watch_sees_a_write_during_the_read) {
    // Relecture du plan 69 : l'empreinte était prise après la lecture ; une écriture des Réglages tombée entre les deux
    // entrait dans l'empreinte sans entrer dans les réglages, et le Dock gardait l'avant-dernière valeur.
    const std::wstring dir = md::testTempDir();
    const std::wstring settings = dir + L"\\settings.json";
    md::testWriteFile(settings, "{\"a\":1}");
    md::ConfigWatch watch({settings});
    CHECK(watch.changed());   // jamais lu
    watch.read([] {});
    CHECK(!watch.changed());   // rien n'a bougé depuis la lecture
    watch.read([&] {
        Sleep(20);
        md::testWriteFile(settings, "{\"a\":2}");   // l'app Réglages écrit pendant la lecture
    });
    CHECK(watch.changed());    // la relecture suivante la prendra
}
