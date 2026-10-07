// Modèle des éléments du Dock (logique pure, sans Win32).
#pragma once
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "../config/settings.h"

namespace md {

using WindowId = std::uint64_t;

struct AppIdentity {
    std::wstring appId;        // clé de regroupement (voir makeAppId)
    std::wstring exePath;
    std::wstring aumid;
    std::wstring displayName;
    std::wstring launch;       // cible de relance (exe, .lnk, shell:AppsFolder\AUMID)
};

enum class ItemKind { App, AppsButton, Separator, Stack, MinimizedWindow, Trash };

struct DockItem {
    ItemKind kind = ItemKind::App;
    std::wstring key;          // "app:<appId>", "apps", "sep:1", "sep:2", "stack:<chemin>", "win:<id>", "trash"
    std::wstring appId, name, launch;
    bool pinned = false, running = false, recent = false;
    bool trashFull = false;          // Trash
    std::vector<WindowId> windows;   // App : fenêtres ouvertes
    WindowId window = 0;             // MinimizedWindow
};

// AUMID prioritaire ; sinon chemin de l'exécutable en minuscules.
std::wstring makeAppId(const std::wstring& aumid, const std::wstring& exePath);

class AppModel {
public:
    void loadPinned(const std::vector<PinnedEntry>& pins);
    std::vector<PinnedEntry> pinnedEntries() const { return pinned_; }
    void setShowRecents(bool show);

    void windowOpened(WindowId id, const AppIdentity& app);
    void windowClosed(WindowId id);
    void windowMinimized(WindowId id, bool minimized);
    void windowTitle(WindowId id, const std::wstring& title);
    // « Masquer » : les fenêtres réduites de l'app ne deviennent pas des tuiles. Levé dès qu'une
    // de ses fenêtres est restaurée ou ouverte, ou à la fermeture de l'app.
    void setHidden(const std::wstring& appId, bool hidden);
    bool isHidden(const std::wstring& appId) const;
    void setTrashFull(bool full);

    bool pin(const std::wstring& appId, std::size_t index);
    bool unpin(const std::wstring& key);
    bool movePinned(std::size_t from, std::size_t to);
    // Index dans pinnedEntries() de l'élément de clé key ("app:…", "apps", "stack:…") ; nullopt s'il n'est pas épinglé.
    std::optional<std::size_t> pinnedIndexOf(const std::wstring& key) const;

    std::vector<DockItem> items() const;
    std::vector<WindowId> windowsOf(const std::wstring& appId) const;
    std::optional<AppIdentity> identityOf(const std::wstring& appId) const;
    std::wstring titleOf(WindowId id) const;
    std::wstring appOfWindow(WindowId id) const;   // vide si inconnue
    std::uint64_t revision() const { return revision_; }

private:
    struct App {
        AppIdentity identity;
        std::vector<WindowId> windows;
        std::uint64_t openSeq = 0;
        bool hidden = false;
    };
    struct Window {
        std::wstring appId;
        std::wstring title;
        bool minimized = false;
        std::uint64_t minimizedSeq = 0;
    };

    bool isPinned(const std::wstring& appId) const;
    static std::wstring pinKey(const PinnedEntry& p);
    void touch() { ++revision_; }

    std::vector<PinnedEntry> pinned_;
    std::map<std::wstring, App> apps_;
    std::map<WindowId, Window> windows_;
    std::deque<AppIdentity> recents_;     // plus récent en tête
    bool showRecents_ = true;
    bool trashFull_ = false;
    std::uint64_t seq_ = 0;
    std::uint64_t revision_ = 1;
};

} // namespace md
