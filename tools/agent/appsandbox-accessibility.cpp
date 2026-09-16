#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "../transport/asb_transport.h"
#include "../transport/accessibility_protocol.h"

using Microsoft::WRL::ComPtr;
using namespace winrt::Windows::Data::Json;

static constexpr size_t MaxString = ASB_AX_MAX_STRING;
static constexpr size_t MaxNodes = ASB_AX_MAX_NODES;
static constexpr size_t MaxMessage = ASB_AX_MAX_MESSAGE;
static constexpr size_t MaxSnapshotText = 32 * 1024 * 1024;
static volatile LONG stopping;

static BOOL WINAPI StopHandler(DWORD) {
    InterlockedExchange(&stopping, 1);
    return TRUE;
}

static std::wstring Limited(std::wstring value) {
    if (value.size() > MaxString) {
        value.resize(MaxString);
        if (value.back() >= 0xd800 && value.back() <= 0xdbff) value.pop_back();
    }
    return value;
}

static JsonValue Str(const std::wstring &value) { return JsonValue::CreateStringValue(Limited(value)); }
static JsonValue Num(double value) { return JsonValue::CreateNumberValue(value); }
static JsonValue Bool(bool value) { return JsonValue::CreateBooleanValue(value); }

static bool ReadBytes(AsbConn *conn, void *data, size_t size) {
    auto *bytes = static_cast<unsigned char *>(data);
    ULONGLONG deadline = GetTickCount64() + 1000;
    while (size && !stopping) {
        int ready = asb_poll(conn, 100);
        if (ready < 0 || GetTickCount64() > deadline) return false;
        if (!ready) continue;
        int count = asb_recv(conn, bytes, static_cast<int>(size));
        if (count <= 0) return false;
        size -= count;
        bytes += count;
    }
    return size == 0;
}

static bool WriteBytes(AsbConn *conn, const void *data, size_t size) {
    const auto *bytes = static_cast<const unsigned char *>(data);
    while (size && !stopping) {
        int count = asb_send(conn, bytes, static_cast<int>(size));
        if (count <= 0) return false;
        size -= count;
        bytes += count;
    }
    return size == 0;
}

static JsonObject ReadMessage(AsbConn *conn) {
    unsigned char header[4];
    if (!ReadBytes(conn, header, sizeof(header))) return nullptr;
    size_t size = (size_t(header[0]) << 24) | (size_t(header[1]) << 16) |
                  (size_t(header[2]) << 8) | header[3];
    if (!size || size > MaxMessage) return nullptr;
    std::string bytes(size, '\0');
    if (!ReadBytes(conn, bytes.data(), size)) return nullptr;
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(size), nullptr, 0);
    if (!length) return nullptr;
    std::wstring wide(length, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(size), wide.data(), length)) return nullptr;
    JsonObject value{nullptr};
    if (!JsonObject::TryParse(wide, value)) return nullptr;
    return value;
}

static bool WriteMessage(AsbConn *conn, const JsonObject &value) {
    std::string bytes = winrt::to_string(value.Stringify());
    size_t size = bytes.size();
    if (!size || size > MaxMessage) return false;
    unsigned char header[] = {static_cast<unsigned char>(size >> 24), static_cast<unsigned char>(size >> 16),
                              static_cast<unsigned char>(size >> 8), static_cast<unsigned char>(size)};
    return WriteBytes(conn, header, sizeof(header)) && WriteBytes(conn, bytes.data(), size);
}

struct Variant {
    VARIANT value;
    Variant() { VariantInit(&value); }
    ~Variant() { VariantClear(&value); }
    Variant(const Variant &) = delete;
    Variant &operator=(const Variant &) = delete;
};

static std::wstring CachedString(IUIAutomationElement *element, PROPERTYID property) {
    Variant value;
    if (FAILED(element->GetCachedPropertyValue(property, &value.value)) || value.value.vt != VT_BSTR) return {};
    return Limited(std::wstring(value.value.bstrVal, SysStringLen(value.value.bstrVal)));
}

static int CachedInt(IUIAutomationElement *element, PROPERTYID property, int fallback = 0) {
    Variant value;
    if (FAILED(element->GetCachedPropertyValue(property, &value.value))) return fallback;
    if (value.value.vt == VT_I4) return value.value.lVal;
    if (value.value.vt == VT_BOOL) return value.value.boolVal != VARIANT_FALSE;
    return fallback;
}

