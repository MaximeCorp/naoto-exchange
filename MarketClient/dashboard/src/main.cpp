// main.cpp
//
// Dashboard entry point. Single render/main thread owns ALL ImGui state
// and ALL AppState mutation. Every network source (etcd watcher, two
// multicast receivers, N trading sessions, ad-hoc balance polls) runs on
// its own thread and only ever pushes into an MpscQueue -- this file is
// the one place those queues get drained and turned into UI state, once
// per frame.
//
// This is a skeleton wired for correctness against the given protocols,
// not a finished polished UI -- panel layout/styling is intentionally
// minimal so the structure is easy to extend.

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include "app_state.hpp"
#include "client_details_poller.hpp"
#include "etcd_watcher.hpp"
#include "log.hpp"
#include "mpsc_queue.hpp"
#include "multicast_receiver.hpp"
#include "trading_session.hpp"
#include "wire_formats.hpp"

// ---------------------------------------------------------------------
// Config -- fill in from the etcd schema / deployment once confirmed.
// ---------------------------------------------------------------------
namespace config
{
    constexpr const char *kEtcdEndpoint = "http://127.0.0.1:2379";

    // Fallback defaults if etcd discovery hasn't populated anything yet.
    // Once matching_engines/socket_gateways/cdp_endpoints are populated from
    // etcd (see AppState), the UI lets you pick a discovered entry instead
    // of typing these in by hand.
    constexpr const char *kTradingGatewayHost = "127.0.0.1";
    constexpr uint16_t kTradingGatewayPort = 8000;
    constexpr uint16_t kDefaultGatewayId = 10; // Dedicated GatewayId the
                                               // project owner hardcoded CDP to
                                               // always accept, specifically so
                                               // this dashboard can impersonate
                                               // it instead of the real trading
                                               // gateway's id (0) -- avoids the
                                               // CDP session-hijacking
                                               // collision described in
                                               // client_details_poller.hpp.
                                               // Temporary until per-client
                                               // authorized-gateway assignment
                                               // exists.

    // STILL UNRESOLVED: multicast group/port for the order-state and
    // book-update feeds aren't in etcd and weren't given directly -- these
    // remain placeholders pending confirmation.
    constexpr const char *kOrderStateMcastGroup = "239.1.1.1";
    constexpr uint16_t kOrderStateMcastPort = 30001;
    constexpr const char *kOrderBookMcastGroup = "239.1.1.2";
    constexpr uint16_t kOrderBookMcastPort = 30002;

    // Which local interface to join the multicast groups on. Leave empty to
    // let the kernel pick (INADDR_ANY) -- fine on a box with one relevant
    // interface. For a same-host DPDK net_af veth-pair test setup (sender on
    // veth0, traffic landing on veth1), set this to "veth1" or wherever the
    // traffic actually lands, or the receiver may simply never see packets
    // regardless of any rp_filter setting.
    constexpr const char *kMcastLocalInterface = "veth1";
} // namespace config

// "localhost" isn't something inet_pton() can resolve; etcd entries use
// it (e.g. socket-gateways' "ip":"localhost:8000"). This is a minimal
// stand-in for real DNS resolution (getaddrinfo) -- fine for a
// dashboard talking to services on the same box / a known test network,
// worth upgrading if entries ever use real hostnames beyond localhost.
static std::pair<std::string, uint16_t> parse_host_port(const std::string &addr)
{
    auto pos = addr.find_last_of(':');
    if (pos == std::string::npos)
        return { addr, 0 };
    std::string host = addr.substr(0, pos);
    uint16_t port = static_cast<uint16_t>(std::atoi(addr.c_str() + pos + 1));
    if (host == "localhost")
        host = "127.0.0.1";
    return { host, port };
}

// "2m ago" / "14s ago" style relative time, for the notification history.
static std::string format_time_ago(double seconds_ago)
{
    int s = static_cast<int>(seconds_ago);
    if (s < 3)
        return "just now";
    if (s < 60)
        return std::to_string(s) + "s ago";
    int m = s / 60;
    if (m < 60)
        return std::to_string(m) + "m ago";
    int h = m / 60;
    return std::to_string(h) + "h ago";
}

// ---------------------------------------------------------------------
// Theme: a deliberate dark palette instead of ImGui's stock demo colors,
// with the color language a trading UI actually needs -- a single
// accent for interactive/selected state, and a consistent green/red for
// buy-side/positive vs sell-side/negative/error across every panel.
// ---------------------------------------------------------------------
namespace theme
{
    constexpr ImVec4 rgba(int r, int g, int b, int a = 255)
    {
        return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
    }

    // Light palette, ported from the Meridian HTML/CSS design.
    const ImVec4 kBg = rgba(244, 245, 248); // page background
    const ImVec4 kBgPanel = rgba(255, 255, 255); // card/surface white -- also
                                                 // the DEFAULT ChildBg, so
                                                 // every existing card
                                                 // BeginChild(...) picks
                                                 // this up automatically;
                                                 // only the page-background
                                                 // Content child overrides
                                                 // back to kBg explicitly.
    const ImVec4 kBgElevated =
        rgba(250, 251, 252); // input fields, subtle recess
    const ImVec4 kBorder = rgba(231, 233, 238);
    const ImVec4 kBorderSoft = rgba(238, 240, 244);
    const ImVec4 kText = rgba(21, 23, 28);
    const ImVec4 kTextDim = rgba(92, 98, 112); // secondary
    const ImVec4 kTextFaint = rgba(155, 161, 172); // tertiary / placeholder-ish
    const ImVec4 kAccent = rgba(91, 79, 232);
    const ImVec4 kAccentHover = rgba(76, 63, 224);
    const ImVec4 kAccentActive = rgba(66, 53, 204);
    const ImVec4 kAccentSoft = rgba(238, 236, 252); // selected-nav / badge tint
    const ImVec4 kPositive = rgba(22, 163, 74); // buy / accepted / connected
    const ImVec4 kPositiveSoft = rgba(231, 248, 238);
    const ImVec4 kNegative = rgba(225, 75, 66); // sell / rejected / error
    const ImVec4 kNegativeSoft = rgba(253, 236, 236);
    const ImVec4 kWarning = rgba(154, 107, 10); // "Cash" label accent

    void apply()
    {
        ImGuiStyle &style = ImGui::GetStyle();
        ImVec4 *c = style.Colors;

        c[ImGuiCol_Text] = kText;
        c[ImGuiCol_TextDisabled] = kTextFaint;
        c[ImGuiCol_WindowBg] = kBg;
        c[ImGuiCol_ChildBg] = kBgPanel;
        c[ImGuiCol_PopupBg] = kBgPanel;
        c[ImGuiCol_Border] = kBorderSoft;
        c[ImGuiCol_BorderShadow] = rgba(0, 0, 0, 0);
        c[ImGuiCol_FrameBg] = kBgElevated;
        c[ImGuiCol_FrameBgHovered] = rgba(242, 243, 246);
        c[ImGuiCol_FrameBgActive] = rgba(236, 237, 242);
        c[ImGuiCol_TitleBg] = kBgPanel;
        c[ImGuiCol_TitleBgActive] = kBgPanel;
        c[ImGuiCol_TitleBgCollapsed] = kBgPanel;
        c[ImGuiCol_MenuBarBg] = kBgPanel;
        c[ImGuiCol_ScrollbarBg] = rgba(0, 0, 0, 0);
        c[ImGuiCol_ScrollbarGrab] = rgba(215, 217, 224);
        c[ImGuiCol_ScrollbarGrabHovered] = rgba(191, 194, 204);
        c[ImGuiCol_ScrollbarGrabActive] = rgba(170, 173, 184);
        c[ImGuiCol_CheckMark] = kAccent;
        c[ImGuiCol_SliderGrab] = kAccent;
        c[ImGuiCol_SliderGrabActive] = kAccentHover;
        c[ImGuiCol_Button] = kBgElevated;
        c[ImGuiCol_ButtonHovered] = rgba(242, 243, 246);
        c[ImGuiCol_ButtonActive] = rgba(236, 237, 242);
        c[ImGuiCol_Header] = kAccentSoft;
        c[ImGuiCol_HeaderHovered] = rgba(228, 225, 250);
        c[ImGuiCol_HeaderActive] = rgba(222, 217, 249);
        c[ImGuiCol_Separator] = kBorderSoft;
        c[ImGuiCol_SeparatorHovered] = kAccent;
        c[ImGuiCol_SeparatorActive] = kAccentHover;
        c[ImGuiCol_ResizeGrip] = rgba(0, 0, 0, 0);
        c[ImGuiCol_ResizeGripHovered] = kAccent;
        c[ImGuiCol_ResizeGripActive] = kAccentHover;
        c[ImGuiCol_TableHeaderBg] = kBgElevated;
        c[ImGuiCol_TableBorderStrong] = kBorderSoft;
        c[ImGuiCol_TableBorderLight] = rgba(244, 245, 247);
        c[ImGuiCol_TableRowBg] = rgba(0, 0, 0, 0);
        c[ImGuiCol_TableRowBgAlt] = rgba(15, 15, 25, 10);
        c[ImGuiCol_TextSelectedBg] = rgba(91, 79, 232, 60);
        c[ImGuiCol_DragDropTarget] = kAccent;
        c[ImGuiCol_NavHighlight] = kAccent;

        style.WindowRounding = 10.0f;
        style.ChildRounding = 12.0f;
        style.FrameRounding = 8.0f;
        style.PopupRounding = 10.0f;
        style.ScrollbarRounding = 8.0f;
        style.GrabRounding = 8.0f;
        style.WindowBorderSize = 0.0f;
        style.ChildBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;
        style.WindowPadding = ImVec2(14, 14);
        style.FramePadding = ImVec2(10, 7);
        style.CellPadding = ImVec2(10, 7);
        style.ItemSpacing = ImVec2(8, 9);
        style.ItemInnerSpacing = ImVec2(6, 6);
        style.IndentSpacing = 16.0f;
        style.ScrollbarSize = 11.0f;
        style.GrabMinSize = 10.0f;
    }
} // namespace theme

