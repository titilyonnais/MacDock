// Règles du dépôt de fichiers sur le Dock (spec 4.6).
#include "minitest.h"
#include "../src/interact/drop_rules.h"

using md::DropAction;
using md::DropTargetInfo;
using md::ItemKind;

namespace {
DropTargetInfo between() { return DropTargetInfo{ItemKind::App, true}; }
DropTargetInfo on(ItemKind k) { return DropTargetInfo{k, false}; }
} // namespace

TEST_CASE(drop_rules_pin_single_exe_between_pinned) {
    CHECK(md::dropAction(between(), {L"C:\\Tools\\x.exe"}) == DropAction::Pin);
    CHECK(md::dropAction(between(), {L"C:\\Users\\a\\Desktop\\Outil.lnk"}) == DropAction::Pin);
    CHECK(md::dropAction(between(), {L"C:\\Apps\\Outil.appref-ms"}) == DropAction::Pin);
}

TEST_CASE(drop_rules_folder_not_pinned) {
    CHECK(md::dropAction(between(), {L"C:\\Users\\a\\Documents"}) == DropAction::None);
    CHECK(md::dropAction(between(), {L"C:\\notes.txt"}) == DropAction::None);
}

TEST_CASE(drop_rules_two_exes_not_pinned) {
    CHECK(md::dropAction(between(), {L"C:\\a.exe", L"C:\\b.exe"}) == DropAction::None);
}

TEST_CASE(drop_rules_files_on_app_open_with) {
    CHECK(md::dropAction(on(ItemKind::App), {L"C:\\a.txt", L"C:\\b.txt"}) == DropAction::OpenWith);
    CHECK(md::dropAction(on(ItemKind::App), {L"C:\\x.exe"}) == DropAction::OpenWith);   // sur l'icône, pas entre deux
    CHECK(md::dropAction(on(ItemKind::AppsButton), {L"C:\\a.txt"}) == DropAction::None);
    CHECK(md::dropAction(on(ItemKind::MinimizedWindow), {L"C:\\a.txt"}) == DropAction::None);
    CHECK(md::dropAction(on(ItemKind::Separator), {L"C:\\a.txt"}) == DropAction::None);
}

TEST_CASE(drop_rules_on_trash_recycle) {
    CHECK(md::dropAction(on(ItemKind::Trash), {L"C:\\a.txt", L"C:\\Dossier"}) == DropAction::Recycle);
}

TEST_CASE(drop_rules_on_stack_move) {
    CHECK(md::dropAction(on(ItemKind::Stack), {L"C:\\a.txt"}) == DropAction::MoveInto);
}

TEST_CASE(drop_rules_empty_none) {
    CHECK(md::dropAction(between(), {}) == DropAction::None);
    CHECK(md::dropAction(on(ItemKind::Trash), {}) == DropAction::None);
    CHECK(md::dropAction(on(ItemKind::App), {}) == DropAction::None);
}

TEST_CASE(drop_rules_case_insensitive) {
    CHECK(md::isPinnableFile(L"C:\\X.EXE"));
    CHECK(md::isPinnableFile(L"C:\\Raccourci.LNK"));
    CHECK(md::isPinnableFile(L"C:\\a.AppRef-MS"));
    CHECK(!md::isPinnableFile(L"C:\\a.exe.txt"));
    CHECK(!md::isPinnableFile(L"C:\\exe"));
    CHECK(!md::isPinnableFile(L""));
}

TEST_CASE(drop_effect_respects_source) {
    const DWORD all = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
    CHECK_EQ(md::chooseEffect(DropAction::Pin, all), DWORD(DROPEFFECT_LINK));
    CHECK_EQ(md::chooseEffect(DropAction::OpenWith, all), DWORD(DROPEFFECT_COPY));
    CHECK_EQ(md::chooseEffect(DropAction::Recycle, all), DWORD(DROPEFFECT_MOVE));
    // Source qui n'autorise que la copie : jamais de déplacement ni de mise à la Corbeille.
    CHECK_EQ(md::chooseEffect(DropAction::Recycle, DROPEFFECT_COPY), DWORD(DROPEFFECT_NONE));
    CHECK_EQ(md::chooseEffect(DropAction::MoveInto, DROPEFFECT_COPY | DROPEFFECT_LINK), DWORD(DROPEFFECT_NONE));
    // Ouvrir ou épingler ne touche pas aux fichiers : copie ou lien suffisent, jamais MOVE.
    CHECK_EQ(md::chooseEffect(DropAction::Pin, DROPEFFECT_COPY), DWORD(DROPEFFECT_COPY));
    CHECK_EQ(md::chooseEffect(DropAction::OpenWith, DROPEFFECT_MOVE), DWORD(DROPEFFECT_NONE));
    CHECK_EQ(md::chooseEffect(DropAction::None, all), DWORD(DROPEFFECT_NONE));
}

TEST_CASE(drop_return_effect_never_move_when_dock_moves) {
    // Déplacement fait par la cible (plus tard) : la source ne doit pas supprimer l'original.
    CHECK_EQ(md::dropReturnEffect(DropAction::Recycle, DROPEFFECT_MOVE), DWORD(DROPEFFECT_NONE));
    CHECK_EQ(md::dropReturnEffect(DropAction::MoveInto, DROPEFFECT_MOVE), DWORD(DROPEFFECT_NONE));
    CHECK_EQ(md::dropReturnEffect(DropAction::OpenWith, DROPEFFECT_COPY), DWORD(DROPEFFECT_COPY));
    CHECK(md::dockPerformsMove(DropAction::Recycle));
    CHECK(!md::dockPerformsMove(DropAction::Pin));
}