static std::wstring Role(CONTROLTYPEID type) {
    switch (type) {
        case UIA_ButtonControlTypeId: return L"AXButton";
        case UIA_CalendarControlTypeId: return L"AXGroup";
        case UIA_CheckBoxControlTypeId: return L"AXCheckBox";
        case UIA_ComboBoxControlTypeId: return L"AXPopUpButton";
        case UIA_EditControlTypeId: return L"AXTextField";
        case UIA_HyperlinkControlTypeId: return L"AXLink";
        case UIA_ImageControlTypeId: return L"AXImage";
        case UIA_ListItemControlTypeId: return L"AXRow";
        case UIA_ListControlTypeId: return L"AXList";
        case UIA_MenuControlTypeId: return L"AXMenu";
        case UIA_MenuBarControlTypeId: return L"AXMenuBar";
        case UIA_MenuItemControlTypeId: return L"AXMenuItem";
        case UIA_ProgressBarControlTypeId: return L"AXProgressIndicator";
        case UIA_RadioButtonControlTypeId: return L"AXRadioButton";
        case UIA_ScrollBarControlTypeId: return L"AXScrollBar";
        case UIA_SliderControlTypeId: return L"AXSlider";
        case UIA_SpinnerControlTypeId: return L"AXIncrementor";
        case UIA_TabControlTypeId: return L"AXTabGroup";
        case UIA_TabItemControlTypeId: return L"AXRadioButton";
        case UIA_TextControlTypeId: return L"AXStaticText";
        case UIA_ToolBarControlTypeId: return L"AXToolbar";
        case UIA_ToolTipControlTypeId: return L"AXHelpTag";
        case UIA_TreeControlTypeId: return L"AXOutline";
        case UIA_TreeItemControlTypeId: return L"AXRow";
        case UIA_DataGridControlTypeId: return L"AXTable";
        case UIA_DataItemControlTypeId: return L"AXRow";
        case UIA_DocumentControlTypeId: return L"AXTextArea";
        case UIA_WindowControlTypeId: return L"AXWindow";
        case UIA_HeaderControlTypeId: return L"AXGroup";
        case UIA_HeaderItemControlTypeId: return L"AXColumn";
        case UIA_TableControlTypeId: return L"AXTable";
        case UIA_SeparatorControlTypeId: return L"AXSplitter";
        default: return L"AXGroup";
    }
}

template<class T> static ComPtr<T> Pattern(IUIAutomationElement *element, PATTERNID pattern) {
    ComPtr<T> result;
    element->GetCurrentPatternAs(pattern, __uuidof(T), reinterpret_cast<void **>(result.GetAddressOf()));
    return result;
}

static bool RangeText(IUIAutomationTextRange *range, std::wstring &text) {
    BSTR value = nullptr;
    HRESULT hr = range->GetText(static_cast<int>(MaxString + 1), &value);
    if (FAILED(hr)) return false;
    text.assign(value ? value : L"", SysStringLen(value));
    SysFreeString(value);
    return true;
}

struct Node {
    ComPtr<IUIAutomationElement> element;
    std::wstring identity;
    std::wstring text;
    DWORD pid = 0;
    bool writableValue = false;
    bool writableFocus = false;
    bool writableRange = false;
    bool secure = false;
    std::unordered_set<std::wstring> actions;
};

class Exporter {
    ComPtr<IUIAutomation> automation_;
    ComPtr<IUIAutomationCacheRequest> cache_;
    ComPtr<IUIAutomationCacheRequest> childrenCache_;
    ComPtr<IUIAutomationCacheRequest> focusCache_;
    std::unordered_map<std::wstring, Node> nodes_;
    std::unordered_map<std::wstring, std::wstring> identities_;
    std::wstring session_;
    ULONGLONG revision_ = 0;
    ULONGLONG nextId_ = 0;
    ULONGLONG refreshGeneration_ = 0;
    HWND foreground_ = nullptr;
    DWORD sessionId_ = 0;
    AsbConn *conn_ = nullptr;
    bool truncated_ = false;
    ULONGLONG nextHeartbeat_ = 0;
    size_t textUnits_ = 0;

    std::wstring SnapshotText(std::wstring value) {
        size_t available = MaxSnapshotText - textUnits_;
        if (value.size() > available) {
            value.resize(available);
            if (!value.empty() && value.back() >= 0xd800 && value.back() <= 0xdbff) value.pop_back();
            truncated_ = true;
        }
        textUnits_ += value.size();
        return value;
    }