// ---------------------------------------------------------------------
// Fonts: ImGui's built-in font is a small bitmap font meant for demos,
// not a real UI. We load actual typefaces bundled with ImGui itself
// (misc/fonts/, copied next to the executable by CMake's POST_BUILD
// step) -- checked for existence first, since ImGui hard-asserts on a
// missing font file rather than failing gracefully. If the copy step
// was skipped or the working directory doesn't match, we fall back to
// the default font instead of crashing.
// ---------------------------------------------------------------------
struct AppFonts
{
    ImFont *regular = nullptr; // body text, ~17px
    ImFont *header = nullptr; // section titles, ~22px, same face
    ImFont *mono = nullptr; // numeric/tabular data, ~16px
};

static bool file_exists(const std::string &path)
{
    std::ifstream f(path);
    return f.good();
}

// Directory the running binary actually lives in (e.g. ".../build/"),
// via /proc/self/exe -- Linux-specific but this whole project already
// assumes Linux throughout (raw POSIX sockets, epoll-adjacent patterns,
// etc.), so that's consistent, not a new constraint.
//
// Returns "" on failure (e.g. /proc unavailable), in which case callers
// should treat exe-relative paths as simply unavailable and fall back
// to whatever else they already try.
static std::string get_executable_dir()
{
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0)
        return "";
    buf[len] = '\0';
    std::string path(buf);
    auto pos = path.find_last_of('/');
    return pos == std::string::npos ? "" : path.substr(0, pos + 1);
}

// Tries a font path two ways before giving up: relative to the current
// working directory (works when launched as `./trading_dashboard` from
// inside build/, the common case), then relative to the executable's
// OWN directory (works regardless of cwd -- e.g. launched via a desktop
// shortcut, a different terminal cwd, or a script that cd's elsewhere
// first). Returns "" if neither resolves.
static std::string resolve_font_path(const std::string &relative_path)
{
    if (file_exists(relative_path))
        return relative_path;
    std::string exe_relative = get_executable_dir() + relative_path;
    if (!exe_relative.empty() && file_exists(exe_relative))
        return exe_relative;
    return "";
}

static AppFonts load_fonts()
{
    AppFonts fonts;
    ImGuiIO &io = ImGui::GetIO();

    std::string regular_path = resolve_font_path("fonts/Roboto-Medium.ttf");
    std::string mono_path = resolve_font_path("fonts/Cousine-Regular.ttf");

    if (!regular_path.empty())
    {
        fonts.regular =
            io.Fonts->AddFontFromFileTTF(regular_path.c_str(), 17.0f);
        fonts.header =
            io.Fonts->AddFontFromFileTTF(regular_path.c_str(), 22.0f);
    }
    else
    {
        DASHBOARD_LOG("Fonts",
                      "'fonts/Roboto-Medium.ttf' not found relative to cwd or "
                      "to the executable's own directory -- using ImGui's "
                      "default font. Check CMake's font-copy step actually ran "
                      "(build/fonts/ should exist next to the binary).");
        fonts.regular = io.Fonts->AddFontDefault();
        fonts.header = fonts.regular;
    }

    if (!mono_path.empty())
        fonts.mono = io.Fonts->AddFontFromFileTTF(mono_path.c_str(), 16.0f);
    else
        fonts.mono = fonts.regular; // acceptable fallback, just not monospaced

    return fonts;
}

// ---------------------------------------------------------------------
// App shell: one full-window layout (sidebar + content), not a pile of
// independent floating windows. Only one page's content is drawn per
// frame, so no cross-page widget-ID collisions are possible even though
// every draw_*_panel function below shares the same window/ID scope now.
// ---------------------------------------------------------------------
enum class Page
{
    Trading,
    OrderBooks,
    TradeFeed,
    ServiceDiscovery,
    ClientLookup,
};

struct NavItem
{
    Page page;
    const char *label;
};

constexpr NavItem kNavItems[] = {
    { Page::Trading, "Trading" },
    { Page::OrderBooks, "Order Books" },
    { Page::TradeFeed, "Trade Feed" },
    { Page::ServiceDiscovery, "Service Discovery" },
    { Page::ClientLookup, "Client Lookup" },
};

constexpr float kTopBarHeight = 66.0f;

// ---------------------------------------------------------------------
// One instance of this = one trading panel = one authenticated client.
// ---------------------------------------------------------------------
struct TradingPanel
{
    std::string label; // "Client A", "Client B", ...
    ImVec4 avatar_color =
        ImVec4(0.36f, 0.31f, 0.91f, 1.0f); // set per-panel at
                                           // construction, see
                                           // panel_a/panel_b below
    char host_buf[64];
    int port = config::kTradingGatewayPort;
    uint32_t client_id = 0;
    int gateway_id = config::kDefaultGatewayId; // ONLY used when impersonating
                                                // a gateway to CDP (see
                                                // query_balance()) -- the
                                                // trading ClientRequest
                                                // handshake has no GatewayId
                                                // field, so this never
                                                // reaches the real gateway
                                                // connection at all.
    char key_hex[65] = { 0 }; // hex input for the 32-byte raw key
    std::string connect_error;

    MpscQueue<Gateways::OrderConfirmation> confirmations;
    std::unique_ptr<TradingSession> session;
    uint32_t next_local_order_id = 1;

    // Balance is queried from CDP only on explicit user action (the
    // "Refresh Balance" button), never automatically.
    //
    // Root cause of the earlier "client 1 can't authenticate" bug: this
    // isn't a timing race, it's an identity collision. CDP appears to
    // track a single active connection/fd per GatewayId -- so ANY
    // connection presenting GatewayId=0 (real gateway or, as here, an
    // impersonator) can have CDP start routing traffic meant for the
    // real SocketGateway's own connection to it instead, for as long as
    // that connection is open. It doesn't matter whether anything else
    // happens to be connecting at that exact moment.
    //
    // Manual-only + short-lived (this poller closes its socket right
    // after each query) shrinks the window this can happen in, but does
    // NOT eliminate it -- every CDP query still briefly presents itself
    // as "gateway 0" and can still hijack the real gateway's session for
    // that window. The durable fix is a GatewayId dedicated to this
    // dashboard that's distinct from any real trading gateway's id, once
    // there's a way to register/authorize that (see the "gateway id to
    // be defined later" note -- worth using that upcoming API to assign
    // the dashboard its own id rather than reusing a real gateway's).
    MpscQueue<ClientBalanceSnapshot> balance_results;
    bool balance_query_in_flight = false;

    // order form state
    int32_t asset_id = 1;
    int64_t price = 0;
    uint32_t amount = 0;
    int type_idx = 0; // 0 = LIMIT, 1 = MARKET
    int side_idx = 0; // 0 = BUY, 1 = SELL

    // Order book preview shown alongside this panel -- independent of
    // connection state (pure market-data viewing), for debugging while
    // placing orders without switching to the separate Order Books page.
    int32_t watch_asset_id = 1;

    // Floating popup toasts (ephemeral, ~4s lifetime) -- see
    // draw_toast_overlay().
    struct Toast
    {
        std::string text;
        bool is_error;
        float ttl;
    };
    std::vector<Toast> toasts;

    // Separate, actually-persistent history for the "Recent activity"
    // card -- NOT the same list as toasts above. created_at is a
    // glfwGetTime() timestamp, used to render "Ns ago"/"Nm ago".
    struct NotificationEntry
    {
        std::string text;
        bool is_error;
        double created_at;
    };
    std::vector<NotificationEntry> notification_history;
    static constexpr size_t kMaxHistory = 50;

    TradingPanel(std::string lbl)
        : label(std::move(lbl))
    {
        std::snprintf(host_buf, sizeof(host_buf), "%s",
                      config::kTradingGatewayHost);
    }

