// "Thư viện Drive" screen: browse a public Google Drive folder, download EPUB/TXT to the SD card.
#include <memory>
#include <vector>

#include "drive_folder.h"
#include "icons.h"
#include "library.h"
#include "online_library.h"
#include "platform.h"
#include "screens.h"
#include "ui_common.h"

namespace screens {
namespace {

constexpr int kPerPage = 7;  // paging instead of scrolling: one clean e-ink refresh per page

struct Level {
    drive::Ref ref;
    std::string title;
    std::vector<drive::Item> items;  // folders first, then readable books
    int hidden = 0;                  // files the reader cannot open (pdf, mp3, …)
    int page = 0;
};

struct State {
    ui::Page page;
    std::shared_ptr<bool> alive;
    std::vector<Level> stack;
    uint32_t pending = 0;
    uint32_t download = 0;
    std::string downloading;  // file id
    lv_obj_t* progress = nullptr;
};

void Render(State* s);
void Load(State* s, const drive::Ref& ref, const std::string& title);

std::string BookPath(const drive::Item& it) {
    const std::string dir = drive::IsAudio(it.name) ? platform::MusicDir() : platform::GetConfig().books_dir;
    return dir + "/" + drive::SafeFileName(it.name);
}

void SetStatus(State* s, const std::string& t) { lv_label_set_text(s->page.header_status, t.c_str()); }

void Back(State* s) {
    if (s->stack.size() > 1) {
        s->stack.pop_back();
        Render(s);
    } else {
        OpenOnlineLibraries();
    }
}

// `it` is taken BY VALUE on purpose: the caller is a closure owned by the row button, and
// Render() below deletes that button (and the closure) before the download URL is built.
void StartDownload(State* s, drive::Item it) {
    if (s->download) { ui::Toast("Đang tải một cuốn khác, đợi xong đã."); return; }
    std::string path = BookPath(it);
    s->downloading = it.ref.id;
    Render(s);
    auto alive = s->alive;
    std::string name = it.name;
    s->download = platform::Download(
        drive::DownloadUrl(it.ref), path,
        [s, alive](uint64_t done, uint64_t total) {
            if (!*alive || !s->progress) return;
            char buf[64];
            if (total) std::snprintf(buf, sizeof buf, "Đang tải %llu%%", (unsigned long long)(done * 100 / total));
            else std::snprintf(buf, sizeof buf, "Đang tải %llu KB", (unsigned long long)(done / 1024));
            lv_label_set_text(s->progress, buf);
        },
        [s, alive, name](const platform::HttpResponse& r) {
            if (!*alive) return;
            s->download = 0;
            s->downloading.clear();
            if (r.ok()) ui::Toast("Đã tải: " + name + (drive::IsAudio(name) ? "\nMở trong Nghe nhạc." : "\nMở trong Thư viện để đọc."));
            else ui::Toast("Tải lỗi: " + (r.status ? "HTTP " + std::to_string(r.status) : r.error));
            Render(s);
        });
}

void Row(State* s, lv_obj_t* parent, const drive::Item& it) {
    const bool have = !it.folder && platform::FileExists(BookPath(it));
    const bool busy = !it.folder && s->downloading == it.ref.id;
    drive::Item copy = it;
    lv_obj_t* b = ui::Button(parent, "", [s, copy, have] {
        if (copy.folder) Load(s, copy.ref, copy.name);
        else if (have) ui::Toast("Đã có trên thẻ nhớ: " + copy.name);
        else StartDownload(s, copy);
    }, LV_PCT(100), 112);
    lv_obj_set_style_pad_all(b, 14, 0);
    lv_obj_t* name = ui::Label(b, (it.folder ? "› " : "") + it.name, ui::F().body_bold);
    lv_obj_set_size(name, LV_PCT(100), lv_font_get_line_height(ui::F().body_bold));
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 0);
    std::string tag = it.folder ? "Thư mục" : busy ? "Đang tải…" : have ? "Đã có trên thẻ nhớ"
                      : drive::IsAudio(it.name) ? "Nhạc · chạm để tải về" : "Chạm để tải về";
    lv_obj_t* t = ui::Label(b, tag, ui::F().small);
    lv_obj_align(t, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    if (busy) s->progress = t;
    ui::Invert(b, have);
}

void Render(State* s) {
    s->progress = nullptr;
    lv_obj_t* body = s->page.body;
    lv_obj_clean(body);
    if (s->stack.empty()) return;
    Level& lv = s->stack.back();
    std::string path;
    for (size_t i = 0; i < s->stack.size(); ++i) path += (i ? " › " : "") + s->stack[i].title;
    SetStatus(s, path);

    if (lv.items.empty()) {
        ui::Label(body, lv.hidden ? "Thư mục này không có sách (EPUB/TXT) hay nhạc (MP3)." : "Thư mục trống.", ui::F().body);
    }
    const int pages = ((int)lv.items.size() + kPerPage - 1) / kPerPage;
    if (lv.page >= pages) lv.page = pages ? pages - 1 : 0;
    for (int i = lv.page * kPerPage; i < (int)lv.items.size() && i < (lv.page + 1) * kPerPage; ++i)
        Row(s, body, lv.items[i]);

    lv_obj_t* nav = lv_obj_create(body);
    lv_obj_set_size(nav, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(nav, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(nav, 0, 0);
    lv_obj_set_style_pad_all(nav, 0, 0);
    lv_obj_remove_flag(nav, LV_OBJ_FLAG_SCROLLABLE);
    if (lv.page > 0) {
        lv_obj_t* p = ui::Button(nav, "‹ Trang trước", [s] { s->stack.back().page--; Render(s); }, 280);
        lv_obj_align(p, LV_ALIGN_LEFT_MID, 0, 0);
    }
    if (lv.page + 1 < pages) {
        lv_obj_t* n = ui::Button(nav, "Trang sau ›", [s] { s->stack.back().page++; Render(s); }, 280);
        lv_obj_align(n, LV_ALIGN_RIGHT_MID, 0, 0);
    }
    char info[128];
    std::snprintf(info, sizeof info, "Trang %d/%d · %d mục%s", pages ? lv.page + 1 : 0, pages,
                  (int)lv.items.size(), lv.hidden ? "" : "");
    std::string line = info;
    if (lv.hidden) line += " · ẩn " + std::to_string(lv.hidden) + " tệp không mở được (PDF, ảnh…)";
    lv_obj_t* l = ui::Label(body, line, ui::F().small);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
}

void Load(State* s, const drive::Ref& ref, const std::string& title) {
    if (s->pending) return;
    SetStatus(s, "Đang mở " + title + "…");
    platform::HttpRequest req;
    req.url = drive::ListUrl(ref);
    req.timeout_ms = 15000;
    req.max_body = 2 * 1024 * 1024;
    auto alive = s->alive;
    s->pending = platform::Http(req, [s, alive, ref, title](const platform::HttpResponse& r) {
        if (!*alive) return;
        s->pending = 0;
        drive::Folder f;
        if (!r.ok() || !drive::ParseFolderHtml(r.body, f)) {
            std::string why = !r.ok() ? (r.status ? "HTTP " + std::to_string(r.status) : r.error)
                                      : "Thư mục không công khai hoặc link sai";
            if (s->stack.empty()) {
                lv_obj_clean(s->page.body);
                ui::Label(s->page.body, "Không mở được thư mục Drive.\n" + why, ui::F().body);
                ui::Button(s->page.body, "Thử lại", [s, ref, title] { Load(s, ref, title); }, LV_PCT(100));
                SetStatus(s, "Lỗi");
            } else {
                ui::Toast("Không mở được: " + why);
                Render(s);
            }
            return;
        }
        Level lv;
        lv.ref = ref;
        lv.title = s->stack.empty() && !f.title.empty() ? f.title : title;
        for (auto& it : f.items) if (it.folder) lv.items.push_back(it);
        for (auto& it : f.items) {
            if (it.folder) continue;
            if (drive::IsReadable(it.name) || drive::IsAudio(it.name)) lv.items.push_back(it);
            else lv.hidden++;
        }
        s->stack.push_back(std::move(lv));
        Render(s);
    });
}

}  // namespace

void OpenDriveSource(const online::Source& src) {
    library::Invalidate();
    auto* s = new State();
    s->page = ui::NewPage(src.name.c_str(), [] {});
    // The header back button must follow the same stack logic as the BACK key.
    s->alive = ui::Alive(s->page.screen);
    auto alive = s->alive;
    ui::SetBackHandler([s, alive] { if (*alive) Back(s); });
    lv_obj_add_event_cb(s->page.screen, [](lv_event_t* e) {
        auto* st = static_cast<State*>(lv_event_get_user_data(e));
        if (st->pending) platform::Cancel(st->pending);
        if (st->download) platform::Cancel(st->download);
        delete st;
    }, LV_EVENT_DELETE, s);
    drive::Ref root = drive::ParseLink(src.link);
    ui::Show(s->page.screen);
    if (root.empty()) {
        ui::Label(s->page.body, "Link thư mục Google Drive không hợp lệ.", ui::F().body);
        return;
    }
    Load(s, root, src.name);
}

void OpenDriveLibrary() {
    auto srcs = online::Sources();
    if (srcs.size() == 1) OpenDriveSource(srcs[0]);
    else OpenOnlineLibraries();
}

// "Thư viện online": the Google Drive folders the owner set up, add / rename / remove.
void OpenOnlineLibraries() {
    ui::Page p = ui::NewPage("THƯ VIỆN ONLINE", [] { OpenLauncher(); });
    auto srcs = online::Sources();
    lv_label_set_text_fmt(p.header_status, "%d thư viện · thư mục Google Drive công khai", (int)srcs.size());
    lv_obj_set_style_pad_row(p.body, 12, 0);
    const lv_color_t muted = lv_color_hex(0x5f5d58);
    if (srcs.empty()) {
        lv_obj_t* e = ui::Label(p.body, "Chưa có thư viện nào. Thêm link một thư mục Google Drive đã chia sẻ \"Bất kỳ ai có đường liên kết\": sách EPUB/TXT trong đó sẽ tải được về thẻ nhớ.", ui::F().serif_italic);
        lv_obj_set_width(e, LV_PCT(100));
        lv_label_set_long_mode(e, LV_LABEL_LONG_MODE_WRAP);
    }
    for (size_t i = 0; i < srcs.size(); ++i) {
        online::Source src = srcs[i];
        lv_obj_t* row = ui::Button(p.body, "", [src] { OpenDriveSource(src); }, LV_PCT(100), 120);
        lv_obj_set_style_pad_hor(row, 18, 0);
        lv_obj_set_style_radius(row, 12, 0);
        lv_obj_set_style_border_width(row, 2, 0);
        lv_obj_align(ui::IconLabel(row, icon::DOWNLOAD), LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_t* n = ui::Label(row, src.name, ui::F().serif_book);
        lv_obj_set_width(n, 270);
        lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_align(n, LV_ALIGN_TOP_LEFT, 62, 14);
        std::string id = drive::ParseLink(src.link).id;
        lv_obj_t* l = ui::Label(row, "drive · " + (id.size() > 10 ? id.substr(0, 10) + "…" : id), ui::F().small);
        lv_obj_set_style_text_color(l, muted, 0);
        lv_obj_align(l, LV_ALIGN_BOTTOM_LEFT, 62, -14);
        lv_obj_t* ren = ui::Button(row, icon::PEN, [i, src] {
            ui::PromptOptions o;
            o.title = "Đổi tên thư viện";
            o.initial = src.name;
            ui::TextPrompt(o, [i](const std::string& t) { online::Rename(i, t); OpenOnlineLibraries(); });
        }, 76, 76);
        lv_obj_set_style_text_font(ren, ui::F().icon, 0);
        lv_obj_set_style_border_width(ren, 1, 0);
        lv_obj_align(ren, LV_ALIGN_RIGHT_MID, -90, 0);
        lv_obj_t* shr = ui::Button(row, icon::SHARE, [src] { OpenShareLink(src.name, src.link); }, 76, 76);
        lv_obj_set_style_border_width(shr, 1, 0);
        lv_obj_align(shr, LV_ALIGN_RIGHT_MID, -180, 0);
        auto armed = std::make_shared<bool>(false);
        lv_obj_t* del = ui::Button(row, icon::TRASH, [i, armed] {
            if (!*armed) { *armed = true; ui::Toast("Bấm lần nữa để xoá thư viện này (sách đã tải vẫn giữ)."); return; }
            online::Remove(i);
            OpenOnlineLibraries();
        }, 76, 76);
        lv_obj_set_style_text_font(del, ui::F().icon, 0);
        lv_obj_set_style_border_width(del, 1, 0);
        lv_obj_align(del, LV_ALIGN_RIGHT_MID, 0, 0);
    }
    lv_obj_t* add = ui::Button(p.body, std::string(icon::PLUS) + "  Thêm thư viện (dán link Drive)", [] {
        ui::PromptOptions o;
        o.title = "Link Google Drive";
        o.hint = "Dán hoặc gõ link thư mục (drive.google.com/drive/folders/…). Gõ trên điện thoại dễ hơn: Chuyển file › Thư viện online.";
        o.placeholder = "https://drive.google.com/drive/folders/…";
        o.vietnamese = false;
        o.ok = "Tiếp";
        ui::TextPrompt(o, [](const std::string& link) {
            if (link.empty()) return;
            ui::PromptOptions n;
            n.title = "Tên thư viện";
            n.placeholder = "Ví dụ: Sách của nhà";
            n.ok = "Lưu";
            ui::TextPrompt(n, [link](const std::string& name) {
                std::string err;
                if (!online::Add(name, link, err)) ui::Toast(err);
                else ui::Toast("Đã thêm thư viện.");
                OpenOnlineLibraries();
            });
        });
    }, LV_PCT(100), 84);
    ui::Invert(add, true);
    ui::Button(p.body, std::string(icon::TRANSFER) + "  Thêm từ điện thoại qua Wi-Fi", [] { OpenTransfer(); }, LV_PCT(100), 76);
    ui::Button(p.body, std::string(icon::DEVICE) + "  Nhận sách từ máy Metalio khác", [] { OpenReceive(); }, LV_PCT(100), 76);
    lv_obj_t* hint = ui::Label(p.body, "Sách tải về nằm trong thư mục books/ trên thẻ nhớ và hiện ngay trên kệ. Link được lưu trên thẻ nhớ của máy, không gửi đi đâu.", ui::F().small);
    lv_obj_set_style_text_color(hint, muted, 0);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_WRAP);
    ui::Show(p.screen);
}

}  // namespace screens
