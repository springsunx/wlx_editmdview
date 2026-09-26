#pragma once

#include "document.hpp"
#include "editor.hpp"
#include "preview.hpp"
#include "session_state.hpp"

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace editmdview {

struct PreviewRenderWorker;

enum class ViewMode {
    Edit,
    Split,
    Preview,
};

struct DocumentViewState {
    EditorViewState editor;
    double previewScrollFraction = 0.0;
    double splitRatio = 0.5;
    ViewMode mode = ViewMode::Edit;
};

class AppWindow {
public:
    static bool register_class(HINSTANCE instance);
    static void unregister_classes(HINSTANCE instance) noexcept;
    static HWND create(HWND parent, HINSTANCE instance, const std::filesystem::path& path, int showFlags);
    static AppWindow* from(HWND window) noexcept;

    ~AppWindow();
    bool load_file(const std::filesystem::path& path);
    void close();
    bool search(const std::wstring& text, int flags);
    int send_command(int command, int parameter);

private:
    AppWindow(HINSTANCE instance, Document document);
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK editor_subclass_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR subclassId, DWORD_PTR referenceData);
    static LRESULT CALLBACK toolbar_subclass_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR subclassId, DWORD_PTR referenceData);
    static LRESULT CALLBACK status_subclass_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR subclassId, DWORD_PTR referenceData);
    static LRESULT CALLBACK divider_subclass_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR subclassId, DWORD_PTR referenceData);
    static LRESULT CALLBACK find_subclass_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR subclassId, DWORD_PTR referenceData);
    static LRESULT CALLBACK replace_window_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK replace_control_subclass_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR subclassId, DWORD_PTR referenceData);
    static LRESULT CALLBACK slash_window_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK slash_control_subclass_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR subclassId, DWORD_PTR referenceData);

    bool initialize(HWND window);
    LRESULT handle_message(UINT message, WPARAM wParam, LPARAM lParam);
    void layout();
    void restore_pending_editor_view_state();
    void set_mode(ViewMode mode);
    void refresh_preview(std::optional<double> initialScrollFraction = std::nullopt);
    void schedule_preview_sync(bool useCaretLine);
    void sync_preview_to_editor();
    void check_external_change();
    void check_configuration_change();
    void reload_configuration(bool automatic, bool announce = true);
    void update_status();
    void show_status_menu(int part);
    void set_eol_mode(int scintillaEolMode);
    void set_language(SyntaxLanguage language);
    void save();
    void save_as();
    bool confirm_discard_or_save();
    void toggle_theme();
    void show_language_menu(POINT screenPoint);
    void apply_ui_language();
    bool prefill_find_from_selection();
    void perform_find(bool backwards = false, bool fromStart = false);
    void show_editor_menu(POINT screenPoint, bool contextMenu);
    void show_go_to_line();
    void show_replace_dialog();
    void perform_replace_action(int command);
    void load_search_history();
    void remember_search_history(std::wstring_view find,
        std::optional<std::wstring_view> replacement = std::nullopt);
    void save_search_history();
    void populate_history_combo(HWND combo, const std::vector<std::wstring>& entries,
        std::wstring_view current = {});
    bool delete_selected_history(HWND control);
    std::filesystem::path history_path() const;
    void update_slash_popup();
    void hide_slash_popup(bool restoreEditorFocus = false);
    void move_slash_selection(int delta);
    void scroll_slash_popup(int wheelDelta);
    void execute_slash_selection();
    void execute_slash_command(int commandIndex);
    void show_table_form();
    void insert_table_from_form();
    void paint_slash_popup(HDC device);
    void remember_document_view_state();
    std::optional<DocumentViewState> stored_document_view_state(const std::filesystem::path& path);
    bool offer_recovery_snapshot();
    void write_recovery_snapshot();
    static std::wstring document_view_state_key(const std::filesystem::path& path);

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HWND toolbar_ = nullptr;
    HWND status_ = nullptr;
    HWND splitDivider_ = nullptr;
    HFONT uiFont_ = nullptr;
    HWND findBox_ = nullptr;
    HWND findEdit_ = nullptr;
    Editor editor_;
    Preview preview_;
    std::unique_ptr<PreviewRenderWorker> previewRenderWorker_;
    Document document_;
    ViewMode mode_ = ViewMode::Edit;
    bool dark_ = false;
    bool closing_ = false;
    bool comInitialized_ = false;
    int pendingSyncLine_ = 0;
    HWND replaceDialog_ = nullptr;
    HWND replaceFindCombo_ = nullptr;
    HWND replaceWithCombo_ = nullptr;
    HWND replaceResult_ = nullptr;
    std::vector<std::wstring> findHistory_;
    std::vector<std::wstring> replaceHistory_;
    std::wstring lastPreviewFind_;
    std::wstring previewFindStatus_;
    bool suppressShortcutCharacter_ = false;
    bool historyWriteWarned_ = false;
    bool locatingFromPreview_ = false;
    bool externalChangeNotified_ = false;
    bool previewRendering_ = false;
    std::uint64_t configurationSignature_ = 0;
    std::wstring configurationStatus_;
    bool splitDragging_ = false;
    bool splitHover_ = false;
    double splitRatio_ = 0.5;
    HWND slashPopup_ = nullptr;
    HWND slashRowsEdit_ = nullptr;
    HWND slashColumnsEdit_ = nullptr;
    HWND slashInsertButton_ = nullptr;
    std::vector<int> slashVisibleCommands_;
    int slashSelected_ = 0;
    int slashScrollOffset_ = 0;
    int slashWheelRemainder_ = 0;
    bool slashTableMode_ = false;
    bool restoringDocumentState_ = false;
    std::optional<EditorViewState> pendingEditorViewState_;
    bool discardingChanges_ = false;
    bool recoveryWriteFailed_ = false;
    std::string recoverySnapshotContent_;
    std::filesystem::path runtimeDataDirectory_;
    std::unordered_map<std::wstring, DocumentViewState> documentViewStates_;
    std::vector<std::wstring> documentViewStateOrder_;
};

} // namespace editmdview