    static bool parse_hex_key(const char *hex, std::array<uint8_t, 32> &out)
    {
        size_t len = std::strlen(hex);
        if (len != 64)
            return false;
        for (size_t i = 0; i < 32; ++i)
        {
            unsigned int byte;
            if (std::sscanf(hex + i * 2, "%2x", &byte) != 1)
                return false;
            out[i] = static_cast<uint8_t>(byte);
        }
        return true;
    }

    void do_connect()
    {
        std::array<uint8_t, 32> key{};
        if (!parse_hex_key(key_hex, key))
        {
            connect_error = "key must be 64 hex chars (32 bytes)";
            return;
        }
        connect_error.clear();
        session = std::make_unique<TradingSession>(confirmations);
        std::string host = host_buf;
        uint16_t connect_port = static_cast<uint16_t>(port);
        uint32_t cid = client_id;
        // Blocking connect happens off the render thread.
        std::thread([this, host, connect_port, cid, key] {
            session->connect(host, connect_port, cid, key);
        }).detach();
    }

    void do_disconnect()
    {
        if (session)
        {
            std::thread([s = session.get()] { s->disconnect(); }).detach();
        }
    }

    // Manual-only balance query -- see the comment on balance_results
    // above for why this isn't automatic. cdp_host/cdp_port come from
    // whatever CDP endpoint was discovered in etcd (see call site).
    void query_balance(const std::string &cdp_host, uint16_t cdp_port)
    {
        if (balance_query_in_flight)
            return;
        std::array<uint8_t, 32> key{};
        if (!parse_hex_key(key_hex, key))
            return;
        balance_query_in_flight = true;
        uint32_t cid = client_id;
        uint16_t gw_id = static_cast<uint16_t>(gateway_id);
        std::thread([this, cdp_host, cdp_port, cid, key, gw_id] {
            auto snap =
                ClientDetailsPoller::query(cdp_host, cdp_port, cid, key, gw_id);
            balance_results.push(std::move(snap));
        }).detach();
    }

    void push_toast(std::string text, bool is_error)
    {
        // Recorded into two places on purpose: toasts (ephemeral, for the
        // floating popup) and notification_history (persistent, for the
        // "Recent activity" card) -- these used to be the same list,
        // which meant "recent activity" silently vanished after ~4s.
        notification_history.push_back({ text, is_error, glfwGetTime() });
        if (notification_history.size() > kMaxHistory)
            notification_history.erase(notification_history.begin());
        toasts.push_back({ std::move(text), is_error, 4.0f });
    }

    // Drains this panel's confirmation queue and its balance tracking is
    // handled centrally in AppState (via client_id), not here.
    void drain_confirmations()
    {
        for (auto &c : confirmations.drain_all())
        {
            std::ostringstream oss;
            oss << "Order " << c.OrderId << " (local #" << c.ClientOrderId
                << "): " << Gateways::ToString(c.Status);
            push_toast(oss.str(),
                       c.Status != Gateways::OrderConfirmationStatus::Accepted);
        }
    }

    // Drains a panel's own balance_results queue into app_state.balances,
    // clearing its in-flight flag. Called once per frame per panel from
    // the render loop.
    void drain_balance_results(AppState &app_state)
    {
        for (auto &snap : balance_results.drain_all())
        {
            balance_query_in_flight = false;
            auto &bal = app_state.balances[snap.client_id];
            if (snap.ok)
            {
                bal.has_snapshot = true;
                bal.positions.clear();
                for (auto &[asset_id, confirmed, attempt] : snap.positions)
                    bal.positions[asset_id] = { confirmed, attempt };
                bal.last_error.clear();
            }
            else
            {
                bal.last_error = snap.error;
            }
        }
    }

    void tick_toasts(float dt)
    {
        for (auto &t : toasts)
            t.ttl -= dt;
        toasts.erase(
            std::remove_if(toasts.begin(), toasts.end(),
                           [](const Toast &t) { return t.ttl <= 0.0f; }),
            toasts.end());
    }
};

// ---------------------------------------------------------------------
// UI panels
// ---------------------------------------------------------------------

// A field label above its widget, full width, instead of ImGui's default
// widget-then-label-beside-it convention -- the default overflows badly
// on anything but the shortest labels once two panels share a row.
static void field_label(const char *text)
{
    ImGui::TextColored(theme::kTextDim, "%s", text);
    ImGui::SetNextItemWidth(-1);
}

// Forward declaration -- defined below draw_trading_panel, but the
// trading panel embeds a compact per-asset order book for debugging, so
// it needs to call this before the definition appears in the file.
static void draw_order_book_table(const OrderBook &book, const AppFonts &fonts,
                                  int max_rows = 15);