    bool Active() const {
        if (sessionId_ != WTSGetActiveConsoleSessionId()) return false;
        HDESK desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
        if (!desktop) return false;
        wchar_t name[128] = {};
        DWORD needed = 0;
        bool active = GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), &needed) &&
                      _wcsicmp(name, L"Default") == 0;
        CloseDesktop(desktop);
        return active;
    }

    JsonObject Message(const wchar_t *type) const {
        JsonObject message;
        message.Insert(L"type", Str(type));
        message.Insert(L"version", Num(ASB_AX_VERSION));
        message.Insert(L"session", Str(session_));
        return message;
    }

    std::wstring Identity(IUIAutomationElement *element, DWORD pid) {
        Variant value;
        if (FAILED(element->GetCachedPropertyValue(UIA_RuntimeIdPropertyId, &value.value)) ||
            value.value.vt != (VT_ARRAY | VT_I4) || !value.value.parray) return {};
        LONG first = 0, last = -1;
        if (FAILED(SafeArrayGetLBound(value.value.parray, 1, &first)) ||
            FAILED(SafeArrayGetUBound(value.value.parray, 1, &last)) || last - first > 64) return {};
        std::wstring identity = std::to_wstring(pid);
        for (LONG i = first; i <= last; ++i) {
            LONG part = 0;
            if (FAILED(SafeArrayGetElement(value.value.parray, &i, &part))) return {};
            identity += L":" + std::to_wstring(part);
        }
        return identity;
    }

    void AddText(Node &record, JsonObject &node, CONTROLTYPEID type) {
        if (record.secure) return;
        if (CachedInt(record.element.Get(), UIA_IsValuePatternAvailablePropertyId)) {
            record.text = CachedString(record.element.Get(), UIA_ValueValuePropertyId);
            node.Insert(L"value", Str(record.text));
        }
        if (type == UIA_CheckBoxControlTypeId) node.Insert(L"value", Num(CachedInt(record.element.Get(), UIA_ToggleToggleStatePropertyId)));
        if (type == UIA_RadioButtonControlTypeId || type == UIA_TabItemControlTypeId)
            node.Insert(L"value", Num(CachedInt(record.element.Get(), UIA_SelectionItemIsSelectedPropertyId)));
        if (!CachedInt(record.element.Get(), UIA_IsTextPatternAvailablePropertyId) ||
            (type != UIA_EditControlTypeId && type != UIA_DocumentControlTypeId)) return;
        auto pattern = Pattern<IUIAutomationTextPattern>(record.element.Get(), UIA_TextPatternId);
        ComPtr<IUIAutomationTextRange> document;
        if (!pattern || FAILED(pattern->get_DocumentRange(&document))) return;
        std::wstring text;
        if (!RangeText(document.Get(), text)) return;
        record.text = Limited(text);
        node.Insert(L"value", Str(record.text));
        if (text.size() > MaxString) { truncated_ = true; return; }
        SupportedTextSelection supported = SupportedTextSelection_None;
        if (FAILED(pattern->get_SupportedTextSelection(&supported)) || supported == SupportedTextSelection_None) return;
        ComPtr<IUIAutomationTextRangeArray> selection;
        int count = 0;
        if (FAILED(pattern->GetSelection(&selection)) || FAILED(selection->get_Length(&count)) || count != 1) return;
        ComPtr<IUIAutomationTextRange> selected, prefix;
        if (FAILED(selection->GetElement(0, &selected)) || FAILED(document->Clone(&prefix)) ||
            FAILED(prefix->MoveEndpointByRange(TextPatternRangeEndpoint_End, selected.Get(), TextPatternRangeEndpoint_Start))) return;
        std::wstring before, chosen;
        if (!RangeText(prefix.Get(), before) || !RangeText(selected.Get(), chosen) ||
            before.size() > text.size() || chosen.size() > text.size() - before.size() ||
            text.compare(before.size(), chosen.size(), chosen) != 0) return;
        JsonArray range;
        range.Append(Num(static_cast<double>(before.size())));
        range.Append(Num(static_cast<double>(chosen.size())));
        node.Insert(L"selectedTextRange", range);
        record.writableRange = true;
    }

    std::wstring Append(IUIAutomationElement *element, const std::wstring &parent, JsonArray &output,
                        std::unordered_map<std::wstring, Node> &next, unsigned depth) {
        if (!element || next.size() >= MaxNodes || depth > 128) { truncated_ = true; return {}; }
        DWORD pid = static_cast<DWORD>(CachedInt(element, UIA_ProcessIdPropertyId));
        std::wstring identity = Identity(element, pid);
        std::wstring id;
        auto prior = identities_.find(identity);
        if (!identity.empty() && prior != identities_.end()) id = prior->second;
        else id = L"w" + std::to_wstring(++nextId_);
        if (next.count(id)) return {};
        Node record;
        record.element = element;
        record.identity = identity;
        record.pid = pid;
        record.secure = CachedInt(element, UIA_IsPasswordPropertyId) != 0;
        bool enabled = CachedInt(element, UIA_IsEnabledPropertyId) != 0;
        bool focused = CachedInt(element, UIA_HasKeyboardFocusPropertyId) != 0;
        CONTROLTYPEID type = CachedInt(element, UIA_ControlTypePropertyId);
        record.writableFocus = enabled && CachedInt(element, UIA_IsKeyboardFocusablePropertyId);
        record.writableValue = enabled && !record.secure && CachedInt(element, UIA_IsValuePatternAvailablePropertyId) &&
                               !CachedInt(element, UIA_ValueIsReadOnlyPropertyId, 1);
        if (enabled && (CachedInt(element, UIA_IsInvokePatternAvailablePropertyId) ||
                        CachedInt(element, UIA_IsTogglePatternAvailablePropertyId) ||
                        CachedInt(element, UIA_IsSelectionItemPatternAvailablePropertyId))) record.actions.insert(L"AXPress");
        if (enabled && CachedInt(element, UIA_IsExpandCollapsePatternAvailablePropertyId)) {
            record.actions.insert(L"AXPress");
            record.actions.insert(L"AXShowMenu");
            if (CachedInt(element, UIA_ExpandCollapseExpandCollapseStatePropertyId) == ExpandCollapseState_Expanded)
                record.actions.insert(L"AXCancel");
        }
        JsonObject node;
        node.Insert(L"id", Str(id));
        node.Insert(L"role", Str(Role(type)));
        node.Insert(L"label", Str(SnapshotText(CachedString(element, UIA_NamePropertyId))));
        if (!parent.empty()) node.Insert(L"parent", Str(parent));
        node.Insert(L"enabled", Bool(enabled));
        node.Insert(L"focused", Bool(focused));
        node.Insert(L"secure", Bool(record.secure));
        node.Insert(L"writableFocused", Bool(record.writableFocus));
        node.Insert(L"writableValue", Bool(record.writableValue));
        RECT rect{};
        if (FAILED(element->get_CachedBoundingRectangle(&rect))) rect = {};
        JsonArray frame;
        for (double coordinate : {double(rect.left), double(rect.top), double(std::max<LONG>(0, rect.right - rect.left)),
                                  double(std::max<LONG>(0, rect.bottom - rect.top))}) frame.Append(Num(coordinate));
        node.Insert(L"frame", frame);
        JsonArray actions;
        for (const auto &action : record.actions) actions.Append(Str(action));
        node.Insert(L"actions", actions);
        if (!record.secure && CachedInt(element, UIA_IsValuePatternAvailablePropertyId)) {
            record.text = CachedString(element, UIA_ValueValuePropertyId);
            node.Insert(L"value", Str(record.text));
        }
        if (type == UIA_CheckBoxControlTypeId) node.Insert(L"value", Num(CachedInt(element, UIA_ToggleToggleStatePropertyId)));
        if (type == UIA_RadioButtonControlTypeId || type == UIA_TabItemControlTypeId)
            node.Insert(L"value", Num(CachedInt(element, UIA_SelectionItemIsSelectedPropertyId)));
        if (node.HasKey(L"value") && node.GetNamedValue(L"value").ValueType() == JsonValueType::String) {
            auto value = SnapshotText(record.text);
            if (value != record.text) {
                record.writableRange = false;
                if (node.HasKey(L"selectedTextRange")) node.Remove(L"selectedTextRange");
            }
            record.text = std::move(value);
            node.Insert(L"value", Str(record.text));
        }
        node.Insert(L"writableSelectedTextRange", Bool(enabled && record.writableRange));
        next.emplace(id, std::move(record));
        node.Insert(L"children", JsonArray());
        output.Append(node);
        return id;
    }

    bool Status(const wchar_t *status, bool invalidated = false) {
        auto message = Message(L"status");
        message.Insert(L"status", Str(status));
        message.Insert(L"refreshSupported", Bool(true));
        message.Insert(L"invalidated", Bool(invalidated));
        return WriteMessage(conn_, message);
    }

    bool ContinueCollection() {
        if (stopping || asb_poll(conn_, 0) != 0) return false;
        if (GetTickCount64() >= nextHeartbeat_) {
            if (!Status(L"updating")) return false;
            nextHeartbeat_ = GetTickCount64() + 500;
        }
        return true;
    }

    bool Snapshot(bool &published) {
        published = false;
        if (!Active()) { nodes_.clear(); identities_.clear(); foreground_ = nullptr; return Status(L"inactive"); }
        HWND foreground = GetForegroundWindow();
        if (!foreground) foreground = GetShellWindow();
        DWORD pid = 0;
        GetWindowThreadProcessId(foreground, &pid);
        if (!foreground || !pid) return Status(L"inactive");
        MONITORINFO monitor{sizeof(monitor)};
        if (!GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor)) return Status(L"inactive");
        std::vector<HWND> windows{foreground};
        struct WindowContext { HWND root; std::vector<HWND> *windows; } context{GetAncestor(foreground, GA_ROOTOWNER), &windows};
        EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
            auto *context = reinterpret_cast<WindowContext *>(parameter);
            if (context->windows->size() >= 8) return FALSE;
            if (window != context->windows->front() && IsWindowVisible(window) && !IsIconic(window) &&
                GetAncestor(window, GA_ROOTOWNER) == context->root) context->windows->push_back(window);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&context));
        if (!Status(L"updating", foreground != foreground_ || nodes_.empty())) return false;
        ULONGLONG started = GetTickCount64();
        JsonArray output, roots;
        std::unordered_map<std::wstring, Node> next;
        std::unordered_map<std::wstring, JsonObject> records;
        struct Pending { ComPtr<IUIAutomationElement> element; std::wstring parent; unsigned depth; };
        std::deque<Pending> pending;
        truncated_ = false;
        textUnits_ = 0;
        nextHeartbeat_ = started + 500;
        for (HWND window : windows) {
            if (!ContinueCollection()) return true;
            ComPtr<IUIAutomationElement> element;
            if (FAILED(automation_->ElementFromHandleBuildCache(window, cache_.Get(), &element)) || !element) continue;
            pending.push_back({element, L"", 0});
        }
        while (!pending.empty() && next.size() < MaxNodes) {
            if (!ContinueCollection()) return true;
            Pending item = std::move(pending.front()); pending.pop_front();
            std::wstring id = Append(item.element.Get(), item.parent, output, next, item.depth);
            if (id.empty()) continue;
            JsonObject node = output.GetObjectAt(output.Size() - 1);
            records.emplace(id, node);
            if (item.parent.empty()) roots.Append(Str(id));
            else records.at(item.parent).GetNamedArray(L"children").Append(Str(id));
            if (item.depth >= 127) { truncated_ = true; continue; }
            ComPtr<IUIAutomationElement> expanded;
            if (FAILED(item.element->BuildUpdatedCache(childrenCache_.Get(), &expanded)) || !expanded) {
                truncated_ = true;
                continue;
            }
            ComPtr<IUIAutomationElementArray> children;
            int count = 0;
            if (FAILED(expanded->GetCachedChildren(&children)) || !children || FAILED(children->get_Length(&count))) continue;
            for (int i = 0; i < count; ++i) {
                if (pending.size() + next.size() >= MaxNodes) { truncated_ = true; break; }
                ComPtr<IUIAutomationElement> child;
                if (SUCCEEDED(children->GetElement(i, &child)) && child) pending.push_back({child, id, item.depth + 1});
            }
        }
        if (!pending.empty()) truncated_ = true;
        for (unsigned pass = 0; pass < 2; ++pass) for (const auto &entry : records) {
            auto &record = next.at(entry.first);
            bool focused = CachedInt(record.element.Get(), UIA_HasKeyboardFocusPropertyId) != 0;
            if ((pass == 0) != focused) continue;
            if (!ContinueCollection()) return true;
            CONTROLTYPEID type = CachedInt(record.element.Get(), UIA_ControlTypePropertyId);
            if (type != UIA_EditControlTypeId && type != UIA_DocumentControlTypeId) continue;
            JsonObject node = entry.second;
            AddText(record, node, type);
            if (node.HasKey(L"value") && node.GetNamedValue(L"value").ValueType() == JsonValueType::String) {
                auto value = SnapshotText(record.text);
                if (value != record.text) {
                    record.writableRange = false;
                    if (node.HasKey(L"selectedTextRange")) node.Remove(L"selectedTextRange");
                }
                record.text = std::move(value);
                node.Insert(L"value", Str(record.text));
            }
            node.Insert(L"writableSelectedTextRange", Bool(CachedInt(record.element.Get(), UIA_IsEnabledPropertyId) && record.writableRange));
        }
        if (!Active() || (GetForegroundWindow() && GetForegroundWindow() != foreground) || next.empty()) {
            nodes_.clear(); identities_.clear(); foreground_ = nullptr;
            return Status(L"updating", true);
        }
        foreground_ = foreground;
        nodes_ = std::move(next);
        identities_.clear();
        for (const auto &entry : nodes_) if (!entry.second.identity.empty()) identities_[entry.second.identity] = entry.first;
        wchar_t path[1024] = {};
        DWORD length = ARRAYSIZE(path);
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (process) { QueryFullProcessImageNameW(process, 0, path, &length); CloseHandle(process); }
        const wchar_t *base = wcsrchr(path, L'\\');
        std::wstring name = base ? base + 1 : path;
        if (name.empty()) name = L"Windows";
        JsonObject app, display;
        app.Insert(L"pid", Num(pid));
        app.Insert(L"name", Str(name));
        app.Insert(L"bundleId", Str(L"win32." + std::to_wstring(pid)));
        display.Insert(L"x", Num(monitor.rcMonitor.left));
        display.Insert(L"y", Num(monitor.rcMonitor.top));
        display.Insert(L"width", Num(monitor.rcMonitor.right - monitor.rcMonitor.left));
        display.Insert(L"height", Num(monitor.rcMonitor.bottom - monitor.rcMonitor.top));
        display.Insert(L"pixelWidth", Num(monitor.rcMonitor.right - monitor.rcMonitor.left));
        display.Insert(L"pixelHeight", Num(monitor.rcMonitor.bottom - monitor.rcMonitor.top));
        auto message = Message(L"snapshot");
        message.Insert(L"revision", Num(static_cast<double>(++revision_)));
        message.Insert(L"app", app);
        message.Insert(L"display", display);
        message.Insert(L"nodes", output);
        message.Insert(L"roots", roots);
        message.Insert(L"truncated", Bool(truncated_));
        message.Insert(L"refreshGeneration", Num(static_cast<double>(refreshGeneration_)));
        message.Insert(L"captureMs", Num(static_cast<double>(GetTickCount64() - started)));
        ComPtr<IUIAutomationElement> focused;
        if (SUCCEEDED(automation_->GetFocusedElementBuildCache(focusCache_.Get(), &focused)) && focused) {
            auto identity = Identity(focused.Get(), static_cast<DWORD>(CachedInt(focused.Get(), UIA_ProcessIdPropertyId)));
            auto found = identities_.find(identity);
            if (found != identities_.end()) message.Insert(L"focused", Str(found->second));
        }
        published = WriteMessage(conn_, message);
        return published;
    }

    static ComPtr<IUIAutomationTextRange> Prefix(IUIAutomationTextRange *document, size_t offset) {
        int low = 0, high = static_cast<int>(offset);
        bool first = true;
        while (low <= high) {
            int units = first ? high : low + (high - low) / 2;
            first = false;
            ComPtr<IUIAutomationTextRange> range;
            int moved = 0;
            if (FAILED(document->Clone(&range)) ||
                FAILED(range->MoveEndpointByRange(TextPatternRangeEndpoint_End, document, TextPatternRangeEndpoint_Start)) ||
                FAILED(range->MoveEndpointByUnit(TextPatternRangeEndpoint_End, TextUnit_Character, units, &moved))) return {};
            std::wstring value;
            if (!RangeText(range.Get(), value)) return {};
            if (value.size() == offset) return range;
            if (value.size() < offset) low = units + 1;
            else high = units - 1;
        }
        return {};
    }

    HRESULT SelectRange(Node &record, const JsonArray &value) {
        if (value.Size() != 2) return E_INVALIDARG;
        double location = value.GetNumberAt(0), length = value.GetNumberAt(1);
        if (!std::isfinite(location) || !std::isfinite(length) || location < 0 || length < 0 ||
            std::floor(location) != location || std::floor(length) != length || location + length > record.text.size()) return E_INVALIDARG;
        auto pattern = Pattern<IUIAutomationTextPattern>(record.element.Get(), UIA_TextPatternId);
        ComPtr<IUIAutomationTextRange> document;
        if (!pattern || FAILED(pattern->get_DocumentRange(&document))) return UIA_E_NOTSUPPORTED;
        std::wstring text;
        if (!RangeText(document.Get(), text) || text != record.text) return UIA_E_ELEMENTNOTAVAILABLE;
        auto start = Prefix(document.Get(), static_cast<size_t>(location));
        auto end = Prefix(document.Get(), static_cast<size_t>(location + length));
        if (!start || !end) return UIA_E_NOTSUPPORTED;
        HRESULT hr = document->MoveEndpointByRange(TextPatternRangeEndpoint_Start, start.Get(), TextPatternRangeEndpoint_End);
        if (SUCCEEDED(hr)) hr = document->MoveEndpointByRange(TextPatternRangeEndpoint_End, end.Get(), TextPatternRangeEndpoint_End);
        if (SUCCEEDED(hr)) hr = document->Select();
        return hr;
    }

    HRESULT Perform(const JsonObject &message) {
        if (!Active() || !foreground_ || GetForegroundWindow() != foreground_ ||
            message.GetNamedString(L"session", L"") != session_) return UIA_E_ELEMENTNOTAVAILABLE;
        auto found = nodes_.find(std::wstring(message.GetNamedString(L"nodeId", L"")));
        if (found == nodes_.end()) return UIA_E_ELEMENTNOTAVAILABLE;
        Node &record = found->second;
        BOOL enabled = FALSE, secure = TRUE;
        int pid = 0;
        if (FAILED(record.element->get_CurrentIsEnabled(&enabled)) || !enabled ||
            FAILED(record.element->get_CurrentProcessId(&pid)) || static_cast<DWORD>(pid) != record.pid ||
            FAILED(record.element->get_CurrentIsPassword(&secure))) return UIA_E_ELEMENTNOTAVAILABLE;
        std::wstring action(message.GetNamedString(L"action", L""));
        if (action == L"setFocused") {
            if (!record.writableFocus || !message.GetNamedBoolean(L"value", false)) return UIA_E_NOTSUPPORTED;
            return record.element->SetFocus();
        }
        if (action == L"setValue") {
            if (!record.writableValue || secure) return UIA_E_NOTSUPPORTED;
            auto value = message.GetNamedString(L"value");
            if (value.size() > MaxString) return E_INVALIDARG;
            auto pattern = Pattern<IUIAutomationValuePattern>(record.element.Get(), UIA_ValuePatternId);
            BOOL readOnly = TRUE;
            if (!pattern || FAILED(pattern->get_CurrentIsReadOnly(&readOnly)) || readOnly) return UIA_E_NOTSUPPORTED;
            BSTR text = SysAllocStringLen(value.c_str(), value.size());
            if (!text) return E_OUTOFMEMORY;
            HRESULT hr = pattern->SetValue(text);
            SysFreeString(text);
            return hr;
        }
        if (action == L"setSelectedTextRange") {
            if (!record.writableRange || secure) return UIA_E_NOTSUPPORTED;
            return SelectRange(record, message.GetNamedArray(L"value"));
        }
        if (!record.actions.count(action)) return UIA_E_NOTSUPPORTED;
        if (action == L"AXShowMenu" || action == L"AXCancel") {
            auto pattern = Pattern<IUIAutomationExpandCollapsePattern>(record.element.Get(), UIA_ExpandCollapsePatternId);
            if (!pattern) return UIA_E_NOTSUPPORTED;
            return action == L"AXShowMenu" ? pattern->Expand() : pattern->Collapse();
        }
        if (action == L"AXPress") {
            auto invoke = Pattern<IUIAutomationInvokePattern>(record.element.Get(), UIA_InvokePatternId);
            if (invoke) return invoke->Invoke();
            auto toggle = Pattern<IUIAutomationTogglePattern>(record.element.Get(), UIA_TogglePatternId);
            if (toggle) return toggle->Toggle();
            auto select = Pattern<IUIAutomationSelectionItemPattern>(record.element.Get(), UIA_SelectionItemPatternId);
            if (select) return select->Select();
            auto expand = Pattern<IUIAutomationExpandCollapsePattern>(record.element.Get(), UIA_ExpandCollapsePatternId);
            if (expand) {
                ExpandCollapseState state;
                HRESULT hr = expand->get_CurrentExpandCollapseState(&state);
                if (FAILED(hr)) return hr;
                return state == ExpandCollapseState_Expanded ? expand->Collapse() : expand->Expand();
            }
        }
        return UIA_E_NOTSUPPORTED;
    }