static void draw_trading_panel(TradingPanel &panel, AppState &app_state,
                               const AppFonts &fonts)
{
    ImGui::PushID(panel.label.c_str());

    // Avatar circle -- drawn via ImDrawList since ImGui has no native
    // avatar widget. Just a colored disc + first letter of the label.
    {
        float d = 34.0f;
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 center = ImVec2(p0.x + d * 0.5f, p0.y + d * 0.5f);
        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        draw_list->AddCircleFilled(center, d * 0.5f,
                                   ImGui::GetColorU32(panel.avatar_color), 24);
        char initial[2] = { panel.label.empty() ? '?' : panel.label.back(),
                            '\0' };
        // label is "Client A"/"Client B" -- last char is the identifying letter
        ImVec2 text_size = ImGui::CalcTextSize(initial);
        ImVec2 text_pos = ImVec2(center.x - text_size.x * 0.5f,
                                 center.y - text_size.y * 0.5f);
        draw_list->AddText(text_pos, IM_COL32(255, 255, 255, 255), initial);
        ImGui::Dummy(ImVec2(d, d));
    }
    ImGui::SameLine(0, 10);
    ImGui::BeginGroup();
    ImGui::PushFont(fonts.header);
    ImGui::TextUnformatted(panel.label.c_str());
    ImGui::PopFont();

    bool connected =
        panel.session && panel.session->state() == SessionState::Connected;
    if (connected)
        ImGui::TextColored(theme::kPositive, "Connected");
    else
        ImGui::TextColored(theme::kTextFaint, "Disconnected");
    ImGui::EndGroup();

    ImGui::Dummy(ImVec2(0, 6));

    // ---- Connection card --------------------------------------------
    ImGui::BeginChild("connection_card", ImVec2(0, connected ? 74 : 460), true);
    if (!connected)
    {
        if (!app_state.socket_gateways.empty())
        {
            field_label("Discovered gateways");
            if (ImGui::BeginCombo("##discovered_gw", "pick to autofill"))
            {
                for (auto &[gw_id, info] : app_state.socket_gateways)
                {
                    std::string item_label = "gateway " + std::to_string(gw_id)
                        + " (" + info.addr + ", " + info.status + ")";
                    if (ImGui::Selectable(item_label.c_str()))
                    {
                        auto [host, p] = parse_host_port(info.addr);
                        std::snprintf(panel.host_buf, sizeof(panel.host_buf),
                                      "%s", host.c_str());
                        panel.port = p;
                        // Deliberately NOT setting panel.gateway_id here --
                        // that field is only for CDP impersonation and is
                        // unrelated to which trading gateway host/port this
                        // is. Overwriting it with the discovered (real)
                        // gateway's id would silently revert the
                        // config::kDefaultGatewayId=10 CDP-safe default
                        // back to the real gateway's id (0), reintroducing
                        // the session-hijacking collision.
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::Dummy(ImVec2(0, 6));
        }
        field_label("Host");
        ImGui::InputText("##host", panel.host_buf, sizeof(panel.host_buf));
        field_label("Port");
        ImGui::InputInt("##port", &panel.port, 0, 0);
        field_label("Gateway ID");
        ImGui::InputInt("##gateway_id", &panel.gateway_id, 0, 0);
        ImGui::TextDisabled(
            "Used only for CDP balance queries, not sent to the gateway");
        field_label("Client ID");
        ImGui::InputScalar("##client_id", ImGuiDataType_U32, &panel.client_id);
        field_label("Key (hex, 64 chars)");
        ImGui::InputText("##key", panel.key_hex, sizeof(panel.key_hex));
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::PushStyleColor(ImGuiCol_Button, theme::kAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::kAccentHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::kAccentActive);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::rgba(255, 255, 255));
        if (ImGui::Button("Connect", ImVec2(120, 0)))
            panel.do_connect();
        ImGui::PopStyleColor(4);
        if (!panel.connect_error.empty())
            ImGui::TextColored(theme::kNegative, "%s",
                               panel.connect_error.c_str());
        if (panel.session && panel.session->state() == SessionState::Failed)
            ImGui::TextColored(theme::kNegative, "Connection failed: %s",
                               panel.session->last_error().c_str());
    }
    else
    {
        ImGui::Text("Client %u @ %s:%d", panel.client_id, panel.host_buf,
                    panel.port);
        ImGui::SameLine(0, 20);
        // Danger-ghost: light red tint + red text, not a solid red fill --
        // reserves the strongest color for BUY/SELL, where it matters more.
        ImGui::PushStyleColor(ImGuiCol_Button, theme::kNegativeSoft);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              theme::rgba(250, 220, 218));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                              theme::rgba(247, 205, 202));
        ImGui::PushStyleColor(ImGuiCol_Text, theme::kNegative);
        if (ImGui::Button("Disconnect"))
            panel.do_disconnect();
        ImGui::PopStyleColor(4);
    }
    ImGui::EndChild();

    if (connected)
    {
        ImGui::Dummy(ImVec2(0, 8));

        // ---- Balances card --------------------------------------------
        ImGui::TextUnformatted("Balances");
        ImGui::BeginChild("balances_card", ImVec2(0, 175), true);
        if (panel.balance_query_in_flight)
            ImGui::TextDisabled("Querying CDP...");
        else if (ImGui::Button("Refresh Balance"))
        {
            if (!app_state.cdp_endpoints.empty())
            {
                auto [cdp_host, cdp_port] = parse_host_port(
                    app_state.cdp_endpoints.begin()->second.addr);
                panel.query_balance(cdp_host, cdp_port);
            }
        }
        auto bal_it = app_state.balances.find(panel.client_id);
        if (bal_it != app_state.balances.end() && bal_it->second.has_snapshot)
        {
            ImGui::PushFont(fonts.mono);
            if (ImGui::BeginTable("balances", 3,
                                  ImGuiTableFlags_Borders
                                      | ImGuiTableFlags_RowBg
                                      | ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableSetupColumn("Asset");
                ImGui::TableSetupColumn("Confirmed");
                ImGui::TableSetupColumn("Pending");
                ImGui::TableHeadersRow();
                for (auto &[asset_id, pos] : bal_it->second.positions)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    if (asset_id == 0)
                        ImGui::TextColored(theme::kWarning, "Cash");
                    else
                        ImGui::Text("Asset %u", asset_id);
                    ImGui::TableNextColumn();
                    ImGui::Text("%lld", (long long)pos.first);
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%lld", (long long)pos.second);
                }
                ImGui::EndTable();
            }
            ImGui::PopFont();
        }
        else
        {
            ImGui::TextDisabled("No balance snapshot yet");
        }
        ImGui::EndChild();

        ImGui::Dummy(ImVec2(0, 8));

        // ---- Order form card --------------------------------------------
        ImGui::TextUnformatted("Place Order");
        ImGui::BeginChild("order_card", ImVec2(0, 470), true);
        if (!app_state.matching_engines.empty())
        {
            ImGui::TextDisabled("Known assets:");
            ImGui::SameLine();
            for (auto &[key, info] : app_state.matching_engines)
                ImGui::TextDisabled("%u ", info.asset_id);
            ImGui::Dummy(ImVec2(0, 4));
        }
        field_label("Asset");
        ImGui::InputInt("##asset", &panel.asset_id, 0, 0);
        field_label("Order Type");
        const char *types[] = { "LIMIT", "MARKET" };
        ImGui::Combo("##type", &panel.type_idx, types, 2);
        bool is_market = panel.type_idx == 1;
        if (!is_market)
        {
            field_label("Price");
            ImGui::InputScalar("##price", ImGuiDataType_S64, &panel.price);
        }
        field_label("Amount");
        ImGui::InputScalar("##amount", ImGuiDataType_U32, &panel.amount);

        // Side selector as two colored toggle buttons rather than a combo
        // -- buy/sell is the single most important choice on this form,
        // it should be impossible to misread at a glance.
        ImGui::Dummy(ImVec2(0, 2));
        ImGui::TextColored(theme::kTextDim, "Side");
        bool is_buy = panel.side_idx == 0;
        ImGui::PushStyleColor(ImGuiCol_Button,
                              is_buy ? theme::kPositive : theme::kBgElevated);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              is_buy ? theme::rgba(40, 180, 95)
                                     : theme::rgba(242, 243, 246));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                              is_buy ? theme::rgba(15, 140, 60)
                                     : theme::rgba(236, 237, 242));
        ImGui::PushStyleColor(ImGuiCol_Text,
                              is_buy ? theme::rgba(255, 255, 255)
                                     : theme::kTextDim);
        if (ImGui::Button("BUY", ImVec2(90, 0)))
            panel.side_idx = 0;
        ImGui::PopStyleColor(4);
        ImGui::SameLine();
        bool is_sell = panel.side_idx == 1;
        ImGui::PushStyleColor(ImGuiCol_Button,
                              is_sell ? theme::kNegative : theme::kBgElevated);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              is_sell ? theme::rgba(235, 100, 92)
                                      : theme::rgba(242, 243, 246));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                              is_sell ? theme::rgba(200, 55, 48)
                                      : theme::rgba(236, 237, 242));
        ImGui::PushStyleColor(ImGuiCol_Text,
                              is_sell ? theme::rgba(255, 255, 255)
                                      : theme::kTextDim);
        if (ImGui::Button("SELL", ImVec2(90, 0)))
            panel.side_idx = 1;
        ImGui::PopStyleColor(4);

        ImGui::Dummy(ImVec2(0, 8));
        // Hover/active need to be visibly different from the resting
        // state, or the button reads as inert/unclickable -- the base
        // color alone isn't enough feedback.
        ImVec4 submit_base = is_buy ? theme::kPositive : theme::kNegative;
        ImVec4 submit_hover =
            is_buy ? theme::rgba(40, 180, 95) : theme::rgba(235, 100, 92);
        ImVec4 submit_active =
            is_buy ? theme::rgba(15, 140, 60) : theme::rgba(200, 55, 48);
        ImGui::PushStyleColor(ImGuiCol_Button, submit_base);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, submit_hover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, submit_active);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::rgba(255, 255, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 12));
        if (ImGui::Button(is_buy ? "Submit BUY order" : "Submit SELL order",
                          ImVec2(-1, 0)))
        {
            uint32_t local_id = panel.next_local_order_id++;
            bool ok = panel.session->send_order(
                local_id,
                panel.type_idx == 0 ? Gateways::OrderType::LIMIT
                                    : Gateways::OrderType::MARKET,
                panel.side_idx == 0 ? Gateways::OrderSide::BUY
                                    : Gateways::OrderSide::SELL,
                panel.price, panel.amount, panel.asset_id);
            if (!ok)
                panel.push_toast("Failed to send order (socket error)", true);
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);
        ImGui::EndChild();
    }

    ImGui::Dummy(ImVec2(0, 8));

    // ---- Order book preview card ---------------------------------------
    // Independent of connection state -- lets you watch the book for
    // whatever asset you're about to trade without leaving this page.
    ImGui::TextUnformatted("Order Book Preview");
    ImGui::BeginChild("book_preview_card", ImVec2(0, 260), true);
    field_label("Asset");
    ImGui::SetNextItemWidth(140);
    ImGui::InputInt("##watch_asset", &panel.watch_asset_id, 0, 0);
    ImGui::Dummy(ImVec2(0, 4));
    auto book_it =
        app_state.order_books.find(static_cast<uint16_t>(panel.watch_asset_id));
    if (book_it != app_state.order_books.end())
        draw_order_book_table(book_it->second, fonts, 6);
    else
        ImGui::TextDisabled("No order book data for asset %d yet",
                            panel.watch_asset_id);
    ImGui::EndChild();

    ImGui::Dummy(ImVec2(0, 8));

    // ---- Recent activity card -------------------------------------------
    ImGui::TextUnformatted("Recent Activity");
    ImGui::BeginChild("notif_history", ImVec2(0, 130), true);
    if (panel.notification_history.empty())
        ImGui::TextDisabled("No recent activity");
    else
    {
        double now = glfwGetTime();
        // Newest first.
        for (auto it = panel.notification_history.rbegin();
             it != panel.notification_history.rend(); ++it)
        {
            ImVec4 col = it->is_error ? theme::kNegative : theme::kPositive;
            ImGui::TextColored(col, "%s", it->text.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s",
                                format_time_ago(now - it->created_at).c_str());
        }
    }
    ImGui::EndChild();

    ImGui::PopID();
}

// Draws a partial-width filled rect at an explicit position, anchored to
// one edge -- used for order book depth bars. Takes an explicit position
// (rather than the current cursor) so ONE bar can span both the amount
// and price columns for a side as a single continuous shape, matching
// how real exchange order books draw depth (Binance, etc.) -- an
// ImGui Table can't do this cleanly since each cell clips independently,
// which is why the whole book row below is laid out manually rather
// than with ImGui::BeginTable.
static void draw_depth_bar_at(ImDrawList *dl, ImVec2 p0, float w, float h,
                              ImVec4 color, float frac, bool anchor_right)
{
    float bar_w = w * std::clamp(frac, 0.0f, 1.0f);
    ImVec2 bmin, bmax;
    if (anchor_right)
    {
        bmin = ImVec2(p0.x + w - bar_w, p0.y);
        bmax = ImVec2(p0.x + w, p0.y + h);
    }
    else
    {
        bmin = ImVec2(p0.x, p0.y);
        bmax = ImVec2(p0.x + bar_w, p0.y + h);
    }
    dl->AddRectFilled(bmin, bmax, ImGui::GetColorU32(color));
}

// Binance-style book: Amount|Price for bids (right-aligned, growing from
// the shared center divider), Price|Amount for asks (left-aligned,
// growing from the divider outward) -- one continuous bar per side, plus
// a top bid/ask proportion strip.
static void draw_order_book_table(const OrderBook &book, const AppFonts &fonts,
                                  int max_rows)
{
    ImGui::PushFont(fonts.mono);

    uint32_t max_qty = 1;
    uint64_t bid_total = 0, ask_total = 0;
    {
        int i = 0;
        for (auto it = book.bids.begin(); it != book.bids.end() && i < max_rows;
             ++it, ++i)
        {
            max_qty = std::max<uint32_t>(max_qty, it->second);
            bid_total += it->second;
        }
        i = 0;
        for (auto it = book.asks.begin(); it != book.asks.end() && i < max_rows;
             ++it, ++i)
        {
            max_qty = std::max<uint32_t>(max_qty, it->second);
            ask_total += it->second;
        }
    }

    // Top bid/ask proportion strip, Binance-style.
    {
        uint64_t total = bid_total + ask_total;
        float bid_frac = total > 0
            ? static_cast<float>(bid_total) / static_cast<float>(total)
            : 0.5f;
        float bar_w = ImGui::GetContentRegionAvail().x;
        float bar_h = 6.0f;
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        float split_x = p0.x + bar_w * bid_frac;
        dl->AddRectFilled(p0, ImVec2(split_x, p0.y + bar_h),
                          ImGui::GetColorU32(theme::kPositive));
        dl->AddRectFilled(ImVec2(split_x, p0.y),
                          ImVec2(p0.x + bar_w, p0.y + bar_h),
                          ImGui::GetColorU32(theme::kNegative));
        ImGui::Dummy(ImVec2(bar_w, bar_h));
        ImGui::PopFont(); // percentages read clearer in the UI face than mono
        ImGui::TextColored(theme::kPositive, "%.1f%%", bid_frac * 100.0f);
        ImGui::SameLine(bar_w - ImGui::CalcTextSize("100.0%").x);
        ImGui::TextColored(theme::kNegative, "%.1f%%",
                           (1.0f - bid_frac) * 100.0f);
        ImGui::PushFont(fonts.mono);
        ImGui::Dummy(ImVec2(0, 4));
    }

    float total_width = ImGui::GetContentRegionAvail().x;
    float gutter = 8.0f;
    float half_width = (total_width - gutter) * 0.5f;
    float sub_width = half_width * 0.5f;

    ImGui::PopFont();
    ImGui::TextColored(theme::kTextFaint, "Amount");
    ImGui::SameLine(sub_width);
    ImGui::TextColored(theme::kTextFaint, "Price");
    ImGui::SameLine(half_width + gutter);
    ImGui::TextColored(theme::kTextFaint, "Price");
    ImGui::SameLine(half_width + gutter + sub_width);
    ImGui::TextColored(theme::kTextFaint, "Amount");
    ImGui::Separator();
    ImGui::PushFont(fonts.mono);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    float row_h = ImGui::GetTextLineHeightWithSpacing();
    auto bid_it = book.bids.begin();
    auto ask_it = book.asks.begin();

    for (int row = 0; row < max_rows
         && (bid_it != book.bids.end() || ask_it != book.asks.end());
         ++row)
    {
        ImVec2 row_p0 = ImGui::GetCursorScreenPos();
        float text_y = row_p0.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f;

        if (bid_it != book.bids.end())
        {
            float frac = static_cast<float>(bid_it->second)
                / static_cast<float>(max_qty);
            draw_depth_bar_at(dl, row_p0, half_width, row_h,
                              theme::rgba(22, 163, 74, 28), frac,
                              /*anchor_right=*/true);

            char qty_buf[32];
            std::snprintf(qty_buf, sizeof(qty_buf), "%u", bid_it->second);
            char price_buf[32];
            std::snprintf(price_buf, sizeof(price_buf), "%lld",
                          (long long)bid_it->first);
            ImVec2 qty_sz = ImGui::CalcTextSize(qty_buf);
            ImVec2 price_sz = ImGui::CalcTextSize(price_buf);
            dl->AddText(ImVec2(row_p0.x + sub_width - 6 - qty_sz.x, text_y),
                        ImGui::GetColorU32(theme::kTextDim), qty_buf);
            dl->AddText(ImVec2(row_p0.x + half_width - 6 - price_sz.x, text_y),
                        ImGui::GetColorU32(theme::kPositive), price_buf);
            ++bid_it;
        }

        if (ask_it != book.asks.end())
        {
            float frac = static_cast<float>(ask_it->second)
                / static_cast<float>(max_qty);
            ImVec2 ask_p0 = ImVec2(row_p0.x + half_width + gutter, row_p0.y);
            draw_depth_bar_at(dl, ask_p0, half_width, row_h,
                              theme::rgba(225, 75, 66, 28), frac,
                              /*anchor_right=*/false);

            char price_buf[32];
            std::snprintf(price_buf, sizeof(price_buf), "%lld",
                          (long long)ask_it->first);
            char qty_buf[32];
            std::snprintf(qty_buf, sizeof(qty_buf), "%u", ask_it->second);
            dl->AddText(ImVec2(ask_p0.x + 6, text_y),
                        ImGui::GetColorU32(theme::kNegative), price_buf);
            dl->AddText(ImVec2(ask_p0.x + sub_width + 6, text_y),
                        ImGui::GetColorU32(theme::kTextDim), qty_buf);
            ++ask_it;
        }

        ImGui::Dummy(ImVec2(total_width, row_h));
    }
    ImGui::PopFont();
}

// Cumulative depth ("valley") chart -- for bids, cumulative(P) = sum of
// qty at price >= P (smallest right at the best bid, growing toward the
// worst price shown); for asks, cumulative(P) = sum of qty at price <= P
// (smallest right at the best ask, growing toward the worst price
// shown). Bids drawn on the left half (worst price at the far left,
// best/spread-side at the center), asks on the right half (best/
// spread-side at the center, worst price at the far right) -- the
// standard exchange depth-chart layout.
static void draw_depth_chart(const OrderBook &book, const AppFonts &fonts,
                             float height, int max_levels = 20)
{
    float width = ImGui::GetContentRegionAvail().x;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList *dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
                      ImGui::GetColorU32(theme::kBgElevated));

    if (book.bids.empty() && book.asks.empty())
    {
        ImGui::Dummy(ImVec2(width, height));
        ImGui::TextDisabled("No depth data yet");
        return;
    }

    // book.bids iterates best->worst already (map uses std::greater<>);
    // book.asks iterates best->worst already (plain ascending map).
    std::vector<std::pair<int64_t, uint32_t>> bid_cum,
        ask_cum; // (price, cumulative)
    {
        uint32_t running = 0;
        int i = 0;
        for (auto it = book.bids.begin();
             it != book.bids.end() && i < max_levels; ++it, ++i)
        {
            running += it->second;
            bid_cum.push_back({ it->first, running });
        }
        running = 0;
        i = 0;
        for (auto it = book.asks.begin();
             it != book.asks.end() && i < max_levels; ++it, ++i)
        {
            running += it->second;
            ask_cum.push_back({ it->first, running });
        }
    }

    uint32_t max_cum = 1;
    if (!bid_cum.empty())
        max_cum = std::max(max_cum, bid_cum.back().second);
    if (!ask_cum.empty())
        max_cum = std::max(max_cum, ask_cum.back().second);

    float half_w = width * 0.5f;
    auto y_for = [&](uint32_t cum) {
        return origin.y + height
            - height * (static_cast<float>(cum) / static_cast<float>(max_cum));
    };

    // Bid side: bid_cum is best(i=0)->worst(i=N-1). Plotted left-to-right
    // as worst->best (worst at x=0, best at x=half_w), so walk it in
    // reverse. Step shape: flat at each level's cumulative until the
    // next (better) price, then step down. Filled as one rectangle per
    // step rather than a single polygon -- AddConvexPolyFilled requires
    // an actually convex shape, and a staircase is not one; per-step
    // rects are simple, correct regardless, and just as fast here.
    if (!bid_cum.empty())
    {
        size_t n = bid_cum.size();
        std::vector<ImVec2> outline;
        outline.push_back(ImVec2(origin.x, origin.y + height));
        for (size_t i = 0; i < n; ++i)
        {
            size_t rev_i = n - 1 - i; // i=0 -> worst price, i=n-1 -> best price
            float x0 = origin.x
                + (static_cast<float>(i) / static_cast<float>(n)) * half_w;
            float x1 = origin.x
                + (static_cast<float>(i + 1) / static_cast<float>(n)) * half_w;
            float y = y_for(bid_cum[rev_i].second);
            dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, origin.y + height),
                              ImGui::GetColorU32(theme::rgba(22, 163, 74, 60)));
            outline.push_back(ImVec2(x0, y));
            outline.push_back(ImVec2(x1, y));
        }
        for (size_t i = 0; i + 1 < outline.size(); ++i)
            dl->AddLine(outline[i], outline[i + 1],
                        ImGui::GetColorU32(theme::kPositive), 1.5f);
    }

    // Ask side: ask_cum is best(i=0)->worst(i=N-1), plotted left-to-right
    // as best->worst directly (best/spread-side at x=half_w, worst at
    // x=width) -- no reversal needed, opposite of the bid side.
    if (!ask_cum.empty())
    {
        size_t n = ask_cum.size();
        std::vector<ImVec2> outline;
        for (size_t i = 0; i < n; ++i)
        {
            float x0 = origin.x + half_w
                + (static_cast<float>(i) / static_cast<float>(n)) * half_w;
            float x1 = origin.x + half_w
                + (static_cast<float>(i + 1) / static_cast<float>(n)) * half_w;
            float y = y_for(ask_cum[i].second);
            dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, origin.y + height),
                              ImGui::GetColorU32(theme::rgba(225, 75, 66, 60)));
            outline.push_back(ImVec2(x0, y));
            outline.push_back(ImVec2(x1, y));
        }
        outline.push_back(ImVec2(origin.x + width, origin.y + height));
        for (size_t i = 0; i + 1 < outline.size(); ++i)
            dl->AddLine(outline[i], outline[i + 1],
                        ImGui::GetColorU32(theme::kNegative), 1.5f);
    }

    // Center divider (the spread).
    dl->AddLine(ImVec2(origin.x + half_w, origin.y),
                ImVec2(origin.x + half_w, origin.y + height),
                ImGui::GetColorU32(theme::kBorder), 1.0f);

    ImGui::Dummy(ImVec2(width, height));

    // Axis labels: worst bid, best bid, best ask, worst ask.
    ImGui::PushFont(fonts.mono);
    if (!bid_cum.empty())
    {
        ImGui::TextColored(theme::kTextFaint, "%lld",
                           (long long)bid_cum.back().first);
        ImGui::SameLine(half_w - ImGui::CalcTextSize("000000").x * 0.5f);
        ImGui::TextColored(theme::kTextFaint, "%lld",
                           (long long)bid_cum.front().first);
    }
    if (!ask_cum.empty())
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld",
                      (long long)ask_cum.front().first);
        ImGui::SameLine(half_w + 4);
        ImGui::TextColored(theme::kTextFaint, "%s", buf);
        std::snprintf(buf, sizeof(buf), "%lld",
                      (long long)ask_cum.back().first);
        ImGui::SameLine(width - ImGui::CalcTextSize(buf).x);
        ImGui::TextColored(theme::kTextFaint, "%s", buf);
    }
    ImGui::PopFont();
}