public:
    Exporter() {
        ProcessIdToSessionId(GetCurrentProcessId(), &sessionId_);
        winrt::check_hresult(CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation_)));
        ComPtr<IUIAutomation2> automation2;
        if (SUCCEEDED(automation_.As(&automation2))) {
            automation2->put_ConnectionTimeout(100);
            automation2->put_TransactionTimeout(100);
        }
        winrt::check_hresult(automation_->CreateCacheRequest(&cache_));
        winrt::check_hresult(cache_->put_TreeScope(TreeScope_Element));
        winrt::check_hresult(cache_->put_AutomationElementMode(AutomationElementMode_Full));
        const PROPERTYID properties[] = {
            UIA_RuntimeIdPropertyId, UIA_ProcessIdPropertyId, UIA_ControlTypePropertyId,
            UIA_NamePropertyId, UIA_BoundingRectanglePropertyId, UIA_IsEnabledPropertyId,
            UIA_HasKeyboardFocusPropertyId, UIA_IsKeyboardFocusablePropertyId, UIA_IsPasswordPropertyId,
            UIA_IsInvokePatternAvailablePropertyId, UIA_IsTogglePatternAvailablePropertyId,
            UIA_IsSelectionItemPatternAvailablePropertyId, UIA_IsExpandCollapsePatternAvailablePropertyId,
            UIA_IsValuePatternAvailablePropertyId, UIA_IsTextPatternAvailablePropertyId,
            UIA_ValueValuePropertyId, UIA_ValueIsReadOnlyPropertyId, UIA_ToggleToggleStatePropertyId,
            UIA_SelectionItemIsSelectedPropertyId, UIA_ExpandCollapseExpandCollapseStatePropertyId
        };
        for (auto property : properties) winrt::check_hresult(cache_->AddProperty(property));
        winrt::check_hresult(cache_->Clone(&childrenCache_));
        winrt::check_hresult(childrenCache_->put_TreeScope(static_cast<TreeScope>(TreeScope_Element | TreeScope_Children)));
        winrt::check_hresult(automation_->CreateCacheRequest(&focusCache_));
        winrt::check_hresult(focusCache_->put_TreeScope(TreeScope_Element));
        winrt::check_hresult(focusCache_->AddProperty(UIA_RuntimeIdPropertyId));
        winrt::check_hresult(focusCache_->AddProperty(UIA_ProcessIdPropertyId));
    }

    void Serve(AsbConn *connection) {
        conn_ = connection;
        refreshGeneration_ = 0;
        nodes_.clear(); identities_.clear(); foreground_ = nullptr;
        GUID guid{};
        wchar_t uuid[40] = {};
        winrt::check_hresult(CoCreateGuid(&guid));
        StringFromGUID2(guid, uuid, ARRAYSIZE(uuid));
        session_ = std::wstring(uuid + 1, 36);
        bool subscribed = false;
        bool captureRequested = false;
        ULONGLONG nextSnapshot = GetTickCount64();
        ULONGLONG nextHeartbeat = nextSnapshot + 1000;
        asb_set_timeout(connection, 1000, 1000);
        if (!Status(L"ready")) return;
        while (!stopping) {
            ULONGLONG now = GetTickCount64();
            int wait = subscribed && captureRequested ? static_cast<int>(nextSnapshot > now ? std::min<ULONGLONG>(nextSnapshot - now, 333) : 0) : 333;
            int ready = asb_poll(connection, wait);
            if (ready < 0) break;
            if (ready) {
                auto message = ReadMessage(connection);
                if (!message || message.GetNamedNumber(L"version", 0) != ASB_AX_VERSION) break;
                auto type = message.GetNamedString(L"type", L"");
                if (type == L"subscribe") {
                    subscribed = message.GetNamedBoolean(L"enabled", false);
                    double requested = message.GetNamedNumber(L"intervalMs", 333);
                    if (!std::isfinite(requested) || requested < 100 || requested > 2000) break;
                    captureRequested = subscribed;
                    nextSnapshot = GetTickCount64() + 1000;
                    if (!subscribed) { nodes_.clear(); identities_.clear(); }
                } else if (type == L"refresh") {
                    double generation = message.GetNamedNumber(L"generation", -1);
                    if (!std::isfinite(generation) || generation < 0 || generation > 9007199254740991.0 ||
                        std::floor(generation) != generation) break;
                    refreshGeneration_ = std::max(refreshGeneration_, static_cast<ULONGLONG>(generation));
                    captureRequested = true;
                    nextSnapshot = 0;
                } else if (type == L"action") {
                    auto request = message.GetNamedString(L"requestId", L"");
                    if (request.empty() || request.size() > 128) break;
                    HRESULT hr = E_INVALIDARG;
                    try { if (subscribed) hr = Perform(message); }
                    catch (const winrt::hresult_error &error) { hr = error.code(); }
                    auto reply = Message(L"actionResult");
                    reply.Insert(L"requestId", Str(std::wstring(request)));
                    reply.Insert(L"ok", Bool(SUCCEEDED(hr)));
                    reply.Insert(L"error", Num(hr));
                    if (!WriteMessage(connection, reply)) break;
                    captureRequested = false;
                } else break;
            }
            if (asb_poll(connection, 0) > 0) continue;
            if (subscribed && captureRequested && GetTickCount64() >= nextSnapshot) {
                bool published = false;
                if (!Snapshot(published)) break;
                captureRequested = !published;
                nextSnapshot = GetTickCount64() + 333;
                nextHeartbeat = GetTickCount64() + 1000;
            } else if (GetTickCount64() >= nextHeartbeat) {
                if (!Active()) {
                    nodes_.clear(); identities_.clear(); foreground_ = nullptr;
                    if (!Status(L"inactive")) break;
                } else if (!Status(L"ready")) break;
                nextHeartbeat = GetTickCount64() + 1000;
            }
        }
        nodes_.clear(); identities_.clear(); foreground_ = nullptr;
        conn_ = nullptr;
    }
};

int wmain() {
    SetConsoleCtrlHandler(StopHandler, TRUE);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        if (asb_transport_init() != 0) return 1;
        Exporter exporter;
        while (!stopping) {
            AsbListener *listener = asb_listen(ASB_CH_ACCESSIBILITY);
            if (!listener) { Sleep(1000); continue; }
            while (!stopping) {
                AsbConn *connection = asb_accept(listener, 333);
                if (!connection) continue;
                try { exporter.Serve(connection); }
                catch (const winrt::hresult_error &) {}
                asb_close(connection);
            }
            asb_close_listener(listener);
        }
    } catch (const winrt::hresult_error &error) {
        fwprintf(stderr, L"Accessibility initialization failed: 0x%08lx\n", static_cast<unsigned long>(error.code().value));
        return 1;
    }
    return 0;
}