static void draw_order_book_panel(const AppState &app_state,
                                  const AppFonts &fonts)
{
    if (app_state.order_books.empty())
    {
        ImGui::TextDisabled("No order book data yet");
        return;
    }
    static int view_mode = 0; // 0 = table, 1 = depth chart
    ImGui::Text("View");
    ImGui::SameLine();
    // Same segmented-toggle pattern as BUY/SELL elsewhere in the app --
    // both options always the same size, with a filled accent background
    // on whichever is selected and a plain neutral background on the
    // other, so the two states are unambiguous at a glance. The earlier
    // version used ImGui::Selectable for both, which only gave the
    // SELECTED one any background at all -- the unselected one was bare
    // text, making the pair look mismatched rather than like a toggle.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 6));
    {
        bool is_table = view_mode == 0;
        ImGui::PushStyleColor(ImGuiCol_Button,
                              is_table ? theme::kAccent : theme::kBgElevated);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              is_table ? theme::kAccentHover
                                       : theme::rgba(242, 243, 246));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                              is_table ? theme::kAccentActive
                                       : theme::rgba(236, 237, 246));
        ImGui::PushStyleColor(ImGuiCol_Text,
                              is_table ? theme::rgba(255, 255, 255)
                                       : theme::kTextDim);
        if (ImGui::Button("Table", ImVec2(70, 0)))
            view_mode = 0;
        ImGui::PopStyleColor(4);
    }
    ImGui::SameLine(0, 4);
    {
        bool is_depth = view_mode == 1;
        ImGui::PushStyleColor(ImGuiCol_Button,
                              is_depth ? theme::kAccent : theme::kBgElevated);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              is_depth ? theme::kAccentHover
                                       : theme::rgba(242, 243, 246));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                              is_depth ? theme::kAccentActive
                                       : theme::rgba(236, 237, 246));
        ImGui::PushStyleColor(ImGuiCol_Text,
                              is_depth ? theme::rgba(255, 255, 255)
                                       : theme::kTextDim);
        if (ImGui::Button("Depth", ImVec2(70, 0)))
            view_mode = 1;
        ImGui::PopStyleColor(4);
    }
    ImGui::PopStyleVar();
    ImGui::Dummy(ImVec2(0, 6));

    for (auto &[asset_id, book] : app_state.order_books)
    {
        ImGui::PushFont(fonts.header);
        bool open = ImGui::TreeNodeEx((void *)(intptr_t)asset_id,
                                      ImGuiTreeNodeFlags_DefaultOpen
                                          | ImGuiTreeNodeFlags_Framed,
                                      "Asset %u", asset_id);
        ImGui::PopFont();
        if (open)
        {
            if (view_mode == 0)
                draw_order_book_table(book, fonts);
            else
                draw_depth_chart(book, fonts, 220.0f);
            ImGui::TreePop();
        }
        ImGui::Dummy(ImVec2(0, 8));
    }
}

static void draw_trade_feed_panel(const AppState &app_state,
                                  const AppFonts &fonts)
{
    ImGui::BeginChild("feed_scroll", ImVec2(0, 0), false);
    if (app_state.trade_feed.empty())
        ImGui::TextDisabled("No trades yet");
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    for (auto &e : app_state.trade_feed)
    {
        // Every report has one leg in the settlement currency (asset 0)
        // and one leg in the actual traded asset -- pick whichever leg
        // ISN'T asset 0 as "the trade", and read bought/sold off that
        // leg's own sign. This is what turns "BoughtDelta=-13,
        // SoldDelta=+13" (the wire format, written from the matching
        // engine's fixed labeling, not this client's perspective) into
        // a plain "Client 1 sold 13 of asset 1" -- the raw numbers
        // stayed correct either way, this only changes how it reads.
        uint16_t asset = e.bought_asset != 0 ? e.bought_asset : e.sold_asset;
        int64_t delta = e.bought_asset != 0 ? e.bought_delta : e.sold_delta;
        bool bought = delta >= 0;
        int64_t amount = delta < 0 ? -delta : delta;
        ImVec4 color = bought ? theme::kPositive : theme::kNegative;

        float dot_r = 4.0f;
        float line_h = ImGui::GetTextLineHeight();
        ImVec2 p = ImGui::GetCursorScreenPos();
        draw_list->AddCircleFilled(ImVec2(p.x + dot_r, p.y + line_h * 0.5f),
                                   dot_r, ImGui::GetColorU32(color));
        ImGui::Dummy(ImVec2(dot_r * 2 + 10, 0));
        ImGui::SameLine(0, 0);

        ImGui::Text("Client %u", e.client_id);
        ImGui::SameLine();
        ImGui::TextColored(color, "%s", bought ? "bought" : "sold");
        ImGui::SameLine();
        ImGui::PushFont(fonts.mono);
        ImGui::TextColored(color, "%lld", (long long)amount);
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextDisabled("of asset %u", asset);
        ImGui::SameLine();
        ImGui::TextDisabled("#%u", e.trade_id);
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

// Non-interactive rounded status badge (e.g. "active"), drawn manually
// via ImDrawList rather than a real button -- a real widget would be
// clickable, which is misleading for something that's just a status
// readout.
static void draw_status_pill(const char *text, ImVec4 bg, ImVec4 fg)
{
    ImVec2 text_size = ImGui::CalcTextSize(text);
    ImVec2 padding(9, 3);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 size(text_size.x + padding.x * 2, text_size.y + padding.y * 2);
    ImGui::GetWindowDrawList()->AddRectFilled(
        p0, ImVec2(p0.x + size.x, p0.y + size.y), ImGui::GetColorU32(bg),
        size.y * 0.5f);
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(p0.x + padding.x, p0.y + padding.y), ImGui::GetColorU32(fg),
        text);
    ImGui::Dummy(size);
}

struct DiscoveryCard
{
    std::string title;
    std::string subkey;
    std::string addr;
    std::string status;
};

static void draw_discovery_card(const DiscoveryCard &c, const AppFonts &fonts,
                                float width)
{
    ImGui::BeginChild(c.subkey.c_str(), ImVec2(width, 210), true);
    ImGui::PushFont(fonts.header);
    ImGui::TextWrapped("%s", c.title.c_str());
    ImGui::PopFont();
    ImGui::PushFont(fonts.mono);
    ImGui::TextColored(theme::kTextFaint, "%s", c.subkey.c_str());
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 8));

    ImGui::TextColored(theme::kTextDim, "Address");
    ImGui::PushFont(fonts.mono);
    ImGui::TextUnformatted(c.addr.c_str());
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 4));

    ImGui::TextColored(theme::kTextDim, "Status");
    bool active = c.status == "active";
    draw_status_pill(c.status.empty() ? "unknown" : c.status.c_str(),
                     active ? theme::kPositiveSoft : theme::kBgElevated,
                     active ? theme::kPositive : theme::kTextFaint);
    ImGui::EndChild();
}

static void draw_etcd_panel(const AppState &app_state, const AppFonts &fonts)
{
    std::vector<DiscoveryCard> cards;
    for (auto &[key, info] : app_state.matching_engines)
        cards.push_back(
            { "Matching Engine -- Asset " + std::to_string(info.asset_id), key,
              info.addr, info.status });
    for (auto &[gw_id, info] : app_state.socket_gateways)
        cards.push_back({ "Socket Gateway " + std::to_string(gw_id),
                          "/socket-gateways/" + std::to_string(gw_id),
                          info.addr, info.status });
    for (auto &[key, info] : app_state.cdp_endpoints)
        cards.push_back(
            { "Client Details Provider", key, info.addr, info.status });

    if (cards.empty())
    {
        ImGui::TextDisabled("No resources discovered yet");
    }
    else
    {
        // Simple wrapping flow layout instead of a Table -- a fixed-height
        // bordered BeginChild nested inside a Table cell was silently
        // truncating that child's own content past a certain point (only
        // reproducible with real rendering, not caught by compilation or
        // structural checks). SameLine-based wrapping is a much more
        // battle-tested pattern for this and sidesteps the issue entirely.
        const float card_width = 380.0f;
        float avail_width = ImGui::GetContentRegionAvail().x;
        float used_width = 0.0f;
        for (size_t i = 0; i < cards.size(); ++i)
        {
            if (i > 0)
            {
                if (used_width + card_width <= avail_width)
                    ImGui::SameLine();
                else
                    used_width = 0.0f;
            }
            ImGui::PushID(static_cast<int>(i));
            draw_discovery_card(cards[i], fonts, card_width);
            ImGui::PopID();
            used_width += card_width + ImGui::GetStyle().ItemSpacing.x;
        }
    }

    ImGui::Dummy(ImVec2(0, 8));
    if (ImGui::CollapsingHeader("Raw entries"))
    {
        ImGui::PushFont(fonts.mono);
        for (auto &[key, value] : app_state.etcd_entries)
        {
            ImGui::Separator();
            ImGui::TextWrapped("%s", key.c_str());
            ImGui::TextWrapped("  %s", value.c_str());
        }
        ImGui::PopFont();
    }
}

static void draw_client_lookup_panel(AppState &app_state, const AppFonts &fonts)
{
    static uint32_t lookup_client_id = 0;
    static char lookup_key_hex[65] = { 0 };
    static int lookup_gateway_id = config::kDefaultGatewayId;
    static std::string lookup_error;
    static MpscQueue<ClientBalanceSnapshot> lookup_results;

    if (app_state.cdp_endpoints.empty())
        ImGui::TextDisabled("No CDP endpoint discovered in etcd yet");

    ImGui::BeginChild("lookup_form_card", ImVec2(0, 360), true);
    field_label("Client ID");
    ImGui::InputScalar("##lookup_client_id", ImGuiDataType_U32,
                       &lookup_client_id);
    field_label("Key (hex, 64 chars)");
    ImGui::InputText("##lookup_key", lookup_key_hex, sizeof(lookup_key_hex));
    field_label("Gateway ID to impersonate");
    ImGui::InputInt("##lookup_gateway_id", &lookup_gateway_id, 0, 0);
    ImGui::TextDisabled(
        "Must match this client's authorized gateway or CDP refuses the query");
    ImGui::TextDisabled(
        "CDP tracks one connection per GatewayId -- using a real gateway's id "
        "here can briefly steal its session, not just if it's mid-connect.");
    ImGui::Dummy(ImVec2(0, 4));

    ImGui::PushStyleColor(ImGuiCol_Button, theme::kAccent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::kAccentHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::kAccentActive);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::rgba(255, 255, 255));
    bool query_clicked = ImGui::Button("Query", ImVec2(120, 0))
        && !app_state.cdp_endpoints.empty();
    ImGui::PopStyleColor(4);

    if (query_clicked)
    {
        std::array<uint8_t, 32> key{};
        if (TradingPanel::parse_hex_key(lookup_key_hex, key))
        {
            lookup_error.clear();
            uint32_t cid = lookup_client_id;
            uint16_t gw_id = static_cast<uint16_t>(lookup_gateway_id);
            // Use whichever CDP endpoint was discovered first.
            auto [cdp_host, cdp_port] =
                parse_host_port(app_state.cdp_endpoints.begin()->second.addr);
            std::thread([cdp_host, cdp_port, cid, key, gw_id] {
                auto snap = ClientDetailsPoller::query(cdp_host, cdp_port, cid,
                                                       key, gw_id);
                lookup_results.push(std::move(snap));
            }).detach();
        }
        else
        {
            lookup_error = "key must be 64 hex chars";
        }
    }
    if (!lookup_error.empty())
        ImGui::TextColored(theme::kNegative, "%s", lookup_error.c_str());
    ImGui::EndChild();

    for (auto &snap : lookup_results.drain_all())
    {
        auto &bal = app_state.balances[snap.client_id];
        if (snap.ok)
        {
            bal.has_snapshot = true;
            bal.positions.clear();
            for (auto &[asset_id, confirmed, attempt] : snap.positions)
                bal.positions[asset_id] = { confirmed, attempt };
            bal.last_error.clear();
        }
        else
        {
            bal.last_error = snap.error;
        }
    }

    ImGui::Dummy(ImVec2(0, 8));
    ImGui::TextUnformatted("Result");
    ImGui::BeginChild("lookup_result_card", ImVec2(0, 0), true);
    auto it = app_state.balances.find(lookup_client_id);
    if (it != app_state.balances.end())
    {
        if (!it->second.last_error.empty())
            ImGui::TextColored(theme::kNegative, "Error: %s",
                               it->second.last_error.c_str());
        else if (it->second.has_snapshot)
        {
            ImGui::PushFont(fonts.mono);
            if (ImGui::BeginTable("lookup_balances", 3,
                                  ImGuiTableFlags_Borders
                                      | ImGuiTableFlags_RowBg
                                      | ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableSetupColumn("Asset");
                ImGui::TableSetupColumn("Confirmed");
                ImGui::TableSetupColumn("Pending");
                ImGui::TableHeadersRow();
                for (auto &[asset_id, pos] : it->second.positions)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    if (asset_id == 0)
                        ImGui::TextColored(theme::kWarning, "Cash");
                    else
                        ImGui::Text("Asset %u", asset_id);
                    ImGui::TableNextColumn();
                    ImGui::Text("%lld", (long long)pos.first);
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%lld", (long long)pos.second);
                }
                ImGui::EndTable();
            }
            ImGui::PopFont();
        }
    }
    else
    {
        ImGui::TextDisabled("No query run yet");
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------------
// Toast overlay: floating popup notifications for order confirmations,
// separate from the persistent per-panel "Notifications" history list.
// Drawn as its own top-level window (no border/title/input) anchored to
// the top-right of the viewport, on top of everything else, once per
// frame -- this is what actually "pops up" rather than sitting quietly
// in a scrollable box the person might not be looking at.
// ---------------------------------------------------------------------
static void draw_toast_overlay(TradingPanel &panel_a, TradingPanel &panel_b,
                               const AppFonts &fonts)
{
    struct Entry
    {
        const std::string *owner;
        const std::string *text;
        bool is_error;
        float ttl;
    };
    std::vector<Entry> entries;
    for (auto &t : panel_a.toasts)
        entries.push_back({ &panel_a.label, &t.text, t.is_error, t.ttl });
    for (auto &t : panel_b.toasts)
        entries.push_back({ &panel_b.label, &t.text, t.is_error, t.ttl });
    if (entries.empty())
        return;

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const float pad = 16.0f;
    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - pad,
               viewport->WorkPos.y + pad),
        ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("##ToastOverlay", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground
                     | ImGuiWindowFlags_AlwaysAutoResize
                     | ImGuiWindowFlags_NoSavedSettings
                     | ImGuiWindowFlags_NoFocusOnAppearing
                     | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove
                     | ImGuiWindowFlags_NoInputs);

    ImGui::PushFont(fonts.regular);
    for (auto &e : entries)
    {
        // Fade out over the last second of life rather than popping off
        // abruptly at ttl==0 (it's removed from the source vector by
        // tick_toasts() once ttl<=0, so this only ever animates the tail).
        float alpha = e.ttl < 1.0f ? std::max(e.ttl, 0.0f) : 1.0f;
        ImVec4 accent = e.is_error ? theme::kNegative : theme::kPositive;

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::kBgElevated);
        ImGui::PushStyleColor(ImGuiCol_Border, accent);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 2.0f);
        ImGui::BeginChild(
            ("##toast_" + std::to_string(reinterpret_cast<intptr_t>(e.text)))
                .c_str(),
            ImVec2(320, 0),
            ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY);
        ImGui::TextColored(accent, "%s", e.owner->c_str());
        ImGui::TextWrapped("%s", e.text->c_str());
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        ImGui::Dummy(ImVec2(0, 8));
    }
    ImGui::PopFont();
    ImGui::End();
}

// ---------------------------------------------------------------------
// main
// ---------------------------------------------------------------------
int main()
{
    if (!glfwInit())
    {
        std::fprintf(stderr, "glfwInit failed\n");
        return 1;
    }

    GLFWwindow *window =
        glfwCreateWindow(1600, 1000, "Meridian", nullptr, nullptr);
    if (!window)
    {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    theme::apply();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");
    AppFonts fonts = load_fonts();

    // ---- Shared state and cross-thread queues ---------------------------
    AppState app_state;

    MpscQueue<EtcdEvent> etcd_events;
    EtcdResourceWatcher etcd_watcher(config::kEtcdEndpoint, etcd_events);
    std::thread([&etcd_watcher] {
        etcd_watcher.load_initial_snapshot();
        etcd_watcher.start_watch();
    }).detach();

    MpscQueue<MarketExecution::OrderStateReport> trade_events;
    MulticastReceiver order_state_receiver(
        config::kOrderStateMcastGroup, config::kOrderStateMcastPort,
        [&trade_events](const uint8_t *data, size_t len) {
            // A single UDP packet can carry more than one OrderStateReport
            // back-to-back (e.g. both sides of a match, or several fills
            // from one order matching multiple resting orders) -- confirmed
            // in practice by seeing exactly 2x the struct size (72 bytes
            // for two 36-byte reports) rather than a corrupted/mismatched
            // size. Loop over however many whole structs fit instead of
            // requiring an exact one-report-per-packet match.
            constexpr size_t kReportSize =
                sizeof(MarketExecution::OrderStateReport);
            if (len == 0 || len % kReportSize != 0)
            {
                DASHBOARD_LOG(
                    "OrderStateFeed",
                    "dropped packet: got %zu bytes, not a multiple of %zu", len,
                    kReportSize);
                return;
            }
            size_t count = len / kReportSize;
            for (size_t i = 0; i < count; ++i)
            {
                MarketExecution::OrderStateReport r;
                std::memcpy(&r, data + i * kReportSize, kReportSize);
                trade_events.push(r);
            }
        },
        config::kMcastLocalInterface);
    order_state_receiver.start();

    MpscQueue<MarketExecution::OrderBookUpdate> book_events;
    MulticastReceiver order_book_receiver(
        config::kOrderBookMcastGroup, config::kOrderBookMcastPort,
        [&book_events](const uint8_t *data, size_t len) {
            // Same batching tolerance as the order-state feed above --
            // not yet observed batched on this feed, but it's the same
            // underlying sender architecture, so handled the same way
            // defensively rather than waiting to hit it in the wild too.
            constexpr size_t kUpdateSize =
                sizeof(MarketExecution::OrderBookUpdate);
            if (len == 0 || len % kUpdateSize != 0)
            {
                DASHBOARD_LOG(
                    "OrderBookFeed",
                    "dropped packet: got %zu bytes, not a multiple of %zu", len,
                    kUpdateSize);
                return;
            }
            size_t count = len / kUpdateSize;
            for (size_t i = 0; i < count; ++i)
            {
                MarketExecution::OrderBookUpdate u;
                std::memcpy(&u, data + i * kUpdateSize, kUpdateSize);
                book_events.push(u);
            }
        },
        config::kMcastLocalInterface);
    order_book_receiver.start();

    // Two independent trading panels == two independent client sessions,
    // satisfying "trade as two separate clients at once."
    TradingPanel panel_a("Client A");
    TradingPanel panel_b("Client B");
    panel_a.avatar_color = theme::kAccent; // violet
    panel_b.avatar_color = theme::rgba(23, 181, 144, 255); // teal

    Page current_page = Page::Trading;

    double last_time = glfwGetTime();

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        double now = glfwGetTime();
        float dt = static_cast<float>(now - last_time);
        last_time = now;

        // ---- Drain all network queues into AppState (main thread only) --
        for (auto &ev : etcd_events.drain_all())
            app_state.apply_etcd_event(ev);

        for (auto &r : trade_events.drain_all())
        {
            TradeFeedEntry entry{ r.SequenceId,  r.TradeId,       r.ClientId,
                                  r.OrderId,     r.BoughtAssetId, r.SoldAssetId,
                                  r.BoughtDelta, r.SoldDelta };
            app_state.push_trade(entry);
            app_state.apply_trade_to_balance(r); // optimistic local update
        }

        for (auto &u : book_events.drain_all())
        {
            if (u.AssetId == 0)
                continue; // asset 0 is settlement currency, not shown in book
                          // view
            app_state.order_books[u.AssetId].apply(u);
        }

        panel_a.drain_confirmations();
        panel_b.drain_confirmations();
        panel_a.drain_balance_results(app_state);
        panel_b.drain_balance_results(app_state);
        panel_a.tick_toasts(dt);
        panel_b.tick_toasts(dt);

        // ---- Draw ----------------------------------------------------------
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Single full-window app shell (fills the whole OS window every
        // frame) instead of a pile of independent floating ImGui windows.
        // No title bar / resize / move / collapse -- this isn't itself
        // draggable, it just IS the app's client area.
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("##AppShell", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
                         | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
                         | ImGuiWindowFlags_NoBringToFrontOnFocus
                         | ImGuiWindowFlags_NoNavFocus
                         | ImGuiWindowFlags_NoScrollbar
                         | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();

        // ---- Top bar ----------------------------------------------------
        // Deliberately no manual SetCursorPos/SetCursorPosY math here --
        // an earlier version hand-computed a vertical-centering offset
        // that (with this bar's WindowPadding + the nav buttons' custom
        // FramePadding) placed the buttons partially outside the child's
        // padded content region. They still rendered, but were no longer
        // reliably clickable. Natural ImGui layout avoids that class of
        // bug entirely, at the cost of only near-enough vertical centering.
        ImGui::BeginChild("##TopBar", ImVec2(0, kTopBarHeight), true);
        ImGui::AlignTextToFramePadding();
        ImGui::PushFont(fonts.header);
        ImGui::TextColored(theme::kAccent, "Meridian");
        ImGui::PopFont();
        ImGui::SameLine(0, 28);
        for (auto &item : kNavItems)
        {
            bool selected = current_page == item.page;
            if (selected)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, theme::kAccentSoft);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                      theme::kAccentSoft);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                      theme::kAccentSoft);
                ImGui::PushStyleColor(ImGuiCol_Text, theme::kAccent);
            }
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14, 9));
            if (ImGui::Button(item.label))
                current_page = item.page;
            ImGui::PopStyleVar();
            if (selected)
                ImGui::PopStyleColor(4);
            ImGui::SameLine(0, 4);
        }
        ImGui::EndChild();

        // ---- Content: only the selected page's panel(s) are drawn -----
        // Explicit light-gray page background here, distinct from the
        // white default ChildBg every "card" BeginChild elsewhere in
        // this file relies on -- this is the one deliberate exception.
        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::kBg);
        ImGui::BeginChild("##Content", ImVec2(0, 0), false,
                          ImGuiWindowFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleColor();

        auto page_header = [&](const char *title) {
            ImGui::PushFont(fonts.header);
            ImGui::TextUnformatted(title);
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, 2));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 10));
        };

        switch (current_page)
        {
        case Page::Trading:
            page_header("Trading -- Split View");
            // Tables (not the legacy Columns() API) for the two-panel
            // split -- Columns() has known quirks with per-column cursor
            // Y tracking (ImGui's own docs point people at Tables
            // instead); Tables' row/column model doesn't have that
            // failure mode.
            if (ImGui::BeginTable("trading_split", 2, ImGuiTableFlags_None))
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                draw_trading_panel(panel_a, app_state, fonts);
                ImGui::TableNextColumn();
                draw_trading_panel(panel_b, app_state, fonts);
                ImGui::EndTable();
            }
            break;
        case Page::OrderBooks:
            page_header("Order Books");
            draw_order_book_panel(app_state, fonts);
            break;
        case Page::TradeFeed:
            page_header("Trade Feed");
            draw_trade_feed_panel(app_state, fonts);
            break;
        case Page::ServiceDiscovery:
            page_header("Service Discovery (etcd)");
            draw_etcd_panel(app_state, fonts);
            break;
        case Page::ClientLookup:
            page_header("Client Lookup");
            draw_client_lookup_panel(app_state, fonts);
            break;
        }
        ImGui::EndChild();

        ImGui::End();

        draw_toast_overlay(panel_a, panel_b, fonts);

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    order_state_receiver.stop();
    order_book_receiver.stop();
    etcd_watcher.stop();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}